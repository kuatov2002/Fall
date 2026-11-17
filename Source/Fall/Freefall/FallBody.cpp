#include "FallBody.h"
#include "FallStraps.h"
#include "FallArt.h"
#include "Components/SphereComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "DrawDebugHelpers.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

namespace
{
	FQuat SpringQuat(const FQuat& Target, FQuat& Q, FVector& W, float Hz, float Damping, float Dt)
	{
		const float Omega = 2.f * PI * Hz;
		const int32 Steps = FMath::Clamp(FMath::CeilToInt(Dt * 240.f), 1, 16);
		const float H = Dt / Steps;
		for (int32 s = 0; s < Steps; ++s)
		{
			FQuat Err = Target * Q.Inverse();
			Err.EnforceShortestArcWith(FQuat::Identity);
			W += (Err.ToRotationVector() * (Omega * Omega) - W * (2.f * Damping * Omega)) * H;
			Q = (FQuat::MakeFromRotationVector(W * H) * Q).GetNormalized();
		}
		return Q;
	}
}

AFallBody::AFallBody()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(55.f);
	Collision->SetCollisionProfileName(TEXT("Pawn"));
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Collision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	RootComponent = Collision;

	BodyPivot = CreateDefaultSubobject<USceneComponent>(TEXT("BodyPivot"));
	BodyPivot->SetupAttachment(Collision);

	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(BodyPivot);
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> Manny(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	if (Manny.Succeeded())
	{
		Mesh->SetSkeletalMeshAsset(Manny.Object);
	}
	Mesh->SetAnimInstanceClass(UFallAnimInstance::StaticClass());
	Mesh->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Mesh->bEnableUpdateRateOptimizations = false;
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->bCastDynamicShadow = true;

	Straps = CreateDefaultSubobject<UFallStrapsComponent>(TEXT("Straps"));
	auto Add = [this](FName Bone, FVector Offset, FVector Rest, FVector Width, int32 Segs, float Len, FVector2D W, FLinearColor C, float Flutter = 9000.f, float Stiff = 0.25f)
	{
		FFallStrapDef D;
		D.Bone = Bone; D.Offset = Offset; D.RestDir = Rest; D.WidthDir = Width;
		D.Segments = Segs; D.SegmentLength = Len; D.Width = W; D.Color = C; D.Flutter = Flutter; D.RootStiffness = Stiff;
		Straps->Straps.Add(D);
	};
	const FLinearColor Webbing(0.03f, 0.03f, 0.035f);
	// Goggle strap tails from the back of the helmet: small accent that whips in the wind.
	Add("head", FVector(4, -11, 8), FVector(0.2f, -0.5f, -0.85f), FVector(1, 0, 0), 6, 5.f, FVector2D(3.f, 1.5f), FLinearColor(0.05f, 0.05f, 0.05f), 140000.f, 0.15f);
	Add("head", FVector(-4, -11, 8), FVector(-0.2f, -0.5f, -0.85f), FVector(1, 0, 0), 6, 5.f, FVector2D(3.f, 1.5f), FLinearColor(0.05f, 0.05f, 0.05f), 140000.f, 0.15f);
	// Leg strap tails.
	Add("thigh_l", FVector(5, 9, -12), FVector(0, 0.2f, -1), FVector(1, 0, 0), 4, 7.f, FVector2D(3.5f, 3.f), Webbing, 12000.f);
	Add("thigh_r", FVector(-5, 9, -12), FVector(0, 0.2f, -1), FVector(1, 0, 0), 4, 7.f, FVector2D(3.5f, 3.f), Webbing, 12000.f);
	// Chest strap tails.
	Add("spine_04", FVector(6, 15, 0), FVector(0, 0.3f, -1), FVector(1, 0, 0), 3, 6.f, FVector2D(3.f, 2.5f), Webbing, 10000.f);
	Add("spine_04", FVector(-6, 15, 0), FVector(0, 0.3f, -1), FVector(1, 0, 0), 3, 6.f, FVector2D(3.f, 2.5f), Webbing, 10000.f);
	// Suit fabric flapping on the forearms and shins: the cheapest strong cue of airspeed.
	const FLinearColor Fabric(0.03f, 0.045f, 0.09f);
	(void)Fabric;
	// Bridle trailing from the empty harness.
	Add("spine_03", FVector(0, -16, 4), FVector(0, -0.3f, -1), FVector(1, 0, 0), 7, 8.f, FVector2D(3.f, 2.f), FLinearColor(0.2f, 0.075f, 0.02f), 80000.f, 0.1f);
}

void AFallBody::BeginPlay()
{
	if (!AnimData) AnimData = LoadObject<UFallAnimData>(nullptr, TEXT("/Game/Freefall/Data/DA_FallAnim.DA_FallAnim"));
	if (USkeletalMesh* Char = UFallArtData::Get()->CharacterMesh.LoadSynchronous())
	{
		Mesh->SetSkeletalMeshAsset(Char);
		Mesh->SetAnimInstanceClass(UFallAnimInstance::StaticClass());
	}
	Super::BeginPlay();
	Health = MaxHealth;
	Seed = FMath::RandRange(0, 999);
	Mesh->AddTickPrerequisiteActor(this);

	// Pivot at the pelvis: the body rotates around its centre of mass, not its feet.
	if (USkeletalMesh* SK = Mesh->GetSkeletalMeshAsset())
	{
		TArray<FTransform> RefCS;
		FVector Basis[3];
		FallComputeRefCS(SK->GetRefSkeleton(), RefCS, Basis);
		const int32 Pelvis = SK->GetRefSkeleton().FindBoneIndex("pelvis");
		if (Pelvis >= 0)
		{
			Mesh->SetRelativeLocation(-Mesh->GetRelativeRotation().RotateVector(RefCS[Pelvis].GetLocation()));
		}
	}
	if (UFallAnimInstance* AI = Cast<UFallAnimInstance>(Mesh->GetAnimInstance()))
	{
		AI->AnimData = AnimData;
	}

	// Flight suit and gear per role.
	if (UMaterialInterface* RM = RoleMaterial())
	{
		for (int32 m = 0; m < Mesh->GetNumMaterials(); ++m) Mesh->SetMaterial(m, RM);
	}
	if (UMaterialInterface* Add = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Freefall/Materials/M_FF_Additive.M_FF_Additive")))
	{
		FlashMID = UMaterialInstanceDynamic::Create(Add, this);
		FlashMID->SetScalarParameterValue("Intensity", RimIntensity);
		FlashMID->SetScalarParameterValue("Rim", 1.f);
		FlashMID->SetVectorParameterValue("Color", RimColor);
		if (!FParse::Param(FCommandLine::Get(), TEXT("FallNoRim"))) Mesh->SetOverlayMaterial(FlashMID);
	}
	BuildOutfit();
	if (UMaterialInterface* Lit = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Freefall/Materials/M_FF_LitTwoSided.M_FF_LitTwoSided")))
	{
		Straps->Setup(Mesh, Lit);
	}
	PrevMeshRot = Mesh->GetComponentQuat();
	PrevAirVel = Control.AirVelocity;
	AttQ = FQuat(FVector::RightVector, FMath::DegreesToRadians(90.f));
}

UStaticMeshComponent* AFallBody::AttachProp(FName Bone, const TCHAR* Shape, const FVector& Offset, const FRotator& BodyRot, const FVector& Size, const FLinearColor& Color, float Roughness, float Metallic)
{
	USkeletalMesh* SK = Mesh->GetSkeletalMeshAsset();
	UStaticMesh* SM = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Shape, Shape));
	if (!SK || !SM) return nullptr;
	const int32 B = SK->GetRefSkeleton().FindBoneIndex(Bone);
	if (B < 0) return nullptr;
	TArray<FTransform> RefCS;
	FVector Basis[3];
	FallComputeRefCS(SK->GetRefSkeleton(), RefCS, Basis);
	const FTransform& T = RefCS[B];
	const FVector OffCS = Basis[0] * Offset.X + Basis[1] * Offset.Y + Basis[2] * Offset.Z;
	// Body frame as a rotation in component space, then the prop's own rotation inside it.
	const FQuat BodyFrame = FRotationMatrix::MakeFromXY(Basis[0], Basis[1]).ToQuat();
	const FQuat RotCS = BodyFrame * BodyRot.Quaternion();
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetStaticMesh(SM);
	C->SetupAttachment(Mesh, Bone);
	C->SetRelativeLocation(T.InverseTransformPosition(T.GetLocation() + OffCS));
	C->SetRelativeRotation(T.GetRotation().Inverse() * RotCS);
	C->SetRelativeScale3D(Size / 100.f);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->RegisterComponent();
	if (UMaterialInterface* Lit = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Freefall/Materials/M_FF_Lit.M_FF_Lit")))
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Lit, this);
		MID->SetVectorParameterValue("Color", Color);
		MID->SetScalarParameterValue("Roughness", Roughness);
		MID->SetScalarParameterValue("Metallic", Metallic);
		C->SetMaterial(0, MID);
	}
	Props.Add(C);
	return C;
}

void AFallBody::BuildOutfit()
{
	// The flight suit, helmet and chest rig are part of the character model; the gear here is what the story needs.
}

UMaterialInterface* AFallBody::RoleMaterial() const
{
	return UFallArtData::Get()->PlayerMaterial.LoadSynchronous();
}

UStaticMeshComponent* AFallBody::AttachMesh(FName Bone, UStaticMesh* InMesh, const FVector& Offset, const FRotator& BodyRot, float Scale)
{
	USkeletalMesh* SK = Mesh->GetSkeletalMeshAsset();
	if (!SK || !InMesh) return nullptr;
	const int32 B = SK->GetRefSkeleton().FindBoneIndex(Bone);
	if (B < 0) return nullptr;
	TArray<FTransform> RefCS;
	FVector Basis[3];
	FallComputeRefCS(SK->GetRefSkeleton(), RefCS, Basis);
	const FTransform& T = RefCS[B];
	const FVector OffCS = Basis[0] * Offset.X + Basis[1] * Offset.Y + Basis[2] * Offset.Z;
	const FQuat BodyFrame = FRotationMatrix::MakeFromXY(Basis[0], Basis[1]).ToQuat();
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetStaticMesh(InMesh);
	C->SetupAttachment(Mesh, Bone);
	C->SetRelativeLocation(T.InverseTransformPosition(T.GetLocation() + OffCS));
	C->SetRelativeRotation(T.GetRotation().Inverse() * BodyFrame * BodyRot.Quaternion());
	C->SetRelativeScale3D(FVector(Scale));
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->RegisterComponent();
	Props.Add(C);
	return C;
}

void AFallBody::ArmWeapon()
{
	const UFallArtData* Art = UFallArtData::Get();
	USkeletalMesh* SK = Mesh->GetSkeletalMeshAsset();
	UStaticMesh* Gun = Art->Pistol.LoadSynchronous();
	if (!SK || !Gun) return;
	const FReferenceSkeleton& Ref = SK->GetRefSkeleton();
	TArray<FTransform> RefCS;
	FVector Basis[3];
	FallComputeRefCS(Ref, RefCS, Basis);
	auto Pos = [&](const TCHAR* Name) { const int32 i = Ref.FindBoneIndex(Name); return i >= 0 ? RefCS[i].GetLocation() : FVector::ZeroVector; };
	const int32 HandIdx = Ref.FindBoneIndex("hand_r");
	if (HandIdx < 0) return;
	const FVector H = Pos(TEXT("hand_r")), LA = Pos(TEXT("lowerarm_r")), Mid = Pos(TEXT("middle_01_r")), Idx = Pos(TEXT("index_01_r")), Pnk = Pos(TEXT("pinky_01_r"));
	// A real grip: the barrel continues the forearm (wrist straight), the slide sits on the thumb side,
	// the grip lies across the palm under the knuckles.
	const FVector Fwd = (H - LA).GetSafeNormal(); // the wrist is held straight while armed
	const FVector Across = (Idx - Pnk).GetSafeNormal();
	const FVector Up = (Across - Fwd * (Across | Fwd)).GetSafeNormal();
	const FVector Palm = ((Mid - H).GetSafeNormal() ^ Across).GetSafeNormal();
	const FVector GripCS = H + (Mid - H) * 0.55f + Palm * 2.5f;
	const FVector GunX = Art->PistolForward.X < 0.f ? -Fwd : Fwd;
	const FQuat Rot = FRotationMatrix::MakeFromXZ(GunX, Up).ToQuat();
	const FTransform GunCS(Rot, GripCS - Rot.RotateVector(Art->PistolGrip));
	const FTransform Rel = GunCS.GetRelativeTransform(RefCS[HandIdx]);
	WeaponMesh = NewObject<UStaticMeshComponent>(this, TEXT("WeaponMesh"));
	WeaponMesh->SetStaticMesh(Gun);
	WeaponMesh->SetupAttachment(Mesh, "hand_r");
	WeaponMesh->SetRelativeTransform(Rel);
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponMesh->RegisterComponent();
	Props.Add(WeaponMesh);
	if (UStaticMesh* Mag = Art->Magazine.LoadSynchronous())
	{
		SpareMag = NewObject<UStaticMeshComponent>(this, TEXT("SpareMag"));
		SpareMag->SetStaticMesh(Mag);
		SpareMag->SetupAttachment(Mesh, "hand_l");
		SpareMag->SetRelativeLocation(FVector(8.f, 3.f, 0.f));
		SpareMag->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SpareMag->SetVisibility(false);
		SpareMag->RegisterComponent();
		Props.Add(SpareMag);
	}
}

FVector AFallBody::MuzzleLocation() const
{
	return WeaponMesh ? WeaponMesh->GetComponentTransform().TransformPosition(UFallArtData::Get()->PistolMuzzle) : Mesh->GetSocketLocation("hand_r");
}

FVector AFallBody::MuzzleForward() const
{
	return WeaponMesh ? WeaponMesh->GetComponentTransform().TransformVectorNoScale(UFallArtData::Get()->PistolForward).GetSafeNormal() : GetActorForwardVector();
}

float AFallBody::MeasureSelfIntersection(FString* OutWorst) const
{
	// Simplified body volumes: a head sphere, a neck capsule, a torso capsule along the spine.
	auto B = [this](const TCHAR* N) { return Mesh->GetSocketLocation(N); };
	const FVector Head = B(TEXT("head")), Neck = B(TEXT("neck_01"));
	const FVector HeadC = Head + (Head - Neck).GetSafeNormal() * 9.f;
	struct FCap { FVector A, Bv; float R; const TCHAR* Name; };
	const FCap Caps[] = {{HeadC, HeadC, 11.f, TEXT("head")}, {Neck, Head, 6.f, TEXT("neck")}, {B(TEXT("spine_01")), B(TEXT("spine_05")), 12.f, TEXT("torso")}};
	struct FSeg { const TCHAR* A; const TCHAR* Bn; float R; const TCHAR* Name; };
	static const FSeg Segs[] = {
		{TEXT("lowerarm_l"), TEXT("hand_l"), 4.f, TEXT("forearm_l")}, {TEXT("hand_l"), TEXT("middle_03_l"), 3.f, TEXT("hand_l")},
		{TEXT("lowerarm_r"), TEXT("hand_r"), 4.f, TEXT("forearm_r")}, {TEXT("hand_r"), TEXT("middle_03_r"), 3.f, TEXT("hand_r")},
		{TEXT("index_01_l"), TEXT("index_03_l"), 1.5f, TEXT("index_l")}, {TEXT("index_01_r"), TEXT("index_03_r"), 1.5f, TEXT("index_r")},
	};
	float Worst = 0.f;
	for (const FSeg& S : Segs)
	{
		const FVector P0 = B(S.A), P1 = B(S.Bn);
		for (int32 k = 0; k <= 4; ++k)
		{
			const FVector P = FMath::Lerp(P0, P1, k / 4.f);
			for (const FCap& C : Caps)
			{
				const float D = (C.A - C.Bv).IsNearlyZero() ? (float)FVector::Dist(P, C.A) : (float)FMath::PointDistToSegment(P, C.A, C.Bv);
				const float Pen = C.R + S.R - D;
				if (Pen > Worst)
				{
					Worst = Pen;
					if (OutWorst) *OutWorst = FString::Printf(TEXT("%s in %s"), S.Name, C.Name);
				}
			}
		}
	}
	return Worst;
}

void AFallBody::StartReloadMotion(float Duration)
{
	ReloadT = 0.f;
	ReloadDur = FMath::Max(Duration, 0.3f);
	ReloadStartL = Mesh->GetSocketLocation("hand_l");
}

void AFallBody::UpdateWeaponHands(float Dt)
{
	TriggerPull = FMath::FInterpTo(TriggerPull, 0.f, Dt, 6.f);
	if (!WeaponMesh) return;
	const bool bOwnerLeft = Control.HandWeight[0] > 0.f && !bWeaponHand[0];
	const bool bOwnerRight = Control.HandWeight[1] > 0.f && !bWeaponHand[1];
	auto Release = [this](int32 h) { if (bWeaponHand[h]) { Control.HandWeight[h] = 0.f; bWeaponHand[h] = false; } };
	auto Set = [this](int32 h, const FVector& P, float W) { Control.HandTarget[h] = P; Control.HandWeight[h] = W; bWeaponHand[h] = true; };

	// Body frame from the bones: up the spine, across the shoulders, out of the chest.
	const FVector Pel = Mesh->GetSocketLocation("pelvis"), Chest = Mesh->GetSocketLocation("spine_05");
	const FVector Up = (Mesh->GetSocketLocation("neck_01") - Pel).GetSafeNormal();
	const FVector Right = (Mesh->GetSocketLocation("upperarm_r") - Mesh->GetSocketLocation("upperarm_l")).GetSafeNormal();
	const FVector Front = (Right ^ Up).GetSafeNormal();
	const UFallArtData* Art = UFallArtData::Get();
	const FTransform GT = WeaponMesh->GetComponentTransform();
	const FVector GunUp = GT.TransformVectorNoScale(FVector::UpVector);

	if (ReloadT >= 0.f)
	{
		ReloadT += Dt / ReloadDur;
		if (ReloadT >= 1.f) { ReloadT = -1.f; if (SpareMag) SpareMag->SetVisibility(false); Release(0); Release(1); return; }
		if (bOwnerLeft || bOwnerRight) return; // holding on to something: the hands are busy
		// Gun low in front of the chest, well clear of the head; the left hand goes to the pouch on the left hip,
		// comes back in front of the belly and seats the magazine from below. No point of the path is near the head.
		Set(1, Chest + Front * 36.f + Right * 5.f + Up * 8.f, 1.f); // gun up in front of the chest, at chin height
		const FVector Pouch = Pel - Right * 21.f + Front * 13.f - Up * 2.f;
		const FVector Belly = Chest + Front * 22.f - Right * 26.f - Up * 10.f; // along the left side of the torso
		const FVector Well = GT.TransformPosition(Art->PistolMagWell) - GunUp * 7.f;
		const float t = ReloadT;
		auto Seg = [](float x, float a, float b) { return FMath::SmoothStep(0.f, 1.f, FMath::Clamp((x - a) / (b - a), 0.f, 1.f)); };
		FVector L;
		if (t < 0.3f) L = FMath::Lerp(ReloadStartL, Pouch, Seg(t, 0.f, 0.3f));
		else if (t < 0.55f) L = FMath::Lerp(Pouch, Belly, Seg(t, 0.3f, 0.55f));
		else if (t < 0.75f) L = FMath::Lerp(Belly, Well, Seg(t, 0.55f, 0.75f));
		else L = Well + GunUp * 3.f * FMath::Sin(Seg(t, 0.75f, 0.85f) * PI); // the slap
		Set(0, L, FMath::Min(Seg(t, 0.f, 0.08f), 1.f - Seg(t, 0.9f, 1.f)));
		if (SpareMag) SpareMag->SetVisibility(t > 0.3f && t < 0.8f);
		return;
	}
	// Two hands only when the gun is out in front of the chest, clear of the head, and not pointing across the body
	// to the left; otherwise one-handed, as people shoot at targets beside or behind them.
	const FVector Grip = GT.TransformPosition(Art->PistolGrip);
	const FVector AimDir = MuzzleForward();
	const bool bTwoHands = bTwoHandedAim && Control.bAim && !bOwnerLeft
		&& FVector::Dist(Grip, Mesh->GetSocketLocation("head")) > 55.f
		&& ((Grip - Chest) | Front) > 12.f
		&& (AimDir | Right) > -0.25f;
	SupportBlend = FMath::FInterpTo(SupportBlend, bTwoHands ? 1.f : 0.f, Dt, bTwoHands ? 6.f : 12.f);
	if (SupportBlend > 0.02f) Set(0, Grip - Right * 6.f - GunUp * 5.5f + Front * 3.f, SupportBlend);
	else Release(0);
	Release(1);
}

void AFallBody::ResetBody()
{
	Health = MaxHealth;
	KickTime = -1.f;
	AttW = FVector::ZeroVector;
	PendingHit = FVector::ZeroVector;
	FlashAmount = 0.f;
	CanopyClock = 0.f;
	if (Straps) Straps->ResetSim();
}

bool AFallBody::StartKick(bool bRightLeg)
{
	if (KickTime >= 0.f) return false;
	KickTime = 0.f;
	bKickRight = bRightLeg;
	return true;
}

float AFallBody::KickImpactTime() const
{
	const UFallAnimData* D = UFallAnimData::Resolve(AnimData);
	return D->KickTimes.X + D->KickTimes.Y;
}

void AFallBody::Flash(const FLinearColor& Color, float Strength)
{
	FlashColor = Color;
	FlashAmount = FMath::Max(FlashAmount, Strength);
}

void AFallBody::AddImpulse(const FVector& DirWorld, float Strength)
{
	PendingHit += DirWorld.GetSafeNormal() * Strength;
	const FVector LocalPush = GetActorQuat().Inverse().RotateVector(DirWorld.GetSafeNormal());
	AttW += (AttQ.RotateVector(FVector::UpVector) ^ LocalPush) * Strength * 3.f;
}

bool AFallBody::ReceiveHit(const FVector& PushDirWorld, float Strength, float Damage)
{
	if (IsDead()) return false;
	PendingHit += PushDirWorld.GetSafeNormal() * Strength;
	// Rotate the whole body away from the hit: torque from a push at the chest.
	const FVector LocalPush = GetActorQuat().Inverse().RotateVector(PushDirWorld.GetSafeNormal());
	const FVector ChestLocal = AttQ.RotateVector(FVector::UpVector);
	AttW += (ChestLocal ^ LocalPush) * Strength * 5.f;
	Health = FMath::Max(0.f, Health - Damage);
	Flash(HitFlashColor, (0.7f + 0.3f * Strength) * HitFlashScale);
	return Health <= 0.f;
}

void AFallBody::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateBody(DeltaSeconds);
}

void AFallBody::UpdateBody(float Dt)
{
	if (Dt <= 0.f) return;
	const UFallAnimData* Data = UFallAnimData::Resolve(AnimData);

	// Stance follows the control with inertia.
	auto Follow = [Dt](float& V, float Target, float Rate) { V = FMath::FInterpTo(V, Target, Dt, Rate); };
	Follow(Dive, Control.Dive, StanceRate);
	Follow(Bank, Control.Bank, StanceRate * 1.2f);
	if (Control.Override != OverridePose && OverrideWeight > 0.02f)
	{
		Follow(OverrideWeight, 0.f, 5.f); // fade the old override out first
	}
	else
	{
		OverridePose = Control.Override;
		Follow(OverrideWeight, Control.Override.IsNone() ? 0.f : Control.OverrideTarget, OverridePose == "Canopy" ? 1.4f : 4.f);
	}
	UpdateWeaponHands(Dt);
	Follow(AimWeight, Control.bAim ? 1.f : 0.f, 9.f);
	Follow(LookWeight, Control.LookAt.IsZero() ? 0.f : 1.f, 4.f);
	for (int32 h = 0; h < 2; ++h) Follow(HandWeight[h], Control.HandWeight[h], 7.f);
	if (KickTime >= 0.f)
	{
		KickTime += Dt;
		const FVector K = Data->KickTimes;
		if (KickTime > K.X + K.Y + K.Z) KickTime = -1.f;
	}

	UFallAnimInstance* AI = Cast<UFallAnimInstance>(Mesh->GetAnimInstance());
	FFallAnimInput Scratch;
	FFallAnimInput& In = AI ? AI->Input : Scratch;
	In.Wind = FMath::Clamp(Control.AirVelocity.Size() / 8800.f, 0.f, 1.f);
	In.bDiveExit = Control.Dive < Dive - 0.05f && Dive > 0.f;
	In.Dive = Dive;
	In.Bank = Bank;
	In.Wind = FMath::Clamp(Control.AirVelocity.Size() / 8800.f, 0.f, 1.f);
	In.OverridePose = OverridePose;
	In.OverrideWeight = OverrideWeight;
	In.KickTime = KickTime;
	In.bKickRight = bKickRight;
	In.Seed = Seed;

	// Body attitude from the target pose: pitch around the lateral axis, roll from bank, optional yaw offset.
	const FFallPose Target = FallComputeTargetPose(*Data, In);
	BodyPitch = Target.BodyPitch;
	const float Roll = -Bank * 30.f * (1.f - 0.4f * FMath::Max(Dive, 0.f)) * (1.f - OverrideWeight);
	const FQuat AttTarget = FQuat(FVector::UpVector, FMath::DegreesToRadians(Control.YawOffset))
		* FQuat(FVector::ForwardVector, FMath::DegreesToRadians(Roll))
		* FQuat(FVector::RightVector, FMath::DegreesToRadians(BodyPitch));
	Follow(AttitudeOverrideWeight, Control.AttitudeWeight, 5.f);
	const FQuat AttFinal = AttitudeOverrideWeight > 0.001f ? FQuat::Slerp(AttTarget, Control.AttitudeOverride, AttitudeOverrideWeight) : AttTarget;
	// Under a canopy the body hangs like a pendulum: soft, underdamped, with a slow sway.
	const float Canopy = OverridePose == "Canopy" ? OverrideWeight : 0.f;
	// Constant small balance corrections of the whole body, stronger with airspeed.
	const float Tw = GetWorld()->GetTimeSeconds();
	const float Sway = (4.5f + 3.f * FMath::Max(-Dive, 0.f)) * In.Wind;
	FQuat AttGoal = FQuat(FVector::ForwardVector, FMath::DegreesToRadians(Sway * FMath::PerlinNoise1D(Tw * 0.45f + Seed * 0.1f)))
		* FQuat(FVector::RightVector, FMath::DegreesToRadians(Sway * 0.7f * FMath::PerlinNoise1D(Tw * 0.38f + Seed * 0.1f + 17.f))) * AttFinal;
	if (Canopy > 0.f)
	{
		// Pendulum under the canopy: a decaying fore-aft swing after the opening shock, then a slow sway.
		CanopyClock += Dt;
		const float Swing = 18.f * FMath::Exp(-CanopyClock / 2.2f) * FMath::Sin(2.f * PI * 0.55f * CanopyClock);
		AttGoal = FQuat(FVector::RightVector, FMath::DegreesToRadians(Swing * Canopy)) * AttGoal;
		const float T = GetWorld()->GetTimeSeconds();
		AttGoal = FQuat(FVector::ForwardVector, FMath::DegreesToRadians(Canopy * (7.f * FMath::PerlinNoise1D(T * 0.35f + Seed) + 5.f * FMath::Sin(2.f * PI * 0.4f * T))))
			* FQuat(FVector::RightVector, FMath::DegreesToRadians(6.f * Canopy * FMath::PerlinNoise1D(T * 0.3f + Seed + 5.f))) * AttGoal;
	}
	if (OverridePose == "Limp" && OverrideWeight > 0.5f)
	{
		// A limp body tumbles slowly; the limbs trail behind on their soft springs.
		LimpSpin += Dt * (1.6f + 1.2f * FMath::PerlinNoise1D(GetWorld()->GetTimeSeconds() * 0.7f + Seed));
		const FVector Axis(0.4f + FMath::PerlinNoise1D(LimpSpin * 0.3f + Seed), 1.f, 0.3f * FMath::PerlinNoise1D(LimpSpin * 0.2f + Seed + 3.f));
		AttGoal = FQuat(Axis.GetSafeNormal(), LimpSpin) * AttGoal;
	}
	SpringQuat(AttGoal, AttQ, AttW, FMath::Lerp(Attitude.X, 0.6f, Canopy), FMath::Lerp(Attitude.Y, 0.2f, Canopy), Dt);
	AttW = AttW.GetClampedToMaxSize(4.5f); // no 120-degree flips in a few frames
	BodyPivot->SetRelativeRotation(AttQ);

	const FTransform MeshT = Mesh->GetComponentTransform();
	const FQuat MeshRot = MeshT.GetRotation();
	if (AI)
	{
		In.CompDeltaInv = (MeshRot.Inverse() * PrevMeshRot) * In.CompDeltaInv;
		const FVector AccWorld = -(Control.AirVelocity - PrevAirVel) / Dt;
		In.AccelCS = FMath::Lerp(In.AccelCS, MeshRot.UnrotateVector(AccWorld), FMath::Min(1.f, Dt * 8.f));
		const FVector HeadPos = Mesh->GetSocketLocation("head");
		if (!Control.LookAt.IsZero())
		{
			In.LookDirCS = MeshRot.UnrotateVector((Control.LookAt - HeadPos).GetSafeNormal());
		}
		In.LookWeight = LookWeight;
		if (!Control.AimAt.IsZero()) In.AimPointCS = MeshT.InverseTransformPosition(Control.AimAt);
		In.AimWeight = AimWeight;
		for (int32 h = 0; h < 2; ++h)
		{
			if (!Control.HandTarget[h].IsZero()) In.HandIKCS[h] = MeshT.InverseTransformPosition(Control.HandTarget[h]);
			In.HandIKWeight[h] = HandWeight[h];
		}
		In.HitCS += MeshRot.UnrotateVector(PendingHit);
		In.WindDirCS = MeshRot.UnrotateVector(Control.AirVelocity.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector));
		In.Trail = OverridePose == "Limp" ? OverrideWeight : 0.f;
		In.Recoil += PendingRecoil;
		In.bGunInRight = WeaponMesh != nullptr;
		In.TriggerPull = TriggerPull;
	}
	PendingHit = FVector::ZeroVector;
	PendingRecoil = 0.f;
	PrevMeshRot = MeshRot;
	PrevAirVel = Control.AirVelocity;

	Straps->AirVelocity = Control.AirVelocity;
	Straps->WindAmount = In.Wind;

	if (FlashMID)
	{
		FlashAmount = FMath::FInterpTo(FlashAmount, 0.f, Dt, 14.f);
		LowPulse = FMath::FInterpTo(LowPulse, 0.f, Dt, 3.f);
		const float Fl = FMath::Clamp(FlashAmount, 0.f, 1.f);
		FlashMID->SetScalarParameterValue("Intensity", RimIntensity + FlashAmount + LowPulse * 2.f);
		FlashMID->SetScalarParameterValue("Rim", 1.f - Fl * 0.35f);
		FlashMID->SetVectorParameterValue("Color", FMath::Lerp(FMath::Lerp(RimColor, FLinearColor(1.f, 0.1f, 0.05f), FMath::Clamp(LowPulse * 3.f, 0.f, 1.f)), FlashColor, Fl));
	}

	if (bDrawTrails)
	{
		static const FName Ends[4] = {"hand_l", "hand_r", "foot_l", "foot_r"};
		static const FColor Cols[4] = {FColor(60, 200, 255), FColor(255, 220, 40), FColor(80, 255, 120), FColor(255, 90, 200)};
		for (int32 e = 0; e < 4; ++e)
		{
			Trails[e].Add(Mesh->GetSocketLocation(Ends[e]));
			if (Trails[e].Num() > 40) Trails[e].RemoveAt(0);
			for (int32 k = 1; k < Trails[e].Num(); ++k)
			{
				DrawDebugLine(GetWorld(), Trails[e][k - 1], Trails[e][k], Cols[e], false, -1.f, SDPG_Foreground, 0.6f);
			}
		}
	}
}
