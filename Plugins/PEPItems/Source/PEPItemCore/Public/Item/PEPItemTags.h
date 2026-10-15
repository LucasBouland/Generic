#pragma once

#include "NativeGameplayTags.h"

/**
 * ItemCore's native vocabulary is a single convention root. What kinds of items exist — weapons,
 * ammunition, keys, currencies — is game vocabulary: each project declares its own children of
 * Item.Type (Item.Type.Weapon.Rifle, ...) in its own module or tag tables. Editor tooling such as
 * the PEP item browser groups definitions by the path after "Item.Type.".
 */
namespace PEPItemTags
{
	/** Root of item kinds. Projects extend it; ItemCore defines no children. */
	PEPITEMCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Item_Type);
}
