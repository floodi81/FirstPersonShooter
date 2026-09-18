#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "FPSAnimConnectBPLibrary.generated.h"

UCLASS()
class UFPSAnimConnectBPLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Fix FPS_AnimBP so Arms_idle plays: clear self-overlay, wire Idle state, SM->Output Pose, silence broken EventGraph. */
	UFUNCTION(BlueprintCallable, Category = "FPS Anim Connect")
	static bool WireArmsIdleToOutputPose();

	/** Cache AnimationBlueprint on BeginPlay; Aim Pressed/Released set IsAiming with IsValid. */
	UFUNCTION(BlueprintCallable, Category = "FPS Anim Connect")
	static bool FixCharacterAimIsAiming();

	/** Enable Loop on Aim Walking Sequence Player (and Arms_Aim_Walk asset) so it keeps playing. */
	UFUNCTION(BlueprintCallable, Category = "FPS Anim Connect")
	static bool FixAimWalkLoop();
};
