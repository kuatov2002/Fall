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
	// Weapon in hand (FallReviewWeapon.cpp)
	FString WeaponProblem(AFallPlayer* P);
	void CheckWeapon(AFallPlayer* P, class UFallWorldSubsystem* FW);
	void TickCloseups(AFallPlayer* P, const TArray<FString>& Labels);
	void WeaponTestInput(float Dt, FFallInputFrame& Out);
	void WriteWeaponMetrics(TSharedPtr<class FJsonObject> Root);

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
	int32 SelfIntFrames = 0, SelfIntBad = 0;
	float SelfIntWorst = 0.f;
	FString SelfIntPair;
	TMap<FString, int32> SelfIntByCase;
	float NextSelfShot = 0.f;
	float NextPrimScan = 0.f;

	struct FWeaponStat { int32 Frames = 0, Bad = 0; FString FirstBad; };
	TMap<FString, FWeaponStat> WeaponStats;
	TMap<FString, float> LabelSince;
	TSet<FString> CloseDone;
	FString CloseState;
	int32 CloseStep = -1;
	TWeakObjectPtr<class ACameraActor> CloseCam;
	bool bWasIntro = false;
	float AfterIntroUntil = -1.f, AfterRestartUntil = -1.f;
	int32 SeenWeaponEvents = 0;
	float WT = 0.f;
	bool bSeqActive = false, bSeqDone = false, bCloseups = false;
	int32 SeqFrame = 0;
	// Reload bake (-FallBakeReload): local bone transforms of the procedural reload, for the montage.
	TArray<TSharedPtr<class FJsonValue>> BakeFrames;
	bool bBakeStarted = false;
	void TickBake(AFallPlayer* P);
	// Reload and recoil checks (every run): montage plays for the expected time, the left hand passes the pouch and
	// the magazine well; every shot visibly moves the muzzle.
	struct FReloadRec { float Start = -1.f, Expected = 0.f, MinPouch = 1e9f, MinWell = 1e9f; int32 Frames = 0, MontageFrames = 0, SelfInt = 0; FString Ctx; };
	FReloadRec CurReload;
	TArray<TSharedPtr<class FJsonValue>> ReloadResults;
	int32 ReloadFails = 0;
	struct FRecoilRec { FVector Dir; int32 Left = 0; float MaxDeg = 0.f; FString Ctx; };
	TArray<FRecoilRec> RecoilOpen;
	TMap<FString, FIntPoint> RecoilStats; // state -> (shots, visible)
	float PrevTrigger = 0.f;
	void CheckReloadAndRecoil(AFallPlayer* P, float SelfPen);
	// Tandem: lines never through the player, the two bodies never inside each other, hands on the straps.
	int32 TandemFrames = 0, LineHits = 0, BodyOverlap = 0, HandsOff = 0;
	float LineMinCm = 1e9f, BodyMinCm = 1e9f, HandGapSum = 0.f, UpAngleMax = 0.f, TargetGapSum = 0.f, WeightSum = 0.f, ReachSum = 0.f, AheadSum = 0.f, FacingSum = 0.f, PackBackSum = 0.f, PackPivotBackSum = 0.f;
	int32 PackBetween = 0;
	FString LineWorst;
	void CheckTandem(AFallPlayer* P);
};
