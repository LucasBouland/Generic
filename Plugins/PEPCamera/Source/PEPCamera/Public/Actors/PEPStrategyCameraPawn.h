#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"

#include "PEPStrategyCameraPawn.generated.h"

class UInputMappingContext;
class UPEPCameraComponent;
class UPEPCameraInputConfig;

/**
 * Pawn a player possesses in a strategy (RTS) game: a camera with no body.
 *
 * Its camera component defaults to the strategy mode. The pawn stands at the camera focus so that
 * everything keyed on the pawn location (network relevancy, audio listener position, streaming)
 * follows what the player looks at. Only the owning client knows the focus; it sends it to the server
 * at SyncInterval when it has moved more than SyncDistance. That location is a hint for relevancy,
 * never gameplay authority.
 *
 * Input: InputMappingContext is added for the local player while this pawn is controlled, and
 * InputConfig's actions are bound to the camera. With PEPPlayer, list the context in the player
 * controller's GameplayMappingContexts so the menu suspends it.
 */
UCLASS(Blueprintable)
class PEPCAMERA_API APEPStrategyCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	APEPStrategyCameraPawn(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void NotifyControllerChanged() override;

	UFUNCTION(BlueprintPure, Category="PEP|Camera")
	UPEPCameraComponent* GetCameraComponent() const
	{
		return CameraComponent;
	}

protected:
	UFUNCTION(Server, Unreliable)
	void Server_SetFocusLocation(FVector_NetQuantize FocusLocation);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="PEP|Camera")
	TObjectPtr<UPEPCameraComponent> CameraComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Camera|Input")
	TObjectPtr<const UPEPCameraInputConfig> InputConfig;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Camera|Input")
	TObjectPtr<UInputMappingContext> InputMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Camera|Input")
	int32 InputMappingPriority = 0;

	/** Seconds between focus updates sent to the server. 0 disables them. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Camera|Network", meta=(ClampMin="0.0", Units="s"))
	float SyncInterval = 0.2f;

	/** Minimum focus movement before an update is sent. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Camera|Network", meta=(ClampMin="0.0", Units="cm"))
	float SyncDistance = 200.0f;

private:
	void AddMappingContext(AController* ForController);
	void RemoveMappingContext(AController* ForController);

	FVector LastSyncedFocus = FVector::ZeroVector;
	float TimeSinceSync = 0.0f;
	bool bHasSyncedFocus = false;
};
