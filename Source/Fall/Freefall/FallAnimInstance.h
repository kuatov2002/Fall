// Native freefall animation: key poses described as limb directions, blended by stance, then a procedural layer
// (per-bone springs with drag against body rotation, wind noise, gusts, look-at, aim, two-bone IK, hit reactions).
// Runs entirely in FAnimInstanceProxy::Evaluate on the animation worker thread. No AnimGraph needed.
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "FallTuning.h"
#include "FallAnimInstance.generated.h"

/** Everything the body tells the animation each frame. Written on the game thread, copied in PreUpdate. */
struct FFallAnimInput
{
	float Dive = 0.f;            // -1 brake .. 0 belly .. 0.5 track .. 1 head-down
	float Bank = 0.f;            // -1 .. 1, positive banks right
	float Wind = 0.5f;           // 0 .. 1 normalized airspeed
	FName OverridePose;          // Grab, Wingsuit, Limp, Canopy ...
	float OverrideWeight = 0.f;
	float KickTime = -1.f;       // seconds since kick start, negative = no kick
	bool bKickRight = true;
	FVector AimPointCS = FVector::ZeroVector; // right arm points here (component space)
	float AimWeight = 0.f;
	FVector LookDirCS = FVector(0.f, 1.f, 0.f);
	float LookWeight = 0.f;
	FVector HandIKCS[2] = {FVector::ZeroVector, FVector::ZeroVector}; // 0 = left, 1 = right
	float HandIKWeight[2] = {0.f, 0.f};
	FQuat CompDeltaInv = FQuat::Identity; // keeps world orientation while the body rotates
	FVector AccelCS = FVector::ZeroVector; // cm/s^2
	FVector HitCS = FVector::ZeroVector;   // accumulated push direction * strength since last frame
	float Recoil = 0.f;                    // accumulated since last frame
	int32 Seed = 0;
	FVector WindDirCS = FVector(0.f, 0.f, 1.f); // where the air pushes the limbs, component space
	float Trail = 0.f;                          // 0..1, limbs trail with the air (limp bodies)
	bool bDiveExit = false;                     // coming out of the dive: go straight to belly, not through the track
};

/** Reference pose in component space and the body basis (right, front, up) derived from it. */
FALL_API void FallComputeRefCS(const FReferenceSkeleton& Ref, TArray<FTransform>& OutCS, FVector OutBasis[3]);

/** Target pose for the current input, before the procedural layer. Thread-safe. */
FALL_API FFallPose FallComputeTargetPose(const UFallAnimData& Data, const FFallAnimInput& In);

struct FFallAnimProxy : public FAnimInstanceProxy
{
	FFallAnimProxy() = default;
	explicit FFallAnimProxy(UAnimInstance* Inst) : FAnimInstanceProxy(Inst) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

private:
	enum class ERole : uint8
	{
		None, Pelvis, Spine, Neck, Clavicle, Upper, Lower, End, Finger
	};

	struct FLimbRef
	{
		int32 Upper = INDEX_NONE, Lower = INDEX_NONE, End = INDEX_NONE, Root = INDEX_NONE;
		FVector DirU, DirL, Hinge, RootDir;
		float LenU = 0.f, LenL = 0.f;
		bool bArm = true, bRight = false;
		// per frame
		FVector TU, TL, TH;
		float Flex = 0.f, Twist = 0.f, Curl = 0.f;
	};

	void Init(const FReferenceSkeleton& Ref);
	FQuat Spring(int32 Bone, const FQuat& Target, const FFallBoneGroupTuning& G, float StiffMul, float Dt);
	FQuat Noise(int32 Key, float AmpDeg) const;
	/** High-frequency buffeting applied after the spring (the spring would filter it out). */
	FQuat Flutter(int32 Key, float AmpDeg) const;
	void SolveLimb(FLimbRef& L, const FFallLimb& Pose, const FVector Axes[3], const FVector& RootPos, float Dt);

	FFallAnimInput In;
	TWeakObjectPtr<const UFallAnimData> DataPtr;
	float PendingDt = 0.f;
	float Clock = 0.f;
	float NextGust = 0.f;
	FRandomStream Rng;

	bool bInit = false;
	int32 NumBones = 0;
	TArray<int32> Parents;
	TArray<FTransform> RefLocal, RefCS, OutCS;
	TArray<ERole> Roles;
	TArray<uint8> RoleIndex; // limb index or spine/neck order
	TArray<FQuat> SpringQ;
	TArray<FVector> SpringW;
	TArray<bool> SpringValid;
	FVector Basis[3]; // Right, Front, Up in component space
	int32 Spine[5] = {INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE};
	int32 Neck[3] = {INDEX_NONE, INDEX_NONE, INDEX_NONE};
	FLimbRef Limbs[4]; // arm L, arm R, leg L, leg R
	FVector PalmRef[2], CurlAxisRef[2];
	float LagDive[3] = {0.f, 0.f, 0.f};
	float LagBank[3] = {0.f, 0.f, 0.f};
	bool bLagInit = false;
	float LookReach = 1.f;
};

UCLASS(Transient, BlueprintType)
class FALL_API UFallAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Animation tuning. Class defaults are used when empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fall")
	TObjectPtr<UFallAnimData> AnimData;

	FFallAnimInput Input;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FFallAnimProxy(this); }
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override { delete static_cast<FFallAnimProxy*>(InProxy); }

	friend struct FFallAnimProxy;
};
