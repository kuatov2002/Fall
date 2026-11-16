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
	void BotFull(float Dt, FFallInputFrame& Out);
	void TickDamageTest(float Dt);
	void TickGrabTest(float Dt);
	void ScanPrimitives();
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
	float HoldTime = 0.f;
	float BotClock = 0.f;
	int32 SeenBurstEvents = 0;

	// Average player: delayed reactions, noisy aim, no hidden knowledge.
	bool bAvg = false;
	TArray<TPair<float, FFallInputFrame>> Delayed;
	float GrabPressT = -10.f;
	bool bPassed = false;
	int32 Deaths = 0;

	// Damage test: every enemy type dies from the computed number of hits.
	int32 DmgCase = -1;
	float DmgCaseT = 0.f;
	int32 DmgHits = 0, DmgShots = 0, DmgFails = 0, DmgSeen = 0;
	TWeakObjectPtr<class AFallEnemy> DmgDummy;
	TArray<TSharedPtr<class FJsonValue>> DmgResults;

	// Grab test: pieces fly at the player at different speeds, with and without a grab press.
	TWeakObjectPtr<class AFallDebris> GrabPiece;
	bool bGrabPress = false, bGrabPressed = false, bGrabbed = false;

	// Primitive check: engine utility meshes visible in the game frame (owner class -> mesh path -> max count).
	TMap<FString, int32> Primitives;
	float NextPrimScan = 0.f;
};
