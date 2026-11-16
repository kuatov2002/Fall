#include "FallAirship.h"
#include "FallWorld.h"
#include "FallDebris.h"
#include "FallFX.h"
#include "FallAudio.h"
#include "FallPlayer.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/PointLightComponent.h"

AFallAirship::AFallAirship()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Mover = CreateDefaultSubobject<UFallMoverComponent>(TEXT("Mover"));
	Mover->FallSpeed = 0.f;
	Mover->KillDistance = 60000.f;
}

UStaticMeshComponent* AFallAirship::Part(const TCHAR* Shape, const FVector& Loc, const FRotator& Rot, const FVector& SizeM, const FLinearColor& Color)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Shape, Shape)));
	C->SetupAttachment(RootComponent);
	C->SetRelativeLocation(Loc * 100.f);
	C->SetRelativeRotation(Rot);
	C->SetRelativeScale3D(SizeM);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->RegisterComponent();
	static UMaterialInterface* Lit = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Freefall/Materials/M_FF_Lit.M_FF_Lit"));
	if (Lit)
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Lit, this);
		MID->SetVectorParameterValue("Color", Color);
		MID->SetScalarParameterValue("Roughness", 0.75f);
		MID->SetScalarParameterValue("Metallic", 0.1f);
		C->SetMaterial(0, MID);
	}
	Parts.Add(C);
	return C;
}

void AFallAirship::BeginPlay()
{
	Super::BeginPlay();
	const FLinearColor Envelope(0.14f, 0.15f, 0.13f), Metal(0.05f, 0.05f, 0.05f), Red(0.35f, 0.05f, 0.03f);
	// Envelope 70 m long with ribs, gondola below, engine pods, tail fins.
	Part(TEXT("Sphere"), FVector(0, 0, 0), FRotator::ZeroRotator, FVector(70.f, 16.f, 16.f), Envelope);
	for (int32 i = -3; i <= 3; ++i)
		Part(TEXT("Cylinder"), FVector(i * 9.f, 0, 0), FRotator(90.f, 0, 0), FVector(16.4f - FMath::Abs(i) * 1.6f, 16.4f - FMath::Abs(i) * 1.6f, 0.4f), Metal);
	Part(TEXT("Cube"), FVector(4.f, 0, -9.5f), FRotator::ZeroRotator, FVector(18.f, 4.f, 3.f), Metal);
	Part(TEXT("Cube"), FVector(4.f, 0, -8.f), FRotator::ZeroRotator, FVector(14.f, 4.2f, 0.4f), Red);
	for (int32 s = -1; s <= 1; s += 2)
	{
		Part(TEXT("Cylinder"), FVector(-5.f, s * 10.f, -6.f), FRotator(90.f, 0, 0), FVector(2.4f, 2.4f, 6.f), Metal);
		Part(TEXT("Cylinder"), FVector(14.f, s * 9.f, -6.f), FRotator(90.f, 0, 0), FVector(2.4f, 2.4f, 6.f), Metal);
		Part(TEXT("Cube"), FVector(-33.f, s * 7.f, 0), FRotator(0, 0, 0), FVector(8.f, 0.5f, 6.f), Envelope * 0.8f);
	}
	Part(TEXT("Cube"), FVector(-33.f, 0, 7.f), FRotator::ZeroRotator, FVector(8.f, 0.5f, 7.f), Envelope * 0.8f);
	FallSynth::Play(this, FallSynth::EKind::Explosion, GetActorLocation(), 0.6f, 0.7f);
	// Fire inside the envelope lights the hull from within.
	Glow = NewObject<UPointLightComponent>(this);
	Glow->SetupAttachment(RootComponent);
	Glow->SetRelativeLocation(FVector(0, 0, -600.f));
	Glow->SetIntensityUnits(ELightUnits::Candelas);
	Glow->SetIntensity(400000.f);
	Glow->SetAttenuationRadius(12000.f);
	Glow->SetLightColor(FLinearColor(1.f, 0.45f, 0.12f));
	Glow->SetCastShadows(false);
	Glow->RegisterComponent();
}

void AFallAirship::Breakup()
{
	bBroken = true;
	AFallDebrisSpawner* Sp = AFallDebrisSpawner::Get(this);
	AFallFX* FX = AFallFX::Get(this);
	const UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this);
	const FVector PlayerPos = FW && FW->Player.IsValid() ? FW->Player->GetActorLocation() : FVector::ZeroVector;
	const FTransform T = GetActorTransform();
	FallSynth::Play(this, FallSynth::EKind::Explosion, FVector::ZeroVector, 1.f, 0.8f);
	if (AFallPlayer* P = FW ? FW->Player.Get() : nullptr)
	{
		// The shockwave throws the player.
		P->AddShake(1.5f);
		P->HitStop(0.06f);
		P->AddImpulse((P->GetActorLocation() - GetActorLocation()).GetSafeNormal(), 2.5f);
		P->AddHorizontalImpulse((P->GetActorLocation() - GetActorLocation()).GetSafeNormal2D() * 600.f);
	}
	for (int32 i = 0; i < 7; ++i)
	{
		if (FX) FX->Explosion(T.TransformPosition(FVector(FMath::FRandRange(-3200.f, 3200.f), FMath::FRandRange(-700.f, 700.f), FMath::FRandRange(-900.f, 500.f))), FMath::FRandRange(2.5f, 4.f));
	}
	if (!Sp) { Destroy(); return; }

	struct FChunk { EFallDebrisType Type; int32 Count; float Scale; FVector2D Terminal; };
	const FChunk Chunks[] = {
		{EFallDebrisType::Section, 3, 1.f, FVector2D(70.f, 85.f)},
		{EFallDebrisType::Engine, 3, 1.f, FVector2D(75.f, 90.f)},
		{EFallDebrisType::Plate, 8, 1.f, FVector2D(55.f, 70.f)},
		{EFallDebrisType::Girder, 6, 1.f, FVector2D(60.f, 75.f)},
		{EFallDebrisType::Shards, 18, 1.f, FVector2D(50.f, 80.f)},
	};
	for (const FChunk& C : Chunks)
	{
		for (int32 i = 0; i < C.Count; ++i)
		{
			const FVector Local(FMath::FRandRange(-3200.f, 3200.f), FMath::FRandRange(-700.f, 700.f), FMath::FRandRange(-900.f, 600.f));
			const FVector World = T.TransformPosition(Local);
			const FVector ToPlayer = (PlayerPos - World) * FVector(1.f, 1.f, 0.f);
			// Horizontal drift that brings the chunk over the player's line within a few seconds, plus some scatter.
			const FVector Out = ToPlayer / FMath::FRandRange(450.f, 900.f) + FMath::VRand().GetSafeNormal2D() * FMath::FRandRange(1.f, 4.f);
			AFallDebris* D = Sp->SpawnPiece(C.Type, T.TransformPosition(Local) - PlayerPos, FVector(Out.X, Out.Y, 0.f), C.Scale * FMath::FRandRange(0.8f, 1.2f), C.Type == EFallDebrisType::Engine || FMath::FRand() < 0.3f);
			if (D)
			{
				// Chunks start at the airship's (zero) fall speed and accelerate to their own terminal speed.
				// The blast throws chunks down: they overtake the falling player a few seconds later.
				D->Mover->FallSpeed = FMath::FRandRange(1000.f, 3500.f);
				D->Mover->FallAccel = 980.f;
				D->Mover->TerminalSpeed = FMath::FRandRange(C.Terminal.X, C.Terminal.Y) * 100.f;
				D->Mover->KillDistance = 60000.f;
			}
		}
	}
	Destroy();
}

void AFallAirship::Tick(float Dt)
{
	Super::Tick(Dt);
	Age += Dt;
	FireTimer -= Dt;
	if (FireTimer <= 0.f)
	{
		FireTimer = 0.025f;
		if (AFallFX* FX = AFallFX::Get(this))
		{
			const FVector P = GetActorTransform().TransformPosition(FVector(FMath::FRandRange(-2500.f, 1500.f), FMath::FRandRange(-600.f, 600.f), FMath::FRandRange(-700.f, 700.f)));
			const UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this);
			const float Pl = FW ? FW->PlayerFallSpeed : 0.f;
			FX->Sprite(P, FVector(0, 0, -Pl), 1100.f, 500.f, 0.4f, FLinearColor(1.f, 0.4f, 0.08f), 0.95f, true, 14.f);
			FX->Sprite(P, FVector(0, 0, 400.f - Pl), 600.f, 2600.f, 3.f, FLinearColor(0.05f, 0.045f, 0.04f), 0.8f, true, 1.f);
		}
	}
	if (!bBroken && Age >= BreakupDelay) Breakup();
}
