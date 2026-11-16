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
#include "FallArt.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

AFallAirship::AFallAirship()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Mover = CreateDefaultSubobject<UFallMoverComponent>(TEXT("Mover"));
	Mover->FallSpeed = 0.f;
	Mover->KillDistance = 60000.f;
}

void AFallAirship::BeginPlay()
{
	Super::BeginPlay();
	const UFallArtData* Art = UFallArtData::Get();
	const float S = Art->AirshipScale;
	// The model runs along its Y axis; the slice expects the length along X.
	const FRotator ModelRot(0.f, 90.f, 0.f);
	for (const TSoftObjectPtr<UStaticMesh>& Soft : Art->AirshipChunks)
	{
		UStaticMesh* M = Soft.LoadSynchronous();
		if (!M) continue;
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(M);
		C->SetupAttachment(RootComponent);
		C->SetRelativeRotation(ModelRot);
		C->SetRelativeScale3D(FVector(S));
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->RegisterComponent();
		Parts.Add(C);
		const FString N = M->GetName();
		// Fire: the envelopes burn hard, the hull smoulders.
		UNiagaraSystem* Sys = (N.Contains(TEXT("Envelope")) ? Art->FireLarge : Art->Fire).LoadSynchronous();
		if (Sys && (N.Contains(TEXT("Envelope")) || N.Contains(TEXT("Hull_Mid")) || N.Contains(TEXT("Engine"))))
		{
			const FBox B = M->GetBoundingBox();
			for (int32 k = 0; k < (N.Contains(TEXT("Envelope")) ? 3 : 1); ++k)
			{
				const FVector Local = B.GetCenter() + FVector(0.f, B.GetExtent().Y * (k - 1) * 0.55f, B.GetExtent().Z * 0.6f);
				UNiagaraComponent* F = UNiagaraFunctionLibrary::SpawnSystemAttached(Sys, C, NAME_None, Local, FRotator::ZeroRotator, FVector(N.Contains(TEXT("Envelope")) ? 9.f : 4.f), EAttachLocation::KeepRelativeOffset, false, ENCPoolMethod::None);
				if (F) Fires.Add(F);
			}
		}
	}
	FallSynth::Play(this, FallSynth::EKind::Explosion, GetActorLocation(), 0.6f, 0.7f);
	// Fire inside the envelope lights the hull from within.
	Glow = NewObject<UPointLightComponent>(this);
	Glow->SetupAttachment(RootComponent);
	Glow->SetRelativeLocation(FVector(0, 0, 800.f));
	Glow->SetIntensityUnits(ELightUnits::Candelas);
	Glow->SetIntensity(600000.f);
	Glow->SetAttenuationRadius(15000.f);
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
	for (int32 i = 0; i < 6; ++i)
	{
		if (FX) FX->Explosion(T.TransformPosition(FVector(FMath::FRandRange(-3200.f, 3200.f), FMath::FRandRange(-1500.f, 1500.f), FMath::FRandRange(-900.f, 1200.f))), FMath::FRandRange(2.5f, 4.f));
	}
	if (!Sp) { Destroy(); return; }

	const float S = UFallArtData::Get()->AirshipScale;
	// Every chunk of the model becomes a falling piece where it was.
	for (UStaticMeshComponent* C : Parts)
	{
		UStaticMesh* M = C ? C->GetStaticMesh() : nullptr;
		if (!M) continue;
		const FString N = M->GetName();
		const bool bEnvelope = N.Contains(TEXT("Envelope")), bProp = N.Contains(TEXT("Propeller"));
		const FVector Center = C->GetComponentTransform().TransformPosition(M->GetBoundingBox().GetCenter());
		const FVector ToPlayer = (PlayerPos - Center) * FVector(1.f, 1.f, 0.f);
		const FVector Out = ToPlayer / FMath::FRandRange(900.f, 1600.f) + FMath::VRand().GetSafeNormal2D() * FMath::FRandRange(0.5f, 2.f);
		AFallDebris* D = Sp->SpawnMeshPiece(M, FTransform(C->GetComponentQuat(), Center), FVector(Out.X, Out.Y, 0.f), S, !bEnvelope && !bProp, bEnvelope ? 10.f : 6.f, bEnvelope || bProp, bProp, !bProp);
		if (D)
		{
			// Fabric drags: the envelopes fall slowest; the hull and engines overtake the player.
			D->Mover->FallSpeed = bEnvelope ? FMath::FRandRange(0.f, 800.f) : FMath::FRandRange(1000.f, 3000.f);
			D->Mover->FallAccel = 980.f;
			D->Mover->TerminalSpeed = (bEnvelope ? FMath::FRandRange(40.f, 50.f) : FMath::FRandRange(70.f, 85.f)) * 100.f;
			D->Mover->KillDistance = 80000.f;
		}
	}
	// Cargo and fittings thrown out of the gondola.
	struct FChunk { EFallDebrisType Type; int32 Count; FVector2D Terminal; };
	const FChunk Loose[] = {
		{EFallDebrisType::Crate, 6, FVector2D(60.f, 75.f)}, {EFallDebrisType::Barrel, 5, FVector2D(60.f, 75.f)},
		{EFallDebrisType::Shards, 16, FVector2D(50.f, 80.f)}, {EFallDebrisType::Plate, 4, FVector2D(55.f, 70.f)},
		{EFallDebrisType::Girder, 4, FVector2D(60.f, 75.f)}, {EFallDebrisType::Ductwork, 3, FVector2D(55.f, 70.f)},
	};
	for (const FChunk& C : Loose)
	{
		for (int32 i = 0; i < C.Count; ++i)
		{
			const FVector Local(FMath::FRandRange(-2500.f, 2500.f), FMath::FRandRange(-900.f, 900.f), FMath::FRandRange(-1200.f, 0.f));
			const FVector World = T.TransformPosition(Local);
			const FVector ToPlayer = (PlayerPos - World) * FVector(1.f, 1.f, 0.f);
			const FVector Out = ToPlayer / FMath::FRandRange(450.f, 900.f) + FMath::VRand().GetSafeNormal2D() * FMath::FRandRange(1.f, 4.f);
			AFallDebris* D = Sp->SpawnPiece(C.Type, World - PlayerPos, FVector(Out.X, Out.Y, 0.f), FMath::FRandRange(0.8f, 1.2f), FMath::FRand() < 0.3f);
			if (D)
			{
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
