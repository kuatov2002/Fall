#include "FallBody.h"
#include "FallStraps.h"
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
	// Bridle trailing from the empty harness.
	Add("spine_03", FVector(0, -16, 4), FVector(0, -0.3f, -1), FVector(1, 0, 0), 7, 8.f, FVector2D(3.f, 2.f), FLinearColor(0.2f, 0.075f, 0.02f), 80000.f, 0.1f);
}

void AFallBody::BeginPlay()
{
	if (!AnimData) AnimData = LoadObject<UFallAnimData>(nullptr, TEXT("/Game/Freefall/Data/DA_FallAnim.DA_FallAnim"));
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

	// Jumpsuit: replace the mannequin plastic with a matte fabric colour per slot.
	if (UMaterialInterface* Lit = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Freefall/Materials/M_FF_Lit.M_FF_Lit")))
	{
		for (int32 m = 0; m < Mesh->GetNumMaterials(); ++m)
		{
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Lit, this);
			MID->SetVectorParameterValue("Color", m == 0 ? SuitColor : SuitColor * 1.35f);
			MID->SetScalarParameterValue("Roughness", 0.8f);
			MID->SetScalarParameterValue("Metallic", 0.f);
			Mesh->SetMaterial(m, MID);
		}
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
		if (Straps->Straps.Num() > 0) Straps->Straps[0].Color = AccentColor;
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
	// Helmet with goggles, harness: chest strap, shoulder straps, leg loops, an empty container frame on the back.
	AttachProp("head", TEXT("Sphere"), FVector(0, -1.5f, 9.5f), FRotator::ZeroRotator, FVector(23.f, 26.f, 23.f), HarnessColor * 2.f, 0.35f, 0.2f);
	AttachProp("head", TEXT("Cube"), FVector(0, 9.5f, 6.f), FRotator::ZeroRotator, FVector(17.f, 4.f, 5.f), FLinearColor(0.03f, 0.04f, 0.05f), 0.08f, 0.9f);
	AttachProp("head", TEXT("Cube"), FVector(0, -1.5f, 21.f), FRotator::ZeroRotator, FVector(5.f, 22.f, 2.f), AccentColor, 0.5f, 0.f);
	AttachProp("spine_04", TEXT("Cube"), FVector(0, 14.f, 2.f), FRotator::ZeroRotator, FVector(30.f, 3.f, 4.f), HarnessColor);
	for (int32 s = -1; s <= 1; s += 2)
	{
		AttachProp("spine_05", TEXT("Cube"), FVector(s * 9.f, 0.f, 9.f), FRotator(0.f, 0.f, 0.f), FVector(4.f, 30.f, 3.f), HarnessColor);
		AttachProp(s < 0 ? "thigh_l" : "thigh_r", TEXT("Cylinder"), FVector(0, 0, -8.f), FRotator::ZeroRotator, FVector(19.f, 19.f, 4.f), HarnessColor);
	}
	AttachProp("spine_03", TEXT("Cube"), FVector(0, -15.f, 2.f), FRotator::ZeroRotator, FVector(28.f, 5.f, 36.f), HarnessColor * 1.5f, 0.6f, 0.1f);
	for (int32 s = -1; s <= 1; s += 2)
	{
		AttachProp("pelvis", TEXT("Cube"), FVector(s * 9.f, -12.f, -4.f), FRotator(0.f, 0.f, s * 12.f), FVector(4.f, 3.f, 26.f), HarnessColor);
		AttachProp("pelvis", TEXT("Cube"), FVector(s * 8.f, 11.f, -6.f), FRotator(0.f, 0.f, s * -20.f), FVector(4.f, 3.f, 20.f), HarnessColor);
	}
}

void AFallBody::ResetBody()
{
	Health = MaxHealth;
	KickTime = -1.f;
	AttW = FVector::ZeroVector;
	PendingHit = FVector::ZeroVector;
	FlashAmount = 0.f;
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
	Flash(FLinearColor(1.f, 0.15f, 0.05f), 2.f + Strength);
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
	const float Sway = 3.f * In.Wind;
	FQuat AttGoal = FQuat(FVector::ForwardVector, FMath::DegreesToRadians(Sway * FMath::PerlinNoise1D(Tw * 0.45f + Seed * 0.1f)))
		* FQuat(FVector::RightVector, FMath::DegreesToRadians(Sway * 0.7f * FMath::PerlinNoise1D(Tw * 0.38f + Seed * 0.1f + 17.f))) * AttFinal;
	if (Canopy > 0.f)
	{
		const float T = GetWorld()->GetTimeSeconds();
		AttGoal = FQuat(FVector::ForwardVector, FMath::DegreesToRadians(7.f * Canopy * FMath::PerlinNoise1D(T * 0.35f + Seed)))
			* FQuat(FVector::RightVector, FMath::DegreesToRadians(6.f * Canopy * FMath::PerlinNoise1D(T * 0.3f + Seed + 5.f))) * AttGoal;
	}
	if (OverridePose == "Limp" && OverrideWeight > 0.5f)
	{
		// A limp body tumbles slowly; the limbs trail behind on their soft springs.
		LimpSpin += Dt * 0.7f;
		AttGoal = FQuat(FVector(0.3f, 1.f, 0.2f).GetSafeNormal(), LimpSpin) * AttGoal;
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
		In.Recoil += PendingRecoil;
	}
	PendingHit = FVector::ZeroVector;
	PendingRecoil = 0.f;
	PrevMeshRot = MeshRot;
	PrevAirVel = Control.AirVelocity;

	Straps->AirVelocity = Control.AirVelocity;
	Straps->WindAmount = In.Wind;

	if (FlashMID)
	{
		FlashAmount = FMath::FInterpTo(FlashAmount, 0.f, Dt, 9.f);
		LowPulse = FMath::FInterpTo(LowPulse, 0.f, Dt, 3.f);
		const float Fl = FMath::Clamp(FlashAmount, 0.f, 1.f);
		FlashMID->SetScalarParameterValue("Intensity", RimIntensity + FlashAmount + LowPulse * 2.f);
		FlashMID->SetScalarParameterValue("Rim", 1.f - Fl * 0.7f);
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
