// Self-review tooling: a scripted input bot, timed screenshots with bursts around transitions, and feel metrics.
// Enabled from the command line:
//   -FallReview=M1/run01 -FallBot=core -FallShot=0.333 -FallDuration=40 [-FallBursts] [-FallPerf] [-FallStartTime=70]
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FallPlayer.h"
#include "FallReview.generated.h"

UCLASS()
class FALL_API AFallReview : public AActor
{
	GENERATED_BODY()

public:
	AFallReview();
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

	void BotInput(float Dt, FFallInputFrame& Out);
	void Shot(const FString& Name);
	void WriteMetrics();

	FString Dir, Bot, StageDir;
	float ShotEvery = 0.333f;
	float Duration = 40.f;
	bool bBursts = false;
	bool bPerf = false;

	float T = 0.f;
	float NextShot = 0.f;
	int32 Frame = 0;
	int32 ShotIndex = 0;
	int32 BurstLeft = 0;
	FString BurstName;
	int32 BurstIndex = 0;
	bool bShotThisFrame = false;
	FFallInputFrame LastBot;

	// Metrics
	double LastReal = 0.0;
	TArray<float> FrameMs;
	struct FPress { float Time; int32 Frame; float Kind; float PitchAtPress; float SpeedAtPress; int32 PoseFrame = -1; int32 SpeedFrame = -1; float Reached = -1.f; };
	TArray<FPress> Presses;
	int32 EnemyFrames = 0, CombatFrames = 0;
	float LastEventTime = 0.f, LongestGap = 0.f;
	int32 SeenEvents = 0;
	TArray<TSharedPtr<class FJsonValue>> EventLog;
	bool bWritten = false;
};
