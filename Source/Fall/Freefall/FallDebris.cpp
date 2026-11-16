#include "FallDebris.h"
#include "FallWorld.h"
#include "FallFX.h"
#include "FallBody.h"
#include "FallPlayer.h"
#include "FallAudio.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EngineUtils.h"

namespace
{
	UStaticMesh* Shape(const TCHAR* Name)
	{
		return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Name, Name));
	}
	const FLinearColor Hull(0.16f, 0.17f, 0.14f), HullDark(0.07f, 0.075f, 0.07f), Rust(0.22f, 0.1f, 0.05f), Wood(0.28f, 0.18f, 0.09f), Paint(0.45f, 0.08f, 0.05f);
}

AFallDebris::AFallDebris()
{
	PrimaryActorTick.bCanEverTick = true;
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	Box->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Ignore);
	Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Box->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	RootComponent = Box;
	Mover = CreateDefaultSubobject<UFallMoverComponent>(TEXT("Mover"));
}

UStaticMeshComponent* AFallDebris::AddPart(UStaticMesh* Mesh, const FVector& Loc, const FRotator& Rot, const FVector& Scale, const FLinearColor& Color, float Metal, float Rough)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetStaticMesh(Mesh);
	C->SetupAttachment(RootComponent);
	C->SetRelativeLocation(Loc);
	C->SetRelativeRotation(Rot);
	C->SetRelativeScale3D(Scale);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->RegisterComponent();
	static UMaterialInterface* Lit = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Freefall/Materials/M_FF_Lit.M_FF_Lit"));
	if (Lit)
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Lit, this);
		MID->SetVectorParameterValue("Color", Color);
		MID->SetScalarParameterValue("Metallic", Metal);
		MID->SetScalarParameterValue("Roughness", Rough);
		C->SetMaterial(0, MID);
	}
	Parts.Add(C);
	return C;
}

void AFallDebris::Build(EFallDebrisType InType, float S, bool bInBurning, int32 Seed)
{
	Type = InType;
	bBurning = bInBurning;
	FRandomStream R(Seed);
	UStaticMesh* Cube = Shape(TEXT("Cube"));
	UStaticMesh* Cyl = Shape(TEXT("Cylinder"));
	auto Jit = [&R](float A) { return FRotator(R.FRandRange(-A, A), R.FRandRange(-A, A), R.FRandRange(-A, A)); };
	FVector Ext;
	switch (Type)
	{
	case EFallDebrisType::Plate:
	{
		Ext = FVector(220.f, 130.f, 12.f) * S;
		AddPart(Cube, FVector::ZeroVector, FRotator::ZeroRotator, Ext / 50.f, Hull, 0.5f, 0.55f);
		for (int32 i = -1; i <= 1; ++i)
			AddPart(Cube, FVector(i * 140.f * S, 0, -18.f * S), FRotator::ZeroRotator, FVector(0.18f, 2.5f, 0.35f) * S, HullDark, 0.6f, 0.5f);
		AddPart(Cube, FVector(60.f * S, -95.f * S, 8.f * S), Jit(25.f), FVector(1.6f, 0.9f, 0.15f) * S, Paint, 0.3f, 0.7f);
		AddPart(Cube, FVector(-200.f * S, 90.f * S, 0), FRotator(0, 35, 12), FVector(1.2f, 0.7f, 0.2f) * S, Hull, 0.5f, 0.55f);
		Mass = 1.f;
		break;
	}
	case EFallDebrisType::Girder:
	{
		Ext = FVector(450.f, 24.f, 24.f) * S;
		AddPart(Cube, FVector::ZeroVector, FRotator::ZeroRotator, FVector(9.f, 0.48f, 0.12f) * S, Rust, 0.7f, 0.7f);
		AddPart(Cube, FVector(0, 0, 20.f * S), FRotator::ZeroRotator, FVector(9.f, 0.12f, 0.4f) * S, Rust, 0.7f, 0.7f);
		for (int32 i = -3; i <= 3; ++i)
			AddPart(Cube, FVector(i * 120.f * S, 0, 20.f * S), FRotator(45.f, 0, 0), FVector(0.5f, 0.08f, 0.08f) * S, Rust, 0.7f, 0.7f);
		Mass = 1.5f;
		break;
	}
	case EFallDebrisType::Crate:
	{
		Ext = FVector(65.f) * S;
		AddPart(Cube, FVector::ZeroVector, FRotator::ZeroRotator, FVector(1.3f) * S, Wood, 0.f, 0.85f);
		for (int32 i = -1; i <= 1; i += 2)
			AddPart(Cube, FVector(i * 50.f * S, 0, 0), FRotator::ZeroRotator, FVector(0.08f, 1.34f, 1.34f) * S, HullDark, 0.8f, 0.4f);
		Mass = 1.2f;
		bGrabbable = false;
		break;
	}
	case EFallDebrisType::Engine:
	{
		Ext = FVector(180.f, 95.f, 95.f) * S;
		AddPart(Cyl, FVector::ZeroVector, FRotator(90.f, 0, 0), FVector(1.8f, 1.8f, 3.2f) * S, HullDark, 0.8f, 0.45f);
		AddPart(Cyl, FVector(-170.f * S, 0, 0), FRotator(90.f, 0, 0), FVector(1.3f, 1.3f, 0.6f) * S, Rust, 0.7f, 0.6f);
		AddPart(Cube, FVector(0, 0, 100.f * S), FRotator::ZeroRotator, FVector(2.6f, 0.3f, 0.4f) * S, Hull, 0.5f, 0.5f);
		Propeller = AddPart(Cyl, FVector(185.f * S, 0, 0), FRotator(90.f, 0, 0), FVector(0.4f * S), HullDark, 0.9f, 0.3f);
		for (int32 b = 0; b < 3; ++b)
		{
			UStaticMeshComponent* Blade = AddPart(Cube, FVector::ZeroVector, FRotator::ZeroRotator, FVector(1.f), FLinearColor(0.05f, 0.05f, 0.05f), 0.6f, 0.5f);
			Blade->AttachToComponent(Propeller, FAttachmentTransformRules::KeepRelativeTransform);
			Blade->SetRelativeRotation(FRotator(8.f, b * 120.f, 0.f));
			Blade->SetRelativeLocation(FRotator(0.f, b * 120.f, 0.f).RotateVector(FVector(275.f, 0.f, 0.f)));
			Blade->SetRelativeScale3D(FVector(2.2f, 0.3f, 0.06f) / 0.4f);
		}
		Mass = 3.f;
		break;
	}
	case EFallDebrisType::Section:
	{
		Ext = FVector(650.f, 320.f, 240.f) * S;
		AddPart(Cube, FVector(0, 0, -220.f * S), FRotator::ZeroRotator, FVector(13.f, 6.4f, 0.4f) * S, Hull, 0.5f, 0.55f);
		AddPart(Cube, FVector(0, -300.f * S, -40.f * S), FRotator(0, 0, -12.f), FVector(12.f, 0.3f, 3.6f) * S, Hull, 0.5f, 0.55f);
		AddPart(Cube, FVector(80.f * S, 300.f * S, -90.f * S), FRotator(0, 0, 18.f), FVector(9.f, 0.3f, 2.6f) * S, Paint, 0.3f, 0.7f);
		for (int32 i = -2; i <= 2; ++i)
			AddPart(Cube, FVector(i * 260.f * S, 0, -150.f * S), FRotator::ZeroRotator, FVector(0.3f, 6.2f, 1.2f) * S, HullDark, 0.7f, 0.5f);
		AddPart(Cube, FVector(-200.f * S, 0, 30.f * S), FRotator(0, 0, 0), FVector(3.f, 2.f, 2.f) * S, HullDark, 0.6f, 0.6f);
		Mass = 6.f;
		break;
	}
	case EFallDebrisType::Shards:
	{
		Ext = FVector(35.f, 22.f, 8.f) * S;
		AddPart(Cube, FVector::ZeroVector, Jit(10.f), Ext / 50.f, R.FRand() < 0.5f ? Hull : Rust, 0.5f, 0.6f);
		Mass = 0.3f;
		bGrabbable = false;
		break;
	}
	}
	Box->SetBoxExtent(Ext);
}

FVector AFallDebris::ClosestSurfacePoint(const FVector& P, FVector* OutNormal) const
{
	const FTransform T = Box->GetComponentTransform();
	const FVector E = Box->GetUnscaledBoxExtent();
	const FVector L = T.InverseTransformPositionNoScale(P);
	FVector C(FMath::Clamp(L.X, -E.X, E.X), FMath::Clamp(L.Y, -E.Y, E.Y), FMath::Clamp(L.Z, -E.Z, E.Z));
	// Push to the nearest face, with that face's normal.
	const FVector Rel(FMath::Abs(C.X) / E.X, FMath::Abs(C.Y) / E.Y, FMath::Abs(C.Z) / E.Z);
	FVector N;
	if (L.X > E.X || L.X < -E.X || L.Y > E.Y || L.Y < -E.Y || L.Z > E.Z || L.Z < -E.Z)
	{
		// Outside: the face is the axis with the largest overshoot.
		const FVector Over(FMath::Abs(L.X) - E.X, FMath::Abs(L.Y) - E.Y, FMath::Abs(L.Z) - E.Z);
		const int32 A = Over.X >= Over.Y && Over.X >= Over.Z ? 0 : (Over.Y >= Over.Z ? 1 : 2);
		N = FVector::ZeroVector;
		N[A] = FMath::Sign(L[A]);
	}
	else
	{
		const int32 A = Rel.X >= Rel.Y && Rel.X >= Rel.Z ? 0 : (Rel.Y >= Rel.Z ? 1 : 2);
		N = FVector::ZeroVector;
		N[A] = FMath::Sign(L[A]) == 0 ? 1.f : FMath::Sign(L[A]);
		C[A] = E[A] * N[A];
	}
	if (OutNormal) *OutNormal = T.TransformVectorNoScale(N);
	return T.TransformPositionNoScale(C);
}

float AFallDebris::DistanceTo(const FVector& P) const
{
	const FTransform T = Box->GetComponentTransform();
	const FVector E = Box->GetUnscaledBoxExtent();
	const FVector L = T.InverseTransformPositionNoScale(P);
	const FVector D(FMath::Max(FMath::Abs(L.X) - E.X, 0.f), FMath::Max(FMath::Abs(L.Y) - E.Y, 0.f), FMath::Max(FMath::Abs(L.Z) - E.Z, 0.f));
	return D.Size();
}

void AFallDebris::Tick(float Dt)
{
	Super::Tick(Dt);
	Age += Dt;
	if (Propeller) Propeller->AddLocalRotation(FRotator(0.f, Dt * 420.f, 0.f));
	if (Holder.IsValid())
	{
		Mover->Spin = FMath::RInterpTo(Mover->Spin, FRotator::ZeroRotator, Dt, 3.f);
	}
	if (bBurning)
	{
		AFallFX* FX = AFallFX::Get(this);
		SmokeTimer -= Dt;
		if (FX && SmokeTimer <= 0.f)
		{
			SmokeTimer = 0.12f;
			const FVector E = Box->GetUnscaledBoxExtent();
			const FVector P = Box->GetComponentTransform().TransformPositionNoScale(FVector(FMath::FRandRange(-E.X, E.X) * 0.6f, FMath::FRandRange(-E.Y, E.Y) * 0.6f, E.Z));
			const float Own = Mover->FallSpeed;
			// Fire sticks to the piece (moves with it), smoke is left behind in the air.
			const UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this);
			const FVector PieceVel(Mover->VelocityH.X, Mover->VelocityH.Y, FW ? FW->PlayerFallSpeed - Own : 0.f);
			FX->Sprite(P, PieceVel - FVector(0, 0, FW ? FW->PlayerFallSpeed : 0.f), 160.f, 70.f, 0.3f, FLinearColor(1.f, 0.42f, 0.08f), 0.9f, true, 9.f);
			FX->Sprite(P, FMath::VRand() * 100.f, 180.f, 1100.f, 2.2f, FLinearColor(0.06f, 0.055f, 0.05f), 0.75f, true, 1.f);
		}
	}
}

// ---------------------------------------------------------------- spawner

AFallDebrisSpawner::AFallDebrisSpawner()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	Rng.Initialize(4242);
}

AFallDebrisSpawner* AFallDebrisSpawner::Get(const UObject* WorldContext)
{
	UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!W) return nullptr;
	for (TActorIterator<AFallDebrisSpawner> It(W); It; ++It) return *It;
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return W->SpawnActor<AFallDebrisSpawner>(AFallDebrisSpawner::StaticClass(), FTransform::Identity, P);
}

void AFallDebrisSpawner::StartWave(const FFallDebrisWave& Wave)
{
	FActiveWave A;
	A.Wave = Wave;
	A.Accum = 0.999f; // first piece immediately
	Waves.Add(A);
}

void AFallDebrisSpawner::DestroyAll()
{
	Waves.Reset();
	for (TActorIterator<AFallDebris> It(GetWorld()); It; ++It) It->Destroy();
}

AFallDebris* AFallDebrisSpawner::SpawnPiece(EFallDebrisType Type, const FVector& RelPos, const FVector& RelVelMS, float Scale, bool bBurning)
{
	UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this);
	const FVector PlayerPos = FW && FW->Player.IsValid() ? FW->Player->GetActorLocation() : FVector::ZeroVector;
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FRotator Rot(Rng.FRandRange(-60.f, 60.f), Rng.FRandRange(0.f, 360.f), Rng.FRandRange(-60.f, 60.f));
	AFallDebris* D = GetWorld()->SpawnActor<AFallDebris>(AFallDebris::StaticClass(), FTransform(Rot, PlayerPos + RelPos), P);
	if (!D) return nullptr;
	D->Build(Type, Scale, bBurning, ++Seed);
	const float PlayerFall = FW ? FW->PlayerFallSpeed : 5500.f;
	D->Mover->FallSpeed = PlayerFall - RelVelMS.Z * 100.f;
	D->Mover->VelocityH = FVector(RelVelMS.X, RelVelMS.Y, 0.f) * 100.f;
	const float SpinMax = D->bGrabbable ? 14.f : 60.f;
	D->Mover->Spin = FRotator(Rng.FRandRange(-SpinMax, SpinMax), Rng.FRandRange(-SpinMax, SpinMax), Rng.FRandRange(-SpinMax, SpinMax));
	D->Mover->KillDistance = SpawnDepth * 1.3f;
	return D;
}

void AFallDebrisSpawner::Tick(float Dt)
{
	Super::Tick(Dt);
	UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this);
	if (!FW) return;
	const FVector PlayerPos = FW->Player.IsValid() ? FW->Player->GetActorLocation() : FVector::ZeroVector;

	// Waves.
	for (int32 w = Waves.Num() - 1; w >= 0; --w)
	{
		FActiveWave& A = Waves[w];
		A.Age += Dt;
		A.Accum += A.Wave.Density * Dt;
		while (A.Accum >= 1.f)
		{
			A.Accum -= 1.f;
			const FFallDebrisWave& W = A.Wave;
			const float Rel = Rng.FRandRange(W.RelativeSpeed.X, W.RelativeSpeed.Y);
			const float Z = Rel >= 0.f ? -SpawnDepth : SpawnDepth * 0.6f;
			const float Arrive = FMath::Abs(Z) / FMath::Max(FMath::Abs(Rel) * 100.f, 100.f);
			FVector2D Off;
			if (Rng.FRand() < W.CloseCall)
			{
				const float Ang = Rng.FRandRange(0.f, 2.f * PI);
				Off = FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * Rng.FRandRange(120.f, 420.f);
			}
			else
			{
				const float Ang = Rng.FRandRange(0.f, 2.f * PI);
				Off = FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * FMath::Sqrt(Rng.FRand()) * W.Spread * 100.f;
			}
			// Lead the player's horizontal motion so the pass happens where the player will be.
			const FVector Lead = FW->PlayerVelocityH * Arrive * 0.8f;
			SpawnPiece(W.Type, FVector(Off.X + Lead.X, Off.Y + Lead.Y, Z), FVector(Rng.FRandRange(-1.5f, 1.5f), Rng.FRandRange(-1.5f, 1.5f), Rel),
				Rng.FRandRange(W.Scale.X, W.Scale.Y), Rng.FRand() < W.BurnChance);
		}
		if (A.Age >= A.Wave.Duration) Waves.RemoveAt(w);
	}

	// Collisions with bodies, and near passes for pacing metrics.
	const float Now = GetWorld()->GetTimeSeconds();
	for (TActorIterator<AFallDebris> It(GetWorld()); It; ++It)
	{
		AFallDebris* D = *It;
		const float PrevZ = D->GetActorLocation().Z - (FW->PlayerFallSpeed - D->Mover->FallSpeed) * Dt;
		if ((PrevZ - PlayerPos.Z) * (D->GetActorLocation().Z - PlayerPos.Z) <= 0.f && D->DistanceTo(PlayerPos) < 1200.f)
		{
			FW->NoteEvent("DebrisPass");
			++FW->NearPasses;
		}
		if (Now - D->LastHit < 0.6f) continue;
		for (TActorIterator<AFallBody> B(GetWorld()); B; ++B)
		{
			if (D->Holder.Get() == *B || B->IsDead()) continue;
			const FVector BP = B->GetActorLocation();
			const float RelZ = (FW->PlayerFallSpeed - D->Mover->FallSpeed) * Dt;
			const float Dist = FMath::Min(D->DistanceTo(BP), D->DistanceTo(BP + FVector(0, 0, RelZ * 0.5f)));
			if (Dist > 45.f) continue;
			FVector N;
			const FVector S = D->ClosestSurfacePoint(BP, &N);
			FVector Push = (BP - S).GetSafeNormal(UE_SMALL_NUMBER, N);
			const AFallPlayer* PL = Cast<AFallPlayer>(*B);
			const FVector BodyVelH = PL ? PL->VelocityH : FVector::ZeroVector;
			const FVector RelV = FVector(D->Mover->VelocityH - BodyVelH) + FVector(0, 0, FW->PlayerFallSpeed - D->Mover->FallSpeed);
			const float Strength = FMath::Clamp(RelV.Size() / 1500.f * D->Mass, 0.4f, 3.f);
			B->ReceiveHit(Push + RelV.GetSafeNormal() * 0.5f, Strength, 8.f + 9.f * Strength);
			if (AFallPlayer* P = Cast<AFallPlayer>(*B))
			{
				P->AddHorizontalImpulse(FVector(Push.X, Push.Y, 0.f).GetSafeNormal() * 700.f * Strength);
				P->AddShake(0.4f * Strength);
				P->HitStop(0.05f + 0.02f * Strength);
				FW->NoteEvent("DebrisHit");
			}
			D->Mover->VelocityH -= FVector(Push.X, Push.Y, 0.f) * 250.f / D->Mass;
			D->Mover->Spin += FRotator(Rng.FRandRange(-40.f, 40.f), Rng.FRandRange(-40.f, 40.f), 0.f) / D->Mass;
			D->LastHit = Now;
			if (AFallFX* FX = AFallFX::Get(this)) FX->Sparks(S, Push, 14, FLinearColor(1.f, 0.75f, 0.4f));
			FallSynth::Play(this, FallSynth::EKind::HitMetal, S, 1.f, 1.f / FMath::Sqrt(D->Mass));
			break;
		}
	}
}
