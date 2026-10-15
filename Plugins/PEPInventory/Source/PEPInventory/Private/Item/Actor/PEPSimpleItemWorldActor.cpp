#include "Item/Actor/PEPSimpleItemWorldActor.h"
#include "Item/Fragments/PEPItemFragment_Container.h"

const FPEPItemInstance* APEPSimpleItemWorldActor::FindAuthorityItemInstance() const
{
	return HasAuthority() && AuthorityItemInstance.IsValid() ? &AuthorityItemInstance : nullptr;
}
void APEPSimpleItemWorldActor::ResetAuthorityStorage()
{
	AuthorityItemInstance.Reset();
}
bool APEPSimpleItemWorldActor::StoreAuthorityTree(const FPEPItemInstanceTree& Tree)
{
	if (!Tree.Children.IsEmpty() || Tree.RootItem.GetDefinitionFragment<FPEPItemFragment_Container>())
	{
		return false;
	}
	AuthorityItemInstance = Tree.RootItem;
	return true;
}

bool APEPSimpleItemWorldActor::SupportsDefinition(const UPEPItemDefinition* Definition) const
{
	return Definition && !Definition->GetDefinitionFragment<FPEPItemFragment_Container>();
}
