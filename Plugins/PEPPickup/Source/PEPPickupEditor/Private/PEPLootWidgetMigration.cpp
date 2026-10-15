#include "PEPLootWidgetMigration.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "PEPInventoryUISubsystem.h"
#include "PEPInventoryWindow.h"
#include "Components/PEPInventoryComponent.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "PEPLootWidgetMigration"

DEFINE_LOG_CATEGORY_STATIC(LogPEPLootWidgetMigration, Log, All);

namespace PEPLootWidgetMigration
{
    const FName OwnerItemName(TEXT("OwnerItem"));
    const FName InventoryContextName(TEXT("InventoryContext"));
    const FName RenamedOwnerItemName(TEXT("OwnerItem_0"));
    const FName RenamedInventoryContextName(TEXT("InventoryContext_0"));
    const FName SlotQueryName(TEXT("GetEntriesInContainerSlot"));
    const FName ContextParamName(TEXT("Context"));
    const FName RefreshEventName(TEXT("RefreshSessionContents"));
    const FName RefreshHandlerName(TEXT("HandleInventoryChanged"));

    /** One inventory slot query to re-point at the UI subsystem, resolved before anything changes. */
    struct FSlotQueryRewrite
    {
        UEdGraph* Graph = nullptr;
        UK2Node_CallFunction* Call = nullptr;
        UEdGraphPin* Target = nullptr;
        UEdGraphPin* Subsystem = nullptr;
    };

    /** The class whose properties the Blueprint's graphs see: its skeleton when compiled, else its parent. */
    const UClass* GetGraphClass(const UBlueprint* Blueprint)
    {
        return Blueprint->SkeletonGeneratedClass
            ? Blueprint->SkeletonGeneratedClass.Get()
            : Blueprint->ParentClass.Get();
    }
}

bool UPEPLootWidgetMigration::MigrateWidget(UBlueprint* Blueprint, bool bItemWindow)
{
    using namespace PEPLootWidgetMigration;

    if (!Blueprint) return false;

    /*
     * Everything that can refuse is checked first: a refusal returns before the Blueprint is
     * touched. The edits that follow are one undoable transaction.
     */
    const UFunction* SlotQuery = UPEPInventoryUISubsystem::StaticClass()->FindFunctionByName(SlotQueryName);
    const FStructProperty* SlotQueryContext = SlotQuery
        ? CastField<FStructProperty>(SlotQuery->FindPropertyByName(ContextParamName))
        : nullptr;
    if (!SlotQueryContext)
    {
        UE_LOG(LogPEPLootWidgetMigration, Error,
            TEXT("%s: UPEPInventoryUISubsystem::%s(Context, ...) not found."),
            *Blueprint->GetName(), *SlotQueryName.ToString());
        return false;
    }

    const bool bWindowData = Blueprint->ParentClass
        && Blueprint->ParentClass->IsChildOf(UPEPInventoryWindow::StaticClass());
    const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);

    TArray<FSlotQueryRewrite> Rewrites;
    for (UEdGraph* Graph : Graphs)
    {
        if (!Graph) continue;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
            if (!Call || Call->FunctionReference.GetMemberName() != SlotQueryName
                || Call->FunctionReference.GetMemberParentClass() != UPEPInventoryComponent::StaticClass()) continue;
            UEdGraphPin* Target = Call->FindPin(UEdGraphSchema_K2::PN_Self);
            UK2Node_CallFunction* Getter = Target && Target->LinkedTo.Num() == 1
                ? Cast<UK2Node_CallFunction>(Target->LinkedTo[0]->GetOwningNode()) : nullptr;
            UEdGraphPin* GetterTarget = Getter ? Getter->FindPin(UEdGraphSchema_K2::PN_Self) : nullptr;
            if (!Getter || Getter->FunctionReference.GetMemberName() != TEXT("GetInventoryComponent")
                || !GetterTarget || GetterTarget->LinkedTo.Num() != 1)
            {
                UE_LOG(LogPEPLootWidgetMigration, Error,
                    TEXT("%s: a %s call in %s is not fed by the UI subsystem's GetInventoryComponent."),
                    *Blueprint->GetName(), *SlotQueryName.ToString(), *Graph->GetName());
                return false;
            }

            /* The pin feeding the getter must be able to feed the subsystem query itself. */
            UEdGraphPin* Subsystem = GetterTarget->LinkedTo[0];
            const UClass* SubsystemClass = Subsystem
                ? Cast<UClass>(Subsystem->PinType.PinSubCategoryObject.Get())
                : nullptr;
            if (!SubsystemClass || !SubsystemClass->IsChildOf(UPEPInventoryUISubsystem::StaticClass()))
            {
                UE_LOG(LogPEPLootWidgetMigration, Error,
                    TEXT("%s: the object feeding GetInventoryComponent in %s is not a UPEPInventoryUISubsystem."),
                    *Blueprint->GetName(), *Graph->GetName());
                return false;
            }
            Rewrites.Add({Graph, Call, Target, Subsystem});
        }
    }

    /* The rewritten queries read the window's InventoryContext, which must exist with the right type. */
    if (!Rewrites.IsEmpty())
    {
        const UClass* GraphClass = GetGraphClass(Blueprint);
        const FStructProperty* InventoryContext = GraphClass
            ? FindFProperty<FStructProperty>(GraphClass, InventoryContextName)
            : nullptr;
        if (!InventoryContext || InventoryContext->Struct != SlotQueryContext->Struct)
        {
            UE_LOG(LogPEPLootWidgetMigration, Error,
                TEXT("%s: no %s variable of the subsystem's context type to feed %s."),
                *Blueprint->GetName(), *InventoryContextName.ToString(), *SlotQueryName.ToString());
            return false;
        }
    }

    UEdGraph* RefreshGraph = nullptr;
    if (bItemWindow && Blueprint->UbergraphPages.Num())
    {
        UEdGraph* Graph = Blueprint->UbergraphPages[0];
        bool bExists = false;
        for (UEdGraphNode* Node : Graph->Nodes)
            if (UK2Node_Event* Event = Cast<UK2Node_Event>(Node))
                bExists |= Event->EventReference.GetMemberName() == RefreshEventName;
        if (!bExists)
        {
            const UClass* GraphClass = GetGraphClass(Blueprint);
            const UFunction* RefreshEvent = UPEPInventoryWindow::StaticClass()->FindFunctionByName(RefreshEventName);
            const UFunction* RefreshHandler = GraphClass ? GraphClass->FindFunctionByName(RefreshHandlerName) : nullptr;
            if (!bWindowData || !RefreshEvent)
            {
                UE_LOG(LogPEPLootWidgetMigration, Error,
                    TEXT("%s: %s can only be overridden on a UPEPInventoryWindow."),
                    *Blueprint->GetName(), *RefreshEventName.ToString());
                return false;
            }
            /* Linked through its exec pin: it must exist and must not be pure. */
            if (!RefreshHandler || RefreshHandler->HasAnyFunctionFlags(FUNC_BlueprintPure))
            {
                UE_LOG(LogPEPLootWidgetMigration, Error,
                    TEXT("%s: no callable %s function for %s to run."),
                    *Blueprint->GetName(), *RefreshHandlerName.ToString(), *RefreshEventName.ToString());
                return false;
            }
            RefreshGraph = Graph;
        }
    }

    const FScopedTransaction Transaction(LOCTEXT("MigrateLootWidget", "Migrate PEP Loot Widget"));
    Blueprint->Modify();

    if (bWindowData)
    {
        Blueprint->NewVariables.RemoveAll([](const FBPVariableDescription& Variable)
        { return Variable.VarName == OwnerItemName || Variable.VarName == InventoryContextName
            || Variable.VarName == RenamedOwnerItemName || Variable.VarName == RenamedInventoryContextName; });

        for (UEdGraph* Graph : Graphs)
        {
            if (!Graph) continue;
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                UK2Node_Variable* Variable = Cast<UK2Node_Variable>(Node);
                if (!Variable) continue;
                const FName PreviousName = Variable->GetVarName();
                FName NativeName = PreviousName;
                // Reparenting can rename colliding Blueprint properties before this migration runs.
                if (PreviousName == RenamedOwnerItemName) NativeName = OwnerItemName;
                if (PreviousName == RenamedInventoryContextName) NativeName = InventoryContextName;
                if (NativeName != OwnerItemName && NativeName != InventoryContextName) continue;
                Variable->Modify();
                for (UEdGraphPin* Pin : Variable->Pins)
                {
                    if (Pin->PinName == PreviousName) Pin->PinName = NativeName;
                }
                Variable->VariableReference.SetSelfMember(NativeName);
            }
        }
    }

    bool bConnected = true;
    for (const FSlotQueryRewrite& Rewrite : Rewrites)
    {
        UEdGraph* Graph = Rewrite.Graph;
        UK2Node_CallFunction* Call = Rewrite.Call;
        Graph->Modify();
        Call->Modify();
        Rewrite.Target->BreakAllPinLinks();
        Call->SetFromFunction(SlotQuery);
        Call->ReconstructNode();
        UK2Node_VariableGet* Context = NewObject<UK2Node_VariableGet>(Graph);
        Context->SetFlags(RF_Transactional);
        Graph->AddNode(Context, false, false);
        Context->CreateNewGuid();
        Context->VariableReference.SetSelfMember(InventoryContextName);
        Context->NodePosX = Call->NodePosX - 240;
        Context->NodePosY = Call->NodePosY + 160;
        Context->AllocateDefaultPins();
        bConnected &= Schema->TryCreateConnection(Rewrite.Subsystem, Call->FindPin(UEdGraphSchema_K2::PN_Self));
        bConnected &= Schema->TryCreateConnection(Context->GetValuePin(), Call->FindPin(ContextParamName));
    }

    if (RefreshGraph)
    {
        RefreshGraph->Modify();
        UK2Node_Event* Event = NewObject<UK2Node_Event>(RefreshGraph);
        Event->SetFlags(RF_Transactional);
        RefreshGraph->AddNode(Event, false, false);
        Event->CreateNewGuid();
        Event->EventReference.SetExternalMember(RefreshEventName, UPEPInventoryWindow::StaticClass());
        Event->bOverrideFunction = true;
        Event->NodePosY = 1800;
        Event->AllocateDefaultPins();
        UK2Node_CallFunction* Refresh = NewObject<UK2Node_CallFunction>(RefreshGraph);
        Refresh->SetFlags(RF_Transactional);
        RefreshGraph->AddNode(Refresh, false, false);
        Refresh->CreateNewGuid();
        Refresh->FunctionReference.SetSelfMember(RefreshHandlerName);
        Refresh->NodePosX = 300;
        Refresh->NodePosY = 1800;
        Refresh->AllocateDefaultPins();
        bConnected &= Schema->TryCreateConnection(Event->FindPin(UEdGraphSchema_K2::PN_Then),
            Refresh->FindPin(UEdGraphSchema_K2::PN_Execute));
    }

    if (!bConnected)
    {
        /* Not expected after the checks above; the transaction lets the editor undo it whole. */
        UE_LOG(LogPEPLootWidgetMigration, Error,
            TEXT("%s: a migrated node could not be connected; undo the \"Migrate PEP Loot Widget\" transaction to restore it."),
            *Blueprint->GetName());
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    return bConnected && Blueprint->Status != BS_Error;
}

#undef LOCTEXT_NAMESPACE
