#include "FallIntro.h"
#include "FallAirship.h"
#include "FallArt.h"
#include "FallAudio.h"
#include "FallFX.h"
#include "FallPlayer.h"
#include "FallWorld.h"
#include "FreefallGameMode.h"
#include "EngineUtils.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "DefaultLevelSequenceInstanceData.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Sound/SoundBase.h"

namespace
{
	bool GIntroPlayed = false;

	// The character's walk across the deck, deck space (cm, before the airship scale).
	struct FBeat { float T; FVector Pos; const TCHAR* Clip; };
	const FBeat Beats[] = {
		{0.0f, FVector(300.f, -150.f, 0.f), TEXT("Idle")},
		{5.0f, FVector(300.f, -150.f, 0.f), TEXT("Jog")},
		{6.6f, FVector(120.f, 520.f, 0.f), TEXT("Jog")},
		{8.5f, FVector(450.f, 1250.f, 0.f), TEXT("Idle")},
		{10.0f, FVector(450.f, 1250.f, 0.f), TEXT("Jog")},
		{13.0f, FVector(1100.f, 1350.f, 0.f), TEXT("Idle")},
	};
	// Focal length keys matching the camera keys of Tools/make_intro.py.
	const FVector2D Focal[] = {{0.f, 30.f}, {3.8f, 32.f}, {4.6f, 26.f}, {6.5f, 26.f}, {9.f, 30.f}, {11.5f, 28.f}, {13.8f, 26.f}, {15.f, 24.f}, {17.f, 22.f}};
}

AFallIntro::AFallIntro()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

bool AFallIntro::ShouldPlay()
{
	if (GIntroPlayed) return false;
	const TCHAR* Cmd = FCommandLine::Get();
	if (FParse::Param(Cmd, TEXT("FallIntro"))) return true;
	float Start = 0.f;
	FString Dummy;
	if (FParse::Value(Cmd, TEXT("FallStartTime="), Start) && Start > 0.f) return false;
	if (FParse::Value(Cmd, TEXT("FallReview="), Dummy)) return false;
	return !FParse::Param(Cmd, TEXT("FallNoIntro")) && !FParse::Param(Cmd, TEXT("FallNoSequence"));
}

bool AFallIntro::IsBlocking(const UObject* WorldContext)
{
	UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!W) return false;
	for (TActorIterator<AFallIntro> It(W); It; ++It) if (!It->bThrown) return true;
	return false;
}

bool AFallIntro::HandedOver(const UObject* WorldContext)
{
	UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!W) return false;
	for (TActorIterator<AFallIntro> It(W); It; ++It) if (It->bThrown) return true;
	return false;
}

void AFallIntro::BeginPlay()
{
	Super::BeginPlay();
	GIntroPlayed = true;
}

FTransform AFallIntro::DeckTransform() const
{
	const UStaticMeshComponent* C = Ship ? Ship->AnyPart() : nullptr;
	if (!C) return GetActorTransform();
	return FTransform(C->GetComponentQuat(), C->GetComponentLocation());
}

FVector AFallIntro::DeckToWorld(const FVector& Local) const
{
	return DeckTransform().TransformPosition(Local * UFallArtData::Get()->AirshipScale);
}

void AFallIntro::Begin(AFallPlayer* P)
{
	bStarted = true;
	UWorld* W = GetWorld();
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Ship = W->SpawnActor<AFallAirship>(AFallAirship::StaticClass(), FTransform(FRotator(0.f, P->GetActorRotation().Yaw + 90.f, 0.f), P->GetActorLocation()), SP);
	if (!Ship) { bThrown = bDone = true; return; }
	Ship->bIntroShip = true;
	Ship->BreakupDelay = 1e9f;
	ShipRot0 = Ship->GetActorRotation();
	// Put the deck under the player's feet.
	const FVector Up = DeckTransform().GetRotation().GetUpVector();
	Ship->AddActorWorldOffset(P->GetActorLocation() - Up * 95.f - DeckToWorld(Beats[0].Pos));
	P->BeginIntro();
	if (APlayerController* PC = Cast<APlayerController>(P->GetController()))
		if (AFallHUD* HUD = Cast<AFallHUD>(PC->GetHUD())) { HUD->FadeAlpha = 1.f; HUD->FadeColor = FLinearColor::Black; HUD->FadeSpeed = 1.5f; }

	if (ULevelSequence* LS = LoadObject<ULevelSequence>(nullptr, TEXT("/Game/Freefall/Cinematics/LS_Intro.LS_Intro")))
	{
		FMovieSceneSequencePlaybackSettings Settings;
		Settings.bDisableMovementInput = true;
		Settings.bDisableLookAtInput = true;
		ALevelSequenceActor* OutActor = nullptr;
		SeqPlayer = ULevelSequencePlayer::CreateLevelSequencePlayer(W, LS, Settings, OutActor);
		SeqActor = OutActor;
		if (SeqActor)
		{
			SeqActor->bOverrideInstanceData = true;
			if (UDefaultLevelSequenceInstanceData* ID = Cast<UDefaultLevelSequenceInstanceData>(SeqActor->DefaultInstanceData))
			{
				ID->TransformOrigin = DeckTransform();
			}
		}
		if (SeqPlayer) SeqPlayer->Play();
	}

	// Red alarm lamp at the front of the deckhouse, ambience of wind, fire and the klaxon.
	Alarm = NewObject<UPointLightComponent>(this);
	Alarm->SetupAttachment(RootComponent);
	Alarm->SetUsingAbsoluteLocation(true);
	Alarm->SetLightColor(FLinearColor(1.f, 0.08f, 0.04f));
	Alarm->SetAttenuationRadius(1600.f);
	Alarm->SetCastShadows(false);
	Alarm->RegisterComponent();
	const UFallArtData* Art = UFallArtData::Get();
	for (const TCHAR* Name : {TEXT("/Game/Freefall/Audio/SW_wind_inside_1.SW_wind_inside_1"), TEXT("/Game/Freefall/Audio/SW_big_fire_1.SW_big_fire_1"), TEXT("/Game/Freefall/Audio/SW_evacuation_alarm_1.SW_evacuation_alarm_1")})
	{
		if (USoundBase* S = LoadObject<USoundBase>(nullptr, Name))
		{
			UAudioComponent* A = UGameplayStatics::SpawnSound2D(this, S, 0.f);
			if (A) { A->FadeIn(1.5f, 0.6f); Ambience.Add(A); }
		}
	}
	(void)Art;
}

void AFallIntro::Throw()
{
	if (bThrown) return;
	bThrown = true;
	AFallPlayer* P = Cast<AFallPlayer>(UGameplayStatics::GetPlayerPawn(this, 0));
	const FTransform DT = DeckTransform();
	if (AFallFX* FX = AFallFX::Get(this)) FX->Explosion(DeckToWorld(FVector(650.f, 1250.f, 120.f)), 2.5f);
	FallSynth::Play(this, FallSynth::EKind::Explosion, FVector::ZeroVector, 1.f, 0.9f);
	Shake = 1.5f;
	if (P)
	{
		// The blast throws him over the starboard rail, tumbling. Control is his from here.
		P->SetActorLocation(DeckToWorld(Beats[UE_ARRAY_COUNT(Beats) - 1].Pos) + DT.GetRotation().GetUpVector() * 120.f);
		P->EndIntro(DT.TransformVectorNoScale(FVector(1.f, 0.f, 0.35f)).GetSafeNormal());
		if (APlayerController* PC = Cast<APlayerController>(P->GetController()))
		{
			if (SeqPlayer) SeqPlayer->Pause(); // the cine camera stays where it is and the view eases into the chase camera
			PC->SetViewTargetWithBlend(P, 0.9f, VTBlend_EaseInOut, 2.f); // longer, the blend lingered on a tiny figure far below
		}
	}
	for (UAudioComponent* A : Ambience) if (A) A->FadeOut(1.2f, 0.f);
}

void AFallIntro::Tick(float Dt)
{
	Super::Tick(Dt);
	AFallPlayer* P = Cast<AFallPlayer>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!P) return;
	if (!bStarted) { Begin(P); return; }
	if (bDone) return;

	if (bThrown)
	{
		AfterThrow += Dt;
		if (AfterThrow > 2.2f)
		{
			if (SeqPlayer) SeqPlayer->Stop();
			if (Alarm) Alarm->SetVisibility(false);
			bDone = true;
		}
		return;
	}

	T = SeqPlayer ? (float)SeqPlayer->GetCurrentTime().AsSeconds() : T + Dt;

	// Any key skips straight to the throw (the pause menu key does not).
	if (APlayerController* PC = Cast<APlayerController>(P->GetController()))
	{
		TArray<FKey> Keys;
		EKeys::GetAllKeys(Keys);
		for (const FKey& K : Keys)
		{
			if (K.IsAxis1D() || K.IsAxis2D() || K.IsAxis3D() || K.IsTouch() || K == EKeys::Escape || K == EKeys::Gamepad_Special_Right) continue;
			if (PC->WasInputKeyJustPressed(K)) { T = ThrowTime; break; }
		}
	}

	// Beats on the sequence clock.
	if (!bExplosion1 && T >= 4.2f)
	{
		bExplosion1 = true;
		if (AFallFX* FX = AFallFX::Get(this)) FX->Explosion(DeckToWorld(FVector(-700.f, -1500.f, 1600.f)), 3.5f);
		FallSynth::Play(this, FallSynth::EKind::Explosion, FVector::ZeroVector, 1.f, 0.75f);
		Shake = 1.f;
	}
	if (bExplosion1) Roll = FMath::FInterpTo(Roll, 9.f, Dt, 0.8f);
	if (Ship) Ship->SetActorRotation(ShipRot0 + FRotator(0.f, 0.f, Roll + FMath::Sin(T * 0.7f) * 1.2f));
	if (T >= ThrowTime) { Throw(); return; }

	// Camera rides the deck; explosions shake it.
	Shake = FMath::Max(0.f, Shake - Dt * 0.9f);
	const float A = Shake * Shake;
	ShakeOffset = FVector(FMath::PerlinNoise1D(T * 23.f), FMath::PerlinNoise1D(T * 23.f + 7.f), FMath::PerlinNoise1D(T * 23.f + 13.f)) * 22.f * A;
	if (SeqActor)
	{
		if (UDefaultLevelSequenceInstanceData* ID = Cast<UDefaultLevelSequenceInstanceData>(SeqActor->DefaultInstanceData))
		{
			FTransform O = DeckTransform();
			O.AddToTranslation(ShakeOffset);
			ID->TransformOrigin = O;
		}
	}
	if (APlayerController* PC = Cast<APlayerController>(P->GetController()))
	{
		if (ACineCameraActor* Cine = Cast<ACineCameraActor>(PC->GetViewTarget()))
		{
			int32 k = 0;
			while (k + 2 < UE_ARRAY_COUNT(Focal) && Focal[k + 1].X <= T) ++k;
			const float a = FMath::Clamp((T - Focal[k].X) / FMath::Max(Focal[k + 1].X - Focal[k].X, 0.01f), 0.f, 1.f);
			Cine->GetCineCameraComponent()->SetCurrentFocalLength(FMath::Lerp(Focal[k].Y, Focal[k + 1].Y, FMath::SmoothStep(0.f, 1.f, a)));
			Cine->GetCineCameraComponent()->FocusSettings.FocusMethod = ECameraFocusMethod::Disable;
			// The same fixed exposure as the game camera (no jump at the handover), a little darker on the sunlit deck
			// so the fire and the red lamp read.
			FPostProcessSettings& CP = Cine->GetCineCameraComponent()->PostProcessSettings;
			CP.bOverride_AutoExposureMethod = true; CP.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
			CP.bOverride_AutoExposureApplyPhysicalCameraExposure = true; CP.AutoExposureApplyPhysicalCameraExposure = false;
			CP.bOverride_AutoExposureBias = true;
			CP.AutoExposureBias = (P ? P->ExposureEV : 1.5f) - 0.8f;
		}
	}

	// Alarm lamp pulses.
	if (Alarm)
	{
		Alarm->SetWorldLocation(DeckToWorld(FVector(300.f, -470.f, 330.f)));
		Alarm->SetIntensity(FMath::Max(0.f, FMath::Sin(T * 6.f)) * 15000.f);
	}

	// The character: walk the beats, the clip changes with the beat.
	int32 b = 0;
	while (b + 1 < UE_ARRAY_COUNT(Beats) && Beats[b + 1].T <= T) ++b;
	const FBeat& B0 = Beats[b];
	const FBeat& B1 = Beats[FMath::Min(b + 1, (int32)UE_ARRAY_COUNT(Beats) - 1)];
	const float S = (B1.T > B0.T) ? FMath::Clamp((T - B0.T) / (B1.T - B0.T), 0.f, 1.f) : 0.f;
	const bool bMoving = FCString::Strcmp(B0.Clip, TEXT("Jog")) == 0;
	const FVector Local = bMoving ? FMath::Lerp(B0.Pos, B1.Pos, S) : B0.Pos;
	const FTransform DT = DeckTransform();
	const FVector Up = DT.GetRotation().GetUpVector();
	P->SetActorLocation(DeckToWorld(Local) + Up * 95.f);
	FVector Face = bMoving ? (B1.Pos - B0.Pos) : (b >= 5 ? FVector(1.f, 0.f, 0.f) : (b == 3 || b == 4 ? FVector(0.3f, 1.f, 0.f) : FVector(1.f, -0.6f, 0.f)));
	const FVector FaceW = DT.TransformVectorNoScale(Face.GetSafeNormal());
	P->SetActorRotation(FRotationMatrix::MakeFromXZ(FVector::VectorPlaneProject(FaceW, Up), Up).Rotator());
	const FName Want(B0.Clip);
	if (Want != Clip)
	{
		Clip = Want;
		const TCHAR* Path = Clip == FName("Jog") ? TEXT("/Game/RetroFuturisticSoldier/Anims/Unarmed/Jog/MF_Unarmed_Jog_Fwd.MF_Unarmed_Jog_Fwd") : TEXT("/Game/RetroFuturisticSoldier/Anims/Unarmed/MM_Idle.MM_Idle");
		if (UAnimSequence* Anim = LoadObject<UAnimSequence>(nullptr, Path)) P->Mesh->PlayAnimation(Anim, true);
	}
	if (!bGunShown && T >= 9.3f)
	{
		bGunShown = true;
		if (P->WeaponMesh) P->WeaponMesh->SetVisibility(true);
		FallSynth::Play(this, FallSynth::EKind::Grab, FVector::ZeroVector, 0.6f, 1.4f);
	}
}
