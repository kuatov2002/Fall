#include "FallPlayer.h"
#include "FallTuning.h"
#include "FallWorld.h"
#include "FallAudio.h"
#include "FallStraps.h"
#include "FallDebris.h"
#include "FallFX.h"
#include "EngineUtils.h"
#include "FallEnemy.h"
#include "FallCombatData.h"
#include "FreefallGameMode.h"
#include "FallMenu.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"

AFallPlayer::AFallPlayer()
{
	CameraArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraArm"));
	CameraArm->SetupAttachment(RootComponent);
	CameraArm->SetUsingAbsoluteRotation(true);
	CameraArm->bUsePawnControlRotation = false;
	CameraArm->bDoCollisionTest = false;
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
	Camera->PostProcessSettings.bOverride_ColorContrast = true;
	Camera->PostProcessSettings.ColorContrast = FVector4(1.08f, 1.08f, 1.08f, 1.f);

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
	WindWave = NewObject<UFallWindWave>(this);
	WindAudio->SetSound(WindWave);
	WindAudio->Play();
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
	FallSpeed += (Target - FallSpeed) * FMath::Min(1.f, Rate * Dt);

	// Horizontal: slide along the camera right, drift forward in the track.
	const FRotator Yaw(0.f, GetActorRotation().Yaw, 0.f);
	const FVector Fwd = Yaw.Vector();
	const FVector Right = FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y);
	const float Track = FMath::Clamp(1.f - FMath::Abs(Dive - 0.5f) * 2.5f, 0.f, 1.f);
	const FVector WantH = Right * (InputNow.Slide * M->SlideSpeed * 100.f) + Fwd * (Track * M->TrackForwardSpeed * 100.f);
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
		const float D = It->DistanceTo(GetActorLocation());
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
	SetActorLocation(FMath::VInterpTo(GetActorLocation(), A + N * 38.f, Dt, 9.f));
	FallSpeed = P->Mover->FallSpeed / 100.f;
	VelocityH = P->Mover->VelocityH;
	AddActorWorldOffset(VelocityH * Dt);

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
	Control.HandTarget[0] = A + UpHint * 38.f - Lat * 30.f + N * 3.f;
	Control.HandTarget[1] = A + UpHint * 38.f + Lat * 30.f + N * 3.f;
	Control.HandWeight[0] = 1.f;
	Control.HandWeight[1] = Control.bAim ? 0.f : 1.f;
}

void AFallPlayer::BuildOutfit()
{
	Super::BuildOutfit();
	// Compact SMG in the right hand: barrel along the hand.
	Gun = AttachProp("hand_r", TEXT("Cube"), FVector(14.f, 2.f, -3.f), FRotator::ZeroRotator, FVector(26.f, 5.f, 8.f), FLinearColor(0.02f, 0.02f, 0.022f), 0.4f, 0.7f);
	AttachProp("hand_r", TEXT("Cube"), FVector(8.f, 2.f, -10.f), FRotator::ZeroRotator, FVector(4.f, 4.f, 10.f), FLinearColor(0.02f, 0.02f, 0.022f), 0.5f, 0.3f);
}

AFallEnemy* AFallPlayer::FindEnemyInReach(float Range) const
{
	AFallEnemy* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	const FVector Fwd = Camera->GetForwardVector();
	for (TActorIterator<AFallEnemy> It(GetWorld()); It; ++It)
	{
		if (It->State == EFallEnemyState::Dead || It->State == EFallEnemyState::Robbed) continue;
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
	const FVector Muzzle = Gun ? Gun->GetComponentTransform().TransformPosition(FVector(60.f, 0.f, 0.f)) : Mesh->GetSocketLocation("hand_r");
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
		if (FX) FX->Sparks(Impact, Hit.ImpactNormal, 10, FLinearColor(1.f, 0.75f, 0.4f), 2200.f);
		FallSynth::Play(this, FMath::RandBool() ? FallSynth::EKind::Ricochet : FallSynth::EKind::HitMetal, Impact, 0.8f, 1.3f);
		if (AFallDebris* D = Cast<AFallDebris>(Hit.GetActor())) D->Mover->Spin += FRotator(FMath::FRandRange(-6.f, 6.f), FMath::FRandRange(-6.f, 6.f), 0.f);
	}
}

void AFallPlayer::StartReload()
{
	if (ReloadLeft > 0.f || Ammo >= UFallWeaponData::Resolve(Weapon)->MagazineSize) return;
	ReloadLeft = UFallWeaponData::Resolve(Weapon)->ReloadTime;
	FallSynth::Play(this, FallSynth::EKind::Grab, FVector::ZeroVector, 0.4f, 1.6f);
}

void AFallPlayer::TryKick()
{
	if (KickTime >= 0.f || RobTime >= 0.f) return;
	const UFallWeaponData* W = UFallWeaponData::Resolve(Weapon);
	AFallEnemy* E = FindEnemyInReach(W->KickRange * 1.6f);
	// A stunned parachute carrier in reach: take the parachute instead of kicking.
	if (E && E->CanBeRobbed() && FVector::Dist(E->GetActorLocation(), GetActorLocation()) < W->KickRange * 1.55f)
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
	StartKick(bRight);
	FallSynth::Play(this, FallSynth::EKind::Whoosh, FVector::ZeroVector, 0.6f, 1.3f);
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
	LastDamage = GetWorld()->GetTimeSeconds();
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

	if (RobTime < 0.f && !bCanopy)
	{
		if (InputNow.bFire) Fire();
		if (Pressed(&FFallInputFrame::bReload)) StartReload();
		if (Pressed(&FFallInputFrame::bKick)) TryKick();
	}
	const bool bAiming = (InputNow.bAim || InputNow.bFire || Now - LastShot < 1.2f) && ReloadLeft <= 0.f && RobTime < 0.f;
	Control.bAim = bAiming;
	Control.AimAt = AimPoint;
	// Reload: the left hand comes to the gun.
	if (ReloadLeft > 0.f && Gun)
	{
		Control.HandTarget[0] = Gun->GetComponentLocation();
		Control.HandWeight[0] = 1.f;
	}

	// Kick impact.
	if (KickTime >= 0.f && !bKickApplied && KickTime >= KickImpactTime())
	{
		bKickApplied = true;
		AFallEnemy* E = KickTarget.Get();
		if (E && FVector::Dist(E->GetActorLocation(), GetActorLocation()) < W->KickRange * 1.4f)
		{
			const FVector Push = (E->GetActorLocation() - GetActorLocation()).GetSafeNormal();
			if (E->GetType() == EFallEnemyType::Heavy) E->Knockout(Push);
			else if (E->GetType() == EFallEnemyType::Target) E->ReceiveHit(Push, 2.5f, 100.f);
			else E->ReceiveHit(Push, 2.5f, W->KickDamage);
			E->VelH += FVector(Push.X, Push.Y, 0.f) * W->KickPush;
			HitStop(W->KickHitStop);
			AddShake(0.5f);
			FallSynth::Play(this, FallSynth::EKind::Kick, E->GetActorLocation(), 1.f);
			FallSynth::Play(this, FallSynth::EKind::HitBody, E->GetActorLocation(), 1.f, 0.75f);
			if (AFallFX* FX = AFallFX::Get(this)) FX->Flash(E->GetActorLocation(), 60.f, FLinearColor(1.f, 0.9f, 0.7f), 0.06f);
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
			bHasParachute = true;
			T->Rob((T->GetActorLocation() - GetActorLocation()).GetSafeNormal());
			ParachutePack = AttachProp("spine_03", TEXT("Cube"), FVector(0, -19.f, 2.f), FRotator::ZeroRotator, FVector(36.f, 18.f, 46.f), FLinearColor(1.f, 0.62f, 0.02f), 0.5f, 0.f);
			PendingHit += -(T->GetActorLocation() - GetActorLocation()).GetSafeNormal() * 1.5f;
			HitStop(0.08f);
			AddShake(0.6f);
			FallSynth::Play(this, FallSynth::EKind::Kick, GetActorLocation(), 1.f, 0.7f);
			if (UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this)) FW->NoteEvent("ParachuteTaken");
		}
		if (RobTime > 1.0f) { RobTime = -1.f; RobTarget = nullptr; Control.HandWeight[0] = Control.HandWeight[1] = 0.f; }
	}

	// Health: regenerates after a quiet moment. Low health desaturates the world and the rim pulses red.
	if (Now - LastDamage > 3.f && Health < MaxHealth) Health = FMath::Min(MaxHealth, Health + 18.f * Dt);
	DamagePulse = FMath::FInterpTo(DamagePulse, 0.f, Dt, 2.5f);
	const float Low = 1.f - FMath::Clamp(Health / MaxHealth, 0.f, 1.f);
	Camera->PostProcessSettings.bOverride_VignetteIntensity = true;
	Camera->PostProcessSettings.VignetteIntensity = 0.35f + DamagePulse * 0.9f + Low * 0.5f;
	Camera->PostProcessSettings.bOverride_SceneColorTint = true;
	Camera->PostProcessSettings.SceneColorTint = FLinearColor(1.f, 1.f - DamagePulse * 0.35f, 1.f - DamagePulse * 0.4f);
	Camera->PostProcessSettings.bOverride_ColorSaturation = true;
	Camera->PostProcessSettings.ColorSaturation = FVector4(1.f, 1.f, 1.f, 1.f - Low * 0.6f);
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
	const float DiveBias = M->DivePitchBias * FMath::Max(Dive, 0.f) - 7.f * FMath::Clamp(1.f - FMath::Abs(Dive) * 2.f, 0.f, 1.f);
	CameraArm->SetWorldRotation(FRotator(CR.Pitch + DiveBias, CR.Yaw, 0.f) + Shake);
	ArmPunch = FMath::FInterpTo(ArmPunch, 0.f, Dt, 3.f);
	CameraArm->TargetArmLength = M->ArmLength * Dolly * (1.f + ArmPunch) * (1.f - 0.15f * FMath::Abs(InputNow.Slide));
	CameraArm->SocketOffset = M->CameraOffset * FMath::Lerp(1.f, Dolly, 0.5f) + FVector(0.f, 0.f, CamAccelOffset);
	// Head-down, the body hangs below the pelvis pivot: aim at its middle.
	CameraArm->TargetOffset = FVector(0.f, 0.f, -45.f * FMath::Max(Dive, 0.f));
	if (GetWorld()->GetTimeSeconds() > 1.f) Camera->PostProcessSettings.MotionBlurAmount = 0.2f;

	if (WindWave)
	{
		WindWave->Gain.store(FMath::Lerp(0.25f, 0.9f, SpeedN));
		WindWave->Brightness.store(FMath::Lerp(0.2f, 0.85f, SpeedN));
		WindWave->Whistle.store(FMath::Clamp((SpeedN - 0.5f) * 2.f, 0.f, 1.f));
		WindWave->Buffet.store(FMath::Lerp(0.3f, 1.f, SpeedN));
	}
}

void AFallPlayer::Tick(float DeltaSeconds)
{
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
		AddShake(0.4f); AddImpulse(FVector::UpVector, 1.8f); FovKick = -7.f; CamAccelOffset = -70.f; ArmPunch = -0.18f;
		for (TActorIterator<AFallSpeedField> It(GetWorld()); It; ++It) It->Burst(2);
	}
	if (InputNow.Dive > 0.5f && InputPrev.Dive <= 0.5f) { AddShake(0.35f); AddImpulse(GetActorForwardVector() * -1.f + FVector(0, 0, -0.5f), 0.8f); FovKick = 8.f; CamAccelOffset = 90.f; ArmPunch = 0.1f; }
	if (IsHolding())
	{
		if (Pressed(&FFallInputFrame::bPush)) PushOff();
		else if (Pressed(&FFallInputFrame::bGrab)) Release();
	}
	else if (Pressed(&FFallInputFrame::bGrab) && GrabCooldown <= 0.f)
	{
		Grab(FindGrabbable());
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
