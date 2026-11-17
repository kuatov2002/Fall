#include "FallDirector.h"
#include "FallArt.h"
#include "FallIntro.h"
#include "Sound/SoundBase.h"
#include "Kismet/GameplayStatics.h"
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
	for (int32& R : Risers) R = INDEX_NONE;
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
	Canopy->SetStaticMesh(UFallArtData::Get()->Canopy.LoadSynchronous());
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

void AFallDirector::JumpTo(float T, bool bFromIntro)
{
	UWorld* W = GetWorld();
	UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this);
	const UFallSequence* S = Seq();
	if (!W || !FW || !S) return;

	// Clear the stage.
	for (TActorIterator<AFallEnemy> It(W); It; ++It) It->Destroy();
	for (TActorIterator<AFallAirship> It(W); It; ++It) if (!(bFromIntro && It->bIntroShip)) It->Destroy();
	if (AFallDebrisSpawner* Sp = AFallDebrisSpawner::Get(this)) Sp->DestroyAll();

	Time = T;
	Checkpoint = FMath::Max(S->RestartPointBefore(T), (bCheckpointParachute && T >= Checkpoint) ? Checkpoint : 0.f);
	if (T <= 0.01f) bCheckpointParachute = false;
	DeathTime = -1.f;
	bLanded = false;
	UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
	bCanopy = false;
	bShockDone = false;
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

	if (AFallPlayer* P = bFromIntro ? nullptr : FW->Player.Get())
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
		if (AFallHUD* HUD = Cast<AFallHUD>(PC->GetHUD())) { if (!bFromIntro) { HUD->FadeAlpha = 1.f; HUD->FadeColor = FLinearColor::Black; HUD->FadeSpeed = 3.f; } }
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
		AFallAirship* IntroShip = nullptr;
		for (TActorIterator<AFallAirship> It(W); It; ++It) if (It->bIntroShip) IntroShip = *It;
		if (E.OneShot == EFallOneShot::AirshipBreakup && IntroShip)
		{
			IntroShip->BreakupDelay = IntroShip->GetAge() + 2.2f; // breaks up above the falling player
		}
		else if (E.OneShot == EFallOneShot::AirshipBreakup)
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

void AFallDirector::LineSegments(TArray<TPair<FVector, FVector>>& Out) const
{
	if (const AFallFX* FX = AFallFX::Get(this))
	{
		for (int32 R : Risers) { FVector A, B; if (FX->BeamEnds(R, A, B)) Out.Add(TPair<FVector, FVector>(A, B)); }
	}
}

void AFallDirector::Deploy()
{
	if (bCanopy) return;
	bCanopy = true;
	bShockDone = false;
	CanopyTime = 0.f;
	Canopy->SetVisibility(false); // the bag comes out first; the canopy shows once the lines are out
	UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this);
	// The descent under the canopy is time-compressed to ~15 s whatever the height of the pull.
	CanopyDescent = FMath::Max(14.f, FW->HeightAboveGround() / (6.f * 15.f));
	if (AFallPlayer* P = FW->Player.Get()) P->bCanopy = true;
	FallSynth::Play(this, FallSynth::EKind::Canopy, FVector::ZeroVector, 0.5f, 1.3f); // the bag leaving the container
	FW->NoteEvent("Ripcord");
}

// Opening shock: the canopy has filled. Both bodies jerk, fabric snaps, the rush of air is gone.
static void FallCanopyShock(AFallDirector* D, AFallPlayer* P)
{
	P->AddShake(1.2f);
	P->HitStop(0.07f);
	P->AddImpulse(P->GetActorForwardVector() + FVector(0, 0, 0.6f), 2.5f); // swings the bodies under the canopy
	FallSynth::Play(D, FallSynth::EKind::Canopy, FVector::ZeroVector, 1.f);
	if (const TSoftObjectPtr<USoundBase>* Pop = UFallArtData::Get()->Sounds.Find("CanopyPop"))
		if (USoundBase* S = Pop->LoadSynchronous()) UGameplayStatics::PlaySound2D(D, S, 1.3f, 0.8f);
	UFallWorldSubsystem::Get(D)->NoteEvent("CanopyOpen");
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
		if (AFallHUD* HUD = Cast<AFallHUD>(PC->GetHUD())) { HUD->FadeAlpha = 0.f; HUD->FadeColor = FLinearColor::Black; HUD->FadeSpeed = 0.f; }
	}
	if (AFallFX* FX = AFallFX::Get(this))
	{
		const FVector G = UFallWorldSubsystem::Get(this)->Player->GetActorLocation() - FVector(0, 0, 150.f);
		for (int32 i = 0; i < 10; ++i) FX->Sprite(G + FMath::VRand() * 200.f, FMath::VRand() * 1500.f, 300.f, 2500.f, 1.2f, FLinearColor(0.3f, 0.24f, 0.16f), 0.95f, false, 1.f);
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
		if (AFallIntro::IsBlocking(this)) return; // the cutscene holds the fall until the throw
		bStarted = true;
		JumpTo(StartTime, AFallIntro::HandedOver(this));
		return;
	}

	// Impact frame, then straight back to the restart point (well under a second).
	if (CrashTime >= 0.f)
	{
		CrashTime += Dt;
		// 4 frames of impact (dust, shake), then a hard cut to black, then the restart.
		if (CrashTime > 0.13f)
		{
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
				if (AFallHUD* HUD = Cast<AFallHUD>(PC->GetHUD())) HUD->FadeAlpha = 1.f;
		}
		if (CrashTime > 0.45f) JumpTo(Checkpoint);
		return;
	}
	if (P->bDied)
	{
		if (DeathTime < 0.f)
		{
			DeathTime = 0.f;
			UGameplayStatics::SetGlobalTimeDilation(this, 0.4f);
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

	// Under the canopy the descent is time-compressed, easing off over the last 300 m: the landing approach is
	// seen at a believable speed, not as ground rushing up at hundreds of metres a second.
	if (FW->bAltitudeLive) FW->Altitude -= P->FallSpeed * Dt * (bCanopy ? FMath::Max(1.f, FMath::Clamp((CanopyTime - 1.5f) * 3.f, 1.f, CanopyDescent) * FMath::Clamp(FW->HeightAboveGround() / 300.f, 0.05f, 1.f)) : 1.f);
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
		// Who carries the canopy: the hooked carrier (tandem) or the player (review restarts with -FallGiveParachute).
		AFallBody* Bearer = P;
		for (TActorIterator<AFallEnemy> It(GetWorld()); It; ++It) if (It->IsTandem()) Bearer = *It;
		const FVector Up = FVector::UpVector;
		const FRotator Yaw(0.f, P->GetActorRotation().Yaw, 0.f);
		const FVector ShL = Bearer->Mesh->GetSocketLocation("clavicle_l") + Up * 6.f, ShR = Bearer->Mesh->GetSocketLocation("clavicle_r") + Up * 6.f;
		const FVector Base = (ShL + ShR) * 0.5f;
		const FVector Pack = Bearer == P ? Base : (Bearer->Mesh->GetSocketLocation("spine_05") + Bearer->Mesh->GetSocketLocation("head")) * 0.5f; // out over the shoulders
		// 0-0.3 s the bag leaves the container and climbs; 0.3-0.65 s the lines pay out, the canopy a narrow column
		// trailing above; then it spreads and fills, the shock at CanopyShockTime, a small overshoot.
		const float LineLen = 760.f;
		const float Climb = FMath::SmoothStep(0.f, 0.65f, CanopyTime);
		const float Spread = FMath::Clamp((CanopyTime - 0.65f) / (CanopyShockTime + 0.35f - 0.65f), 0.f, 1.f);
		const float Over = 1.f + 0.12f * FMath::Sin(FMath::Clamp((CanopyTime - CanopyShockTime) / 0.5f, 0.f, 1.f) * PI);
		// In freefall the player rides on the carrier's back, right where the airstream takes the bag: it leaves over
		// the carrier's head instead, clear of the player, and only straightens up once both hang under the canopy.
		const FVector BearerUp = (Bearer->Mesh->GetSocketLocation("neck_01") - Bearer->Mesh->GetSocketLocation("pelvis")).GetSafeNormal();
		const FVector ClimbDir = FMath::Lerp((Up + BearerUp * 0.8f).GetSafeNormal(), Up, FMath::SmoothStep(0.f, 1.f, Spread)).GetSafeNormal();
		const FVector Top = FMath::Lerp(Pack, Base + ClimbDir * LineLen, Climb);
		Canopy->SetVisibility(true);
		Canopy->SetWorldLocation(Top);
		Canopy->SetWorldRotation(Yaw);
		const float SpanW = FMath::Lerp(0.07f, 1.f, FMath::SmoothStep(0.f, 1.f, Spread)) * Over;
		const float Bag = FMath::Clamp(CanopyTime / 0.3f, 0.f, 1.f);
		Canopy->SetWorldScale3D(CanopyTime < 0.3f ? FVector(0.06f, 0.04f, 0.08f) * (0.5f + 0.5f * Bag)
			: FVector(FMath::Lerp(0.12f, 1.f, Spread) * Over, SpanW, FMath::Lerp(1.8f, 1.f, Spread)));
		if (!bShockDone && CanopyTime >= CanopyShockTime) { bShockDone = true; FallCanopyShock(this, P); }
		if (AFallFX* FX = AFallFX::Get(this))
		{
			// Four risers from the bearer's shoulders (front and rear on each side), each fanning into four lines to
			// the leading or trailing edge across the span. While the bag climbs they run together.
			for (int32 i = 0; i < NumLines; ++i)
			{
				const int32 Riser = i / 4, k = i % 4;
				const bool bLeft = Riser < 2, bFront = Riser % 2 == 0;
				const FVector Root = (bLeft ? ShL : ShR) + Yaw.RotateVector(FVector(bFront ? 6.f : -6.f, 0.f, 0.f));
				const float SpanY = (bLeft ? -1.f : 1.f) * (60.f + 110.f * k) * SpanW;
				const FVector Edge = Top + Yaw.RotateVector(FVector((bFront ? 100.f : -110.f) * SpanW, SpanY, -20.f));
				Risers[i] = FX->Beam(Risers[i], Root, CanopyTime < 0.3f ? Top : Edge, FLinearColor(0.05f, 0.05f, 0.045f), k == 0 ? 1.2f : 0.7f, true, true);
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
			Canopy->SetWorldLocation(Top + P->GetActorForwardVector() * 900.f * FMath::Min(LandTime, 2.f) - FVector(0, 0, 300.f * FMath::Min(LandTime, 2.f)));
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
	DrawDebugCapsule(W, O, 90.f, 30.f, FQuat::Identity, FColor::White, false, -1.f, 0, 12.f);
	DrawDebugDirectionalArrow(W, O, O + FVector(300.f, 0, 0), 80.f, FColor::White, false, -1.f, 0, 12.f);

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
	auto Text = [&](const FVector& At, const FString& Str, const FColor& C, float Size = 260.f)
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
		T->SetWorldRotation(FRotator(0.f, 180.f, 0.f)); // readable from the default editor view (looking down +X)
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
			DrawDebugCircle(W, O + FVector(0, 0, Z), E.Wave.Spread * 100.f, 48, C, false, -1.f, 0, 16.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
			DrawDebugDirectionalArrow(W, O + FVector(E.Wave.Spread * 70.f, 0, Z), O + FVector(E.Wave.Spread * 70.f, 0, Z + Rel * 150.f), 200.f, C, false, -1.f, 0, 24.f);
			Text(O + FVector(E.Wave.Spread * 100.f, 0, Z), FString::Printf(TEXT("%s %s x%.1f/s  %.0f..%.0f m/s"), *When, *UEnum::GetDisplayValueAsText(E.Wave.Type).ToString(), E.Wave.Density, E.Wave.RelativeSpeed.X, E.Wave.RelativeSpeed.Y), C);
		}
		if (Dt < -2.f || Dt > PreviewWindow) continue;
		const FVector P = O + E.Position * 100.f;
		switch (E.Type)
		{
		case EFallEventType::SpawnEnemy:
			DrawDebugCapsule(W, P, 100.f, 40.f, FQuat::Identity, C, false, -1.f, 0, 16.f);
			DrawDebugLine(W, O, P, C, false, -1.f, 0, 4.f);
			Text(P + FVector(0, 0, 160.f), FString::Printf(TEXT("%s %s"), *When, *UEnum::GetDisplayValueAsText(E.EnemyType).ToString()), C);
			break;
		case EFallEventType::DebrisPiece:
			DrawDebugBox(W, P, FVector(400.f, 250.f, 150.f) * E.Value, C, false, -1.f, 0, 16.f);
			DrawDebugDirectionalArrow(W, P, P + E.Velocity * 150.f, 200.f, C, false, -1.f, 0, 16.f);
			Text(P + FVector(0, 0, 300.f), FString::Printf(TEXT("%s %s"), *When, *UEnum::GetDisplayValueAsText(E.PieceType).ToString()), C);
			break;
		case EFallEventType::OneShot:
			DrawDebugSphere(W, P, 400.f, 16, C, false, -1.f, 0, 12.f);
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
	DrawDebugLine(W, TX(0.f), TX(S->Duration), FColor(90, 90, 90), false, -1.f, 0, 16.f);
	FVector PrevA, PrevV;
	for (int32 i = 0; i <= 180; ++i)
	{
		const float T = S->Duration * i / 180.f;
		const FVector A = TX(T) + FVector(0, 0, S->AltitudeAt(T) / 10000.f * H);
		const FVector V = TX(T) + FVector(0, 0, S->SpeedAt(T) / 100.f * H);
		if (i > 0)
		{
			DrawDebugLine(W, PrevA, A, FColor::White, false, -1.f, 0, 20.f);
			DrawDebugLine(W, PrevV, V, FColor::Cyan, false, -1.f, 0, 20.f);
		}
		PrevA = A; PrevV = V;
	}
	for (const FFallSequenceEvent& E : S->Events)
	{
		const FColor C = TypeColor(E);
		if (E.Type == EFallEventType::SegmentStart)
		{
			DrawDebugLine(W, TX(E.Time), TX(E.Time) + FVector(0, 0, H), C, false, -1.f, 0, 24.f);
			Text(TX(E.Time) + FVector(0, 0, H + 300.f), E.Segment.ToString(), C, 380.f);
		}
		else
		{
			DrawDebugLine(W, TX(E.Time) - FVector(0, 0, 150.f), TX(E.Time), C, false, -1.f, 0, 32.f);
		}
	}
	DrawDebugLine(W, TX(PreviewTime) - FVector(0, 0, 300.f), TX(PreviewTime) + FVector(0, 0, H + 300.f), FColor::Yellow, false, -1.f, 0, 40.f);
	Text(TX(PreviewTime) - FVector(0, 0, 450.f), FString::Printf(TEXT("%.1f s  %.0f m  %.0f m/s"), PreviewTime, S->AltitudeAt(PreviewTime), S->SpeedAt(PreviewTime)), FColor::Yellow, 420.f);

	for (int32 i = Label; i < Labels.Num(); ++i) Labels[i]->SetVisibility(false);
}
