#include "FallPlayer.h"
#include "FallTuning.h"
#include "FallWorld.h"
#include "FallAudio.h"
#include "FallStraps.h"
#include "FallDebris.h"
#include "FallFX.h"
#include "EngineUtils.h"
#include "FallEnemy.h"
#include "FallArt.h"
#include "FallAnimInstance.h"
#include "Sound/SoundBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraSystem.h"
#include "FallCombatData.h"
#include "FreefallGameMode.h"
#include "FallMenu.h"
#include "Components/BoxComponent.h"
#include "FallDirector.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/PointLightComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"

AFallPlayer::AFallPlayer()
{
	CameraArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraArm"));
	CameraArm->SetupAttachment(RootComponent);
	CameraArm->SetUsingAbsoluteRotation(true);
	CameraArm->bUsePawnControlRotation = false;
	CameraArm->bDoCollisionTest = true; // only debris blocks the camera channel
	CameraArm->ProbeSize = 14.f;
	CameraArm->bEnableCameraLag = true;
	CameraArm->CameraLagSpeed = 18.f;
	CameraArm->CameraLagMaxDistance = 70.f;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(CameraArm, USpringArmComponent::SocketName);
	Camera->PostProcessSettings.bOverride_MotionBlurAmount = true;
	Camera->PostProcessSettings.MotionBlurAmount = 0.f; // enabled after the first second (no smear on spawn)
	Camera->PostProcessSettings.bOverride_VignetteIntensity = true;
	Camera->PostProcessSettings.VignetteIntensity = 0.35f;
	// Slightly darker and punchier than the default auto exposure: snow and cloud stop clipping, the dark suit reads.
	Camera->PostProcessSettings.bOverride_AutoExposureBias = true;
	Camera->PostProcessSettings.AutoExposureBias = -0.6f;
	Camera->PostProcessSettings.bOverride_AutoExposureSpeedDown = true;
	Camera->PostProcessSettings.AutoExposureSpeedDown = 0.6f;
	Camera->PostProcessSettings.bOverride_AutoExposureSpeedUp = true;
	Camera->PostProcessSettings.AutoExposureSpeedUp = 1.2f;
	Camera->PostProcessSettings.bOverride_ColorContrast = true;
	Camera->PostProcessSettings.ColorContrast = FVector4(1.08f, 1.08f, 1.08f, 1.f);

	// Soft fill from the camera side: the body keeps its shape when it is backlit against dark valleys.
	FillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FillLight"));
	FillLight->SetupAttachment(Camera);
	FillLight->SetRelativeLocation(FVector(60.f, 0.f, 40.f));
	FillLight->SetIntensityUnits(ELightUnits::Candelas);
	FillLight->SetIntensity(1.6f);
	FillLight->SetAttenuationRadius(900.f);
	FillLight->SetCastShadows(false);
	FillLight->SetLightColor(FLinearColor(0.75f, 0.85f, 1.f));

	WindAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("WindAudio"));
	WindAudio->SetupAttachment(RootComponent);
	WindAudio->bAutoActivate = false;
	WindAudio->bAllowSpatialization = false;

	AutoPossessPlayer = EAutoReceiveInput::Player0;
}

void AFallPlayer::BeginPlay()
{
	if (!Movement) Movement = LoadObject<UFallMovementData>(nullptr, TEXT("/Game/Freefall/Data/DA_FallMovement.DA_FallMovement"));
	if (!Weapon) Weapon = LoadObject<UFallWeaponData>(nullptr, TEXT("/Game/Freefall/Data/DA_FallWeapon.DA_FallWeapon"));
	Super::BeginPlay();
	const UFallMovementData* M = UFallMovementData::Resolve(Movement);
	StanceRate = M->StanceRate;
	Attitude = FVector2D(M->AttitudeHz, M->AttitudeDamping);
	CameraArm->TargetArmLength = M->ArmLength;
	CameraArm->SocketOffset = M->CameraOffset;

	if (UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this))
	{
		FW->Player = this;
	}
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
		PC->SetControlRotation(FRotator(-12.f, 0.f, 0.f));
	}
	HitFlashScale = 0.3f;
	HitFlashColor = FLinearColor(1.f, 0.3f, 0.15f);
	WindWave = NewObject<UFallWindWave>(this);
	WindAudio->SetSound(WindWave);
	WindAudio->Play();
	{
		const UFallArtData* Art = UFallArtData::Get();
		for (const TCHAR* Key : {TEXT("WindLow"), TEXT("WindMid"), TEXT("WindHigh"), TEXT("WindFlutter")})
		{
			const TSoftObjectPtr<USoundBase>* S = Art->Sounds.Find(Key);
			USoundBase* Snd = S ? S->LoadSynchronous() : nullptr;
			UAudioComponent* A = NewObject<UAudioComponent>(this);
			A->SetupAttachment(RootComponent);
			A->bAllowSpatialization = false;
			A->bAutoActivate = false;
			A->RegisterComponent();
			if (Snd)
			{
				A->SetSound(Snd);
				A->SetVolumeMultiplier(0.f);
				A->Play(FMath::FRandRange(0.f, 20.f)); // each layer starts at a different point of its loop
			}
			WindLayers.Add(A);
		}
		if (UMaterialInterface* PP = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Freefall/Materials/M_FF_ScreenWind.M_FF_ScreenWind")))
		{
			ScreenWind = UMaterialInstanceDynamic::Create(PP, this);
			Camera->PostProcessSettings.AddBlendable(ScreenWind, 1.f);
		}
		// Grading: cool shadows, warm highlights, bloom on fire and sun, no crushed blacks.
		FPostProcessSettings& PS = Camera->PostProcessSettings;
		PS.bOverride_ColorGainShadows = true; PS.ColorGainShadows = FVector4(0.94f, 0.98f, 1.08f, 1.f);
		PS.bOverride_ColorGainHighlights = true; PS.ColorGainHighlights = FVector4(1.07f, 1.0f, 0.9f, 1.f);
		PS.bOverride_ColorOffsetShadows = true; PS.ColorOffsetShadows = FVector4(0.004f, 0.008f, 0.016f, 0.f);
		PS.bOverride_BloomIntensity = true; PS.BloomIntensity = 0.9f;
		PS.bOverride_AutoExposureBias = true; PS.AutoExposureBias = 0.2f;
	}
}

void AFallPlayer::ResetBody()
{
	Super::ResetBody();
	Release();
	bDied = false;
	bCanopy = false;
	if (ParachutePack) { ParachutePack->DestroyComponent(); ParachutePack = nullptr; }
	Ammo = UFallWeaponData::Resolve(Weapon)->MagazineSize;
	ReloadLeft = 0.f;
	RobTime = -1.f;
	DamagePulse = 0.f;
	FallSpeed = UFallMovementData::Resolve(Movement)->BellySpeed;
	PrevFallSpeed = FallSpeed;
	VelocityH = FVector::ZeroVector;
	Control = FFallBodyControl();
	Dive = Bank = 0.f;
	if (HitStopLeft > 0.f)
	{
		HitStopLeft = 0.f;
		UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
	}
}

void AFallPlayer::HitStop(float Seconds)
{
	HitStopLeft = FMath::Max(HitStopLeft, Seconds);
	UGameplayStatics::SetGlobalTimeDilation(this, 0.03f);
}

float AFallPlayer::StanceSpeed() const
{
	const UFallMovementData* M = UFallMovementData::Resolve(Movement);
	const float D = Dive;
	if (D < 0.f) return FMath::Lerp(M->BellySpeed, M->BrakeSpeed, -D);
	if (D < 0.5f) return FMath::Lerp(M->BellySpeed, M->TrackSpeed, D / 0.5f);
	return FMath::Lerp(M->TrackSpeed, M->DiveSpeed, (D - 0.5f) / 0.5f);
}

void AFallPlayer::GatherInput(float Dt)
{
	InputPrev = InputNow;
	FFallInputFrame F;
	if (BotInput)
	{
		BotInput(Dt, F);
	}
	else if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		const UFallMovementData* M = UFallMovementData::Resolve(Movement);
		auto Key = [PC](const FKey& K) { return PC->IsInputKeyDown(K); };
		F.Dive = (Key(EKeys::W) ? 1.f : 0.f) - (Key(EKeys::S) ? 1.f : 0.f) - PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftY) * -1.f;
		F.Slide = (Key(EKeys::D) ? 1.f : 0.f) - (Key(EKeys::A) ? 1.f : 0.f) + PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftX);
		float Mx = 0.f, My = 0.f;
		PC->GetInputMouseDelta(Mx, My);
		F.Look = FVector2D(Mx, My) * M->LookSensitivity
			+ FVector2D(PC->GetInputAnalogKeyState(EKeys::Gamepad_RightX), PC->GetInputAnalogKeyState(EKeys::Gamepad_RightY)) * 160.f * Dt;
		F.bFire = Key(EKeys::LeftMouseButton) || PC->GetInputAnalogKeyState(EKeys::Gamepad_RightTriggerAxis) > 0.4f;
		F.bAim = Key(EKeys::RightMouseButton) || PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftTriggerAxis) > 0.4f;
		F.bReload = Key(EKeys::R) || Key(EKeys::Gamepad_FaceButton_Left);
		F.bGrab = Key(EKeys::E) || Key(EKeys::Gamepad_FaceButton_Top);
		F.bPush = Key(EKeys::SpaceBar) || Key(EKeys::Gamepad_FaceButton_Bottom);
		F.bKick = Key(EKeys::F) || Key(EKeys::Gamepad_FaceButton_Right);
		F.bMenu = Key(EKeys::Escape) || Key(EKeys::Gamepad_Special_Right);
	}
	F.Dive = FMath::Clamp(F.Dive, -1.f, 1.f);
	F.Slide = FMath::Clamp(F.Slide, -1.f, 1.f);
	InputNow = F;
}

void AFallPlayer::UpdateMovement(float Dt)
{
	const UFallMovementData* M = UFallMovementData::Resolve(Movement);
	UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this);

	// Camera control rotation from look input.
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		FRotator R = PC->GetControlRotation();
		R.Yaw += InputNow.Look.X;
		R.Pitch = FMath::Clamp(FRotator::NormalizeAxis(R.Pitch + InputNow.Look.Y), -75.f, 35.f);
		R.Roll = 0.f;
		PC->SetControlRotation(R);
		// The body turns with the camera, a little late; the turn rate banks the body.
		const float Yaw = GetActorRotation().Yaw;
		const float Diff = FRotator::NormalizeAxis(R.Yaw - Yaw);
		const float NewYaw = Yaw + Diff * FMath::Min(1.f, Dt * 4.f);
		YawVel = FMath::FInterpTo(YawVel, Diff * 4.f, Dt, 6.f);
		SetActorRotation(FRotator(0.f, NewYaw, 0.f));
	}

	if (bCanopy)
	{
		// Opening shock: the canopy brakes the body hard, then a slow steady descent.
		FallSpeed = FMath::FInterpTo(FallSpeed, 6.f, Dt, 2.2f);
		VelocityH = FMath::VInterpTo(VelocityH, GetActorForwardVector() * 600.f, Dt, 0.5f);
		AddActorWorldOffset(VelocityH * Dt);
		Control.Override = "Canopy";
		Control.OverrideTarget = 1.f;
		Control.AttitudeWeight = 0.f;
		Control.HandWeight[0] = Control.HandWeight[1] = 0.f;
		Control.Dive = 0.f;
		Control.Bank = FMath::Clamp(InputNow.Slide, -1.f, 1.f) * 0.3f;
		if (FW)
		{
			FW->PlayerFallSpeed = FallSpeed * 100.f;
			FW->PlayerVelocityH = VelocityH;
			Control.AirVelocity = FW->AirVelocityFor(FallSpeed * 100.f, VelocityH);
		}
		return;
	}
	if (IsHolding())
	{
		UpdateHold(Dt);
		if (FW)
		{
			FW->PlayerFallSpeed = FallSpeed * 100.f;
			FW->PlayerVelocityH = VelocityH;
			Control.AirVelocity = FVector(0.f, 0.f, 700.f); // sheltered behind the piece
		}
		return;
	}
	Control.Override = NAME_None;
	Control.AttitudeWeight = 0.f;
	Control.HandWeight[0] = Control.HandWeight[1] = 0.f;
	Control.Dive = InputNow.Dive;
	Control.Bank = FMath::Clamp(InputNow.Slide + YawVel / 180.f, -1.f, 1.f);

	// Fall speed: speeding up is gravity against drag (slow), slowing down is drag (fast).
	const float Target = StanceSpeed();
	const float Rate = Target > FallSpeed ? M->SpeedUpRate : M->SlowDownRate;
	float DV = (Target - FallSpeed) * FMath::Min(1.f, Rate * Dt);
	if (DV > 0.f) DV = FMath::Min(DV, M->MaxAccel * Dt); // gravity is the limit when speeding up
	FallSpeed += DV;

	// Horizontal: slide along the camera right, drift forward in the track.
	const FRotator Yaw(0.f, GetActorRotation().Yaw, 0.f);
	const FVector Fwd = Yaw.Vector();
	const FVector Right = FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y);
	const float Track = FMath::Clamp(1.f - FMath::Abs(Dive - 0.5f) * 2.5f, 0.f, 1.f);
	// Diving carries you towards where you look: the flatter the look, the more forward glide (so a target ahead can be caught).
	const float LookFlat = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(Camera->GetComponentRotation().Pitch, -90.f, 0.f)));
	const float DiveGlide = FMath::Max(Dive, 0.f) * LookFlat * M->TrackForwardSpeed * 1.1f;
	const FVector WantH = Right * (InputNow.Slide * M->SlideSpeed * 100.f) + Fwd * (FMath::Max(Track * M->TrackForwardSpeed, DiveGlide) * 100.f);
	VelocityH = FMath::Lerp(VelocityH, WantH, FMath::Min(1.f, M->HorizontalRate * Dt));
	AddActorWorldOffset(VelocityH * Dt);

	if (FW)
	{
		FW->PlayerFallSpeed = FallSpeed * 100.f;
		FW->PlayerVelocityH = VelocityH;
		Control.AirVelocity = FW->AirVelocityFor(FallSpeed * 100.f, VelocityH);
	}
}

AFallDebris* AFallPlayer::FindGrabbable() const
{
	const UFallMovementData* M = UFallMovementData::Resolve(Movement);
	AFallDebris* Best = nullptr;
	float BestD = M->GrabRange;
	for (TActorIterator<AFallDebris> It(GetWorld()); It; ++It)
	{
		if (!It->bGrabbable || It->Holder.IsValid()) continue;
		const float D = It->DistanceTo(GetActorLocation()) - It->Box->GetUnscaledBoxExtent().Size() * 0.15f; // big pieces win ties
		if (D < BestD) { BestD = D; Best = *It; }
	}
	return Best;
}

void AFallPlayer::Grab(AFallDebris* Piece)
{
	if (!Piece) return;
	FVector N;
	const FVector A = Piece->ClosestSurfacePoint(GetActorLocation(), &N);
	const FTransform T = Piece->GetActorTransform();
	HeldAnchorLocal = T.InverseTransformPositionNoScale(A);
	HeldNormalLocal = T.InverseTransformVectorNoScale(N);
	Held = Piece;
	Piece->Holder = this;
	FallSynth::Play(this, FallSynth::EKind::Grab, A, 0.9f);
	AddShake(0.25f);
	PendingHit += -N * 0.6f; // the body thuds against the surface
	if (UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this)) FW->NoteEvent("Grab");
}

void AFallPlayer::Release()
{
	if (AFallDebris* P = Held.Get())
	{
		P->Holder = nullptr;
		VelocityH = P->Mover->VelocityH;
		FallSpeed = P->Mover->FallSpeed / 100.f;
	}
	Held = nullptr;
	GrabCooldown = 0.35f;
}

void AFallPlayer::PushOff()
{
	AFallDebris* P = Held.Get();
	if (!P) return;
	const UFallMovementData* M = UFallMovementData::Resolve(Movement);
	const FVector Dir = Camera->GetForwardVector();
	Release();
	VelocityH += FVector(Dir.X, Dir.Y, 0.f) * M->PushOffSpeed * 100.f;
	FallSpeed = FMath::Max(8.f, FallSpeed - Dir.Z * M->PushOffSpeed);
	P->Mover->VelocityH -= FVector(Dir.X, Dir.Y, 0.f) * M->PushOffSpeed * 100.f * 0.3f / P->Mass;
	P->Mover->FallSpeed += Dir.Z * M->PushOffSpeed * 100.f * 0.3f / P->Mass;
	P->LastHit = GetWorld()->GetTimeSeconds() + 0.6f;
	AddShake(0.5f);
	PendingHit += Dir * 1.2f;
	FallSynth::Play(this, FallSynth::EKind::Whoosh, FVector::ZeroVector, 0.9f);
	if (AFallFX* FX = AFallFX::Get(this)) FX->Sparks(GetActorLocation() - Dir * 60.f, -Dir, 6, FLinearColor(0.8f, 0.8f, 0.8f), 1500.f);
	if (UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this)) FW->NoteEvent("PushOff");
}

void AFallPlayer::UpdateHold(float Dt)
{
	AFallDebris* P = Held.Get();
	const FTransform T = P->GetActorTransform();
	const FVector A = T.TransformPositionNoScale(HeldAnchorLocal);
	const FVector N = T.TransformVectorNoScale(HeldNormalLocal).GetSafeNormal();
	// The body hangs off the surface; it glides there in the first moments of the grab.
	SetActorLocation(FMath::VInterpTo(GetActorLocation(), A + N * 30.f, Dt, 9.f));
	FallSpeed = P->Mover->FallSpeed / 100.f;
	VelocityH = P->Mover->VelocityH;
	AddActorWorldOffset(VelocityH * Dt);

	// Keep the horizon while holding on: the camera frames the threat beyond the cover, not the ground.
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		FRotator R = PC->GetControlRotation();
		R.Pitch = FMath::Clamp(FRotator::NormalizeAxis(R.Pitch), -40.f, 35.f);
		PC->SetControlRotation(R);
	}
	// Face the surface: body front into the surface, body up towards the sky (or the camera when the face is horizontal).
	FVector UpHint = FVector::UpVector - N * (N | FVector::UpVector);
	if (UpHint.SizeSquared() < 0.05f) UpHint = Camera->GetForwardVector() - N * (N | Camera->GetForwardVector());
	UpHint = UpHint.GetSafeNormal();
	const FQuat World = FRotationMatrix::MakeFromXZ(-N, UpHint).ToQuat();
	Control.AttitudeOverride = GetActorQuat().Inverse() * World;
	Control.AttitudeWeight = 1.f;
	Control.Override = "Grab";
	Control.OverrideTarget = 1.f;
	Control.Dive = 0.f;
	Control.Bank = 0.f;
	const FVector Lat = (N ^ UpHint).GetSafeNormal();
	Control.HandTarget[0] = A + UpHint * 52.f - Lat * 32.f + N * 3.f;
	Control.HandTarget[1] = A + UpHint * 52.f + Lat * 32.f + N * 3.f;
	Control.HandWeight[0] = 1.f;
	Control.HandWeight[1] = Control.bAim ? 0.f : 1.f;
}

void AFallPlayer::BuildOutfit()
{
	Super::BuildOutfit();
	ArmWeapon();
	Gun = WeaponMesh;
}

AFallEnemy* AFallPlayer::FindEnemyInReach(float Range) const
{
	AFallEnemy* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	const FVector Fwd = Camera->GetForwardVector();
	for (TActorIterator<AFallEnemy> It(GetWorld()); It; ++It)
	{
		if ((It->State == EFallEnemyState::Dead && !It->CanBeRobbed()) || It->State == EFallEnemyState::Robbed) continue;
		const FVector D = It->GetActorLocation() - GetActorLocation();
		const float Dist = D.Size();
		if (Dist > Range) continue;
		const float Score = Dist * (1.5f - 0.5f * (D.GetSafeNormal() | Fwd));
		if (Score < BestScore) { BestScore = Score; Best = *It; }
	}
	return Best;
}

void AFallPlayer::Fire()
{
	const UFallWeaponData* W = UFallWeaponData::Resolve(Weapon);
	if (FireCooldown > 0.f || ReloadLeft > 0.f) return;
	if (Ammo <= 0) { StartReload(); return; }
	--Ammo;
	FireCooldown = W->FireInterval;
	LastShot = GetWorld()->GetTimeSeconds();
	const FVector Muzzle = MuzzleLocation();
	TriggerPull = 1.f;
	const float Cone = FMath::Lerp(W->SpreadDeg.X, W->SpreadDeg.Y, FMath::Clamp(Spread, 0.f, 1.f));
	Spread = FMath::Min(1.f, Spread + W->SpreadPerShot);
	const FVector Dir = FMath::VRandCone((AimPoint - Muzzle).GetSafeNormal(), FMath::DegreesToRadians(Cone));
	FHitResult Hit;
	FCollisionQueryParams Q(TEXT("PlayerShot"), false, this);
	if (AFallDebris* H = Held.Get()) Q.AddIgnoredActor(H);
	const FVector End = Muzzle + Dir * W->Range;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Muzzle, End, ECC_Visibility, Q);
	const FVector Impact = bHit ? Hit.ImpactPoint : End;

	AFallFX* FX = AFallFX::Get(this);
	if (FX)
	{
		FX->Tracer(Muzzle, Impact, W->TracerColor, 2.5f);
		FX->Flash(Muzzle, 35.f, FLinearColor(1.f, 0.7f, 0.35f), 0.04f);
		const UFallArtData* Art = UFallArtData::Get();
		FX->SpawnOn(Art->MuzzleFlash.LoadSynchronous(), WeaponMesh, Muzzle, 0.28f, MuzzleForward().Rotation());
		if (WeaponMesh)
		{
			const FTransform GT = WeaponMesh->GetComponentTransform();
			FX->Casing(GT.TransformPosition(FVector(-4.f, 2.f, 5.f)), GT.TransformVectorNoScale(FVector(0.f, 1.f, 0.6f)).GetSafeNormal() * 350.f);
		}
	}
	FallSynth::Play(this, FallSynth::EKind::Gunshot, FVector::ZeroVector, 0.55f, FMath::FRandRange(0.95f, 1.05f));
	AddRecoil(1.f);
	AddShake(0.06f);
	AFallHUD* HUD = nullptr;
	if (APlayerController* PC = Cast<APlayerController>(GetController())) HUD = Cast<AFallHUD>(PC->GetHUD());
	if (HUD) HUD->Spread = FMath::Min(1.f, HUD->Spread + 0.35f);

	if (!bHit) return;
	if (AFallEnemy* E = Cast<AFallEnemy>(Hit.GetActor()))
	{
		const bool bKill = E->ReceiveHit(Dir, W->HitPush, W->Damage);
		HitStop(bKill ? W->HitStop.Y : W->HitStop.X);
		FallSynth::Play(this, FallSynth::EKind::HitBody, Impact, 1.f, bKill ? 0.8f : 1.f);
		if (FX) { FX->Sparks(Impact, -Dir, 6, FLinearColor(1.f, 0.25f, 0.1f), 1200.f); FX->Sprite(Impact, -Dir * 300.f, 30.f, 120.f, 0.35f, FLinearColor(0.35f, 0.03f, 0.02f), 0.7f, true, 1.f); }
		if (HUD) { HUD->HitPulse = 1.f; if (bKill) HUD->KillPulse = 1.f; }
		if (UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this)) FW->NoteEvent(bKill ? "Kill" : "Hit");
	}
	else
	{
		// Metal: shield, debris, turret. Ricochet sparks, no damage.
		if (FX)
		{
			FX->Sparks(Impact, Hit.ImpactNormal, 10, FLinearColor(1.f, 0.75f, 0.4f), 2200.f);
			FX->SpawnOn(UFallArtData::Get()->ImpactMetal.LoadSynchronous(), Hit.GetComponent(), Impact, 1.f, Hit.ImpactNormal.Rotation());
		}
		FallSynth::Play(this, FMath::RandBool() ? FallSynth::EKind::Ricochet : FallSynth::EKind::HitMetal, Impact, 0.8f, 1.3f);
		if (AFallDebris* D = Cast<AFallDebris>(Hit.GetActor())) D->Mover->Spin += FRotator(FMath::FRandRange(-6.f, 6.f), FMath::FRandRange(-6.f, 6.f), 0.f);
	}
}

void AFallPlayer::StartReload()
{
	if (ReloadLeft > 0.f || Ammo >= UFallWeaponData::Resolve(Weapon)->MagazineSize) return;
	ReloadLeft = UFallWeaponData::Resolve(Weapon)->ReloadTime;
	StartReloadMotion(ReloadLeft);
	FallSynth::Play(this, FallSynth::EKind::Grab, FVector::ZeroVector, 0.6f, 1.6f);
	FallSynth::Play(this, FallSynth::EKind::Grab, FVector::ZeroVector, 0.4f, 1.6f);
}

void AFallPlayer::TryKick()
{
	if (KickTime >= 0.f || RobTime >= 0.f) return;
	const UFallWeaponData* W = UFallWeaponData::Resolve(Weapon);
	AFallEnemy* E = FindEnemyInReach(W->KickRange * 1.6f);
	// A stunned parachute carrier in reach: take the parachute instead of kicking.
	if (E && E->CanBeRobbed() && FVector::Dist(E->GetActorLocation(), GetActorLocation()) < 650.f)
	{
		TakeParachute(E);
		return;
	}
	bool bRight = true;
	if (E)
	{
		const FVector Local = GetActorQuat().UnrotateVector(E->GetActorLocation() - GetActorLocation());
		bRight = Local.Y >= 0.f;
		// Turn the body so the kicking side faces the enemy.
		Control.YawOffset = FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X)) - (bRight ? 90.f : -90.f);
	}
	KickTarget = E;
	bKickApplied = false;
	if (E)
	{
		const FVector To = E->GetActorLocation() - GetActorLocation();
		AddHorizontalImpulse(FVector(To.X, To.Y, 0.f).GetSafeNormal() * FMath::Clamp((To.Size() - 160.f) / 0.3f, 0.f, 1200.f));
	}
	StartKick(bRight);
	FallSynth::Play(this, FallSynth::EKind::Whoosh, FVector::ZeroVector, 0.6f, 1.3f);
}

void AFallPlayer::GiveParachute()
{
	bHasParachute = true;
	if (!ParachutePack) ParachutePack = AttachMesh("spine_03", UFallArtData::Get()->ParachuteContainer.LoadSynchronous(), FVector(0, -14.f, 2.f), FRotator(0.f, 180.f, 0.f), 1.05f);
}

void AFallPlayer::TakeParachute(AFallEnemy* Target)
{
	RobTarget = Target;
	RobTime = 0.f;
	HitStop(0.08f);
	AddShake(0.4f);
	FallSynth::Play(this, FallSynth::EKind::Grab, Target->GetActorLocation(), 1.f, 0.8f);
}

bool AFallPlayer::ReceiveHit(const FVector& PushDirWorld, float Strength, float Damage)
{
	if (bDied) return false;
	const bool bDead = Super::ReceiveHit(PushDirWorld, Strength, Damage);
	Flash(FLinearColor(1.f, 0.8f, 0.45f), 0.6f + 0.3f * Strength); // warm white on the rim: never the enemies' red
	LastDamage = GetWorld()->GetTimeSeconds();
	if (Damage > 0.f) if (UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this)) FW->NoteEvent("PlayerHit");
	DamagePulse = FMath::Min(1.f, DamagePulse + Damage / 30.f + 0.25f);
	AddShake(0.15f + 0.1f * Strength);
	FallSynth::Play(this, FallSynth::EKind::HitBody, FVector::ZeroVector, 0.8f, 0.9f);
	if (bDead)
	{
		bDied = true;
		if (UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this)) FW->NoteEvent("PlayerDeath");
	}
	return bDead;
}

void AFallPlayer::UpdateCombat(float Dt)
{
	const UFallWeaponData* W = UFallWeaponData::Resolve(Weapon);
	const float Now = GetWorld()->GetTimeSeconds();
	FireCooldown -= Dt;
	Spread = FMath::Max(0.f, Spread - Dt * 2.5f);
	if (ReloadLeft > 0.f)
	{
		ReloadLeft -= Dt;
		if (ReloadLeft <= 0.f) { Ammo = W->MagazineSize; FallSynth::Play(this, FallSynth::EKind::Grab, FVector::ZeroVector, 0.5f, 1.9f); }
	}

	// Crosshair ray from the camera.
	{
		const FVector From = Camera->GetComponentLocation();
		const FVector To = From + Camera->GetForwardVector() * W->Range;
		FHitResult Hit;
		FCollisionQueryParams Q(TEXT("Aim"), false, this);
		if (AFallDebris* H = Held.Get()) Q.AddIgnoredActor(H);
		AimPoint = GetWorld()->LineTraceSingleByChannel(Hit, From + Camera->GetForwardVector() * 250.f, To, ECC_Visibility, Q) ? Hit.ImpactPoint : To;
	}

	// Finisher assist: a stunned or dead carrier in reach pulls the player onto the container; F or E takes it.
	if (!bHasParachute && RobTime < 0.f)
	{
		for (TActorIterator<AFallEnemy> It(GetWorld()); It; ++It)
		{
			if (!It->CanBeRobbed()) continue;
			const FVector To = It->GetActorLocation() - GetActorLocation();
			if (To.Size() < 1200.f)
			{
				AddActorWorldOffset(To.GetSafeNormal() * FMath::Min(To.Size() - 150.f, 450.f * Dt));
				if (Pressed(&FFallInputFrame::bGrab) && To.Size() < 650.f) TakeParachute(*It);
			}
		}
	}
	if (RobTime < 0.f && !bCanopy)
	{
		if (InputNow.bFire) Fire();
		if (Pressed(&FFallInputFrame::bReload)) StartReload();
		if (Pressed(&FFallInputFrame::bKick)) TryKick();
	}
	const bool bAiming = (InputNow.bAim || InputNow.bFire || Now - LastShot < 1.2f) && ReloadLeft <= 0.f && RobTime < 0.f;
	Control.bAim = bAiming;
	Control.AimAt = AimPoint;

	// Kick impact.
	if (KickTime >= 0.f && !bKickApplied && KickTime >= KickImpactTime())
	{
		bKickApplied = true;
		AFallEnemy* E = KickTarget.Get();
		if (E && FVector::Dist(E->GetActorLocation(), GetActorLocation()) < W->KickRange * 1.4f)
		{
			const FVector Push = (E->GetActorLocation() - GetActorLocation()).GetSafeNormal();
			if (E->GetType() == EFallEnemyType::Heavy) E->Knockout(Push);
			else if (E->GetType() == EFallEnemyType::Target) { E->ReceiveHit(Push, 2.5f, 60.f); if (!E->IsDead()) E->Stun(); }
			else E->ReceiveHit(Push, 2.5f, W->KickDamage);
			E->VelH += FVector(Push.X, Push.Y, 0.f) * W->KickPush;
			HitStop(W->KickHitStop);
			AddShake(0.5f);
			FallSynth::Play(this, FallSynth::EKind::Kick, E->GetActorLocation(), 1.f);
			FallSynth::Play(this, FallSynth::EKind::HitBody, E->GetActorLocation(), 1.f, 0.75f);
			if (AFallFX* FX = AFallFX::Get(this)) FX->Sparks(E->GetActorLocation(), Push, 20, FLinearColor(1.f, 0.9f, 0.7f), 2500.f);
			if (UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this)) FW->NoteEvent("Kick");
		}
	}
	if (KickTime < 0.f) Control.YawOffset = FMath::FInterpTo(Control.YawOffset, 0.f, Dt, 4.f);

	// Taking the parachute: both hands on the yellow container, a yank, the container is ours.
	if (RobTime >= 0.f)
	{
		RobTime += Dt;
		AFallEnemy* T = RobTarget.Get();
		if (T && T->Pack)
		{
			const FVector PackPos = T->Pack->GetComponentLocation();
			SetActorLocation(FMath::VInterpTo(GetActorLocation(), PackPos + (GetActorLocation() - PackPos).GetSafeNormal() * 70.f, Dt, 6.f));
			const FVector Lat = FVector::CrossProduct((PackPos - GetActorLocation()).GetSafeNormal(), FVector::UpVector).GetSafeNormal();
			Control.HandTarget[0] = PackPos - Lat * 15.f;
			Control.HandTarget[1] = PackPos + Lat * 15.f;
			Control.HandWeight[0] = Control.HandWeight[1] = 1.f;
			Control.LookAt = PackPos;
		}
		if (RobTime > 0.7f && T && !bHasParachute)
		{
			GiveParachute();
			T->Rob((T->GetActorLocation() - GetActorLocation()).GetSafeNormal());
			PendingHit += -(T->GetActorLocation() - GetActorLocation()).GetSafeNormal() * 1.5f;
			HitStop(0.08f);
			AddShake(0.6f);
			FallSynth::Play(this, FallSynth::EKind::Kick, GetActorLocation(), 1.f, 0.7f);
			if (UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this)) FW->NoteEvent("ParachuteTaken");
			if (AFallDirector* Dir = AFallDirector::Get(this)) Dir->MarkParachuteCheckpoint();
		}
		if (RobTime > 1.0f) { RobTime = -1.f; RobTarget = nullptr; Control.HandWeight[0] = Control.HandWeight[1] = 0.f; }
	}

	// Health: regenerates after a quiet moment. Low health desaturates the world and the rim pulses red.
	if (Now - LastDamage > 3.f && Health < MaxHealth) Health = FMath::Min(MaxHealth, Health + 18.f * Dt);
	DamagePulse = FMath::FInterpTo(DamagePulse, 0.f, Dt, 2.5f);
	const float Low = 1.f - FMath::Clamp(Health / MaxHealth, 0.f, 1.f);
	Camera->PostProcessSettings.bOverride_VignetteIntensity = true;
	Camera->PostProcessSettings.VignetteIntensity = 0.35f + FMath::Min(DamagePulse * 0.5f + Low * 0.25f, 0.35f);
	Camera->PostProcessSettings.bOverride_SceneColorTint = true;
	Camera->PostProcessSettings.SceneColorTint = FLinearColor(1.f, 1.f - DamagePulse * 0.35f, 1.f - DamagePulse * 0.4f);
	Camera->PostProcessSettings.bOverride_ColorSaturation = true;
	Camera->PostProcessSettings.ColorSaturation = FVector4(1.f, 1.f, 1.f, 1.f - Low * 0.45f);
	if (Low > 0.5f) PulseLowHealth(0.15f * (0.5f + 0.5f * FMath::Sin(Now * 6.f)) * Low);
}

void AFallPlayer::UpdateCamera(float Dt)
{
	const UFallMovementData* M = UFallMovementData::Resolve(Movement);
	APlayerController* PC = Cast<APlayerController>(GetController());
	const FRotator CR = PC ? PC->GetControlRotation() : GetActorRotation();

	const float SpeedN = FMath::Clamp((FallSpeed - M->BrakeSpeed) / (M->DiveSpeed - M->BrakeSpeed), 0.f, 1.f);
	FovKick = FMath::FInterpTo(FovKick, 0.f, Dt, 3.5f);
	Camera->SetFieldOfView(FMath::Lerp(M->FOV.X, M->FOV.Y, SpeedN) + FovKick);

	// Shake: noise, grows with speed and the dive; impulses from hits.
	ShakeClock += Dt * M->ShakeHz * FMath::Lerp(0.6f, 1.4f, SpeedN);
	ShakeImpulse = FMath::FInterpTo(ShakeImpulse, 0.f, Dt, 4.f);
	const float Amp = FMath::Lerp(M->ShakeDeg.X, M->ShakeDeg.Y, FMath::Max(SpeedN, FMath::Max(Dive, 0.f))) + ShakeImpulse * 3.f;
	const FRotator Shake(FMath::PerlinNoise1D(ShakeClock) * Amp, FMath::PerlinNoise1D(ShakeClock * 1.13f + 7.f) * Amp, FMath::PerlinNoise1D(ShakeClock * 0.7f + 3.f) * Amp * 0.6f);

	// Acceleration: when the body speeds up the camera lags above it; braking lets it catch up.
	const float Acc = (FallSpeed - PrevFallSpeed) / FMath::Max(Dt, 1e-3f);
	PrevFallSpeed = FallSpeed;
	CamAccelOffset = FMath::FInterpTo(CamAccelOffset, FMath::Clamp(Acc * 5.f, -60.f, 70.f), Dt, 3.f);

	// Dolly zoom: the FOV widens with speed while the arm shortens, so the body keeps its size on screen
	// and only the world around it stretches.
	const float Fov = Camera->FieldOfView;
	const float Dolly = FMath::Tan(FMath::DegreesToRadians(M->FOV.X * 0.5f)) / FMath::Tan(FMath::DegreesToRadians((Fov - FovKick) * 0.5f));
	float DiveBias = M->DivePitchBias * FMath::Max(Dive, 0.f) - 7.f * FMath::Clamp(1.f - FMath::Abs(Dive) * 2.f, 0.f, 1.f);
	// Chase framing: when the parachute carrier is below and ahead, the camera leans towards it.
	for (TActorIterator<AFallEnemy> It(GetWorld()); It; ++It)
	{
		if (It->GetType() != EFallEnemyType::Target || bHasParachute || !(It->State == EFallEnemyState::Flee || It->State == EFallEnemyState::Stunned)) continue;
		const FVector To = It->GetActorLocation() - Camera->GetComponentLocation();
		const float WantPitch = FMath::RadiansToDegrees(FMath::Atan2(To.Z, FVector2D(To.X, To.Y).Size()));
		ChaseBias = FMath::FInterpTo(ChaseBias, FMath::Clamp(WantPitch - CR.Pitch, -40.f, 0.f) * 0.45f, Dt, 1.5f);
	}
	if (bHasParachute) ChaseBias = FMath::FInterpTo(ChaseBias, 0.f, Dt, 1.5f);
	DiveBias += ChaseBias;
	bool bCarrierClose = false;
	for (TActorIterator<AFallEnemy> It(GetWorld()); It; ++It)
		if (It->GetType() == EFallEnemyType::Target && !bHasParachute && FVector::Dist(It->GetActorLocation(), GetActorLocation()) < 1800.f) bCarrierClose = true;
	ChaseShoulder = FMath::FInterpTo(ChaseShoulder, (ChaseBias < -2.f || bCarrierClose) ? 1.f : 0.f, Dt, 1.5f);
	CameraArm->SetWorldRotation(FRotator(CR.Pitch + DiveBias, CR.Yaw, 0.f) + Shake);
	ArmPunch = FMath::FInterpTo(ArmPunch, ArmPunchTarget, Dt, 7.f);
	ArmPunchTarget = FMath::FInterpTo(ArmPunchTarget, 0.f, Dt, 2.5f);
	CameraArm->TargetArmLength = M->ArmLength * Dolly * (1.f + ArmPunch) * (1.f - 0.08f * FMath::Abs(InputNow.Slide));
	// In the chase the camera moves over the shoulder so the carrier below is not hidden behind the body.
	CameraArm->SocketOffset = M->CameraOffset * FMath::Lerp(1.f, Dolly, 0.5f) + FVector(0.f, 130.f * ChaseShoulder, CamAccelOffset);
	// Head-down, the body hangs below the pelvis pivot: aim at its middle.
	CameraArm->TargetOffset = FVector(0.f, 0.f, -45.f * FMath::Max(Dive, 0.f));
	// Under the canopy the camera pulls back and up so the canopy and the risers are in frame.
	CanopyCam = FMath::FInterpTo(CanopyCam, bCanopy ? 1.f : 0.f, Dt, 0.9f);
	if (CanopyCam > 0.001f)
	{
		// Low and behind, looking up: the body in the lower third, the canopy above it.
		CameraArm->SetWorldRotation(FRotator(FMath::Lerp(CR.Pitch + DiveBias, 22.f, CanopyCam), CR.Yaw, 0.f) + Shake);
		CameraArm->TargetArmLength *= FMath::Lerp(1.f, 2.1f, CanopyCam);
		CameraArm->TargetOffset += FVector(0.f, 0.f, 200.f * CanopyCam);
		Camera->SetFieldOfView(Camera->FieldOfView + 10.f * CanopyCam);
	}
	if (GetWorld()->GetTimeSeconds() > 1.f) Camera->PostProcessSettings.MotionBlurAmount = FMath::Lerp(0.2f, 0.55f, SpeedN) * (1.f - CanopyCam);

	// Inside the cloud deck: white veil and frost at the edges; out of it the frost melts slower than the veil.
	if (const UFallWorldSubsystem* FWc = UFallWorldSubsystem::Get(this))
	{
		float InCloud = 0.f;
		if (const AFallPlanet* Pl = FWc->Planet.Get())
		{
			const float Km = FWc->Altitude / 1000.f;
			InCloud = FMath::SmoothStep((float)Pl->CloudDeck.X - 0.1f, (float)Pl->CloudDeck.X + 0.15f, Km) * (1.f - FMath::SmoothStep((float)Pl->CloudDeck.X + (float)Pl->CloudDeck.Y - 0.15f, (float)Pl->CloudDeck.X + (float)Pl->CloudDeck.Y + 0.1f, Km));
		}
		CloudVeil = FMath::FInterpTo(CloudVeil, InCloud, Dt, InCloud > CloudVeil ? 3.f : 5.f);
		Frost = FMath::FInterpTo(Frost, FMath::Max(InCloud, 0.f), Dt, InCloud > Frost ? 1.5f : 0.8f);
	}
	if (ScreenWind)
	{
		ScreenWind->SetScalarParameterValue("Strength", FMath::SmoothStep(0.45f, 1.f, SpeedN) * (1.f - CanopyCam) + FovKick * 0.02f);
		ScreenWind->SetScalarParameterValue("Frost", Frost * 0.6f);
		ScreenWind->SetScalarParameterValue("Veil", CloudVeil * 0.45f);
	}
	// Three recorded wind layers by speed and pose; the whistle dies the moment you flare out.
	if (WindLayers.Num() == 4)
	{
		const bool bBrake = Dive < -0.3f;
		const float Under = 1.f - CanopyCam;
		const float Target[4] = {
			(0.35f + 0.45f * SpeedN) * FMath::Lerp(0.35f, 1.f, Under),
			(0.15f + 0.75f * SpeedN) * Under,
			FMath::SmoothStep(0.45f, 1.f, SpeedN) * (bBrake ? 0.f : 0.85f) * Under,
			(0.25f + 0.55f * SpeedN) * Under + 0.25f * CanopyCam};
		const float Rate[4] = {2.f, 3.f, bBrake ? 12.f : 2.5f, 3.f};
		for (int32 i = 0; i < 4; ++i)
		{
			UAudioComponent* A = WindLayers[i];
			if (!A || !A->Sound) continue;
			LayerVol[i] = FMath::FInterpTo(LayerVol[i], Target[i], Dt, Rate[i]);
			A->SetVolumeMultiplier(LayerVol[i]);
			A->SetPitchMultiplier(FMath::Lerp(0.85f, 1.2f, SpeedN));
		}
		if (WindLayers[1]) { WindLayers[1]->SetLowPassFilterEnabled(true); WindLayers[1]->SetLowPassFilterFrequency(FMath::Lerp(900.f, 16000.f, SpeedN) * FMath::Lerp(0.4f, 1.f, Under)); }
	}

	if (WindWave)
	{
		WindWave->Gain.store(FMath::Lerp(0.12f, 0.45f, SpeedN) * (1.f - CanopyCam * 0.8f));
		WindWave->Brightness.store(FMath::Lerp(0.2f, 0.85f, SpeedN));
		WindWave->Whistle.store(FMath::Clamp((SpeedN - 0.5f) * 2.f, 0.f, 1.f));
		WindWave->Buffet.store(FMath::Lerp(0.3f, 1.f, SpeedN));
	}
}

void AFallPlayer::BeginIntro()
{
	bInIntro = true;
	FallSpeed = 0.f;
	VelocityH = FVector::ZeroVector;
	BodyPivot->SetRelativeRotation(FRotator::ZeroRotator);
	Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	if (WeaponMesh) WeaponMesh->SetVisibility(false);
}

void AFallPlayer::EndIntro(const FVector& ThrowDir)
{
	bInIntro = false;
	Mesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Mesh->SetAnimInstanceClass(UFallAnimInstance::StaticClass());
	if (UFallAnimInstance* AI = Cast<UFallAnimInstance>(Mesh->GetAnimInstance())) AI->AnimData = AnimData;
	if (WeaponMesh) WeaponMesh->SetVisibility(true);
	FallSpeed = 0.f;
	AddHorizontalImpulse(FVector(ThrowDir.X, ThrowDir.Y, 0.f).GetSafeNormal() * 900.f);
	AddImpulse(ThrowDir, 3.f);
	AddShake(1.5f);
	HitStop(0.06f);
	if (APlayerController* PC = Cast<APlayerController>(GetController())) PC->SetControlRotation(FRotator(22.f, ThrowDir.Rotation().Yaw, 0.f)); // the same view as a plain start
}

void AFallPlayer::Tick(float DeltaSeconds)
{
	if (bInIntro)
	{
		AActor::Tick(DeltaSeconds);
		FallSpeed = 0.f;
		if (UFallWorldSubsystem* FWi = UFallWorldSubsystem::Get(this)) { FWi->PlayerFallSpeed = 0.f; FWi->PlayerVelocityH = FVector::ZeroVector; }
		return;
	}
	if (HitStopLeft > 0.f)
	{
		HitStopLeft -= FApp::GetDeltaTime();
		if (HitStopLeft <= 0.f) UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
	}
	GatherInput(DeltaSeconds);
	if (Pressed(&FFallInputFrame::bMenu) && !BotInput) SFallMenu::Toggle(Cast<APlayerController>(GetController()));
	GrabCooldown -= DeltaSeconds;
	// Brake: the body hits the air. Dive: the head drops first.
	if (InputNow.Dive < -0.5f && InputPrev.Dive >= -0.5f)
	{
		AddShake(0.4f); AddImpulse(FVector::UpVector, 1.8f); FovKick = -7.f; ArmPunchTarget = -0.15f;
		for (TActorIterator<AFallSpeedField> It(GetWorld()); It; ++It) It->Burst(2);
	}
	if (InputNow.Dive > 0.5f && InputPrev.Dive <= 0.5f) { AddShake(0.35f); AddImpulse(GetActorForwardVector() * -1.f + FVector(0, 0, -0.5f), 0.8f); FovKick = 8.f; ArmPunchTarget = 0.12f; }
	if (IsHolding())
	{
		if (Pressed(&FFallInputFrame::bPush)) PushOff();
		else if (Pressed(&FFallInputFrame::bGrab)) Release();
	}
	else if (GrabCooldown <= 0.f)
	{
		// Grab with a generous window: a press up to 0.8 s before a piece is in reach still counts.
		if (Pressed(&FFallInputFrame::bGrab)) GrabIntentTime = GetWorld()->GetTimeSeconds();
		if (WantsGrab()) if (AFallDebris* Piece = FindGrabbable()) { Grab(Piece); GrabIntentTime = -10.f; }
		// Grab assist: close to a piece that can be held, the player drifts gently towards its handhold.
		AFallDebris* Best = nullptr; float BestD = 380.f;
		for (TActorIterator<AFallDebris> It(GetWorld()); It; ++It)
		{
			if (!It->bGrabbable || It->Holder.IsValid()) continue;
			const float D = It->DistanceTo(GetActorLocation());
			if (D < BestD) { BestD = D; Best = *It; }
		}
		if (Best && BestD > 70.f)
		{
			const FVector S = Best->ClosestSurfacePoint(GetActorLocation());
			AddActorWorldOffset((S - GetActorLocation()).GetSafeNormal() * FMath::Min(BestD - 70.f, 160.f * DeltaSeconds));
		}
	}
	UpdateMovement(DeltaSeconds);

	// Look where the camera looks; combat sets aim, kicks, reloads and the finisher.
	Control.LookAt = Camera->GetComponentLocation() + Camera->GetForwardVector() * 5000.f;
	UpdateCombat(DeltaSeconds);

	if (UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this))
	{
		// Altitude and fall time belong to the director.
	}

	Super::Tick(DeltaSeconds);
	UpdateCamera(DeltaSeconds);
}
