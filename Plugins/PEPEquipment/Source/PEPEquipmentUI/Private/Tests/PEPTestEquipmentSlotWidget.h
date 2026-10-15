#pragma once

#include "CoreMinimal.h"
#include "PEPEquipmentSlotWidget.h"

#include "PEPTestEquipmentSlotWidget.generated.h"

/**
 * Test double: a concrete slot widget driven without Slate. It counts notifications and follows
 * the component's events on request, since NativeConstruct only runs inside a widget tree.
 */
UCLASS(NotBlueprintable)
class UPEPTestEquipmentSlotWidget final : public UPEPEquipmentSlotWidget
{
	GENERATED_BODY()

public:
	void FollowEquipmentEvents()
	{
		SubscribeToEquipment();
	}

	void StopFollowingEquipmentEvents()
	{
		UnsubscribeFromEquipment();
	}

	int32 Notifications = 0;

protected:
	virtual void NativeOnSlotItemChanged() override
	{
		++Notifications;
		Super::NativeOnSlotItemChanged();
	}
};
