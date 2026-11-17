#include "FallEnemy.h"
#include "FallArt.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
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
#include "EngineUtils.h"

void UFallEnemyData::ApplyDefaults(UFallEnemyData* D, EFallEnemyType Type)
{
	D->Type = Type;
	switch (Type)
	{
	case EFallEnemyType::Shooter:
		D->Health = 55.f; D->TelegraphTime = 0.6f; D->BurstCount = 5; D->ShotInterval = 0.1f; D->Cooldown = FVector2D(2.4f, 3.6f);
		D->Damage = 5.f; D->SpreadDeg = 2.f; D->AimLag = 0.3f; D->Distance = FVector2D(14.f, 24.f); D->Height = 0.f; D->Agility = FVector2D(9.f, 18.f);
		D->SuitColor = FLinearColor(0.09f, 0.03f, 0.025f); D->AccentColor = FLinearColor(1.f, 0.1f, 0.05f);
		break;
	case EFallEnemyType::Wingsuiter:
		D->Health = 45.f; D->TelegraphTime = 2.0f; D->BurstCount = 3; D->ShotInterval = 0.12f; D->Cooldown = FVector2D(5.f, 6.5f);
		D->Damage = 4.f; D->SpreadDeg = 2.5f; D->AimLag = 0.2f; D->Distance = FVector2D(60.f, 75.f); D->Height = 12.f; D->Agility = FVector2D(38.f, 20.f);
		D->SuitColor = FLinearColor(0.12f, 0.02f, 0.02f); D->AccentColor = FLinearColor(1.f, 0.85f, 0.2f);
		break;
	case EFallEnemyType::Heavy:
		D->Health = 140.f; D->TelegraphTime = 1.0f; D->BurstCount = 16; D->ShotInterval = 0.1f; D->Cooldown = FVector2D(2.2f, 3.f);
		D->Damage = 4.f; D->SpreadDeg = 1.8f; D->AimLag = 0.45f; D->Distance = FVector2D(22.f, 30.f); D->Height = -4.f; D->Agility = FVector2D(4.f, 12.f);
		D->SuitColor = FLinearColor(0.05f, 0.05f, 0.045f); D->AccentColor = FLinearColor(1.f, 0.1f, 0.05f);
		break;
	case EFallEnemyType::Target:
		D->Health = 100.f; D->TelegraphTime = 0.6f; D->BurstCount = 2; D->ShotInterval = 0.2f; D->Cooldown = FVector2D(3.f, 5.f);
		D->Damage = 5.f; D->SpreadDeg = 4.f; D->AimLag = 0.4f; D->Distance = FVector2D(20.f, 30.f); D->Height = -6.f; D->Agility = FVector2D(10.f, 22.f);
		D->SuitColor = FLinearColor(0.06f, 0.06f, 0.07f); D->AccentColor = FLinearColor(1.f, 0.75f, 0.05f);
		break;
	}
}

int32 AFallEnemy::ActiveTelegraphs = 0;

AFallEnemy::AFallEnemy()
{
	AutoPossessAI = EAutoPossessAI::Disabled;
	AIControllerClass = nullptr;
	RimColor = FLinearColor(1.f, 0.3f, 0.2f);
	HarnessColor = FLinearColor(0.02f, 0.02f, 0.022f);
	RimIntensity = 0.6f;
}

AFallEnemy* AFallEnemy::Spawn(UWorld* World, EFallEnemyType Type, const FVector& RelPos, UFallEnemyData* InData, bool bDummy)
{
	UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(World);
	const FVector PlayerPos = FW && FW->Player.IsValid() ? FW->Player->GetActorLocation() : FVector::ZeroVector;
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	P.bDeferConstruction = true;
	AFallEnemy* E = World->SpawnActor<AFallEnemy>(AFallEnemy::StaticClass(), FTransform(PlayerPos + RelPos), P);
	if (!E) return nullptr;
	E->bTestDummy = bDummy;
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
		BodyPivot->SetRelativeScale3D(FVector(1.25f)); // a big man: readable as the heavy at a glance
		if (!bTestDummy) BuildTurret();
	}
}

void AFallEnemy::EndPlay(const EEndPlayReason::Type Reason)
{
	if (State == EFallEnemyState::Telegraph && GetType() == EFallEnemyType::Shooter) ActiveTelegraphs = FMath::Max(0, ActiveTelegraphs - 1);
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

UMaterialInterface* AFallEnemy::RoleMaterial() const
{
	const UFallArtData* Art = UFallArtData::Get();
	switch (GetType())
	{
	case EFallEnemyType::Heavy: return Art->HeavyMaterial.LoadSynchronous();
	case EFallEnemyType::Wingsuiter: return Art->WingsuitMaterial.LoadSynchronous();
	case EFallEnemyType::Target: return Art->CarrierMaterial.LoadSynchronous();
	default: return Art->ShooterMaterial.LoadSynchronous();
	}
}

void AFallEnemy::BuildOutfit()
{
	// The suit, helmet and the red accent lights come with the character; the heavy carries a fuel bottle on his
	// back (the weak point), the carrier the only parachute container.
	const UFallArtData* Art = UFallArtData::Get();
	const EFallEnemyType T = GetType();
	if (T == EFallEnemyType::Heavy)
	{
		if (UStaticMesh* Bottle = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Freefall/Assets/Models/small_lpg_tank/small_lpg_tank_2k/StaticMeshes/small_lpg_tank_2k.small_lpg_tank_2k")))
		{
			AttachMesh("spine_03", Bottle, FVector(0, -24.f, 2.f), FRotator::ZeroRotator, 0.75f);
		}
	}
	if (T == EFallEnemyType::Wingsuiter)
	{
		// Twin bottles on the back with a burning exhaust: the run is announced by the flame and its smoke trail.
		if (UStaticMesh* Bottle = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Freefall/Assets/Models/propane_tank/propane_tank_2k/StaticMeshes/propane_tank_2k.propane_tank_2k")))
		{
			for (int32 s = -1; s <= 1; s += 2) AttachMesh("spine_03", Bottle, FVector(s * 9.f, -20.f, -4.f), FRotator(0.f, 0.f, 0.f), 0.55f);
		}
		if (UNiagaraSystem* Fl = Art->Fire.LoadSynchronous())
		{
			Exhaust = UNiagaraFunctionLibrary::SpawnSystemAttached(Fl, Mesh, "spine_02", FVector::ZeroVector, FRotator::ZeroRotator, FVector(0.45f), EAttachLocation::SnapToTarget, false, ENCPoolMethod::None);
		}
	}
	if (T == EFallEnemyType::Target)
	{
		Pack = AttachMesh("spine_03", Art->ParachuteContainer.LoadSynchronous(), FVector(0, -16.f, 2.f), FRotator::ZeroRotator, 1.45f); // no turn: turned 180 the container sat inside the torso and stuck out of the chest
	}
	if (T == EFallEnemyType::Shooter || T == EFallEnemyType::Target || T == EFallEnemyType::Wingsuiter)
	{
		ArmWeapon();
		Gun = WeaponMesh;
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

	const float Top = P->Box->GetUnscaledBoxExtent().Z;
	TurretHead = NewObject<USceneComponent>(P, TEXT("TurretHead"));
	TurretHead->SetupAttachment(P->GetRootComponent());
	TurretHead->SetRelativeLocation(FVector(0, 0, Top + 185.f));
	TurretHead->RegisterComponent();
	if (UStaticMesh* TM = UFallArtData::Get()->Turret.LoadSynchronous())
	{
		// Pintle on the deck, receiver at the head pivot; the model's gun points +X.
		UStaticMeshComponent* SM = NewObject<UStaticMeshComponent>(P);
		SM->SetStaticMesh(TM);
		SM->SetupAttachment(TurretHead);
		SM->SetRelativeLocation(FVector(0, 0, -185.f));
		SM->SetRelativeScale3D(FVector(2.2f));
		SM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SM->RegisterComponent();
		TurretParts.Add(SM);
	}
	Shield = NewObject<UBoxComponent>(P, TEXT("TurretShield"));
	Shield->SetupAttachment(TurretHead);
	Shield->SetRelativeLocation(FVector(90.f, 0, 5.f));
	Shield->SetBoxExtent(FVector(10.f, 92.f, 70.f));
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
		return TurretHead->GetComponentTransform().TransformPosition(FVector(400.f, 0, 0.f));
	}
	if (WeaponMesh) return MuzzleLocation();
	return Mesh->GetSocketLocation("hand_r");
}

void AFallEnemy::SetLaser(bool bOn, const FVector& From, const FVector& To)
{
	if (AFallFX* FX = AFallFX::Get(this))
	{
		// The warning starts as a hairline and thickens with a flicker as the shot comes closer.
		const float A = FMath::Clamp(StateTime / FMath::Max(Data ? Data->TelegraphTime : 0.6f, 0.1f), 0.f, 1.f);
		const float Flicker = 0.6f + 0.4f * FMath::Abs(FMath::Sin(StateTime * 40.f));
		LaserHandle = FX->Beam(LaserHandle, From, To, FLinearColor(1.f, 0.04f, 0.02f) * Flicker, FMath::Lerp(0.5f, 2.6f, A * A), bOn);
	}
}

bool AFallEnemy::ReceiveHit(const FVector& PushDirWorld, float Strength, float Damage)
{
	if (State == EFallEnemyState::Dead || State == EFallEnemyState::Robbed) return false;
	VelH += FVector(PushDirWorld.X, PushDirWorld.Y, 0.f).GetSafeNormal() * 350.f * Strength;
	if (GetType() == EFallEnemyType::Target)
	{
		// The carrier takes real damage. Once, below half health, it breaks away in a hard dive (the chase goes on);
		// at zero it goes limp and keeps falling with the container on its back: the parachute is taken from the body.
		const bool bKilled = Super::ReceiveHit(PushDirWorld, Strength, Damage);
		Flash(FLinearColor(1.f, 0.95f, 0.9f), 1.f + 0.5f * Strength);
		if (!bKilled && StunCount == 0 && Health < MaxHealth * 0.5f && State == EFallEnemyState::Flee)
		{
			++StunCount;
			OwnFall += 2000.f;
			VelH += FMath::VRand().GetSafeNormal2D() * 1200.f;
			if (AFallFX* FX = AFallFX::Get(this)) for (int32 i = 0; i < 6; ++i) FX->Sprite(GetActorLocation(), FMath::VRand() * 500.f, 200.f, 1200.f, 2.f, FLinearColor(0.9f, 0.85f, 0.75f), 0.8f, true, 1.f);
			if (UFallWorldSubsystem* FWm = UFallWorldSubsystem::Get(this)) FWm->NoteEvent("TargetBreakaway");
		}
		if (bKilled)
		{
			Knockout(PushDirWorld);
			if (UFallWorldSubsystem* FWm = UFallWorldSubsystem::Get(this)) FWm->NoteEvent("TargetDown");
		}
		return bKilled;
	}
	const bool bKilled = Super::ReceiveHit(PushDirWorld, Strength, Damage);
	Flash(FLinearColor(1.f, 0.95f, 0.9f), 1.f + 0.5f * Strength); // white and short, so the tumble stays readable
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
	if (AFallFX* FX = AFallFX::Get(this))
	{
		FX->Sparks(GetActorLocation(), PushDir, 24, FLinearColor(1.f, 0.6f, 0.3f), 3500.f);
		for (int32 i = 0; i < 4; ++i) FX->Sprite(GetActorLocation() + FMath::VRand() * 60.f, FMath::VRand() * 400.f, 150.f, 700.f, 1.2f, FLinearColor(0.12f, 0.1f, 0.1f), 0.8f, true, 1.f);
	}
	FallSynth::Play(this, FallSynth::EKind::HitMetal, GetActorLocation(), 0.8f, 0.6f);
	if (UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this)) FW->NoteEvent("EnemyDown");
}

void AFallEnemy::StartFlee()
{
	if (GetType() != EFallEnemyType::Target || State == EFallEnemyState::Robbed || State == EFallEnemyState::Dead) return;
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

void AFallEnemy::BeginTandem(AFallPlayer* By)
{
	State = EFallEnemyState::Robbed;
	StateTime = 0.f;
	TandemBy = By;
	{
		// Offset in the player's own body frame (X chest direction, Y right, Z up the spine), from the bones.
		USkeletalMeshComponent* M = By->Mesh;
		const FVector U = (M->GetSocketLocation("neck_01") - M->GetSocketLocation("pelvis")).GetSafeNormal();
		const FVector R = (M->GetSocketLocation("upperarm_r") - M->GetSocketLocation("upperarm_l")).GetSafeNormal();
		const FVector F = (R ^ U).GetSafeNormal();
		const FVector D = GetActorLocation() - By->GetActorLocation();
		TandemOffset = FVector(D | F, D | R, D | U);
	}
	AddTickPrerequisiteActor(By); // placed after the player has moved: no lag between the two bodies
	Control = FFallBodyControl();
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
	if (!bTurret) { AddRecoil(0.6f); KickSlide(false); }
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

	// Once the player has the parachute the fight is over: the escort breaks off and leaves.
	// Near the ground the sky clears too: the ending must read.
	const bool bClearSky = FW && (FW->FallTime > 165.f || FW->HeightAboveGround() < 400.f);
	if (P && (P->bHasParachute || bClearSky) && GetType() != EFallEnemyType::Target && State != EFallEnemyState::Dead)
	{
		NextAction = 1000.f;
		Passes = 3;
		Dist = 30000.f;
		if (State == EFallEnemyState::Telegraph || State == EFallEnemyState::Burst) { State = EFallEnemyState::Cooldown; SetLaser(false, FVector::ZeroVector, FVector::ZeroVector); }
		ShotsLeft = 0;
	}
	if (AFallPlayer* Q = TandemBy.Get())
	{
		// Hooked: in front of the player and a little higher, the same attitude; the player's hands are on the harness.
		const FQuat PQ = Q->BodyPivot->GetComponentQuat();
		USkeletalMeshComponent* M = Q->Mesh;
		const FVector U = (M->GetSocketLocation("neck_01") - M->GetSocketLocation("pelvis")).GetSafeNormal();
		const FVector R = (M->GetSocketLocation("upperarm_r") - M->GetSocketLocation("upperarm_l")).GetSafeNormal();
		const FVector F = (R ^ U).GetSafeNormal();
		// Chest to the container, the player's head level with the carrier's lower back: the suspension lines from
		// the carrier's shoulders rise clear of the player's head.
		const FVector Want = Q->bCanopy ? FVector(70.f, 0.f, 32.f) : FVector(72.f, 0.f, 30.f); // clear of the big container on the carrier's back
		TandemOffset = FMath::VInterpTo(TandemOffset, Want, Dt, 6.f);
		SetActorRotation(Q->GetActorRotation());
		SetActorLocation(Q->GetActorLocation() + F * TandemOffset.X + R * TandemOffset.Y + U * TandemOffset.Z);
		TandemPlacedFrom = Q->GetActorLocation();
		Control.Override = Q->bCanopy ? FName("Canopy") : (IsDead() ? FName("Limp") : FName("Belly"));
		Control.OverrideTarget = Q->bCanopy || IsDead() ? 1.f : 0.f;
		Control.AirVelocity = Q->Control.AirVelocity;
		Super::Tick(Dt);
		BodyPivot->SetWorldRotation(PQ);
		return;
	}
	if (bTestDummy)
	{
		// Damage test: no AI, just a constant relative velocity.
		AddActorWorldOffset(VelH * Dt);
		Control.AirVelocity = FVector(0, 0, 5500.f);
		Super::Tick(Dt);
		if (Membrane) UpdateMembrane();
		return;
	}
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
		if (GetType() == EFallEnemyType::Target && State == EFallEnemyState::Dead)
		{
			OwnFall = FMath::FInterpTo(OwnFall, 6000.f, Dt, 0.8f);
			VelH = FMath::VInterpTo(VelH, FVector::ZeroVector, Dt, 0.5f);
		}
		else
		{
			OwnFall = FMath::FInterpTo(OwnFall, (FW ? FW->PlayerFallSpeed : 5500.f) + (State == EFallEnemyState::Robbed ? 2500.f : -800.f), Dt, 0.8f);
			if (DeadTime > 7.f) Destroy();
		}
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
	{
		Control.bAim = false;
		bool bOtherWarning = false;
		for (TActorIterator<AFallEnemy> It(GetWorld()); It; ++It) if (*It != this && It->IsTelegraphing()) bOtherWarning = true;
		if (NextAction <= 0.f && ToP.Size() < 4500.f && ActiveTelegraphs < 1 && !bOtherWarning)
		{
			State = EFallEnemyState::Telegraph;
			StateTime = 0.f;
			++ActiveTelegraphs;
			FallSynth::Play(this, FallSynth::EKind::Charge, GetActorLocation(), 0.9f);
		}
		break;
	}
	case EFallEnemyState::Telegraph:
		// 0.6 s: the head turns first, then the gun arm comes up and the body braces (spreads to brake); the red laser
		// locks on the player. The aim during the burst lags behind this.
		Control.bAim = StateTime > 0.15f;
		Control.Dive = -0.6f;
		if (StateTime < Dt * 1.5f) AddImpulse(-ToP.GetSafeNormal(), 0.6f);
		Control.AimAt = LaggedAim;
		SetLaser(StateTime > 0.05f, Muzzle(), Muzzle() + (LaggedAim - Muzzle()).GetSafeNormal() * FMath::Min((LaggedAim - Muzzle()).Size() + 400.f, 6000.f));
		if (StateTime >= Data->TelegraphTime)
		{
			ActiveTelegraphs = FMath::Max(0, ActiveTelegraphs - 1);
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
	// A yellow flare trail from the container: the parachute is always findable, without any HUD marker.
	FlareTimer -= Dt;
	const float ToPlayer = FVector::Dist(GetActorLocation(), P->GetActorLocation());
	if (Pack && Pack->IsVisible() && FlareTimer <= 0.f && (State == EFallEnemyState::Flee || State == EFallEnemyState::Dead || State == EFallEnemyState::Stunned))
	{
		FlareTimer = 0.025f;
		const float Near = FMath::SmoothStep(800.f, 1600.f, ToPlayer);
		if (Near > 0.05f)
			if (AFallFX* FX = AFallFX::Get(this)) FX->Sprite(Pack->GetComponentLocation(), FVector(0, 0, -OwnFall * 0.7f), 220.f, 520.f, 1.6f, FLinearColor(1.f, 0.7f, 0.1f), 0.7f * Near, true, 4.f); // big from the start: small puffs read as beads
	}
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
		OwnFall = FMath::FInterpTo(OwnFall, P->FallSpeed * 100.f - 1200.f, Dt, 2.f); // limp, it rises towards the chaser
		VelH = FMath::VInterpTo(VelH, FVector::ZeroVector, Dt, 1.f);
		if (StateTime > 4.5f)
		{
			State = EFallEnemyState::Flee;
			StateTime = 0.f;
			Control.Override = NAME_None;
		}
		break;
	case EFallEnemyState::Robbed:
	case EFallEnemyState::Dead:
		Control.LookAt = FVector::ZeroVector;
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
	const FVector Behind = HT.TransformPosition(FVector(-125.f, 0.f, -20.f));
	SetActorLocation(FMath::VInterpTo(GetActorLocation(), Behind, Dt, 6.f));
	const FVector Fwd = HT.GetUnitAxis(EAxis::X);
	const FQuat FaceWorld = FRotationMatrix::MakeFromXZ(Fwd, FVector::UpVector).ToQuat() * FQuat(FVector::RightVector, FMath::DegreesToRadians(-10.f));
	SetActorRotation(FRotator(0.f, Fwd.Rotation().Yaw, 0.f));
	Control.AttitudeOverride = GetActorQuat().Inverse() * FaceWorld;
	Control.AttitudeWeight = 1.f;
	Control.Override = "Grab";
	Control.OverrideTarget = 1.f;
	Control.HandTarget[0] = HT.TransformPosition(FVector(-62.f, -22.f, -10.f));
	Control.HandTarget[1] = HT.TransformPosition(FVector(-62.f, 22.f, -10.f));
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
		if (!bWhistled) { FallSynth::Play(this, FallSynth::EKind::Whistle, GetActorLocation(), 1.4f); bWhistled = true; AddImpulse(FVector::UpVector, 1.f); }
		// Tucks the wings to dive onto the attack line, then spreads them again for the pass.
		Control.Override = "Wingsuit";
		Control.OverrideTarget = 1.f - 0.6f * FMath::Sin(PI * FMath::Clamp(StateTime / Data->TelegraphTime, 0.f, 1.f));
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
