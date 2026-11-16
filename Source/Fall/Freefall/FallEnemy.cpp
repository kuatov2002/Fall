#include "FallEnemy.h"
#include "FallPlayer.h"
#include "FallWorld.h"
#include "FallFX.h"
#include "FallAudio.h"
#include "FallDebris.h"
#include "FallStraps.h"
#include "ProceduralMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

void UFallEnemyData::ApplyDefaults(UFallEnemyData* D, EFallEnemyType Type)
{
	D->Type = Type;
	switch (Type)
	{
	case EFallEnemyType::Shooter:
		D->Health = 55.f; D->TelegraphTime = 0.6f; D->BurstCount = 5; D->ShotInterval = 0.1f; D->Cooldown = FVector2D(2.4f, 3.6f);
		D->Damage = 6.f; D->SpreadDeg = 2.f; D->AimLag = 0.3f; D->Distance = FVector2D(14.f, 24.f); D->Height = 0.f; D->Agility = FVector2D(9.f, 18.f);
		D->SuitColor = FLinearColor(0.09f, 0.03f, 0.025f); D->AccentColor = FLinearColor(1.f, 0.1f, 0.05f);
		break;
	case EFallEnemyType::Wingsuiter:
		D->Health = 45.f; D->TelegraphTime = 1.5f; D->BurstCount = 4; D->ShotInterval = 0.09f; D->Cooldown = FVector2D(3.5f, 4.5f);
		D->Damage = 7.f; D->SpreadDeg = 2.5f; D->AimLag = 0.2f; D->Distance = FVector2D(60.f, 75.f); D->Height = 12.f; D->Agility = FVector2D(38.f, 20.f);
		D->SuitColor = FLinearColor(0.12f, 0.02f, 0.02f); D->AccentColor = FLinearColor(1.f, 0.85f, 0.2f);
		break;
	case EFallEnemyType::Heavy:
		D->Health = 140.f; D->TelegraphTime = 1.0f; D->BurstCount = 16; D->ShotInterval = 0.1f; D->Cooldown = FVector2D(2.2f, 3.f);
		D->Damage = 5.f; D->SpreadDeg = 1.8f; D->AimLag = 0.45f; D->Distance = FVector2D(22.f, 30.f); D->Height = -4.f; D->Agility = FVector2D(4.f, 12.f);
		D->SuitColor = FLinearColor(0.05f, 0.05f, 0.045f); D->AccentColor = FLinearColor(1.f, 0.1f, 0.05f);
		break;
	case EFallEnemyType::Target:
		D->Health = 999.f; D->TelegraphTime = 0.6f; D->BurstCount = 2; D->ShotInterval = 0.2f; D->Cooldown = FVector2D(3.f, 5.f);
		D->Damage = 5.f; D->SpreadDeg = 4.f; D->AimLag = 0.4f; D->Distance = FVector2D(20.f, 30.f); D->Height = -6.f; D->Agility = FVector2D(10.f, 22.f);
		D->SuitColor = FLinearColor(0.06f, 0.06f, 0.07f); D->AccentColor = FLinearColor(1.f, 0.75f, 0.05f);
		break;
	}
}

AFallEnemy::AFallEnemy()
{
	AutoPossessAI = EAutoPossessAI::Disabled;
	AIControllerClass = nullptr;
	RimColor = FLinearColor(1.f, 0.35f, 0.25f);
	HarnessColor = FLinearColor(0.02f, 0.02f, 0.022f);
	RimIntensity = 0.08f;
}

AFallEnemy* AFallEnemy::Spawn(UWorld* World, EFallEnemyType Type, const FVector& RelPos, UFallEnemyData* InData)
{
	UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(World);
	const FVector PlayerPos = FW && FW->Player.IsValid() ? FW->Player->GetActorLocation() : FVector::ZeroVector;
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	P.bDeferConstruction = true;
	AFallEnemy* E = World->SpawnActor<AFallEnemy>(AFallEnemy::StaticClass(), FTransform(PlayerPos + RelPos), P);
	if (!E) return nullptr;
	if (!InData)
	{
		static const TCHAR* Names[] = {TEXT("Shooter"), TEXT("Wingsuiter"), TEXT("Heavy"), TEXT("Target")};
		InData = LoadObject<UFallEnemyData>(nullptr, *FString::Printf(TEXT("/Game/Freefall/Data/DA_Enemy_%s.DA_Enemy_%s"), Names[(int32)Type], Names[(int32)Type]));
	}
	if (InData)
	{
		E->Data = DuplicateObject<UFallEnemyData>(InData, E);
	}
	else
	{
		E->Data = NewObject<UFallEnemyData>(E);
		UFallEnemyData::ApplyDefaults(E->Data, Type);
	}
	E->Data->Type = Type;
	E->SuitColor = E->Data->SuitColor;
	E->AccentColor = E->Data->AccentColor;
	E->MaxHealth = E->Data->Health;
	E->FinishSpawning(FTransform(PlayerPos + RelPos));
	return E;
}

void AFallEnemy::BeginPlay()
{
	if (!Data)
	{
		Data = NewObject<UFallEnemyData>(this);
		UFallEnemyData::ApplyDefaults(Data, EFallEnemyType::Shooter);
	}
	// Strap tails in the enemy accent colour.
	for (FFallStrapDef& S : Straps->Straps) if (S.Bone == "head") S.Color = Data->AccentColor;
	Super::BeginPlay();

	const UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this);
	OwnFall = FW ? FW->PlayerFallSpeed : 5500.f;
	Orbit = FMath::FRandRange(0.f, 2.f * PI);
	OrbitSpeed = FMath::DegreesToRadians(FMath::FRandRange(6.f, 12.f)) * (FMath::RandBool() ? 1.f : -1.f);
	Dist = FMath::FRandRange(Data->Distance.X, Data->Distance.Y) * 100.f;
	NextAction = FMath::FRandRange(1.f, 2.5f);
	JinkSeed = FMath::FRandRange(0.f, 100.f);
	if (const AActor* Pl = FW ? FW->Player.Get() : nullptr)
	{
		const FVector D = GetActorLocation() - Pl->GetActorLocation();
		Orbit = FMath::Atan2(D.Y, D.X);
		PrevRel = D;
	}

	if (GetType() == EFallEnemyType::Wingsuiter)
	{
		Membrane = NewObject<UProceduralMeshComponent>(this, TEXT("Membrane"));
		Membrane->SetupAttachment(RootComponent);
		Membrane->SetUsingAbsoluteLocation(true);
		Membrane->SetUsingAbsoluteRotation(true);
		Membrane->SetUsingAbsoluteScale(true);
		Membrane->SetWorldTransform(FTransform::Identity);
		Membrane->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Membrane->RegisterComponent();
		if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Freefall/Materials/M_FF_LitTwoSided.M_FF_LitTwoSided")))
		{
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(M, this);
			MID->SetVectorParameterValue("Color", FLinearColor(0.25f, 0.03f, 0.02f));
			Membrane->SetMaterial(0, MID);
		}
		Control.Override = "Wingsuit";
		Control.OverrideTarget = 1.f;
		OverrideWeight = 1.f;
		OverridePose = "Wingsuit";
		State = EFallEnemyState::Approach;
	}
	if (GetType() == EFallEnemyType::Heavy)
	{
		BuildTurret();
	}
}

void AFallEnemy::EndPlay(const EEndPlayReason::Type Reason)
{
	SetLaser(false, FVector::ZeroVector, FVector::ZeroVector);
	if (AFallDebris* P = Platform.Get())
	{
		P->Holder = nullptr;
		for (UStaticMeshComponent* C : TurretParts) if (C) C->DestroyComponent();
		if (Shield) Shield->DestroyComponent();
		if (TurretHead) TurretHead->DestroyComponent();
	}
	Super::EndPlay(Reason);
}

void AFallEnemy::BuildOutfit()
{
	// Same harness, red visor; the heavy wears a bulkier vest; the target carries the only parachute container.
	AttachProp("head", TEXT("Sphere"), FVector(0, -1.5f, 9.5f), FRotator::ZeroRotator, FVector(23.f, 26.f, 23.f), FLinearColor(0.03f, 0.03f, 0.03f), 0.35f, 0.2f);
	AttachProp("head", TEXT("Cube"), FVector(0, 9.5f, 6.f), FRotator::ZeroRotator, FVector(17.f, 4.f, 5.f), Data ? Data->AccentColor * 2.f : FLinearColor::Red, 0.1f, 0.6f);
	for (int32 s = -1; s <= 1; s += 2)
	{
		AttachProp("spine_05", TEXT("Cube"), FVector(s * 9.f, 0.f, 9.f), FRotator::ZeroRotator, FVector(4.f, 30.f, 3.f), HarnessColor);
		AttachProp(s < 0 ? "thigh_l" : "thigh_r", TEXT("Cylinder"), FVector(0, 0, -8.f), FRotator::ZeroRotator, FVector(19.f, 19.f, 4.f), HarnessColor);
	}
	const EFallEnemyType T = GetType();
	if (T == EFallEnemyType::Heavy)
	{
		AttachProp("spine_03", TEXT("Cube"), FVector(0, 4.f, 4.f), FRotator::ZeroRotator, FVector(40.f, 34.f, 40.f), FLinearColor(0.08f, 0.08f, 0.07f), 0.5f, 0.4f);
	}
	if (T == EFallEnemyType::Target)
	{
		// Bright yellow container: the one thing worth chasing, readable at 50 m.
		Pack = AttachProp("spine_03", TEXT("Cube"), FVector(0, -19.f, 2.f), FRotator::ZeroRotator, FVector(36.f, 18.f, 46.f), FLinearColor(1.f, 0.62f, 0.02f), 0.5f, 0.f);
		AttachProp("spine_03", TEXT("Cube"), FVector(0, -29.f, 14.f), FRotator::ZeroRotator, FVector(10.f, 4.f, 10.f), FLinearColor(0.8f, 0.05f, 0.03f), 0.5f, 0.f);
	}
	if (T == EFallEnemyType::Shooter || T == EFallEnemyType::Target || T == EFallEnemyType::Wingsuiter)
	{
		// Compact SMG in the right hand.
		Gun = AttachProp("hand_r", TEXT("Cube"), FVector(14.f, 2.f, -3.f), FRotator::ZeroRotator, FVector(26.f, 5.f, 8.f), FLinearColor(0.02f, 0.02f, 0.02f), 0.4f, 0.7f);
	}
}

void AFallEnemy::BuildTurret()
{
	AFallDebrisSpawner* Sp = AFallDebrisSpawner::Get(this);
	if (!Sp) return;
	const UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this);
	const FVector PlayerPos = FW && FW->Player.IsValid() ? FW->Player->GetActorLocation() : FVector::ZeroVector;
	AFallDebris* P = Sp->SpawnPiece(EFallDebrisType::Section, GetActorLocation() - PlayerPos - FVector(0, 0, 250.f), FVector::ZeroVector, 0.8f, true);
	if (!P) return;
	P->SetActorRotation(FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f));
	P->Mover->Spin = FRotator(0.f, 4.f, 0.f);
	P->Holder = this;
	Platform = P;

	UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* Lit = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Freefall/Materials/M_FF_Lit.M_FF_Lit"));
	auto Part = [&](USceneComponent* Parent, UStaticMesh* M, FVector Loc, FRotator Rot, FVector Scale, FLinearColor C)
	{
		UStaticMeshComponent* SM = NewObject<UStaticMeshComponent>(P);
		SM->SetStaticMesh(M);
		SM->SetupAttachment(Parent);
		SM->SetRelativeLocation(Loc);
		SM->SetRelativeRotation(Rot);
		SM->SetRelativeScale3D(Scale);
		SM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SM->RegisterComponent();
		if (Lit)
		{
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Lit, P);
			MID->SetVectorParameterValue("Color", C);
			MID->SetScalarParameterValue("Metallic", 0.7f);
			MID->SetScalarParameterValue("Roughness", 0.45f);
			SM->SetMaterial(0, MID);
		}
		TurretParts.Add(SM);
		return SM;
	};
	const float Top = P->Box->GetUnscaledBoxExtent().Z;
	Part(P->GetRootComponent(), Cyl, FVector(0, 0, Top + 30.f), FRotator::ZeroRotator, FVector(1.4f, 1.4f, 0.6f), FLinearColor(0.06f, 0.06f, 0.06f));
	TurretHead = NewObject<USceneComponent>(P, TEXT("TurretHead"));
	TurretHead->SetupAttachment(P->GetRootComponent());
	TurretHead->SetRelativeLocation(FVector(0, 0, Top + 95.f));
	TurretHead->RegisterComponent();
	Part(TurretHead, Cube, FVector(0, 0, 0), FRotator::ZeroRotator, FVector(1.2f, 0.9f, 0.7f), FLinearColor(0.1f, 0.1f, 0.09f));
	Part(TurretHead, Cyl, FVector(110.f, 0, 8.f), FRotator(-90.f, 0, 0), FVector(0.25f, 0.25f, 1.8f), FLinearColor(0.03f, 0.03f, 0.03f));
	Part(TurretHead, Cube, FVector(55.f, 0, 25.f), FRotator::ZeroRotator, FVector(0.12f, 1.9f, 1.5f), FLinearColor(0.35f, 0.33f, 0.28f));
	Part(TurretHead, Cube, FVector(56.f, 0, 25.f), FRotator::ZeroRotator, FVector(0.1f, 0.4f, 0.2f), Data->AccentColor * 3.f);
	Shield = NewObject<UBoxComponent>(P, TEXT("TurretShield"));
	Shield->SetupAttachment(TurretHead);
	Shield->SetRelativeLocation(FVector(55.f, 0, 25.f));
	Shield->SetBoxExtent(FVector(8.f, 95.f, 75.f));
	Shield->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Shield->SetCollisionResponseToAllChannels(ECR_Ignore);
	Shield->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Shield->RegisterComponent();
	Control.Override = "Grab";
	Control.OverrideTarget = 1.f;
}

FVector AFallEnemy::Muzzle() const
{
	if (GetType() == EFallEnemyType::Heavy && TurretHead)
	{
		return TurretHead->GetComponentTransform().TransformPosition(FVector(200.f, 0, 8.f));
	}
	if (Gun) return Gun->GetComponentTransform().TransformPosition(FVector(60.f, 0.f, 0.f));
	return Mesh->GetSocketLocation("hand_r");
}

void AFallEnemy::SetLaser(bool bOn, const FVector& From, const FVector& To)
{
	if (AFallFX* FX = AFallFX::Get(this))
	{
		LaserHandle = FX->Beam(LaserHandle, From, To, FLinearColor(1.f, 0.04f, 0.02f), 1.4f, bOn);
	}
}

bool AFallEnemy::ReceiveHit(const FVector& PushDirWorld, float Strength, float Damage)
{
	if (State == EFallEnemyState::Dead || State == EFallEnemyState::Robbed) return false;
	VelH += FVector(PushDirWorld.X, PushDirWorld.Y, 0.f).GetSafeNormal() * 350.f * Strength;
	if (GetType() == EFallEnemyType::Target)
	{
		// The target cannot die before the chase ends: damage fills a stun meter instead.
		PendingHit += PushDirWorld.GetSafeNormal() * Strength;
		Flash(FLinearColor(1.f, 0.6f, 0.1f), 1.5f + Strength);
		StunMeter += Damage;
		if (StunMeter >= 40.f && State != EFallEnemyState::Stunned)
		{
			State = EFallEnemyState::Stunned;
			StateTime = 0.f;
			StunMeter = 0.f;
			SetLaser(false, FVector::ZeroVector, FVector::ZeroVector);
			if (UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this)) FW->NoteEvent("TargetStunned");
		}
		return false;
	}
	const bool bKilled = Super::ReceiveHit(PushDirWorld, Strength, Damage);
	Flash(FLinearColor(1.f, 0.95f, 0.9f), 2.f + Strength); // white, so it never merges with the harness colours
	if (bKilled) Knockout(PushDirWorld);
	return bKilled;
}

void AFallEnemy::Knockout(const FVector& PushDir)
{
	if (State == EFallEnemyState::Dead) return;
	State = EFallEnemyState::Dead;
	StateTime = 0.f;
	Health = 0.f;
	SetLaser(false, FVector::ZeroVector, FVector::ZeroVector);
	Control = FFallBodyControl();
	Control.Override = "Limp";
	Control.OverrideTarget = 1.f;
	VelH += FVector(PushDir.X, PushDir.Y, 0.f).GetSafeNormal() * 900.f;
	AttW += FMath::VRand() * 4.f;
	if (AFallDebris* P = Platform.Get()) P->Holder = nullptr;
	Platform = nullptr;
	if (Membrane) Membrane->SetVisibility(false);
	if (UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this)) FW->NoteEvent("EnemyDown");
}

void AFallEnemy::StartFlee()
{
	if (GetType() != EFallEnemyType::Target || State == EFallEnemyState::Robbed) return;
	State = EFallEnemyState::Flee;
	StateTime = 0.f;
}

void AFallEnemy::Rob(const FVector& PushDir)
{
	State = EFallEnemyState::Robbed;
	StateTime = 0.f;
	if (Pack) Pack->SetVisibility(false);
	Control = FFallBodyControl();
	Control.Override = "Limp";
	Control.OverrideTarget = 1.f;
	VelH += FVector(PushDir.X, PushDir.Y, 0.f).GetSafeNormal() * 700.f;
	OwnFall += 1500.f; // drops away below
	AttW += FMath::VRand() * 3.f;
}

void AFallEnemy::StationKeep(float Dt, const FVector& Desired, float Agility, float FallAgility)
{
	const UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this);
	const float PlayerFall = FW ? FW->PlayerFallSpeed : 5500.f;
	const FVector Err = Desired - GetActorLocation();
	// Vertical: fall faster to go down, slower to go up (seen as dive and brake in the body).
	const float WantFall = PlayerFall - FMath::Clamp(Err.Z * 0.7f, -FallAgility * 100.f, FallAgility * 100.f);
	OwnFall = FMath::FInterpTo(OwnFall, WantFall, Dt, 1.5f);
	const FVector WantH = FVector(Err.X, Err.Y, 0.f).GetClampedToMaxSize(Agility * 100.f / 0.6f) * 0.6f;
	VelH = FMath::VInterpTo(VelH, WantH.GetClampedToMaxSize(Agility * 100.f), Dt, 1.8f);
}

void AFallEnemy::FireShot(const FVector& From, const FVector& AimPoint, float Spread, float Damage, bool bTurret)
{
	const FVector Dir = FMath::VRandCone((AimPoint - From).GetSafeNormal(), FMath::DegreesToRadians(Spread));
	const FVector End = From + Dir * 15000.f;
	FHitResult Hit;
	FCollisionQueryParams Q(TEXT("EnemyShot"), false, this);
	if (AFallDebris* P = Platform.Get()) Q.AddIgnoredActor(P);
	const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, From, End, ECC_Visibility, Q);
	const FVector Impact = bHit ? Hit.ImpactPoint : End;
	AFallFX* FX = AFallFX::Get(this);
	if (FX)
	{
		FX->Tracer(From, Impact, FLinearColor(1.f, 0.3f, 0.15f), bTurret ? 5.f : 3.f);
		FX->Flash(From, bTurret ? 70.f : 40.f, FLinearColor(1.f, 0.55f, 0.2f), 0.05f);
	}
	FallSynth::Play(this, FallSynth::EKind::EnemyShot, From, bTurret ? 1.f : 0.8f, bTurret ? 0.8f : 1.05f);
	if (!bTurret) AddRecoil(0.6f);
	if (bHit)
	{
		if (AFallBody* B = Cast<AFallBody>(Hit.GetActor()))
		{
			B->ReceiveHit(Dir, 0.6f, Damage);
		}
		else if (FX)
		{
			FX->Sparks(Impact, Hit.ImpactNormal, 8, FLinearColor(1.f, 0.7f, 0.35f));
			FallSynth::Play(this, FallSynth::EKind::Ricochet, Impact, 0.7f);
		}
	}
}

void AFallEnemy::Tick(float Dt)
{
	UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this);
	AFallPlayer* P = FW ? FW->Player.Get() : nullptr;
	StateTime += Dt;

	if (P && GetType() != EFallEnemyType::Wingsuiter)
	{
		switch (GetType())
		{
		case EFallEnemyType::Heavy: TickHeavy(Dt, P); break;
		case EFallEnemyType::Target: TickTarget(Dt, P); break;
		default: TickShooter(Dt, P); break;
		}
		// Relative fall.
		if (!Platform.IsValid())
		{
			AddActorWorldOffset(FVector(VelH.X * Dt, VelH.Y * Dt, ((FW ? FW->PlayerFallSpeed : 5500.f) - OwnFall) * Dt));
		}
		Control.AirVelocity = FVector(-VelH.X, -VelH.Y, OwnFall);
	}
	else if (P)
	{
		TickWingsuiter(Dt, P);
	}

	if (State == EFallEnemyState::Dead || State == EFallEnemyState::Robbed)
	{
		DeadTime += Dt;
		OwnFall = FMath::FInterpTo(OwnFall, (FW ? FW->PlayerFallSpeed : 5500.f) + (State == EFallEnemyState::Robbed ? 2500.f : -800.f), Dt, 0.8f);
		if (DeadTime > 7.f) Destroy();
	}
	Super::Tick(Dt);
	if (Membrane) UpdateMembrane();
}

void AFallEnemy::TickShooter(float Dt, AFallPlayer* P)
{
	if (State == EFallEnemyState::Dead) return;
	const FVector PP = P->GetActorLocation();
	Orbit += OrbitSpeed * Dt;
	const FVector Desired = PP + FVector(FMath::Cos(Orbit) * Dist, FMath::Sin(Orbit) * Dist, Data->Height * 100.f + FMath::PerlinNoise1D(StateTime * 0.2f + JinkSeed) * 300.f);
	StationKeep(Dt, Desired, Data->Agility.X, Data->Agility.Y);

	// Body language: dive or brake with the vertical correction, bank with the lateral motion, face the player.
	const float RelFall = (OwnFall - P->FallSpeed * 100.f) / 100.f;
	Control.Dive = FMath::Clamp(RelFall / 12.f, -1.f, 1.f);
	const FVector ToP = PP - GetActorLocation();
	const FRotator Face(0.f, ToP.Rotation().Yaw, 0.f);
	SetActorRotation(FMath::RInterpTo(GetActorRotation(), Face, Dt, 2.5f));
	Control.Bank = FMath::Clamp((VelH | GetActorRightVector()) / 900.f, -1.f, 1.f);
	Control.LookAt = PP;
	const FVector Chest = PP;
	LaggedAim = LaggedAim.IsZero() ? Chest : FMath::VInterpTo(LaggedAim, Chest, Dt, 1.f / FMath::Max(Data->AimLag, 0.05f));

	NextAction -= Dt;
	switch (State)
	{
	case EFallEnemyState::Approach:
		if (ToP.Size() < Dist * 1.4f) { State = EFallEnemyState::Hold; StateTime = 0.f; }
		break;
	case EFallEnemyState::Hold:
	case EFallEnemyState::Cooldown:
		Control.bAim = false;
		if (NextAction <= 0.f && ToP.Size() < 4500.f)
		{
			State = EFallEnemyState::Telegraph;
			StateTime = 0.f;
			FallSynth::Play(this, FallSynth::EKind::Charge, GetActorLocation(), 0.9f);
		}
		break;
	case EFallEnemyState::Telegraph:
		// 0.6 s: gun up, red laser locked on the player. The aim during the burst lags behind this.
		Control.bAim = true;
		Control.AimAt = LaggedAim;
		SetLaser(StateTime > 0.12f, Muzzle(), Muzzle() + (LaggedAim - Muzzle()).GetSafeNormal() * FMath::Min((LaggedAim - Muzzle()).Size() + 400.f, 6000.f));
		if (StateTime >= Data->TelegraphTime)
		{
			State = EFallEnemyState::Burst;
			StateTime = 0.f;
			ShotsLeft = Data->BurstCount;
			ShotTimer = 0.f;
			SetLaser(false, FVector::ZeroVector, FVector::ZeroVector);
		}
		break;
	case EFallEnemyState::Burst:
		Control.bAim = true;
		Control.AimAt = LaggedAim;
		ShotTimer -= Dt;
		if (ShotTimer <= 0.f && ShotsLeft > 0)
		{
			ShotTimer = Data->ShotInterval;
			--ShotsLeft;
			FireShot(Muzzle(), LaggedAim, Data->SpreadDeg, Data->Damage, false);
		}
		if (ShotsLeft <= 0)
		{
			State = EFallEnemyState::Cooldown;
			NextAction = FMath::FRandRange(Data->Cooldown.X, Data->Cooldown.Y);
		}
		break;
	default:
		break;
	}
}

void AFallEnemy::TickTarget(float Dt, AFallPlayer* P)
{
	const FVector PP = P->GetActorLocation();
	const FVector ToP = PP - GetActorLocation();
	Control.LookAt = PP;
	switch (State)
	{
	case EFallEnemyState::Flee:
	{
		// Escape dive: faster than a belly flyer, slower than a full dive, with sideways jinks.
		Control.Dive = 1.f;
		Control.LookAt = GetActorLocation() + FVector(0, 0, -5000.f);
		const float T = StateTime;
		const FVector Jink(FMath::PerlinNoise1D(T * 0.25f + JinkSeed), FMath::PerlinNoise1D(T * 0.25f + JinkSeed + 9.f), 0.f);
		VelH = FMath::VInterpTo(VelH, Jink * 1400.f, Dt, 1.f);
		OwnFall = FMath::FInterpTo(OwnFall, 8000.f, Dt, 0.6f);
		Control.Bank = FMath::Clamp((VelH | GetActorRightVector()) / 900.f, -1.f, 1.f);
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, ToP.Rotation().Yaw + 180.f, 0.f), Dt, 1.5f));
		// Glances back and fires a couple of wild shots now and then.
		NextAction -= Dt;
		if (NextAction <= 0.f && ToP.Size() < 3500.f)
		{
			NextAction = FMath::FRandRange(Data->Cooldown.X, Data->Cooldown.Y);
			ShotsLeft = Data->BurstCount;
			ShotTimer = 0.4f;
			FallSynth::Play(this, FallSynth::EKind::Charge, GetActorLocation(), 0.7f, 1.2f);
		}
		if (ShotsLeft > 0)
		{
			Control.bAim = true;
			Control.AimAt = PP;
			Control.LookAt = PP;
			ShotTimer -= Dt;
			if (ShotTimer <= 0.f) { ShotTimer = Data->ShotInterval; --ShotsLeft; FireShot(Muzzle(), PP, Data->SpreadDeg, Data->Damage, false); }
		}
		else Control.bAim = false;
		break;
	}
	case EFallEnemyState::Stunned:
		Control.Override = "Limp";
		Control.OverrideTarget = 1.f;
		Control.bAim = false;
		Control.Dive = -0.5f;
		OwnFall = FMath::FInterpTo(OwnFall, P->FallSpeed * 100.f - 300.f, Dt, 1.5f); // drifts up towards the chaser
		VelH = FMath::VInterpTo(VelH, FVector::ZeroVector, Dt, 1.f);
		if (StateTime > 3.5f)
		{
			State = EFallEnemyState::Flee;
			StateTime = 0.f;
			Control.Override = NAME_None;
		}
		break;
	case EFallEnemyState::Robbed:
	case EFallEnemyState::Dead:
		break;
	default:
	{
		// Before the chase: keeps its distance below the player and does not attack.
		Orbit += OrbitSpeed * Dt;
		const FVector Desired = PP + FVector(FMath::Cos(Orbit) * Dist, FMath::Sin(Orbit) * Dist, Data->Height * 100.f);
		StationKeep(Dt, Desired, Data->Agility.X, Data->Agility.Y);
		Control.Dive = FMath::Clamp((OwnFall - P->FallSpeed * 100.f) / 1200.f, -1.f, 1.f);
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, ToP.Rotation().Yaw, 0.f), Dt, 2.f));
		break;
	}
	}
}

void AFallEnemy::TickHeavy(float Dt, AFallPlayer* P)
{
	AFallDebris* Plat = Platform.Get();
	if (State == EFallEnemyState::Dead || !Plat || !TurretHead)
	{
		if (State != EFallEnemyState::Dead) Knockout(FVector::UpVector);
		return;
	}
	const UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this);
	const FVector PP = P->GetActorLocation();

	// The platform keeps station below and ahead of the player: the heavy holds the centre of the fight.
	Orbit += OrbitSpeed * 0.3f * Dt;
	const FVector Desired = PP + FVector(FMath::Cos(Orbit) * Dist, FMath::Sin(Orbit) * Dist, Data->Height * 100.f);
	const FVector Err = Desired - Plat->GetActorLocation();
	const float PlayerFall = FW ? FW->PlayerFallSpeed : 5500.f;
	Plat->Mover->FallSpeed = FMath::FInterpTo(Plat->Mover->FallSpeed, PlayerFall - FMath::Clamp(Err.Z * 0.5f, -1200.f, 1200.f), Dt, 1.2f);
	Plat->Mover->VelocityH = FMath::VInterpTo(Plat->Mover->VelocityH, FVector(Err.X, Err.Y, 0.f).GetClampedToMaxSize(600.f) * 0.5f, Dt, 1.f);
	OwnFall = Plat->Mover->FallSpeed;
	VelH = Plat->Mover->VelocityH;

	// Turret: slow traverse (flanking beats it), the heavy stands behind it holding two handles.
	const FTransform PT = Plat->GetActorTransform();
	const FVector LocalAim = PT.InverseTransformPositionNoScale(PP) - TurretHead->GetRelativeLocation();
	const float WantYaw = FMath::RadiansToDegrees(FMath::Atan2(LocalAim.Y, LocalAim.X));
	const float WantPitch = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(LocalAim.Z, FVector2D(LocalAim.X, LocalAim.Y).Size())), -60.f, 60.f);
	TurretYaw += FMath::Clamp(FRotator::NormalizeAxis(WantYaw - TurretYaw), -40.f * Dt, 40.f * Dt);
	TurretPitch = FMath::FInterpTo(TurretPitch, WantPitch, Dt, 2.f);
	TurretHead->SetRelativeRotation(FRotator(TurretPitch, TurretYaw, 0.f));
	const FTransform HT = TurretHead->GetComponentTransform();
	const FVector Behind = HT.TransformPosition(FVector(-115.f, 0.f, -10.f));
	SetActorLocation(FMath::VInterpTo(GetActorLocation(), Behind, Dt, 6.f));
	const FVector Fwd = HT.GetUnitAxis(EAxis::X);
	const FQuat FaceWorld = FRotationMatrix::MakeFromXZ(Fwd, FVector::UpVector).ToQuat() * FQuat(FVector::RightVector, FMath::DegreesToRadians(-10.f));
	SetActorRotation(FRotator(0.f, Fwd.Rotation().Yaw, 0.f));
	Control.AttitudeOverride = GetActorQuat().Inverse() * FaceWorld;
	Control.AttitudeWeight = 1.f;
	Control.Override = "Grab";
	Control.OverrideTarget = 1.f;
	Control.HandTarget[0] = HT.TransformPosition(FVector(-45.f, -38.f, 0.f));
	Control.HandTarget[1] = HT.TransformPosition(FVector(-45.f, 38.f, 0.f));
	Control.HandWeight[0] = Control.HandWeight[1] = 1.f;
	Control.LookAt = PP;
	LaggedAim = LaggedAim.IsZero() ? PP : FMath::VInterpTo(LaggedAim, PP, Dt, 1.f / FMath::Max(Data->AimLag, 0.05f));

	const bool bFacing = FMath::Abs(FRotator::NormalizeAxis(WantYaw - TurretYaw)) < 25.f;
	NextAction -= Dt;
	switch (State)
	{
	case EFallEnemyState::Telegraph:
	{
		// 1 s: spin-up whine and a red laser along the barrel.
		const FVector M = Muzzle();
		SetLaser(true, M, M + Fwd * 6000.f);
		if (StateTime >= Data->TelegraphTime)
		{
			State = EFallEnemyState::Burst;
			StateTime = 0.f;
			ShotsLeft = Data->BurstCount;
			ShotTimer = 0.f;
			SetLaser(false, FVector::ZeroVector, FVector::ZeroVector);
		}
		break;
	}
	case EFallEnemyState::Burst:
		ShotTimer -= Dt;
		if (ShotTimer <= 0.f && ShotsLeft > 0)
		{
			ShotTimer = Data->ShotInterval;
			--ShotsLeft;
			FireShot(Muzzle(), Muzzle() + Fwd * 5000.f, Data->SpreadDeg, Data->Damage, true);
		}
		if (ShotsLeft <= 0) { State = EFallEnemyState::Cooldown; NextAction = FMath::FRandRange(Data->Cooldown.X, Data->Cooldown.Y); }
		break;
	default:
		if (NextAction <= 0.f && bFacing && (PP - GetActorLocation()).Size() < 5000.f)
		{
			State = EFallEnemyState::Telegraph;
			StateTime = 0.f;
			FallSynth::Play(this, FallSynth::EKind::Charge, GetActorLocation(), 1.f, 0.6f);
		}
		break;
	}
}

void AFallEnemy::TickWingsuiter(float Dt, AFallPlayer* P)
{
	const UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this);
	const FVector PP = P->GetActorLocation();
	FVector Rel = GetActorLocation() - PP;

	if (State == EFallEnemyState::Dead)
	{
		AddActorWorldOffset(FVector(VelH.X * Dt, VelH.Y * Dt, ((FW ? FW->PlayerFallSpeed : 5500.f) - OwnFall) * Dt));
		Control.AirVelocity = FVector(-VelH.X, -VelH.Y, OwnFall);
		return;
	}

	FVector NewRel = Rel;
	switch (State)
	{
	case EFallEnemyState::Approach:
	case EFallEnemyState::Cooldown:
	{
		// Circle far out, then set up the next run: entry and exit points on a line passing 8-10 m from the player.
		Orbit += FMath::DegreesToRadians(20.f) * Dt * FMath::Sign(OrbitSpeed);
		const FVector Want(FMath::Cos(Orbit) * Dist, FMath::Sin(Orbit) * Dist, Data->Height * 100.f);
		NewRel = FMath::VInterpTo(Rel, Want, Dt, 0.8f);
		NextAction -= Dt;
		if (NextAction <= 0.f && Passes < 3)
		{
			const FVector In = (-FVector(Want.X, Want.Y, 0.f)).GetSafeNormal();
			const FVector Side = FVector::CrossProduct(In, FVector::UpVector) * (FMath::RandBool() ? 1.f : -1.f);
			PassFrom = -In * 4200.f + Side * 900.f + FVector(0, 0, 700.f);
			PassTo = In * 5200.f + Side * 900.f - FVector(0, 0, 500.f);
			PassStartRel = Rel;
			State = EFallEnemyState::Telegraph;
			StateTime = 0.f;
			bWhistled = false;
		}
		break;
	}
	case EFallEnemyState::Telegraph:
	{
		// 1.5 s warning: rising whistle and a bright trail while it swings onto the attack line.
		if (!bWhistled) { FallSynth::Play(this, FallSynth::EKind::Whistle, GetActorLocation(), 1.4f); bWhistled = true; }
		const float A = FMath::Clamp(StateTime / Data->TelegraphTime, 0.f, 1.f);
		NewRel = FMath::Lerp(PassStartRel, PassFrom, A * A * (3.f - 2.f * A));
		if (StateTime >= Data->TelegraphTime) { State = EFallEnemyState::Pass; StateTime = 0.f; ShotsLeft = Data->BurstCount * 2; ShotTimer = 0.25f; }
		break;
	}
	case EFallEnemyState::Pass:
	{
		const float Len = (PassTo - PassFrom).Size();
		const float A = FMath::Clamp(StateTime * 3800.f / Len, 0.f, 1.f);
		NewRel = FMath::Lerp(PassFrom, PassTo, A);
		const FVector Dir = (PassTo - PassFrom).GetSafeNormal();
		const bool bApproaching = ((PP - GetActorLocation()) | Dir) > 0.f;
		ShotTimer -= Dt;
		if (bApproaching && ShotsLeft > 0 && ShotTimer <= 0.f && Rel.Size() < 3500.f)
		{
			ShotTimer = (ShotsLeft % Data->BurstCount == 0) ? 0.35f : Data->ShotInterval;
			--ShotsLeft;
			FireShot(Muzzle(), PP, Data->SpreadDeg, Data->Damage, false);
		}
		Control.bAim = bApproaching && Rel.Size() < 4000.f;
		Control.AimAt = PP;
		if (A >= 1.f)
		{
			++Passes;
			State = EFallEnemyState::Cooldown;
			NextAction = FMath::FRandRange(Data->Cooldown.X, Data->Cooldown.Y);
			Orbit = FMath::Atan2(Rel.Y, Rel.X);
			if (Passes >= 3) Dist = 20000.f; // leaves
		}
		break;
	}
	default:
		break;
	}

	const FVector RelVel = (NewRel - Rel) / FMath::Max(Dt, 1e-3f);
	SetActorLocation(PP + NewRel);
	// Face the flight direction; the wing pose takes the airflow.
	const FVector FlightH(RelVel.X, RelVel.Y, 0.f);
	if (FlightH.SizeSquared() > 100.f) SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, FlightH.Rotation().Yaw, 0.f), Dt, 4.f));
	Control.Bank = FMath::Clamp((RelVel | GetActorRightVector()) / 1500.f, -1.f, 1.f);
	Control.LookAt = PP;
	Control.AirVelocity = -RelVel + FVector(0, 0, 2200.f);
	OwnFall = (FW ? FW->PlayerFallSpeed : 5500.f) - RelVel.Z;
	VelH = FVector(RelVel.X, RelVel.Y, 0.f);

	// Trail: visible ahead of the attack and during it.
	if (State == EFallEnemyState::Telegraph || State == EFallEnemyState::Pass)
	{
		TrailTimer -= Dt;
		if (TrailTimer <= 0.f)
		{
			TrailTimer = 0.025f;
			if (AFallFX* FX = AFallFX::Get(this))
			{
				const float Pl = FW ? FW->PlayerFallSpeed : 5500.f;
				for (const TCHAR* B : {TEXT("foot_l"), TEXT("foot_r")})
				{
					FX->Sprite(Mesh->GetSocketLocation(B), FVector(0, 0, Pl * 0.25f) - FVector(0, 0, Pl), 50.f, 260.f, 1.4f, FLinearColor(1.f, 0.35f, 0.2f), 0.8f, true, 3.f);
				}
			}
		}
	}
}

void AFallEnemy::UpdateMembrane()
{
	if (!Mesh || State == EFallEnemyState::Dead) return;
	auto S = [this](const TCHAR* B) { return Mesh->GetSocketLocation(FName(B)); };
	const FVector HL = S(TEXT("hand_l")), HR = S(TEXT("hand_r")), EL = S(TEXT("lowerarm_l")), ER = S(TEXT("lowerarm_r"));
	const FVector UL = S(TEXT("upperarm_l")), UR = S(TEXT("upperarm_r")), TL = S(TEXT("thigh_l")), TR = S(TEXT("thigh_r"));
	const FVector FL = S(TEXT("foot_l")), FR = S(TEXT("foot_r")), PV = S(TEXT("pelvis")), CL = S(TEXT("calf_l")), CR = S(TEXT("calf_r"));
	TArray<FVector> V = {HL, EL, UL, TL, HR, ER, UR, TR, PV, CL, FL, CR, FR};
	TArray<int32> T = {
		0, 1, 3, 1, 2, 3,           // left arm wing: wrist to hip
		4, 7, 5, 5, 7, 6,           // right arm wing
		8, 9, 11, 9, 10, 11, 11, 10, 12 // leg wing: crotch to ankles
	};
	if (Membrane->GetNumSections() == 0) Membrane->CreateMeshSection_LinearColor(0, V, T, {}, {}, {}, {}, false);
	else Membrane->UpdateMeshSection_LinearColor(0, V, {}, {}, {}, {});
}
