#include "FallDirector.h"
#include "FallPlayer.h"
#include "FallWorld.h"
#include "FallEnemy.h"
#include "FallDebris.h"
#include "FallFX.h"
#include "FallAudio.h"
#include "FallAirship.h"
#include "FreefallGameMode.h"
#include "Components/TextRenderComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/App.h"
#include "UObject/ConstructorHelpers.h"

AFallDirector::AFallDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Canopy = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Canopy"));
	Canopy->SetupAttachment(RootComponent);
	Canopy->SetUsingAbsoluteLocation(true);
	Canopy->SetUsingAbsoluteRotation(true);
	Canopy->SetUsingAbsoluteScale(true);
	Canopy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Canopy->SetVisibility(false);
	// Ram-air canopy: a long rectangular wing.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded()) Canopy->SetStaticMesh(Cube.Object);
}

AFallDirector* AFallDirector::Get(const UObject* WorldContext)
{
	UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!W) return nullptr;
	for (TActorIterator<AFallDirector> It(W); It; ++It) return *It;
	return nullptr;
}

const UFallSequence* AFallDirector::Seq() const
{
	return Sequence ? Sequence.Get() : (FallbackSequence ? FallbackSequence.Get() : GetDefault<UFallSequence>());
}

void AFallDirector::BeginPlay()
{
	Super::BeginPlay();
	if (!Sequence) Sequence = LoadObject<UFallSequence>(nullptr, TEXT("/Game/Freefall/Data/DA_FallSequence.DA_FallSequence"));
	if (!Sequence)
	{
		FallbackSequence = NewObject<UFallSequence>(this);
	}
	if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Freefall/Materials/M_FF_LitTwoSided.M_FF_LitTwoSided")))
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(M, this);
		MID->SetVectorParameterValue("Color", FLinearColor(1.f, 0.4f, 0.04f));
		MID->SetVectorParameterValue("Emissive", FLinearColor(0.35f, 0.1f, 0.01f)); // light through the fabric
		Canopy->SetMaterial(0, MID);
	}
	for (UTextRenderComponent* L : Labels) if (L) L->SetVisibility(false);
	FParse::Value(FCommandLine::Get(), TEXT("FallStartTime="), StartTime);
	bEventsEnabled = !FParse::Param(FCommandLine::Get(), TEXT("FallNoSequence"));
	// The real start happens on the first tick with a player (the pawn may spawn after the director).
}

FVector AFallDirector::RelToWorld(const FVector& RelMetres) const
{
	const UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this);
	const AActor* P = FW && FW->Player.IsValid() ? FW->Player.Get() : nullptr;
	const FRotator Yaw(0.f, P ? P->GetActorRotation().Yaw : 0.f, 0.f);
	return Yaw.RotateVector(RelMetres * 100.f);
}

void AFallDirector::JumpTo(float T)
{
	UWorld* W = GetWorld();
	UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this);
	const UFallSequence* S = Seq();
	if (!W || !FW || !S) return;

	// Clear the stage.
	for (TActorIterator<AFallEnemy> It(W); It; ++It) It->Destroy();
	for (TActorIterator<AFallAirship> It(W); It; ++It) It->Destroy();
	if (AFallDebrisSpawner* Sp = AFallDebrisSpawner::Get(this)) Sp->DestroyAll();

	Time = T;
	Checkpoint = FMath::Max(S->RestartPointBefore(T), (bCheckpointParachute && T >= Checkpoint) ? Checkpoint : 0.f);
	if (T <= 0.01f) bCheckpointParachute = false;
	DeathTime = -1.f;
	bLanded = false;
	UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
	bCanopy = false;
	bEnded = false;
	CrashTime = -1.f;
	Canopy->SetVisibility(false);
	FW->CloudDensity = 1.f;
	FW->bAltitudeLive = false;
	for (int32& R : Risers) { if (AFallFX* FX = AFallFX::Get(this)) FX->Beam(R, FVector::ZeroVector, FVector::ZeroVector, FLinearColor::Black, 1.f, false); R = INDEX_NONE; }
	FW->Altitude = S->AltitudeAt(T);

	// Rebuild persistent state from events before T; waves still running are resumed.
	NextEvent = 0;
	for (; NextEvent < S->Events.Num() && S->Events[NextEvent].Time < T; ++NextEvent)
	{
		const FFallSequenceEvent& E = S->Events[NextEvent];
		switch (E.Type)
		{
		case EFallEventType::CloudDensity: FW->CloudDensity = E.Value; break;
		case EFallEventType::AltitudeLive: FW->bAltitudeLive = true; FW->Altitude = E.Value - (T - E.Time) * 60.f; break;
		case EFallEventType::SegmentStart: CurrentSegment = E.Segment; break;
		case EFallEventType::DebrisWave:
			if (E.Time + E.Wave.Duration > T)
			{
				FFallDebrisWave Rest = E.Wave;
				Rest.Duration = E.Time + E.Wave.Duration - T;
				if (AFallDebrisSpawner* Sp = AFallDebrisSpawner::Get(this)) Sp->StartWave(Rest);
			}
			break;
		default: break;
		}
	}

	if (AFallPlayer* P = FW->Player.Get())
	{
		P->ResetBody();
		P->bHasParachute = false;
		P->SetActorLocation(FVector::ZeroVector);
		if (FParse::Param(FCommandLine::Get(), TEXT("FallGiveParachute")) || (bCheckpointParachute && T >= Checkpoint)) P->GiveParachute();
		if (T <= 0.01f && bEventsEnabled)
		{
			// Falling out of the airship: starts almost at rest and accelerates.
			P->FallSpeed = 3.f;
			if (APlayerController* PC = Cast<APlayerController>(P->GetController())) PC->SetControlRotation(FRotator(22.f, 0.f, 0.f));
		}
	}
	FW->FallTime = T;
	FW->NoteEvent("Restart");
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (AFallHUD* HUD = Cast<AFallHUD>(PC->GetHUD())) { HUD->FadeAlpha = 1.f; HUD->FadeColor = FLinearColor::Black; HUD->FadeSpeed = 3.f; }
	}
}

void AFallDirector::Fire(const FFallSequenceEvent& E)
{
	UWorld* W = GetWorld();
	UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this);
	AFallDebrisSpawner* Sp = AFallDebrisSpawner::Get(this);
	switch (E.Type)
	{
	case EFallEventType::SegmentStart:
		CurrentSegment = E.Segment;
		if (E.bRestartPoint) Checkpoint = E.Time;
		FW->NoteEvent(E.Segment);
		break;
	case EFallEventType::SegmentEnd:
		break;
	case EFallEventType::DebrisWave:
		if (Sp) Sp->StartWave(E.Wave);
		break;
	case EFallEventType::DebrisPiece:
		if (Sp) Sp->SpawnPiece(E.PieceType, RelToWorld(E.Position), RelToWorld(E.Velocity) / 100.f, E.Value, false);
		break;
	case EFallEventType::SpawnEnemy:
		AFallEnemy::Spawn(W, E.EnemyType, RelToWorld(E.Position), E.EnemyData);
		FW->NoteEvent("EnemySpawn");
		break;
	case EFallEventType::OneShot:
	{
		const FVector P = (FW->Player.IsValid() ? FW->Player->GetActorLocation() : FVector::ZeroVector) + RelToWorld(E.Position);
		if (E.OneShot == EFallOneShot::AirshipBreakup)
		{
			FActorSpawnParameters SP;
			SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			W->SpawnActor<AFallAirship>(AFallAirship::StaticClass(), FTransform(FRotator(0.f, FW->Player.IsValid() ? FW->Player->GetActorRotation().Yaw + 25.f : 0.f, 0.f), P), SP);
		}
		else if (E.OneShot == EFallOneShot::EngineExplosion)
		{
			if (AFallFX* FX = AFallFX::Get(this)) FX->Explosion(P, 1.f);
			FallSynth::Play(this, FallSynth::EKind::Explosion, P, 1.f);
			if (Sp)
			{
				for (int32 i = 0; i < 6; ++i)
				{
					Sp->SpawnPiece(i == 0 ? EFallDebrisType::Engine : EFallDebrisType::Shards, P - (FW->Player.IsValid() ? FW->Player->GetActorLocation() : FVector::ZeroVector),
						FVector(FMath::FRandRange(-8.f, 8.f), FMath::FRandRange(-8.f, 8.f), FMath::FRandRange(-12.f, 2.f)), FMath::FRandRange(0.7f, 1.2f), i < 2);
				}
			}
			if (AFallPlayer* Pl = FW->Player.Get())
			{
				const float D = FVector::Dist(Pl->GetActorLocation(), P);
				Pl->AddShake(FMath::Clamp(1.f - D / 4000.f, 0.15f, 1.f));
			}
		}
		else if (E.OneShot == EFallOneShot::BigFlyby && Sp)
		{
			Sp->SpawnPiece(EFallDebrisType::Section, RelToWorld(E.Position), FVector(0.f, 0.f, -30.f), 1.3f, true);
			FallSynth::Play(this, FallSynth::EKind::Whoosh, FVector::ZeroVector, 1.f, 0.5f);
		}
		FW->NoteEvent("OneShot");
		break;
	}
	case EFallEventType::CloudDensity:
		FW->CloudDensity = E.Value;
		break;
	case EFallEventType::AltitudeLive:
		FW->bAltitudeLive = true;
		if (E.Value > 0.f) FW->Altitude = E.Value;
		break;
	case EFallEventType::TargetFlee:
		for (TActorIterator<AFallEnemy> It(W); It; ++It) if (It->GetType() == EFallEnemyType::Target) It->StartFlee();
		FW->NoteEvent("TargetFlee");
		break;
	}
}

void AFallDirector::Deploy()
{
	bCanopy = true;
	CanopyTime = 0.f;
	Canopy->SetVisibility(true);
	if (AFallPlayer* P = UFallWorldSubsystem::Get(this)->Player.Get())
	{
		P->bCanopy = true;
		P->AddShake(1.f);
		P->HitStop(0.06f);
		P->AddImpulse(P->GetActorForwardVector() + FVector(0, 0, 0.4f), 2.5f); // the opening shock swings the body under the canopy
	}
	FallSynth::Play(this, FallSynth::EKind::Canopy, FVector::ZeroVector, 1.f);
	UFallWorldSubsystem::Get(this)->NoteEvent("CanopyOpen");
}

void AFallDirector::Crash()
{
	CrashTime = 0.f;
	if (AFallPlayer* P = UFallWorldSubsystem::Get(this)->Player.Get()) P->AddShake(1.5f);
	FallSynth::Play(this, FallSynth::EKind::Explosion, FVector::ZeroVector, 1.f, 0.7f);
	FallSynth::Play(this, FallSynth::EKind::HitBody, FVector::ZeroVector, 1.f, 0.5f);
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		// Impact: hard cut to black (the canopy ending fades to white instead).
		if (AFallHUD* HUD = Cast<AFallHUD>(PC->GetHUD())) { HUD->FadeAlpha = 1.f; HUD->FadeColor = FLinearColor::Black; HUD->FadeSpeed = 0.f; }
	}
	UFallWorldSubsystem::Get(this)->NoteEvent("Crash");
}

void AFallDirector::Tick(float Dt)
{
	Super::Tick(Dt);
	UWorld* W = GetWorld();
	if (!W) return;
	if (!W->IsGameWorld())
	{
		if (bPreview) DrawPreview();
		return;
	}
	UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this);
	AFallPlayer* P = FW ? FW->Player.Get() : nullptr;
	const UFallSequence* S = Seq();
	if (!P || !S) return;
	if (!bStarted)
	{
		bStarted = true;
		JumpTo(StartTime);
		return;
	}

	// Impact frame, then straight back to the restart point (well under a second).
	if (CrashTime >= 0.f)
	{
		CrashTime += Dt;
		if (CrashTime > 0.35f) JumpTo(Checkpoint);
		return;
	}
	if (P->bDied)
	{
		if (DeathTime < 0.f)
		{
			DeathTime = 0.f;
			UGameplayStatics::SetGlobalTimeDilation(this, 0.25f);
		}
		DeathTime += FApp::GetDeltaTime();
		if (DeathTime > 0.45f) JumpTo(Checkpoint);
		return;
	}
	if (bEnded)
	{
		EndTime += Dt;
		if (EndTime > 3.f) JumpTo(0.f);
		return;
	}

	Time += Dt;
	FW->FallTime = Time;
	while (bEventsEnabled && NextEvent < S->Events.Num() && S->Events[NextEvent].Time <= Time)
	{
		Fire(S->Events[NextEvent++]);
	}

	if (FW->bAltitudeLive) FW->Altitude -= P->FallSpeed * Dt * (bCanopy ? FMath::Clamp(CanopyTime - 2.5f, 1.f, 9.f) : 1.f);
	else FW->Altitude = S->AltitudeAt(Time);

	const float AGL = FW->HeightAboveGround();
	if (!bCanopy && AGL < 30.f && AFallFX::Get(this))
	{
		const FVector G = P->GetActorLocation() - FVector(0.f, 0.f, AGL * 100.f);
		AFallFX::Get(this)->Sprite(G + FMath::VRand() * 400.f, FMath::VRand() * 600.f, 600.f, 1800.f, 1.f, FLinearColor(0.25f, 0.2f, 0.14f), 0.8f, false, 1.f);
	}
	if (!bCanopy && P->bHasParachute && AGL < S->DeployHeight) Deploy();
	if (!bCanopy && AGL <= 3.f) { Crash(); return; }

	if (bCanopy)
	{
		CanopyTime += Dt;
		// Canopy: inflates with an overshoot above the player, risers to the shoulders.
		const float Inf = FMath::Clamp(CanopyTime / 0.7f, 0.f, 1.f);
		const float Over = 1.f + 0.15f * FMath::Sin(Inf * PI) ;
		const FVector Top = P->GetActorLocation() + FVector(0.f, 0.f, 750.f);
		Canopy->SetWorldLocation(Top);
		Canopy->SetWorldRotation(FRotator(0.f, P->GetActorRotation().Yaw, 0.f));
		Canopy->SetWorldScale3D(FVector(2.8f * FMath::Max(Inf, 0.3f) * Over, 8.5f * Inf * Over, 0.45f));
		if (AFallFX* FX = AFallFX::Get(this))
		{
			int32* Lines = Risers;
			const FVector Sh = P->Mesh->GetSocketLocation("spine_05");
			for (int32 i = 0; i < 4; ++i)
			{
				const FVector Edge = Top + FRotator(0.f, P->GetActorRotation().Yaw, 0.f).RotateVector(FVector((i % 2 ? 120.f : -120.f) * Inf, (i < 2 ? 400.f : -400.f) * Inf, -25.f));
				Lines[i] = FX->Beam(Lines[i], Sh, Edge, FLinearColor(0.15f, 0.15f, 0.15f), 0.8f, true);
			}
		}
		if (!bLanded && AGL < 2.f)
		{
			// Touchdown: the feet hit the meadow, the canopy sags and drops forward, a slow fade to white.
			bLanded = true;
			LandTime = 0.f;
			P->AddShake(0.6f);
			P->AddImpulse(FVector::UpVector, 1.5f);
			P->FallSpeed = 0.f;
			FallSynth::Play(this, FallSynth::EKind::HitBody, FVector::ZeroVector, 0.8f, 0.6f);
			FW->NoteEvent("Landed");
		}
		if (bLanded)
		{
			LandTime += Dt;
			FW->Altitude = FW->GroundHeightAt(P->GetActorLocation()) + 1.f;
			Canopy->SetWorldLocation(Top + P->GetActorForwardVector() * 300.f * FMath::Min(LandTime, 2.f) - FVector(0, 0, 350.f * FMath::Min(LandTime, 2.f)));
			Canopy->SetWorldRotation(FRotator(-35.f * FMath::Min(LandTime, 2.f), P->GetActorRotation().Yaw, 0.f));
			if (LandTime > 1.5f && !bEnded)
			{
				bEnded = true;
				EndTime = 0.f;
				if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
				{
					if (AFallHUD* HUD = Cast<AFallHUD>(PC->GetHUD())) { HUD->FadeColor = FLinearColor::White; HUD->FadeSpeed = -0.5f; HUD->FadeAlpha = 0.01f; }
				}
			}
		}
	}
}

void AFallDirector::DrawPreview()
{
	const UFallSequence* S = Seq();
	UWorld* W = GetWorld();
	if (!S || !W) return;
	const FVector O = GetActorLocation();

	// The player at the origin of the preview.
	DrawDebugCapsule(W, O, 90.f, 30.f, FQuat::Identity, FColor::White, false, -1.f, 0, 3.f);
	DrawDebugDirectionalArrow(W, O, O + FVector(300.f, 0, 0), 80.f, FColor::White, false, -1.f, 0, 3.f);

	auto TypeColor = [](const FFallSequenceEvent& E)
	{
		switch (E.Type)
		{
		case EFallEventType::SpawnEnemy:
			switch (E.EnemyType)
			{
			case EFallEnemyType::Shooter: return FColor(255, 60, 40);
			case EFallEnemyType::Wingsuiter: return FColor(255, 160, 0);
			case EFallEnemyType::Heavy: return FColor(190, 60, 255);
			default: return FColor(255, 230, 0);
			}
		case EFallEventType::DebrisWave: return FColor(120, 200, 255);
		case EFallEventType::DebrisPiece: return FColor(80, 255, 200);
		case EFallEventType::OneShot: return FColor(255, 120, 60);
		case EFallEventType::SegmentStart: return FColor::Green;
		default: return FColor(160, 160, 160);
		}
	};

	int32 Label = 0;
	auto Text = [&](const FVector& At, const FString& Str, const FColor& C, float Size = 60.f)
	{
		if (Labels.Num() <= Label)
		{
			UTextRenderComponent* T = NewObject<UTextRenderComponent>(this);
			T->SetupAttachment(RootComponent);
			T->SetHiddenInGame(true);
			T->SetHorizontalAlignment(EHTA_Center);
			T->RegisterComponent();
			Labels.Add(T);
		}
		UTextRenderComponent* T = Labels[Label++];
		T->SetVisibility(true);
		T->SetWorldLocation(At);
		T->SetText(FText::FromString(Str));
		T->SetTextRenderColor(C);
		T->SetWorldSize(Size);
		T->SetWorldRotation(FRotator(0.f, 180.f, 0.f));
	};

	// What is alive or about to appear around PreviewTime.
	for (const FFallSequenceEvent& E : S->Events)
	{
		const float Dt = E.Time - PreviewTime;
		const FColor C = TypeColor(E);
		const FString When = Dt >= 0.f ? FString::Printf(TEXT("+%.1fs"), Dt) : FString::Printf(TEXT("%.1fs"), Dt);
		if (E.Type == EFallEventType::DebrisWave && E.Time <= PreviewTime + PreviewWindow && E.Time + E.Wave.Duration >= PreviewTime)
		{
			const float Rel = (E.Wave.RelativeSpeed.X + E.Wave.RelativeSpeed.Y) * 0.5f;
			const float Z = Rel >= 0.f ? -4000.f : 4000.f;
			DrawDebugCircle(W, O + FVector(0, 0, Z), E.Wave.Spread * 100.f, 48, C, false, -1.f, 0, 4.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
			DrawDebugDirectionalArrow(W, O + FVector(E.Wave.Spread * 70.f, 0, Z), O + FVector(E.Wave.Spread * 70.f, 0, Z + Rel * 150.f), 200.f, C, false, -1.f, 0, 6.f);
			Text(O + FVector(E.Wave.Spread * 100.f, 0, Z), FString::Printf(TEXT("%s %s x%.1f/s  %.0f..%.0f m/s"), *When, *UEnum::GetDisplayValueAsText(E.Wave.Type).ToString(), E.Wave.Density, E.Wave.RelativeSpeed.X, E.Wave.RelativeSpeed.Y), C);
		}
		if (Dt < -2.f || Dt > PreviewWindow) continue;
		const FVector P = O + E.Position * 100.f;
		switch (E.Type)
		{
		case EFallEventType::SpawnEnemy:
			DrawDebugCapsule(W, P, 100.f, 40.f, FQuat::Identity, C, false, -1.f, 0, 4.f);
			DrawDebugLine(W, O, P, C, false, -1.f, 0, 1.f);
			Text(P + FVector(0, 0, 160.f), FString::Printf(TEXT("%s %s"), *When, *UEnum::GetDisplayValueAsText(E.EnemyType).ToString()), C);
			break;
		case EFallEventType::DebrisPiece:
			DrawDebugBox(W, P, FVector(400.f, 250.f, 150.f) * E.Value, C, false, -1.f, 0, 4.f);
			DrawDebugDirectionalArrow(W, P, P + E.Velocity * 150.f, 200.f, C, false, -1.f, 0, 4.f);
			Text(P + FVector(0, 0, 300.f), FString::Printf(TEXT("%s %s"), *When, *UEnum::GetDisplayValueAsText(E.PieceType).ToString()), C);
			break;
		case EFallEventType::OneShot:
			DrawDebugSphere(W, P, 400.f, 16, C, false, -1.f, 0, 3.f);
			Text(P + FVector(0, 0, 500.f), FString::Printf(TEXT("%s %s"), *When, *UEnum::GetDisplayValueAsText(E.OneShot).ToString()), C);
			break;
		default:
			break;
		}
	}

	// Timeline panel to the side: altitude (white) and fall speed (cyan) over time, segments, events, preview cursor.
	const FVector PanelO = O + FVector(0.f, -6000.f, -1500.f);
	const float Len = 12000.f, H = 3000.f;
	auto TX = [&](float T) { return PanelO + FVector(0.f, T / S->Duration * Len, 0.f); };
	DrawDebugLine(W, TX(0.f), TX(S->Duration), FColor(90, 90, 90), false, -1.f, 0, 4.f);
	FVector PrevA, PrevV;
	for (int32 i = 0; i <= 180; ++i)
	{
		const float T = S->Duration * i / 180.f;
		const FVector A = TX(T) + FVector(0, 0, S->AltitudeAt(T) / 10000.f * H);
		const FVector V = TX(T) + FVector(0, 0, S->SpeedAt(T) / 100.f * H);
		if (i > 0)
		{
			DrawDebugLine(W, PrevA, A, FColor::White, false, -1.f, 0, 5.f);
			DrawDebugLine(W, PrevV, V, FColor::Cyan, false, -1.f, 0, 5.f);
		}
		PrevA = A; PrevV = V;
	}
	for (const FFallSequenceEvent& E : S->Events)
	{
		const FColor C = TypeColor(E);
		if (E.Type == EFallEventType::SegmentStart)
		{
			DrawDebugLine(W, TX(E.Time), TX(E.Time) + FVector(0, 0, H), C, false, -1.f, 0, 6.f);
			Text(TX(E.Time) + FVector(0, 0, H + 120.f), E.Segment.ToString(), C, 120.f);
		}
		else
		{
			DrawDebugLine(W, TX(E.Time) - FVector(0, 0, 150.f), TX(E.Time), C, false, -1.f, 0, 8.f);
		}
	}
	DrawDebugLine(W, TX(PreviewTime) - FVector(0, 0, 300.f), TX(PreviewTime) + FVector(0, 0, H + 300.f), FColor::Yellow, false, -1.f, 0, 10.f);
	Text(TX(PreviewTime) - FVector(0, 0, 450.f), FString::Printf(TEXT("%.1f s  %.0f m  %.0f m/s"), PreviewTime, S->AltitudeAt(PreviewTime), S->SpeedAt(PreviewTime)), FColor::Yellow, 140.f);

	for (int32 i = Label; i < Labels.Num(); ++i) Labels[i]->SetVisibility(false);
}
