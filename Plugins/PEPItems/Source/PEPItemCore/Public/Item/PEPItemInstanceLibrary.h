#pragma once

#include "CoreMinimal.h"
#include "PEPItemDefinition.h"
#include "PEPItemInstance.h"

#include "PEPItemInstanceLibrary.generated.h"

UCLASS()
class PEPITEMCORE_API UPEPItemInstanceLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Creates a runtime item record on the authority. Clients should request creation from the server. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="PEP|Item|Creation",
		meta=(WorldContext="WorldContextObject", DisplayName="Create Item Instance (Authority)"))
	static bool CreateItemInstance(
		const UObject* WorldContextObject,
		const UPEPItemDefinition* Definition,
		FPEPItemInstance& OutItem);

#if WITH_EDITOR
	/** Editor-only, C++ preview path. Never use this to create gameplay items. */
	static bool CreateItemInstanceForEditor(
		const UPEPItemDefinition* Definition,
		FPEPItemInstance& OutItem);
#endif
};
