#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Equipment/PEPEquipmentAbilityMapLedger.h"
#include "Equipment/PEPEquipmentTags.h"
#include "Item/Fragments/PEPItemFragment_EquipmentAbilities.h"

namespace PEPEquipmentGMASTest
{
	FPEPItemId MakeId(const int32 Value)
	{
		FPEPItemId Id;
		Id.Value = Value;
		return Id;
	}

	FAbilityMapData MakeEntry(const FGameplayTag InputTag, const bool bGrantedByDefault = true)
	{
		FAbilityMapData Entry;
		Entry.InputTag = InputTag;
		Entry.bGrantedByDefault = bGrantedByDefault;
		return Entry;
	}

	/* Any registered tag serves as an input tag here; the ledger gives tags no meaning. */
	const FGameplayTag& PrimaryInput()
	{
		return PEPEquipmentTags::Equipment_Action_Equip;
	}

	const FGameplayTag& SecondaryInput()
	{
		return PEPEquipmentTags::Equipment_Event_Changed;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPEquipmentAbilityLedgerCollisionTest,
	"PEP.Equipment.GMAS.Ledger.ResolvesInputTagCollisionsFromStateOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPEquipmentAbilityLedgerCollisionTest::RunTest(const FString& Parameters)
{
	using namespace PEPEquipmentGMASTest;
	const FGameplayTag Input = PrimaryInput();
	const FPEPItemId Rifle = MakeId(20);
	const FPEPItemId Pistol = MakeId(10);

	FPEPEquipmentAbilityMapLedger Ledger;
	TArray<FGameplayTag> Touched;
	Ledger.AddItem(Rifle, { MakeEntry(Input) }, 0, Touched);
	TestTrue(TEXT("a first contribution touches its input tag"),
		Touched.Num() == 1 && Touched[0] == Input);
	TestTrue(TEXT("a new tag asks for its baseline before the first write"),
		Ledger.NeedsBaseline(Input));
	Ledger.CaptureBaseline(Input, nullptr);
	TestFalse(TEXT("the baseline is captured once"), Ledger.NeedsBaseline(Input));

	Touched.Reset();
	Ledger.AddItem(Pistol, { MakeEntry(Input, false) }, 0, Touched);
	const FPEPEquipmentAbilityMapLedger::FContribution* Winner = Ledger.FindWinner(Input);
	TestTrue(TEXT("on equal priority the lower ItemId answers, whatever arrived first"),
		Winner && Winner->ItemId == Pistol);
	TestFalse(TEXT("the grant follows the winner, which does not ask for it"),
		Ledger.WantsGrant(Input));

	FPEPEquipmentAbilityMapLedger Reversed;
	Reversed.AddItem(Pistol, { MakeEntry(Input, false) }, 0, Touched);
	Reversed.AddItem(Rifle, { MakeEntry(Input) }, 0, Touched);
	const FPEPEquipmentAbilityMapLedger::FContribution* ReversedWinner = Reversed.FindWinner(Input);
	TestTrue(TEXT("arrival order does not change the winner"),
		ReversedWinner && ReversedWinner->ItemId == Pistol);

	Touched.Reset();
	Ledger.AddItem(Rifle, { MakeEntry(Input) }, 5, Touched);
	Winner = Ledger.FindWinner(Input);
	TestTrue(TEXT("a higher priority wins over a lower ItemId"), Winner && Winner->ItemId == Rifle);
	TestTrue(TEXT("and its grant request applies"), Ledger.WantsGrant(Input));

	Touched.Reset();
	Ledger.RemoveItem(Rifle, Touched);
	Winner = Ledger.FindWinner(Input);
	TestTrue(TEXT("removing one item restores the other's still-needed entry"),
		Winner && Winner->ItemId == Pistol && Touched.Num() == 1);
	FAbilityMapData Desired;
	TestTrue(TEXT("the remaining entry is what the ASC should hold"),
		Ledger.ResolveDesiredEntry(Input, Desired) && Desired.InputTag == Input);
	TestFalse(TEXT("a written entry never grants by itself"), Desired.bGrantedByDefault);

	Touched.Reset();
	Ledger.RemoveItem(Pistol, Touched);
	TestNull(TEXT("no contributor is left"), Ledger.FindWinner(Input));
	TestTrue(TEXT("the ledger still answers for the tag it took over"),
		Ledger.ResolveDesiredEntry(Input, Desired));
	TestTrue(TEXT("with no baseline, the entry is emptied rather than left to a gone item"),
		Desired.InputTag == Input && Desired.Abilities.IsEmpty() && !Desired.bGrantedByDefault);
	Ledger.ForgetIfIdle(Input);
	TestFalse(TEXT("an idle tag is forgotten"), Ledger.ResolveDesiredEntry(Input, Desired));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPEquipmentAbilityLedgerBaselineTest,
	"PEP.Equipment.GMAS.Ledger.RestoresBaselineAndOwnsOnlyItsGrants",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPEquipmentAbilityLedgerBaselineTest::RunTest(const FString& Parameters)
{
	using namespace PEPEquipmentGMASTest;
	const FGameplayTag Input = PrimaryInput();
	const FGameplayTag OtherInput = SecondaryInput();
	const FPEPItemId Item = MakeId(7);

	FPEPEquipmentAbilityMapLedger Ledger;
	TArray<FGameplayTag> Touched;
	Ledger.AddItem(Item, { MakeEntry(Input), MakeEntry(OtherInput) }, 0, Touched);
	TestEqual(TEXT("both input tags are touched once each"), Touched.Num(), 2);

	FAbilityMapData PawnEntry = MakeEntry(Input, true);
	Ledger.CaptureBaseline(Input, &PawnEntry);
	Ledger.CaptureBaseline(OtherInput, nullptr);
	Ledger.SetGrantedByLedger(OtherInput, true);

	Touched.Reset();
	Ledger.RemoveItem(Item, Touched);
	FAbilityMapData Desired;
	TestTrue(TEXT("the pawn's own entry comes back"),
		Ledger.ResolveDesiredEntry(Input, Desired) && Desired.InputTag == Input);
	TestFalse(TEXT("restoring the pawn's entry never re-grants locally"), Desired.bGrantedByDefault);
	TestFalse(TEXT("nothing wants the tag granted any more"), Ledger.WantsGrant(OtherInput));
	TestTrue(TEXT("a grant the ledger made is remembered until given back"),
		Ledger.IsGrantedByLedger(OtherInput));
	Ledger.ForgetIfIdle(OtherInput);
	TestTrue(TEXT("a tag whose grant is still owed is not forgotten"),
		Ledger.IsGrantedByLedger(OtherInput));
	Ledger.SetGrantedByLedger(OtherInput, false);
	Ledger.ForgetIfIdle(OtherInput);
	TestFalse(TEXT("once given back, the tag is forgotten"),
		Ledger.ResolveDesiredEntry(OtherInput, Desired));
	TestFalse(TEXT("the removed item is no longer tracked"), Ledger.HasItem(Item));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPEquipmentAbilitiesFragmentTest,
	"PEP.Equipment.GMAS.Fragment.AppliesOnlyWhereAuthored",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPEquipmentAbilitiesFragmentTest::RunTest(const FString& Parameters)
{
	FPEPItemFragment_EquipmentAbilities Fragment;
	const FGameplayTag Hand = PEPEquipmentTags::Equipment_Slot_Hand_Right;
	const FGameplayTag Back = PEPEquipmentTags::Equipment_Slot_Back;

	TestTrue(TEXT("a root grants in any slot by default"), Fragment.AppliesTo(Back, false));
	TestFalse(TEXT("an attachment does not grant by default"), Fragment.AppliesTo(Hand, true));
	TestFalse(TEXT("no eligibility requirement by default"), Fragment.HasEligibilityRequirements());

	Fragment.ActiveInSlots.AddTag(Hand);
	TestTrue(TEXT("a slot-limited item grants in its slot"), Fragment.AppliesTo(Hand, false));
	TestFalse(TEXT("and not on the back"), Fragment.AppliesTo(Back, false));

	Fragment.bApplyWhenAttachment = true;
	TestTrue(TEXT("an opted-in attachment grants under a matching root slot"),
		Fragment.AppliesTo(Hand, true));

	Fragment.BlockedActiveTags.AddTag(Back);
	TestTrue(TEXT("a blocked tag is an eligibility requirement"),
		Fragment.HasEligibilityRequirements());
	return true;
}

#endif
