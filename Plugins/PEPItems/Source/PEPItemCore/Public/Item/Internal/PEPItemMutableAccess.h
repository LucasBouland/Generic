#pragma once

#include "CoreMinimal.h"
#include "Item/PEPItemInstance.h"
#include "Item/PEPItemStateFragment.h"

/**
 * Narrow mutation access for PEP item mutation modules.
 * Gameplay/UI code should not include this header directly; use PEPItemMutation or PEPItemHelpersMutation.
 */
struct PEPITEMCORE_API FPEPItemMutableAccess
{
	static TArray<FInstancedStruct>& GetMutableStateFragments(FPEPItemInstance& Item)
	{
		return Item.StateFragments;
	}

	template <typename TState>
	static TState* GetMutableState(FPEPItemInstance& Item)
	{
		static_assert(TIsDerivedFrom<TState, FPEPItemStateFragment>::IsDerived,
		              "TState must derive from FPEPItemStateFragment");
		return Item.Internal_GetMutableState<TState>();
	}

	template <typename TState>
	static TState& FindOrAddState(FPEPItemInstance& Item)
	{
		static_assert(TIsDerivedFrom<TState, FPEPItemStateFragment>::IsDerived,
		              "TState must derive from FPEPItemStateFragment");
		return Item.Internal_FindOrAddState<TState>();
	}
};
