#include "FallAnimGallery.h"
#include "FallBody.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	const TCHAR* AngleNames[3] = {TEXT("front"), TEXT("side"), TEXT("game")};
}

TArray<AFallAnimGallery::FEntry> AFallAnimGallery::Entries()
{
	return {
		{TEXT("01_belly"), 3.f},
		{TEXT("02_brake"), 3.f},
		{TEXT("03_track"), 3.f},
		{TEXT("04_headdown"), 3.f},
		{TEXT("05_bank_sweep"), 4.f},
		{TEXT("06_trans_belly_headdown_belly"), 4.f},
		{TEXT("07_trans_belly_brake_belly"), 4.f},
		{TEXT("08_trans_belly_track_belly"), 4.f},
		{TEXT("09_kick"), 2.5f},
		{TEXT("10_grab"), 3.f},
		{TEXT("11_aim_fire"), 3.f},
		{TEXT("12_hit_react"), 3.f},
		{TEXT("13_wingsuit"), 3.f},
		{TEXT("14_limp"), 3.f},
		{TEXT("15_canopy_open"), 3.5f},
	};
}

AFallAnimGallery::AFallAnimGallery()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	auto Light = [this](const TCHAR* Name, FRotator R, float I, bool bShadow)
	{
		UDirectionalLightComponent* L = CreateDefaultSubobject<UDirectionalLightComponent>(Name);
		L->SetupAttachment(RootComponent);
		L->SetWorldRotation(R);
		L->SetIntensity(I);
		L->SetCastShadows(bShadow);
		L->SetMobility(EComponentMobility::Movable);
		return L;
	};
	Light(TEXT("Key"), FRotator(-50.f, 30.f, 0.f), 6.f, true);
	Light(TEXT("Fill"), FRotator(-10.f, 210.f, 0.f), 2.f, false);
	Light(TEXT("Rim"), FRotator(20.f, 120.f, 0.f), 3.f, false);
	Light(TEXT("Under"), FRotator(70.f, 0.f, 0.f), 1.2f, false);

	Backdrop = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Backdrop"));
	Backdrop->SetupAttachment(RootComponent);
	Backdrop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Backdrop->SetCastShadow(false);
	Prop = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Prop"));
	Prop->SetupAttachment(RootComponent);
	Prop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Prop->SetVisibility(false);
}

void AFallAnimGallery::BeginPlay()
{
	Super::BeginPlay();
	if (UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
	{
		Backdrop->SetStaticMesh(Sphere);
		Backdrop->SetWorldScale3D(FVector(400.f));
		Backdrop->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Freefall/Materials/M_FF_Backdrop.M_FF_Backdrop")));
	}
	if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		Prop->SetStaticMesh(Cube);
		if (UMaterialInterface* Lit = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Freefall/Materials/M_FF_Lit.M_FF_Lit")))
		{
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Lit, this);
			MID->SetVectorParameterValue("Color", FLinearColor(0.25f, 0.22f, 0.2f));
			Prop->SetMaterial(0, MID);
		}
	}
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Cam = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, P);
	Cam->GetCameraComponent()->SetFieldOfView(42.f);
	Cam->GetCameraComponent()->SetConstraintAspectRatio(false);
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		PC->SetViewTarget(Cam);
	}
	bCapture = FParse::Value(FCommandLine::Get(), TEXT("FallGallery="), CaptureDir);
	if (bCapture) CaptureDir = FPaths::ProjectSavedDir() / TEXT("Review") / CaptureDir;
	StartEntry(0, bCapture ? 0 : 2);
}

void AFallAnimGallery::StartEntry(int32 Index, int32 InAngle)
{
	Entry = Index;
	Angle = InAngle;
	T = 0.f;
	PrevT = 0.f;
	CapFrame = 0;
	Warmup = 0.8f;
	if (Body) Body->Destroy();
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Body = GetWorld()->SpawnActor<AFallBody>(AFallBody::StaticClass(), FTransform::Identity, P);
	Body->bDrawTrails = true;
	Prop->SetVisibility(false);

	const FString Name = Entries()[Entry].Name;
	if (Name.Contains(TEXT("grab")))
	{
		// A debris plate in front of the chest, at the attitude the grab pose holds.
		const FQuat Att(FVector::RightVector, FMath::DegreesToRadians(80.f));
		const FVector Front = Att.RotateVector(FVector::ForwardVector);
		const FVector UpB = Att.RotateVector(FVector::UpVector);
		const FVector Center = UpB * 25.f + Front * 50.f;
		Prop->SetWorldLocation(Center);
		Prop->SetWorldRotation(FRotationMatrix::MakeFromZX(-Front, UpB).Rotator());
		Prop->SetWorldScale3D(FVector(1.4f, 1.2f, 0.2f));
		Prop->SetVisibility(true);
		GrabPoint = Center - Front * 12.f + UpB * 30.f;
	}
	PlaceCamera();
	ApplyEntry(0.f);
}

void AFallAnimGallery::PlaceCamera()
{
	// Front looks at the head (the head points +X in the belly pose), side shows the profile, game sits behind and above.
	static const FVector Loc[3] = {FVector(430.f, 0.f, 40.f), FVector(0.f, 430.f, 20.f), FVector(-400.f, 45.f, 160.f)};
	const FVector Target(0.f, 0.f, 0.f);
	Cam->SetActorLocation(Loc[Angle]);
	Cam->SetActorRotation((Target - Loc[Angle]).Rotation());
}

void AFallAnimGallery::ApplyEntry(float Dt)
{
	if (!Body) return;
	FFallBodyControl& C = Body->Control;
	C = FFallBodyControl();
	C.AirVelocity = FVector(0, 0, 5500.f);
	C.LookAt = FVector(2000.f, 0.f, -300.f);
	const FString Name = Entries()[Entry].Name;
	const float t = T;
	auto Crossed = [this](float At) { return PrevT < At && T >= At; };

	if (Name.Contains(TEXT("01_belly"))) {}
	else if (Name.Contains(TEXT("02_brake"))) { C.Dive = -1.f; C.AirVelocity.Z = 3800.f; }
	else if (Name.Contains(TEXT("03_track"))) { C.Dive = 0.5f; C.AirVelocity = FVector(-1400.f, 0.f, 6800.f); }
	else if (Name.Contains(TEXT("04_headdown"))) { C.Dive = 1.f; C.AirVelocity.Z = 8800.f; C.LookAt = FVector(2000.f, 0.f, 0.f); }
	else if (Name.Contains(TEXT("05_bank"))) { C.Bank = FMath::Sin(2.f * PI * t / 4.f); }
	else if (Name.Contains(TEXT("06_trans"))) { C.Dive = (t > 0.6f && t < 2.4f) ? 1.f : 0.f; C.AirVelocity.Z = FMath::Lerp(5500.f, 8800.f, FMath::Max(Body->Dive, 0.f)); }
	else if (Name.Contains(TEXT("07_trans"))) { C.Dive = (t > 0.6f && t < 2.4f) ? -1.f : 0.f; C.AirVelocity.Z = FMath::Lerp(5500.f, 3800.f, FMath::Max(-Body->Dive, 0.f)); }
	else if (Name.Contains(TEXT("08_trans"))) { C.Dive = (t > 0.6f && t < 2.4f) ? 0.5f : 0.f; }
	else if (Name.Contains(TEXT("09_kick"))) { if (Crossed(0.5f)) Body->StartKick(true); C.LookAt = FVector(300.f, 0.f, 0.f); }
	else if (Name.Contains(TEXT("10_grab")))
	{
		C.Override = "Grab"; C.OverrideTarget = 1.f;
		const FVector Lat = FVector::RightVector * 24.f;
		C.HandTarget[0] = GrabPoint - Lat; C.HandTarget[1] = GrabPoint + Lat;
		C.HandWeight[0] = C.HandWeight[1] = 1.f;
		C.LookAt = GrabPoint + FVector(0, 0, -200.f);
		C.AirVelocity.Z = 900.f; // falling with the debris: little relative wind
	}
	else if (Name.Contains(TEXT("11_aim")))
	{
		C.bAim = true; C.AimAt = FVector(1200.f, 300.f, -150.f); C.LookAt = C.AimAt;
		for (float s = 0.8f; s < 2.2f; s += 0.22f) if (Crossed(s)) Body->AddRecoil(1.f);
	}
	else if (Name.Contains(TEXT("12_hit")))
	{
		if (Crossed(0.6f)) Body->ReceiveHit(FVector(0, 1, 0), 1.f, 0.f);
		if (Crossed(1.5f)) Body->ReceiveHit(FVector(-1, 0, 0.3f), 1.4f, 0.f);
		if (Crossed(2.3f)) Body->ReceiveHit(FVector(0, -0.3f, 1), 2.f, 0.f);
	}
	else if (Name.Contains(TEXT("13_wing"))) { C.Override = "Wingsuit"; C.OverrideTarget = 1.f; C.AirVelocity = FVector(-4000.f, 0, 3000.f); }
	else if (Name.Contains(TEXT("14_limp"))) { C.Override = "Limp"; C.OverrideTarget = 1.f; }
	else if (Name.Contains(TEXT("15_canopy")))
	{
		if (t > 0.5f) { C.Override = "Canopy"; C.OverrideTarget = 1.f; C.AirVelocity.Z = 600.f; }
		if (Crossed(0.5f)) Body->ReceiveHit(FVector(0, 0, 1), 2.5f, 0.f);
	}
}

void AFallAnimGallery::Tick(float Dt)
{
	Super::Tick(Dt);
	const TArray<FEntry> E = Entries();

	if (!bCapture)
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		{
			const bool Keys[5] = {PC->IsInputKeyDown(EKeys::Right), PC->IsInputKeyDown(EKeys::Left), PC->IsInputKeyDown(EKeys::One), PC->IsInputKeyDown(EKeys::Two), PC->IsInputKeyDown(EKeys::Three)};
			if (Keys[0] && !bPrevKeys[0]) StartEntry((Entry + 1) % E.Num(), Angle);
			if (Keys[1] && !bPrevKeys[1]) StartEntry((Entry + E.Num() - 1) % E.Num(), Angle);
			for (int32 a = 0; a < 3; ++a) if (Keys[2 + a] && !bPrevKeys[2 + a]) { Angle = a; PlaceCamera(); }
			for (int32 k = 0; k < 5; ++k) bPrevKeys[k] = Keys[k];
		}
	}

	if (Warmup > 0.f)
	{
		Warmup -= Dt;
		ApplyEntry(Dt); // hold the first frame's controls so the springs settle
		return;
	}
	PrevT = T;
	T += Dt;
	ApplyEntry(Dt);

	if (bCapture && T < E[Entry].Duration)
	{
		FScreenshotRequest::RequestScreenshot(CaptureDir / E[Entry].Name / FString::Printf(TEXT("%s_%03d.png"), AngleNames[Angle], CapFrame++), false, false);
	}
	if (T >= E[Entry].Duration)
	{
		if (!bCapture) { StartEntry(Entry, Angle); return; }
		if (Angle < 2) StartEntry(Entry, Angle + 1);
		else if (Entry + 1 < E.Num()) StartEntry(Entry + 1, 0);
		else FPlatformMisc::RequestExit(false);
	}
}
