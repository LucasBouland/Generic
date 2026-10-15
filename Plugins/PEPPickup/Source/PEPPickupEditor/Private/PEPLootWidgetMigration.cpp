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

bool UPEPLootWidgetMigration::MigrateWidget(UBlueprint* Blueprint, bool bItemWindow)
{
    if (!Blueprint) return false;
    Blueprint->Modify();
    const bool bWindowData = Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(UPEPInventoryWindow::StaticClass());
    if (bWindowData)
    {
        Blueprint->NewVariables.RemoveAll([](const FBPVariableDescription& Variable)
        { return Variable.VarName == TEXT("OwnerItem") || Variable.VarName == TEXT("InventoryContext")
            || Variable.VarName == TEXT("OwnerItem_0") || Variable.VarName == TEXT("InventoryContext_0"); });
    }
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
    for (UEdGraph* Graph : Graphs)
    {
        const TArray<TObjectPtr<UEdGraphNode>> Nodes = Graph->Nodes;
        for (UEdGraphNode* Node : Nodes)
        {
            if (UK2Node_Variable* Variable = Cast<UK2Node_Variable>(Node))
            {
                const FName PreviousName = Variable->GetVarName();
                FName NativeName = PreviousName;
                // Reparenting can rename colliding Blueprint properties before this migration runs.
                if (PreviousName == TEXT("OwnerItem_0")) NativeName = TEXT("OwnerItem");
                if (PreviousName == TEXT("InventoryContext_0")) NativeName = TEXT("InventoryContext");
                if (bWindowData && (NativeName == TEXT("OwnerItem") || NativeName == TEXT("InventoryContext")))
                {
                    for (UEdGraphPin* Pin : Variable->Pins)
                    {
                        if (Pin->PinName == PreviousName) Pin->PinName = NativeName;
                    }
                    Variable->VariableReference.SetSelfMember(NativeName);
                }
            }
            UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
            if (!Call || Call->FunctionReference.GetMemberName() != TEXT("GetEntriesInContainerSlot")
                || Call->FunctionReference.GetMemberParentClass() != UPEPInventoryComponent::StaticClass()) continue;
            UEdGraphPin* Target = Call->FindPin(UEdGraphSchema_K2::PN_Self);
            UK2Node_CallFunction* Getter = Target && Target->LinkedTo.Num() == 1
                ? Cast<UK2Node_CallFunction>(Target->LinkedTo[0]->GetOwningNode()) : nullptr;
            UEdGraphPin* GetterTarget = Getter ? Getter->FindPin(UEdGraphSchema_K2::PN_Self) : nullptr;
            if (!Getter || Getter->FunctionReference.GetMemberName() != TEXT("GetInventoryComponent")
                || !GetterTarget || GetterTarget->LinkedTo.Num() != 1) return false;
            UEdGraphPin* Subsystem = GetterTarget->LinkedTo[0];
            Target->BreakAllPinLinks();
            Call->SetFromFunction(UPEPInventoryUISubsystem::StaticClass()->FindFunctionByName(TEXT("GetEntriesInContainerSlot")));
            Call->ReconstructNode();
            UK2Node_VariableGet* Context = NewObject<UK2Node_VariableGet>(Graph);
            Graph->AddNode(Context, false, false);
            Context->CreateNewGuid();
            Context->VariableReference.SetSelfMember(TEXT("InventoryContext"));
            Context->NodePosX = Call->NodePosX - 240;
            Context->NodePosY = Call->NodePosY + 160;
            Context->AllocateDefaultPins();
            if (!Schema->TryCreateConnection(Subsystem, Call->FindPin(UEdGraphSchema_K2::PN_Self))
                || !Schema->TryCreateConnection(Context->GetValuePin(), Call->FindPin(TEXT("Context")))) return false;
        }
    }
    if (bItemWindow && Blueprint->UbergraphPages.Num())
    {
        UEdGraph* Graph = Blueprint->UbergraphPages[0];
        bool bExists = false;
        for (UEdGraphNode* Node : Graph->Nodes)
            if (UK2Node_Event* Event = Cast<UK2Node_Event>(Node))
                bExists |= Event->EventReference.GetMemberName() == TEXT("RefreshSessionContents");
        if (!bExists)
        {
            UK2Node_Event* Event = NewObject<UK2Node_Event>(Graph);
            Graph->AddNode(Event, false, false);
            Event->CreateNewGuid();
            Event->EventReference.SetExternalMember(TEXT("RefreshSessionContents"), UPEPInventoryWindow::StaticClass());
            Event->bOverrideFunction = true;
            Event->NodePosY = 1800;
            Event->AllocateDefaultPins();
            UK2Node_CallFunction* Refresh = NewObject<UK2Node_CallFunction>(Graph);
            Graph->AddNode(Refresh, false, false);
            Refresh->CreateNewGuid();
            Refresh->FunctionReference.SetSelfMember(TEXT("HandleInventoryChanged"));
            Refresh->NodePosX = 300;
            Refresh->NodePosY = 1800;
            Refresh->AllocateDefaultPins();
            if (!Schema->TryCreateConnection(Event->FindPin(UEdGraphSchema_K2::PN_Then),
                Refresh->FindPin(UEdGraphSchema_K2::PN_Execute))) return false;
        }
    }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    return Blueprint->Status != BS_Error;
}
