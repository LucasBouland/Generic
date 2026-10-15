#include "Item/Actor/PEPSimpleItemWorldActor.h"
#include "Item/Fragments/PEPItemFragment_Container.h"

FPEPItemInstance APEPSimpleItemWorldActor::GetAuthorityItemInstance() const
{
	return HasAuthority() ? AuthorityItemInstance : FPEPItemInstance();
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
