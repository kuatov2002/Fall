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
#include "FallEnemy.h"
#include "FallDebris.h"
#include "FallDirector.h"
#include "FallAirship.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "UObject/UObjectIterator.h"

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
	if (!FParse::Value(Cmd, TEXT("FallReview="), Dir)) { SetActorTickEnabled(false); return; } // placed in a map (LookDev) but not reviewing
	FParse::Value(Cmd, TEXT("FallBot="), Bot);
	FParse::Value(Cmd, TEXT("FallShot="), ShotEvery);
	FParse::Value(Cmd, TEXT("FallDuration="), Duration);
	bBursts = FParse::Param(Cmd, TEXT("FallBursts"));
	bPerf = FParse::Param(Cmd, TEXT("FallPerf"));
	FString Skill;
	bAvg = FParse::Value(Cmd, TEXT("FallBotSkill="), Skill) && Skill == TEXT("avg");
	if (FParse::Param(Cmd, TEXT("FallDamageTest"))) Bot = TEXT("damage");
	if (FParse::Param(Cmd, TEXT("FallGrabTest"))) Bot = TEXT("grab");
	if (FParse::Param(Cmd, TEXT("FallWeaponTest"))) { Bot = TEXT("weapon"); Duration = 1e9f; }
	bCloseups = FParse::Param(Cmd, TEXT("FallCloseups"));
	if (FParse::Param(Cmd, TEXT("FallBakeReload"))) { Bot = TEXT("bake"); Duration = 1e9f; ShotEvery = 0.1f; AFallBody::bBakeReload = true; }
	if (FParse::Param(Cmd, TEXT("FallNoShots"))) ShotEvery = 1e9f;
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

void AFallReview::BotFull(float Dt, FFallInputFrame& Out)
{
	// A player-like bot for the whole fall: flies a base pattern, reacts to enemies, debris and the chase.
	AFallPlayer* P = Cast<AFallPlayer>(UGameplayStatics::GetPlayerPawn(this, 0));
	UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this);
	if (!P || !FW) return;
	BotClock += Dt;
	const FVector PP = P->GetActorLocation();
	const FVector CamLoc = P->Camera->GetComponentLocation();
	const FRotator CamRot = P->Camera->GetComponentRotation();

	// Base pattern: alternate belly, dive and brake so every stance shows up.
	const float Cycle = FMath::Fmod(BotClock, 14.f);
	Out.Dive = Cycle < 6.f ? 0.f : (Cycle < 9.f ? 1.f : (Cycle < 11.f ? -1.f : 0.f));
	Out.Slide = FMath::Sin(BotClock * 0.45f) > 0.6f ? 1.f : (FMath::Sin(BotClock * 0.45f) < -0.6f ? -1.f : 0.f);

	AFallEnemy* Target = nullptr;
	AFallEnemy* Nearest = nullptr;
	float NearestD = 4500.f;
	bool bThreat = false;
	for (TActorIterator<AFallEnemy> It(GetWorld()); It; ++It)
	{
		if ((It->State == EFallEnemyState::Dead && !It->CanBeRobbed()) || It->State == EFallEnemyState::Robbed) continue;
		if (It->GetType() == EFallEnemyType::Target) Target = *It;
		if (It->State == EFallEnemyState::Dead) continue;
		const float D = FVector::Dist(It->GetActorLocation(), PP);
		if (D < NearestD && It->GetType() != EFallEnemyType::Target) { NearestD = D; Nearest = *It; }
		if (It->IsTelegraphing() && D < 3500.f) bThreat = true;
	}

	// The chase: dive at the parachute carrier, steer onto it, shoot it, take the parachute when it is stunned.
	const bool bChase = Target && Target->State != EFallEnemyState::Approach && Target->State != EFallEnemyState::Hold;
	AFallEnemy* Aim = Nearest;
	if (bChase && !P->bHasParachute)
	{
		Aim = Target;
		const FVector To = Target->GetActorLocation() - PP;
		Out.Dive = To.Z < -300.f ? 1.f : (To.Z > 300.f ? -1.f : 0.f);
		const FVector Local = P->GetActorQuat().UnrotateVector(FVector(To.X, To.Y, 0.f));
		Out.Slide = FMath::Clamp(Local.Y / 600.f, -1.f, 1.f);
		if (Target->CanBeRobbed() && To.Size() < 600.f) Out.bKick = !LastBot.bKick;
		if (Target->CanBeRobbed()) { Out.Dive = To.Z < -150.f ? 1.f : (To.Z > 150.f ? -1.f : 0.f); Out.Slide = FMath::Clamp(Local.Y / 300.f, -1.f, 1.f); }
	}
	if (P->bHasParachute) { Out.Dive = 0.f; Out.Slide = 0.f; Aim = nullptr; }

	// Aim: turn the camera onto the chosen enemy, fire when the crosshair is on it.
	if (Aim)
	{
		const FVector AimAt = Aim->GetActorLocation() + Aim->GetVelocity() * 0.05f;
		FRotator Want = (AimAt - CamLoc).Rotation();
		if (bAvg) { Want.Yaw += 3.f * FMath::PerlinNoise1D(BotClock * 0.7f); Want.Pitch += 3.f * FMath::PerlinNoise1D(BotClock * 0.7f + 31.f); }
		const float DYaw = FRotator::NormalizeAxis(Want.Yaw - CamRot.Yaw);
		const float DPitch = FRotator::NormalizeAxis(Want.Pitch - CamRot.Pitch);
		const float Gain = bAvg ? 3.f : 6.f;
		Out.Look = FVector2D(FMath::Clamp(DYaw * Gain * Dt, -6.f, 6.f), FMath::Clamp(DPitch * Gain * Dt, -4.f, 4.f));
		const float Err = FMath::Sqrt(DYaw * DYaw + DPitch * DPitch);
		Out.bFire = Err < (bAvg ? 6.f : 4.f) && FVector::Dist(Aim->GetActorLocation(), PP) < 4000.f && !Aim->CanBeRobbed();
		if (FVector::Dist(Aim->GetActorLocation(), PP) < 380.f && !Aim->CanBeRobbed()) Out.bKick = !LastBot.bKick;
	}
	else
	{
		Out.Look = FVector2D(FMath::Sin(BotClock * 0.3f) * 10.f * Dt, -CamRot.Pitch * 0.5f * Dt - 3.f * Dt);
	}

	// Tuning runs (-FallLookSun): the camera stays on the sunward horizon, where the haze is hardest to get right.
	if (FParse::Param(FCommandLine::Get(), TEXT("FallLookSun")))
	{
		const FRotator Cur = P->Camera->GetComponentRotation();
		Out.Look = FVector2D(FRotator::NormalizeAxis(215.f - Cur.Yaw), FRotator::NormalizeAxis(-8.f - Cur.Pitch)) * FMath::Min(1.f, Dt * 4.f);
		return;
	}
	// Right after the jump a player looks back up at the burning airship for a few seconds.
	if (!P->bInIntro && T < AfterIntroUntil - 1.f)
	{
		for (TActorIterator<AFallAirship> It(GetWorld()); It; ++It)
		{
			const FRotator Want = (It->GetActorLocation() - P->Camera->GetComponentLocation()).Rotation();
			const FRotator Cur = P->Camera->GetComponentRotation();
			Out.Look = FVector2D(FRotator::NormalizeAxis(Want.Yaw - Cur.Yaw), FRotator::NormalizeAxis(Want.Pitch - Cur.Pitch)) * FMath::Min(1.f, Dt * 3.f);
			break;
		}
	}
	// Cover: grab a piece when an attack is announced, push off a couple of seconds later.
	if (P->IsHolding())
	{
		bFirstGrabDone = true;
		HoldTime += Dt;
		Out.Dive = 0.f;
		Out.Slide = 0.f;
		if (HoldTime > 2.5f) Out.bPush = !LastBot.bPush;
	}
	else
	{
		HoldTime = 0.f;
		if (!bAvg)
		{
			// Like a player meeting the first walkway: it grabs the first piece in reach once, then for cover.
			if ((bThreat || !bFirstGrabDone || FMath::Fmod(BotClock, 17.f) < 0.1f) && P->FindGrabbable() && !bChase) Out.bGrab = !LastBot.bGrab;
			if (!bFirstGrabDone && !bChase && !bThreat)
			{
				// Steer to the nearest grabbable piece until the first grab.
				AFallDebris* Near = nullptr; float ND = 2500.f;
				for (TActorIterator<AFallDebris> It(GetWorld()); It; ++It)
				{
					if (!It->bGrabbable || It->Holder.IsValid()) continue;
					const float D = It->DistanceTo(PP);
					if (D < ND) { ND = D; Near = *It; }
				}
				if (Near)
				{
					const FVector To = Near->GetActorLocation() - PP;
					const FVector Local = P->GetActorQuat().UnrotateVector(FVector(To.X, To.Y, 0.f));
					Out.Slide = FMath::Clamp(Local.Y / 400.f, -1.f, 1.f);
					Out.Dive = To.Z < -200.f ? 1.f : (To.Z > 200.f ? -1.f : 0.f);
					if (ND < 800.f) Out.bGrab = !LastBot.bGrab;
				}
			}
		}
		else if (bThreat && !bChase)
		{
			AFallDebris* Best = nullptr; float BestD = 1500.f;
			for (TActorIterator<AFallDebris> It(GetWorld()); It; ++It)
			{
				if (!It->bGrabbable || It->Holder.IsValid()) continue;
				const float D = It->DistanceTo(PP);
				if (D < BestD) { BestD = D; Best = *It; }
			}
			if (Best)
			{
				const FVector To = Best->GetActorLocation() - PP;
				const FVector Local = P->GetActorQuat().UnrotateVector(FVector(To.X, To.Y, 0.f));
				Out.Slide = FMath::Clamp(Local.Y / 400.f, -1.f, 1.f);
				Out.Dive = To.Z < -200.f ? 1.f : (To.Z > 200.f ? -1.f : 0.f);
				if (BestD < 800.f && BotClock - GrabPressT > 0.5f) { Out.bGrab = true; GrabPressT = BotClock; }
			}
		}
	}
	if (P->Ammo <= 0) Out.bReload = true;
}

void AFallReview::BotInput(float Dt, FFallInputFrame& Out)
{
	if (Bot == TEXT("damage")) return;
	if (Bot == TEXT("grab")) { Out.bGrab = bGrabPress; bGrabPress = false; return; }
	if (Bot == TEXT("weapon")) { WeaponTestInput(Dt, Out); return; }
	if (Bot == TEXT("bake"))
	{
		Out.bAim = Out.bFire = T >= 1.f && T < 1.05f;
		Out.bReload = T >= 1.8f && T < 1.85f;
		return;
	}
	if (Bot == TEXT("full"))
	{
		BotFull(Dt, Out);
		if (bAvg)
		{
			// 250 ms reaction delay: the body acts on what the bot decided a quarter second ago.
			const FFallInputFrame Decided = Out;
			Delayed.Add(TPair<float, FFallInputFrame>(T, Decided));
			FFallInputFrame Old;
			while (Delayed.Num() > 0 && Delayed[0].Key <= T - 0.25f) { Old = Delayed[0].Value; Delayed.RemoveAt(0); }
			// Look deltas are re-summed so the delayed stream turns as far as the decisions did.
			Out = Old;
			LastBot = Decided;
			return;
		}
		if (bBursts && !bPerf && (Out.Dive != LastBot.Dive) && BurstLeft <= 0)
		{
			BurstLeft = 10; BurstIndex = 0;
			BurstName = FString::Printf(TEXT("t%05.1f_dive%+d"), T, (int32)Out.Dive);
		}
		LastBot = Out;
		return;
	}
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
	if (TSharedPtr<FJsonObject> O = DescribeFrame())
	{
		O->SetStringField(TEXT("file"), Name + TEXT(".png"));
		ShotLog.Add(MakeShared<FJsonValueObject>(O));
	}
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
			if (SeenEvents == 0) LastEventTime = E.Time;
			LongestGap = FMath::Max(LongestGap, E.Time - LastEventTime);
			LastEventTime = E.Time;
			if (E.Name == FName("Landed")) bPassed = true;
			if (E.Name == FName("PlayerDeath") || E.Name == FName("Crash")) ++Deaths;
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("t"), E.Time);
			O->SetStringField(TEXT("name"), E.Name.ToString());
			EventLog.Add(MakeShared<FJsonValueObject>(O));
		}
	}

	// Key moments get a burst too.
	if (FW && bBursts && !bPerf)
	{
		static const FName Key[] = {"EnemySpawn", "Grab", "PushOff", "Kick", "Kill", "DebrisHit", "ParachuteTaken", "CanopyOpen", "Crash", "TargetStunned", "OneShot", "PlayerDeath"};
		for (; SeenBurstEvents < FW->Events.Num(); ++SeenBurstEvents)
		{
			const FName N = FW->Events[SeenBurstEvents].Name;
			for (const FName& K : Key)
			{
				if (N == K && BurstLeft <= 0) { BurstLeft = 10; BurstIndex = 0; BurstName = FString::Printf(TEXT("t%05.1f_%s"), T, *N.ToString()); }
			}
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

	if (T >= NextPrimScan) { NextPrimScan = T + 1.f; ScanPrimitives(); }
	if (P)
	{
		FString Pair;
		const float Pen = P->MeasureSelfIntersection(&Pair);
		++SelfIntFrames;
		if (Pen > 1.f)
		{
			++SelfIntBad;
			const FString Ctx = P->IsReloading() ? TEXT("reload") : (P->IsHolding() ? TEXT("hold") : (P->Control.bAim ? TEXT("aim") : TEXT("free")));
			SelfIntByCase.FindOrAdd(Pair + TEXT(" / ") + Ctx) += 1;
			if (Pen > 4.f && T >= NextSelfShot) { NextSelfShot = T + 4.f; Shot(FString::Printf(TEXT("selfint_t%05.1f_%s"), T, *Pair.Replace(TEXT(" "), TEXT("_")))); }
		}
		if (Pen > SelfIntWorst) { SelfIntWorst = Pen; SelfIntPair = FString::Printf(TEXT("%s at %.1f s"), *Pair, T); }
	}
	if (P) CheckWeapon(P, FW);
	if (P) CheckReloadAndRecoil(P, P->MeasureSelfIntersection());
	if (P) CheckTandem(P);
	if (P && Bot == TEXT("bake")) TickBake(P);
	if (Bot == TEXT("damage")) TickDamageTest(DeltaSeconds);
	if (Bot == TEXT("grab")) TickGrabTest(DeltaSeconds);
	T += DeltaSeconds;
	++Frame;
	if ((T >= Duration || (bPassed && Bot != TEXT("damage") && Bot != TEXT("grab") && Bot != TEXT("weapon") && Bot != TEXT("bake"))) && !bWritten)
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
	Root->SetStringField(TEXT("skill"), bAvg ? TEXT("avg") : TEXT("good"));
	Root->SetBoolField(TEXT("passed"), bPassed);
	{
		TSharedPtr<FJsonObject> PO = MakeShared<FJsonObject>();
		for (const TPair<FString, int32>& Pr : Primitives) PO->SetNumberField(Pr.Key, Pr.Value);
		Root->SetObjectField(TEXT("engine_primitives"), PO);
		Root->SetNumberField(TEXT("engine_primitive_kinds"), Primitives.Num());
		Root->SetNumberField(TEXT("self_intersection_frames"), SelfIntFrames);
		Root->SetNumberField(TEXT("self_intersection_bad_frames"), SelfIntBad);
		Root->SetNumberField(TEXT("self_intersection_worst_cm"), SelfIntWorst);
		Root->SetStringField(TEXT("self_intersection_worst"), SelfIntPair);
		TSharedPtr<FJsonObject> SC = MakeShared<FJsonObject>();
		for (const TPair<FString, int32>& Pr : SelfIntByCase) SC->SetNumberField(Pr.Key, Pr.Value);
		Root->SetObjectField(TEXT("self_intersection_by_case"), SC);
	}
	Root->SetNumberField(TEXT("deaths"), Deaths);
	WriteWeaponMetrics(Root);
	if (Bot == TEXT("damage") || Bot == TEXT("grab"))
	{
		Root->SetArrayField(TEXT("damage_cases"), DmgResults);
		Root->SetNumberField(TEXT("damage_fails"), DmgFails);
		Root->SetBoolField(TEXT("damage_pass"), DmgFails == 0 && DmgResults.Num() > 0);
	}

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
	{
		FString Idx;
		TSharedRef<TJsonWriter<>> IW = TJsonWriterFactory<>::Create(&Idx);
		FJsonSerializer::Serialize(ShotLog, IW);
		FFileHelper::SaveStringToFile(Idx, *(Dir / TEXT("shots_index.json")));
	}
	FFileHelper::SaveStringToFile(Out, *(StageDir / TEXT("metrics.json")));
}

void AFallReview::TickDamageTest(float Dt)
{
	// Each enemy type, three distances, three lateral speeds: the player's own weapon fires at the dummy
	// until it dies; the kill must come on exactly ceil(health / damage) hits.
	AFallPlayer* P = Cast<AFallPlayer>(UGameplayStatics::GetPlayerPawn(this, 0));
	UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this);
	if (!P || !FW || T < 1.f) return;
	static const float Dists[] = {500.f, 2000.f, 4000.f};
	static const float Speeds[] = {0.f, 2000.f, 4000.f};
	const int32 NumCases = 4 * 3 * 3;
	const UFallWeaponData* W = UFallWeaponData::Resolve(P->Weapon);
	auto CaseType = [](int32 C) { return (EFallEnemyType)(C / 9); };
	auto Finish = [&](bool bOk, const TCHAR* Why)
	{
		AFallEnemy* E = DmgDummy.Get();
		const int32 Expected = E ? FMath::CeilToInt(E->MaxHealth / W->Damage) : -1;
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("type"), (int32)CaseType(DmgCase));
		O->SetNumberField(TEXT("distance_cm"), Dists[(DmgCase / 3) % 3]);
		O->SetNumberField(TEXT("lateral_cms"), Speeds[DmgCase % 3]);
		O->SetNumberField(TEXT("health"), E ? E->MaxHealth : -1.f);
		O->SetNumberField(TEXT("expected_hits"), Expected);
		O->SetNumberField(TEXT("hits"), DmgHits);
		O->SetNumberField(TEXT("shots"), DmgShots);
		O->SetBoolField(TEXT("pass"), bOk);
		O->SetStringField(TEXT("note"), Why);
		DmgResults.Add(MakeShared<FJsonValueObject>(O));
		if (!bOk) ++DmgFails;
		UE_LOG(LogTemp, Display, TEXT("FallDamageTest case %d type %d dist %.0f speed %.0f: %s hits %d/%d shots %d %s"), DmgCase, (int32)CaseType(DmgCase), Dists[(DmgCase / 3) % 3], Speeds[DmgCase % 3], bOk ? TEXT("PASS") : TEXT("FAIL"), DmgHits, Expected, DmgShots, Why);
		if (E) E->Destroy();
		DmgDummy = nullptr;
	};

	if (!DmgDummy.IsValid())
	{
		++DmgCase;
		if (DmgCase >= NumCases)
		{
			UE_LOG(LogTemp, Display, TEXT("FallDamageTest %s: %d of %d cases failed"), DmgFails == 0 ? TEXT("PASS") : TEXT("FAIL"), DmgFails, NumCases);
			Duration = 0.f; // writes metrics and exits on this frame
			return;
		}
		const FVector Fwd = P->Camera->GetForwardVector();
		const FVector Right = P->Camera->GetRightVector();
		const FVector Rel = P->Camera->GetComponentLocation() + Fwd * Dists[(DmgCase / 3) % 3] - P->GetActorLocation();
		AFallEnemy* E = AFallEnemy::Spawn(GetWorld(), CaseType(DmgCase), Rel, nullptr, true);
		if (E) E->VelH = Right * Speeds[DmgCase % 3];
		DmgDummy = E;
		DmgCaseT = 0.f;
		DmgHits = 0;
		DmgShots = 0;
		DmgSeen = FW->Events.Num();
		return;
	}
	AFallEnemy* E = DmgDummy.Get();
	DmgCaseT += Dt;
	for (; DmgSeen < FW->Events.Num(); ++DmgSeen)
	{
		const FName N = FW->Events[DmgSeen].Name;
		if (N == FName("Hit") || N == FName("Kill")) ++DmgHits;
	}
	const int32 Expected = FMath::CeilToInt(E->MaxHealth / W->Damage);
	if (E->IsDead())
	{
		Finish(DmgHits == Expected, DmgHits == Expected ? TEXT("killed") : TEXT("killed with a wrong hit count"));
		return;
	}
	if (DmgHits >= Expected) { Finish(false, TEXT("survived the computed number of hits")); return; }
	if (DmgCaseT > 12.f) { Finish(false, TEXT("timeout")); return; }
	if (DmgCaseT < 0.3f) return;
	// Keep the dummy in the shooting lane: it slides sideways, the test wraps it back.
	const FVector Rel = E->GetActorLocation() - P->Camera->GetComponentLocation();
	if (FMath::Abs(Rel | P->Camera->GetRightVector()) > 1500.f) E->AddActorWorldOffset(-P->Camera->GetRightVector() * 2.f * (Rel | P->Camera->GetRightVector()));
	P->Ammo = 24;
	const int32 Before = P->Ammo;
	P->AimPoint = E->GetActorLocation();
	P->Fire();
	if (P->Ammo < Before) ++DmgShots;
}

void AFallReview::TickGrabTest(float Dt)
{
	// A grabbable panel rises at the player. With a grab press (on time or up to 0.6 s early) the player must hold it
	// and take no damage; without a press a slow contact must not hurt; a fast unguarded hit must hurt.
	struct FCase { float Speed; float PressLead; bool bExpectGrab; bool bExpectDamage; const TCHAR* Name; };
	static const FCase Cases[] = {
		{3.f, 0.f, true, false, TEXT("slow, press on arrival")},
		{8.f, 0.f, true, false, TEXT("medium, press on arrival")},
		{14.f, 0.f, true, false, TEXT("fast, press on arrival")},
		{8.f, 0.6f, true, false, TEXT("medium, press 0.6 s early")},
		{5.f, -1.f, false, false, TEXT("slow, no press")},
		{12.f, -1.f, false, false, TEXT("medium, no press")},
		{28.f, -1.f, false, true, TEXT("very fast, no press")},
	};
	const int32 NumCases = UE_ARRAY_COUNT(Cases);
	AFallPlayer* P = Cast<AFallPlayer>(UGameplayStatics::GetPlayerPawn(this, 0));
	AFallDebrisSpawner* Sp = AFallDebrisSpawner::Get(this);
	if (!P || !Sp || T < 1.f) return;
	auto Finish = [&](const TCHAR* Why)
	{
		const FCase& C = Cases[DmgCase];
		const bool bDamaged = P->Health < P->MaxHealth;
		const bool bOk = bGrabbed == C.bExpectGrab && bDamaged == C.bExpectDamage;
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("case"), C.Name);
		O->SetBoolField(TEXT("grabbed"), bGrabbed);
		O->SetNumberField(TEXT("damage"), P->MaxHealth - P->Health);
		O->SetBoolField(TEXT("pass"), bOk);
		O->SetStringField(TEXT("note"), Why);
		DmgResults.Add(MakeShared<FJsonValueObject>(O));
		if (!bOk) ++DmgFails;
		UE_LOG(LogTemp, Display, TEXT("FallGrabTest %s: %s grabbed %d damage %.1f (%s)"), C.Name, bOk ? TEXT("PASS") : TEXT("FAIL"), bGrabbed ? 1 : 0, P->MaxHealth - P->Health, Why);
		if (P->IsHolding()) P->Release();
		if (AFallDebris* D = GrabPiece.Get()) D->Destroy();
		GrabPiece = nullptr;
	};
	if (!GrabPiece.IsValid())
	{
		++DmgCase;
		if (DmgCase >= NumCases)
		{
			UE_LOG(LogTemp, Display, TEXT("FallGrabTest %s: %d of %d cases failed"), DmgFails == 0 ? TEXT("PASS") : TEXT("FAIL"), DmgFails, NumCases);
			Duration = 0.f;
			return;
		}
		P->Health = P->MaxHealth;
		P->VelocityH = FVector::ZeroVector;
		const FCase& C = Cases[DmgCase];
		GrabPiece = Sp->SpawnPiece(EFallDebrisType::Plate, FVector(0, 0, -C.Speed * 100.f * 2.5f), FVector(0, 0, C.Speed), 1.f, false);
		DmgCaseT = 0.f;
		bGrabPressed = false;
		bGrabbed = false;
		return;
	}
	const FCase& C = Cases[DmgCase];
	AFallDebris* D = GrabPiece.Get();
	DmgCaseT += Dt;
	D->Mover->VelocityH = FVector::ZeroVector; // straight at the player
	const float Dist = D->DistanceTo(P->GetActorLocation());
	const float Eta = Dist / (C.Speed * 100.f);
	if (C.PressLead >= 0.f && !bGrabPressed && (Eta <= C.PressLead + Dt || (C.PressLead == 0.f && Dist < 100.f))) { bGrabPress = true; bGrabPressed = true; }
	if (P->IsHolding()) bGrabbed = true;
	if (bGrabbed && DmgCaseT > 0.5f + 2.5f) { Finish(TEXT("held")); return; }
	if (DmgCaseT > 5.5f) Finish(TEXT("piece passed"));
}

void AFallReview::ScanPrimitives()
{
	// Every visible static mesh whose asset lives in the engine's utility folders counts, whoever spawned it.
	// Invisible collision-only components are skipped. The gate is zero entries.
	TMap<FString, int32> Now;
	for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
	{
		UStaticMeshComponent* C = *It;
		if (!C || C->GetWorld() != GetWorld() || !C->IsRegistered() || !C->IsVisible() || C->bHiddenInGame) continue;
		if (C->GetOwner() && C->GetOwner()->IsHidden()) continue;
		const UStaticMesh* M = C->GetStaticMesh();
		if (!M) continue;
		const FString Path = M->GetPathName();
		if (!Path.StartsWith(TEXT("/Engine/"))) continue;
		const FString Key = FString::Printf(TEXT("%s | %s"), C->GetOwner() ? *C->GetOwner()->GetClass()->GetName() : TEXT("none"), *M->GetPackage()->GetName());
		Now.FindOrAdd(Key) += FMath::Max(1, (C->IsA<UInstancedStaticMeshComponent>() ? Cast<UInstancedStaticMeshComponent>(C)->GetInstanceCount() : 1));
	}
	for (const TPair<FString, int32>& Pr : Now)
	{
		int32& Max = Primitives.FindOrAdd(Pr.Key);
		Max = FMath::Max(Max, Pr.Value);
	}
}
