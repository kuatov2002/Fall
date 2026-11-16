#include "FallPlayer.h"
#include "FallTuning.h"
#include "FallWorld.h"
#include "FallAudio.h"
#include "FallStraps.h"
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
	CameraArm->CameraLagSpeed = 9.f;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(CameraArm, USpringArmComponent::SocketName);
	Camera->PostProcessSettings.bOverride_MotionBlurAmount = true;
	Camera->PostProcessSettings.MotionBlurAmount = 0.35f;
	Camera->PostProcessSettings.bOverride_VignetteIntensity = true;
	Camera->PostProcessSettings.VignetteIntensity = 0.35f;

	WindAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("WindAudio"));
	WindAudio->SetupAttachment(RootComponent);
	WindAudio->bAutoActivate = false;
	WindAudio->bAllowSpatialization = false;

	AutoPossessPlayer = EAutoReceiveInput::Player0;
	SuitColor = FLinearColor(0.12f, 0.16f, 0.2f);
}

void AFallPlayer::BeginPlay()
{
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

void AFallPlayer::UpdateCamera(float Dt)
{
	const UFallMovementData* M = UFallMovementData::Resolve(Movement);
	APlayerController* PC = Cast<APlayerController>(GetController());
	const FRotator CR = PC ? PC->GetControlRotation() : GetActorRotation();

	const float SpeedN = FMath::Clamp((FallSpeed - M->BrakeSpeed) / (M->DiveSpeed - M->BrakeSpeed), 0.f, 1.f);
	Camera->SetFieldOfView(FMath::Lerp(M->FOV.X, M->FOV.Y, SpeedN));

	// Shake: noise, grows with speed and the dive; impulses from hits.
	ShakeClock += Dt * M->ShakeHz * FMath::Lerp(0.6f, 1.4f, SpeedN);
	ShakeImpulse = FMath::FInterpTo(ShakeImpulse, 0.f, Dt, 4.f);
	const float Amp = FMath::Lerp(M->ShakeDeg.X, M->ShakeDeg.Y, FMath::Max(SpeedN, FMath::Max(Dive, 0.f))) + ShakeImpulse * 3.f;
	const FRotator Shake(FMath::PerlinNoise1D(ShakeClock) * Amp, FMath::PerlinNoise1D(ShakeClock * 1.13f + 7.f) * Amp, FMath::PerlinNoise1D(ShakeClock * 0.7f + 3.f) * Amp * 0.6f);

	// Acceleration: when the body speeds up the camera lags above it; braking lets it catch up.
	const float Acc = (FallSpeed - PrevFallSpeed) / FMath::Max(Dt, 1e-3f);
	PrevFallSpeed = FallSpeed;
	CamAccelOffset = FMath::FInterpTo(CamAccelOffset, FMath::Clamp(Acc * 6.f, -60.f, 140.f), Dt, 3.f);

	const float DiveBias = M->DivePitchBias * FMath::Max(Dive, 0.f);
	CameraArm->SetWorldRotation(FRotator(CR.Pitch + DiveBias, CR.Yaw, 0.f) + Shake);
	CameraArm->TargetArmLength = M->ArmLength * FMath::Lerp(1.f, 0.9f, FMath::Max(Dive, 0.f));
	CameraArm->SocketOffset = M->CameraOffset + FVector(0.f, 0.f, CamAccelOffset);

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
	UpdateMovement(DeltaSeconds);

	// Look where the camera looks.
	Control.LookAt = Camera->GetComponentLocation() + Camera->GetForwardVector() * 5000.f;
	Control.AimAt = Control.LookAt;

	if (UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this))
	{
		FW->FallTime += DeltaSeconds;
		FW->Altitude -= FallSpeed * DeltaSeconds; // shortcut: M1 integrates altitude here, the director takes over in M4
	}

	Super::Tick(DeltaSeconds);
	UpdateCamera(DeltaSeconds);
}
