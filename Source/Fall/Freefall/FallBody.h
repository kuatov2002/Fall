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
	FLinearColor SuitColor = FLinearColor(0.12f, 0.16f, 0.2f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fall")
	FLinearColor AccentColor = FLinearColor(1.f, 0.35f, 0.05f);

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

	virtual void Tick(float DeltaSeconds) override;

	/** Starts a kick; returns false while one is running. */
	bool StartKick(bool bRightLeg);
	/** Seconds from kick start to the impact frame. */
	float KickImpactTime() const;

	/** Apply a hit: direction the push goes (world), strength ~0..3, damage. Returns true if this hit killed. */
	virtual bool ReceiveHit(const FVector& PushDirWorld, float Strength, float Damage);

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

	FQuat AttQ = FQuat::Identity;
	FVector AttW = FVector::ZeroVector;
	FQuat PrevMeshRot = FQuat::Identity;
	FVector PrevAirVel = FVector::ZeroVector;
	FVector PendingHit = FVector::ZeroVector;
	float PendingRecoil = 0.f;
	float FlashAmount = 0.f;
	FLinearColor FlashColor = FLinearColor::White;
	TArray<FVector> Trails[4];
	int32 Seed = 0;
};
