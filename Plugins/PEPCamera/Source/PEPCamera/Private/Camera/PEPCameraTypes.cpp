#include "Camera/PEPCameraTypes.h"

void FPEPCameraView::Blend(const FPEPCameraView& Other, const float Alpha)
{
	if (Alpha <= 0.0f)
	{
		return;
	}
	if (Alpha >= 1.0f)
	{
		*this = Other;
		return;
	}

	Location = FMath::Lerp(Location, Other.Location, Alpha);
	Rotation = Rotation + (Other.Rotation - Rotation).GetNormalized() * Alpha;
	FieldOfView = FMath::Lerp(FieldOfView, Other.FieldOfView, Alpha);
}
