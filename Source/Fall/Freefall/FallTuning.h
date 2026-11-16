// Data assets with every tunable of the freefall slice. Designers create assets of these classes;
// code falls back to the class defaults when no asset is assigned.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FallTuning.generated.h"

/** One arm or leg. Directions are in body space: X = out (away from the midline), Y = front (belly), Z = up (to the head). */
USTRUCT(BlueprintType)
struct FFallLimb
{
	GENERATED_BODY()

	/** Upper arm / thigh direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limb")
	FVector Upper = FVector(0.f, 0.f, -1.f);

	/** Forearm / shin direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limb")
	FVector Lower = FVector(0.f, 0.f, -1.f);

	/** Wrist or ankle flex around the elbow/knee hinge, degrees. Ankle: positive points the toes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limb")
	float EndFlex = 0.f;

	/** Hand or foot roll around the forearm/shin, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limb")
	float EndTwist = 0.f;

	/** Finger curl 0 (open) .. 1 (fist). Arms only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limb")
	float Curl = 0.25f;

	/** Clavicle raise (arms), degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limb")
	float RootRaise = 0.f;

	/** Clavicle forward (arms), degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limb")
	float RootForward = 0.f;
};

/** A full-body key pose plus the body attitude it implies. */
USTRUCT(BlueprintType)
struct FFallPose
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	FFallLimb ArmL;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose", meta = (EditCondition = "!bMirror"))
	FFallLimb ArmR;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	FFallLimb LegL;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose", meta = (EditCondition = "!bMirror"))
	FFallLimb LegR;

	/** Right side copies the left side. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	bool bMirror = true;

	/** Spine bend backwards (arch), degrees, spread over spine_01..05. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	float Arch = 0.f;

	/** Spine side bend, degrees, positive bends to the subject's left. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	float Side = 0.f;

	/** Spine twist, degrees, positive turns the chest to the subject's right. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	float Twist = 0.f;

	/** Head pitch, degrees, positive looks up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	float HeadPitch = 0.f;

	/** Body attitude: rotation from standing around the lateral axis. 0 standing, 90 belly-to-earth, 180 head-down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	float BodyPitch = 90.f;

	/** Multiplier on the bone springs while this pose dominates. Low = loose, floppy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	float Stiffness = 1.f;

	/** Multiplier on wind noise while this pose dominates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	float NoiseScale = 1.f;

	FFallLimb GetArm(bool bRight) const { return (bRight && !bMirror) ? ArmR : ArmL; }
	FFallLimb GetLeg(bool bRight) const { return (bRight && !bMirror) ? LegR : LegL; }

	static FFallPose Blend(const FFallPose& A, const FFallPose& B, float T);
	/** A + (B - Base) * W : adds the difference between two poses. */
	static FFallPose Additive(const FFallPose& A, const FFallPose& B, const FFallPose& Base, float W);
	FFallPose Mirrored() const;
	FFallPose Resolved() const; // bMirror expanded into explicit right side
};

/** Spring and noise settings per bone group. */
USTRUCT(BlueprintType)
struct FFallBoneGroupTuning
{
	GENERATED_BODY()

	/** Spring frequency, Hz. Higher = snappier, lower = more lag behind the target pose. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Group")
	float Frequency = 3.f;

	/** Damping ratio. 1 = no overshoot, below 1 = overshoot and settle (weight). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Group")
	float Damping = 0.7f;

	/** Wind noise amplitude at zero and at full airspeed, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Group")
	FVector2D NoiseDeg = FVector2D(1.f, 4.f);

	/** How much of the body rotation the bone resists in the first frame (0 = follows the body rigidly). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Group")
	float Drag = 0.3f;
};

UENUM(BlueprintType)
enum class EFallBoneGroup : uint8
{
	Spine, Head, Clavicle, UpperLimb, LowerLimb, Extremity, Count UMETA(Hidden)
};

/** Animation data: key poses and the procedural layer. */
UCLASS(BlueprintType)
class FALL_API UFallAnimData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UFallAnimData();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Poses")
	TArray<FFallPose> Poses;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Springs")
	FFallBoneGroupTuning SpineGroup;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Springs")
	FFallBoneGroupTuning HeadGroup;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Springs")
	FFallBoneGroupTuning ClavicleGroup;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Springs")
	FFallBoneGroupTuning UpperLimbGroup;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Springs")
	FFallBoneGroupTuning LowerLimbGroup;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Springs")
	FFallBoneGroupTuning ExtremityGroup;

	const FFallBoneGroupTuning& Group(EFallBoneGroup G) const;

	/** Low noise octave frequency at zero and full airspeed, Hz (slow balance corrections). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise")
	FVector2D DriftHz = FVector2D(0.35f, 0.8f);

	/** High noise octave frequency at zero and full airspeed, Hz (buffeting). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise")
	FVector2D FlutterHz = FVector2D(2.5f, 7.f);

	/** Share of the noise amplitude carried by the high octave. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise")
	float FlutterShare = 0.3f;

	/** Random wind gusts per second at full airspeed. Each kicks the springs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise")
	float GustsPerSecond = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise")
	float GustStrength = 1.4f;

	/** Spine lean from body acceleration: degrees per (m/s^2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lean")
	float LeanPerAccel = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lean")
	float LeanMaxDeg = 14.f;

	/** Head turn limits towards the look target, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	FVector2D LookYawPitchLimit = FVector2D(75.f, 55.f);

	/** Share of the look yaw carried by the spine twist. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	float LookSpineShare = 0.25f;

	/** Angular velocity added to spine and head per unit of hit strength, rad/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit")
	float HitKick = 9.f;

	/** Kick timeline, seconds: chamber (anticipation), strike, recover. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kick")
	FVector KickTimes = FVector(0.2f, 0.1f, 0.55f);

	const FFallPose* FindPose(FName Name) const;
	static const UFallAnimData* Resolve(const UFallAnimData* Maybe) { return Maybe ? Maybe : ::GetDefault<UFallAnimData>(); }
};

/** Movement, camera and feel. */
UCLASS(BlueprintType)
class FALL_API UFallMovementData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Terminal fall speed per stance, m/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed")
	float BrakeSpeed = 38.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed")
	float BellySpeed = 55.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed")
	float TrackSpeed = 68.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed")
	float DiveSpeed = 88.f;

	/** How fast fall speed approaches the stance target when speeding up, 1/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed")
	float SpeedUpRate = 0.8f;

	/** How fast fall speed approaches the target when slowing down, 1/s (drag is stronger than gravity). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed")
	float SlowDownRate = 1.6f;

	/** How fast the stance follows the stick, 1/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control")
	float StanceRate = 3.2f;

	/** Lateral slide speed, m/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control")
	float SlideSpeed = 13.f;

	/** Forward drift reached in the track stance, m/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control")
	float TrackForwardSpeed = 14.f;

	/** Horizontal velocity response, 1/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control")
	float HorizontalRate = 2.2f;

	/** Bank angle at full slide, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control")
	float BankDeg = 32.f;

	/** Body attitude spring frequency (Hz) and damping. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body")
	float AttitudeHz = 1.9f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body")
	float AttitudeDamping = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float ArmLength = 320.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	FVector CameraOffset = FVector(0.f, 38.f, 70.f);
	/** FOV at the slowest and the fastest fall speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	FVector2D FOV = FVector2D(80.f, 100.f);
	/** Extra downward camera pitch in the dive, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float DivePitchBias = -26.f;
	/** Camera shake rotation amplitude at belly and at full dive, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	FVector2D ShakeDeg = FVector2D(0.12f, 0.75f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float ShakeHz = 9.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float LookSensitivity = 0.35f;

	/** Grab reach, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debris")
	float GrabRange = 520.f;
	/** Push-off speed from a debris piece, m/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debris")
	float PushOffSpeed = 20.f;

	static const UFallMovementData* Resolve(const UFallMovementData* Maybe) { return Maybe ? Maybe : ::GetDefault<UFallMovementData>(); }
};
