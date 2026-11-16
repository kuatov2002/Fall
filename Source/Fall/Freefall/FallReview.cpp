#include "FallReview.h"
#include "FallWorld.h"
#include "FallTuning.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformTime.h"
#include "UnrealClient.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"

AFallReview::AFallReview()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	PrimaryActorTick.bTickEvenWhenPaused = true;
}

void AFallReview::BeginPlay()
{
	Super::BeginPlay();
	const TCHAR* Cmd = FCommandLine::Get();
	FParse::Value(Cmd, TEXT("FallReview="), Dir);
	FParse::Value(Cmd, TEXT("FallBot="), Bot);
	FParse::Value(Cmd, TEXT("FallShot="), ShotEvery);
	FParse::Value(Cmd, TEXT("FallDuration="), Duration);
	bBursts = FParse::Param(Cmd, TEXT("FallBursts"));
	bPerf = FParse::Param(Cmd, TEXT("FallPerf"));
	StageDir = FPaths::ProjectSavedDir() / TEXT("Review") / FPaths::GetPathLeaf(FPaths::GetPath(Dir));
	if (FPaths::GetPath(Dir).IsEmpty()) StageDir = FPaths::ProjectSavedDir() / TEXT("Review");
	Dir = FPaths::ProjectSavedDir() / TEXT("Review") / Dir;
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	PF.CreateDirectoryTree(*Dir);
	LastReal = FPlatformTime::Seconds();

	if (AFallPlayer* P = Cast<AFallPlayer>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		if (!Bot.IsEmpty())
		{
			P->BotInput = [this](float Dt, FFallInputFrame& Out) { BotInput(Dt, Out); };
		}
	}
}

void AFallReview::BotInput(float Dt, FFallInputFrame& Out)
{
	// Scripted fall for the core movement review. Times in seconds since the start.
	struct FSeg { float T0; float Dive; float Slide; float YawRate; float PitchRate; };
	static const FSeg Core[] = {
		{0.f, 0.f, 0.f, 4.f, 0.f},
		{3.f, 1.f, 0.f, 0.f, 0.f},
		{7.f, 0.f, 0.f, 0.f, 0.f},
		{10.f, -1.f, 0.f, 0.f, 0.f},
		{13.f, 0.f, 0.f, 0.f, 0.f},
		{15.f, 0.f, -1.f, 0.f, 0.f},
		{18.f, 0.f, 1.f, 0.f, 0.f},
		{21.f, 1.f, 1.f, 0.f, 0.f},
		{25.f, -1.f, 0.f, 0.f, 0.f},
		{27.f, 0.f, 0.f, 45.f, 0.f},
		{31.f, 1.f, 0.f, 0.f, -4.f},
		{35.f, 0.f, 0.f, -10.f, 4.f},
		{1e9f, 0.f, 0.f, 0.f, 0.f}};
	const FSeg* S = Core;
	while ((S + 1)->T0 <= T) ++S;
	Out.Dive = S->Dive;
	Out.Slide = S->Slide;
	Out.Look = FVector2D(S->YawRate, S->PitchRate) * Dt;

	if (Out.Dive != LastBot.Dive || Out.Slide != LastBot.Slide)
	{
		if (bBursts && !bPerf)
		{
			BurstLeft = 12;
			BurstIndex = 0;
			BurstName = FString::Printf(TEXT("t%05.1f_dive%+d_slide%+d"), T, (int32)Out.Dive, (int32)Out.Slide);
		}
		if (Out.Dive != LastBot.Dive)
		{
			AFallPlayer* P = Cast<AFallPlayer>(UGameplayStatics::GetPlayerPawn(this, 0));
			FPress Pr{T, Frame, Out.Dive, P ? (float)P->BodyPivot->GetRelativeTransform().GetRotation().GetAngle() : 0.f, P ? P->FallSpeed : 0.f};
			Presses.Add(Pr);
		}
	}
	LastBot = Out;
}

void AFallReview::Shot(const FString& Name)
{
	if (bPerf || bShotThisFrame) return;
	bShotThisFrame = true;
	FScreenshotRequest::RequestScreenshot(Dir / Name + TEXT(".png"), false, false);
}

void AFallReview::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	bShotThisFrame = false;
	const double Now = FPlatformTime::Seconds();
	if (Frame > 5) FrameMs.Add((float)((Now - LastReal) * 1000.0));
	LastReal = Now;

	AFallPlayer* P = Cast<AFallPlayer>(UGameplayStatics::GetPlayerPawn(this, 0));
	UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this);

	// Latency and acceleration measurements per press.
	if (P)
	{
		const UFallMovementData* M = UFallMovementData::Resolve(P->Movement);
		const float Pitch = (float)P->BodyPivot->GetRelativeTransform().GetRotation().GetAngle();
		for (FPress& Pr : Presses)
		{
			if (Pr.PoseFrame < 0 && FMath::Abs(FMath::RadiansToDegrees(Pitch - Pr.PitchAtPress)) > 0.5f) Pr.PoseFrame = Frame - Pr.Frame;
			if (Pr.SpeedFrame < 0 && FMath::Abs(P->FallSpeed - Pr.SpeedAtPress) > 0.2f) Pr.SpeedFrame = Frame - Pr.Frame;
			if (Pr.Reached < 0.f)
			{
				if (Pr.Kind > 0.5f && P->FallSpeed >= M->DiveSpeed * 0.95f) Pr.Reached = T - Pr.Time;
				if (Pr.Kind < -0.5f && P->FallSpeed <= M->BrakeSpeed * 1.05f) Pr.Reached = T - Pr.Time;
			}
		}
		// Enemies on screen (combat metrics).
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		{
			bool bAnyEnemy = false, bOnScreen = false;
			int32 SX = 0, SY = 0;
			PC->GetViewportSize(SX, SY);
			for (TActorIterator<AFallBody> It(GetWorld()); It; ++It)
			{
				if (*It == P || It->IsDead()) continue;
				bAnyEnemy = true;
				FVector2D Scr;
				if (PC->ProjectWorldLocationToScreen(It->GetActorLocation(), Scr) && Scr.X > 0 && Scr.Y > 0 && Scr.X < SX && Scr.Y < SY)
				{
					bOnScreen = true;
				}
			}
			if (bAnyEnemy) { ++CombatFrames; if (bOnScreen) ++EnemyFrames; }
		}
	}
	if (FW)
	{
		for (; SeenEvents < FW->Events.Num(); ++SeenEvents)
		{
			const FFallEventRecord& E = FW->Events[SeenEvents];
			LongestGap = FMath::Max(LongestGap, E.Time - LastEventTime);
			LastEventTime = E.Time;
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("t"), E.Time);
			O->SetStringField(TEXT("name"), E.Name.ToString());
			EventLog.Add(MakeShared<FJsonValueObject>(O));
		}
	}

	// Screenshots: bursts around transitions take priority, then the regular interval.
	if (BurstLeft > 0)
	{
		Shot(FString::Printf(TEXT("burst_%s_%02d"), *BurstName, BurstIndex++));
		--BurstLeft;
	}
	else if (T >= NextShot)
	{
		NextShot += ShotEvery;
		Shot(FString::Printf(TEXT("shot_%04d_t%06.2f"), ShotIndex++, T));
	}

	T += DeltaSeconds;
	++Frame;
	if (T >= Duration && !bWritten)
	{
		WriteMetrics();
		bWritten = true;
		FPlatformMisc::RequestExit(false);
	}
}

void AFallReview::WriteMetrics()
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("bot"), Bot);
	Root->SetNumberField(TEXT("duration_s"), T);
	Root->SetNumberField(TEXT("frames"), Frame);

	TArray<TSharedPtr<FJsonValue>> PressArr;
	TArray<float> DiveTimes, BrakeTimes;
	int32 MaxPoseLat = 0, MaxSpeedLat = 0;
	for (const FPress& Pr : Presses)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("t"), Pr.Time);
		O->SetNumberField(TEXT("dive_input"), Pr.Kind);
		O->SetNumberField(TEXT("pose_latency_frames"), Pr.PoseFrame);
		O->SetNumberField(TEXT("speed_latency_frames"), Pr.SpeedFrame);
		O->SetNumberField(TEXT("time_to_target_speed_s"), Pr.Reached);
		PressArr.Add(MakeShared<FJsonValueObject>(O));
		MaxPoseLat = FMath::Max(MaxPoseLat, Pr.PoseFrame);
		MaxSpeedLat = FMath::Max(MaxSpeedLat, Pr.SpeedFrame);
		if (Pr.Kind > 0.5f && Pr.Reached > 0.f) DiveTimes.Add(Pr.Reached);
		if (Pr.Kind < -0.5f && Pr.Reached > 0.f) BrakeTimes.Add(Pr.Reached);
	}
	auto Avg = [](const TArray<float>& A) { float S = 0; for (float V : A) S += V; return A.Num() ? S / A.Num() : -1.f; };
	Root->SetArrayField(TEXT("presses"), PressArr);
	Root->SetNumberField(TEXT("max_pose_latency_frames"), MaxPoseLat);
	Root->SetNumberField(TEXT("max_speed_latency_frames"), MaxSpeedLat);
	Root->SetNumberField(TEXT("dive_time_to_max_speed_s"), Avg(DiveTimes));
	Root->SetNumberField(TEXT("brake_time_to_min_speed_s"), Avg(BrakeTimes));

	const UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this);
	const float EndGap = FW ? (FW->FallTime - LastEventTime) : 0.f;
	Root->SetNumberField(TEXT("longest_gap_without_events_s"), FMath::Max(LongestGap, EndGap));
	const int32 Near = FW ? FW->NearPasses : 0;
	Root->SetNumberField(TEXT("near_passes"), Near);
	Root->SetNumberField(TEXT("near_passes_per_s"), T > 0 ? Near / T : 0);
	Root->SetNumberField(TEXT("enemy_on_screen_pct"), CombatFrames ? 100.f * EnemyFrames / CombatFrames : -1.f);
	Root->SetArrayField(TEXT("events"), EventLog);

	TArray<float> Ms = FrameMs;
	Ms.Sort();
	float Sum = 0.f;
	for (float V : Ms) Sum += V;
	if (Ms.Num() > 0)
	{
		const float AvgMs = Sum / Ms.Num();
		const int32 N1 = FMath::Max(1, Ms.Num() / 100);
		float Worst = 0.f;
		for (int32 i = Ms.Num() - N1; i < Ms.Num(); ++i) Worst += Ms[i];
		Worst /= N1;
		Root->SetNumberField(TEXT("fps_avg"), 1000.f / AvgMs);
		Root->SetNumberField(TEXT("fps_min"), 1000.f / Ms.Last());
		Root->SetNumberField(TEXT("fps_1pct_low"), 1000.f / Worst);
		Root->SetBoolField(TEXT("fps_valid"), bPerf);
	}

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root.ToSharedRef(), W);
	FFileHelper::SaveStringToFile(Out, *(Dir / TEXT("metrics.json")));
	FFileHelper::SaveStringToFile(Out, *(StageDir / TEXT("metrics.json")));
}
