// A falling human body: skeletal mesh on a pivot at the pelvis, body attitude spring, procedural animation input,
// secondary motion, health and hit reactions. The player, the enemies and the animation gallery all use it.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "FallAnimInstance.h"
#include "FallBody.generated.h"

class USphereComponent;
class USkeletalMeshComponent;
class UFallStrapsComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

/** What the body is asked to do this frame (by input, AI or the gallery). */
USTRUCT(BlueprintType)
struct FFallBodyControl
{
	GENERATED_BODY()

	/** Target stance: -1 brake .. 0 belly .. 1 head-down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control")
	float Dive = 0.f;

	/** Target bank: -1 .. 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control")
	float Bank = 0.f;

	/** Pose that overrides the stance (Grab, Wingsuit, Limp, Canopy), with its weight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control")
	FName Override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control")
	float OverrideTarget = 0.f;

	/** World point the head looks at; zero = no look target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control")
	FVector LookAt = FVector::ZeroVector;

	/** World point the right arm aims at, used when bAim. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control")
	FVector AimAt = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control")
	bool bAim = false;

	/** World hand targets for IK (0 left, 1 right), used when weight > 0. */
	FVector HandTarget[2] = {FVector::ZeroVector, FVector::ZeroVector};
	float HandWeight[2] = {0.f, 0.f};

	/** Extra yaw of the body relative to the actor (kick facing), degrees. */
	float YawOffset = 0.f;

	/** Full attitude relative to the actor (facing a debris surface), blended in by AttitudeWeight. */
	FQuat AttitudeOverride = FQuat::Identity;
	float AttitudeWeight = 0.f;

	/** World velocity of the air relative to the body, cm/s (falling body: air flows up). */
	FVector AirVelocity = FVector(0, 0, 5500.f);
};

UCLASS()
class FALL_API AFallBody : public APawn
{
	GENERATED_BODY()

public:
	AFallBody();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fall")
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fall")
	TObjectPtr<USceneComponent> BodyPivot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fall")
	TObjectPtr<USkeletalMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fall")
	TObjectPtr<UFallStrapsComponent> Straps;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fall")
	TObjectPtr<UFallAnimData> AnimData;

	/** Suit tint, readable silhouette colour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fall")
	FLinearColor SuitColor = FLinearColor(0.02f, 0.03f, 0.07f);

	/** Faint red pulse at low health. */
	void PulseLowHealth(float Amount) { LowPulse = FMath::Max(LowPulse, Amount); }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fall")
	FLinearColor AccentColor = FLinearColor(1.f, 0.35f, 0.05f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fall")
	FLinearColor HarnessColor = FLinearColor(0.2f, 0.075f, 0.02f);

	/** Rim highlight that keeps the silhouette readable against snow and cloud. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fall")
	FLinearColor RimColor = FLinearColor(0.55f, 0.7f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fall")
	float RimIntensity = 0.05f;

	/** Attach a basic-shape prop to a bone. Offset and rotation are in body space of the reference pose (X right, Y front, Z up). */
	UStaticMeshComponent* AttachProp(FName Bone, const TCHAR* Shape, const FVector& Offset, const FRotator& BodyRot, const FVector& Size, const FLinearColor& Color, float Roughness = 0.7f, float Metallic = 0.f);
	/** Attaches a mesh to a bone, offset and rotated in body space (X right, Y front, Z up), keeping its own materials. */
	UStaticMeshComponent* AttachMesh(FName Bone, UStaticMesh* InMesh, const FVector& Offset, const FRotator& BodyRot, float Scale = 1.f);

	/** Pistol in the right hand, null when unarmed. */
	UPROPERTY() TObjectPtr<UStaticMeshComponent> WeaponMesh;
	/** Fresh magazine in the left hand during a reload. */
	UPROPERTY() TObjectPtr<UStaticMeshComponent> SpareMag;
	/** Left hand to the belt pouch, magazine into the grip, back. */
	void StartReloadMotion(float Duration);
	bool IsReloading() const { return ReloadT >= 0.f; }
	FVector MuzzleLocation() const;
	FVector MuzzleForward() const;
	/** Deepest penetration (cm) of a hand, finger or forearm into the head, neck or torso this frame; names the pair. */
	float MeasureSelfIntersection(FString* OutWorst = nullptr) const;
	/** Puts the pistol in the right hand, grip in the palm, barrel continuing the forearm. */
	void ArmWeapon();

	/** 0..1 index finger on the trigger, decays by itself. */
	float TriggerPull = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fall")
	float MaxHealth = 100.f;

	UPROPERTY(BlueprintReadOnly, Category = "Fall")
	float Health = 100.f;

	/** Body attitude spring (Hz, damping). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fall")
	FVector2D Attitude = FVector2D(1.9f, 0.55f);

	/** How fast the stance follows the control, 1/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fall")
	float StanceRate = 3.2f;

	FFallBodyControl Control;

	// Smoothed state (read by player, metrics, gallery).
	float Dive = 0.f;
	float Bank = 0.f;
	float OverrideWeight = 0.f;
	FName OverridePose;
	float KickTime = -1.f;
	bool bKickRight = true;
	float AimWeight = 0.f;
	float LookWeight = 0.f;
	float HandWeight[2] = {0.f, 0.f};
	float BodyPitch = 90.f;
	float AttitudeOverrideWeight = 0.f;

	virtual void Tick(float DeltaSeconds) override;

	/** Starts a kick; returns false while one is running. */
	bool StartKick(bool bRightLeg);
	/** Seconds from kick start to the impact frame. */
	float KickImpactTime() const;

	/** Apply a hit: direction the push goes (world), strength ~0..3, damage. Returns true if this hit killed. */
	virtual bool ReceiveHit(const FVector& PushDirWorld, float Strength, float Damage);

	/** Push the body and its limbs without damage or flash (air shocks, canopy opening). */
	void AddImpulse(const FVector& DirWorld, float Strength);

	/** Muzzle recoil on the right arm. */
	void AddRecoil(float Amount) { PendingRecoil += Amount; }

	/** Instant state reset (restarts). */
	virtual void ResetBody();

	bool IsDead() const { return Health <= 0.f; }

	/** Body attitude quaternion relative to the actor, without yaw. */
	FQuat GetAttitude() const { return AttQ; }

	/** Flash the rim highlight (damage, finisher). */
	void Flash(const FLinearColor& Color, float Strength);

	/** Debug: draw hand and foot trajectories. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bDrawTrails = false;

protected:
	virtual void BeginPlay() override;
	virtual void UpdateBody(float DeltaSeconds);

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> FlashMID;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Props;

	/** Builds the helmet and harness. Override to change the outfit. */
	virtual void BuildOutfit();
	/** Character material for this body's role. */
	virtual UMaterialInterface* RoleMaterial() const;
	/** Support hand while aiming, reload path. Runs before the hand targets are handed to the animation. */
	void UpdateWeaponHands(float Dt);
	float ReloadT = -1.f, ReloadDur = 1.f;
	FVector ReloadStartL = FVector::ZeroVector;
	bool bWeaponHand[2] = {false, false};

	FQuat AttQ = FQuat::Identity;
	FVector AttW = FVector::ZeroVector;
	FQuat PrevMeshRot = FQuat::Identity;
	FVector PrevAirVel = FVector::ZeroVector;
	FVector PendingHit = FVector::ZeroVector;
	float PendingRecoil = 0.f;
	float FlashAmount = 0.f;
	float LowPulse = 0.f;
	float LimpSpin = 0.f;
	float CanopyClock = 0.f;
	FLinearColor FlashColor = FLinearColor::White;
	TArray<FVector> Trails[4];
	int32 Seed = 0;
};
