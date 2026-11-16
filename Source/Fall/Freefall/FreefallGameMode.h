#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "FreefallGameMode.generated.h"

UCLASS()
class FALL_API AFreefallGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AFreefallGameMode();

	virtual void StartPlay() override;
	virtual void RestartPlayer(AController* NewPlayer) override;

	bool IsGallery() const;
};

/** Minimal HUD: a crosshair, and the damage vignette is in the camera post-process. */
UCLASS()
class FALL_API AFallHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	/** 0..1 spread of the crosshair (recoil), 0..1 hit confirmation pulse. */
	float Spread = 0.f;
	float HitPulse = 0.f;
	float KillPulse = 0.f;
	bool bVisible = true;

	/** Full-screen fade (restart cut, impact, landing). FadeSpeed > 0 fades out to clear, < 0 fades in to solid. */
	float FadeAlpha = 0.f;
	float FadeSpeed = 2.f;
	FLinearColor FadeColor = FLinearColor::Black;
};
