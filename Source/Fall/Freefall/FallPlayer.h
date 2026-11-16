// The player body: input (keyboard, mouse, gamepad or the review bot), virtual fall speed, camera and wind audio.
#pragma once

#include "CoreMinimal.h"
#include "FallBody.h"
#include "FallPlayer.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UAudioComponent;
class UFallWindWave;
class UFallMovementData;
class AFallDebris;
class AFallEnemy;
class UFallWeaponData;
class UStaticMeshComponent;

/** One frame of player intent. Filled by the input devices or by the review bot. */
USTRUCT(BlueprintType)
struct FFallInputFrame
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Input") float Dive = 0.f;   // +1 W, -1 S
	UPROPERTY(BlueprintReadWrite, Category = "Input") float Slide = 0.f;  // +1 D, -1 A
	UPROPERTY(BlueprintReadWrite, Category = "Input") FVector2D Look = FVector2D::ZeroVector; // degrees this frame (yaw, pitch)
	UPROPERTY(BlueprintReadWrite, Category = "Input") bool bFire = false;
	UPROPERTY(BlueprintReadWrite, Category = "Input") bool bAim = false;
	UPROPERTY(BlueprintReadWrite, Category = "Input") bool bReload = false;
	UPROPERTY(BlueprintReadWrite, Category = "Input") bool bGrab = false;
	UPROPERTY(BlueprintReadWrite, Category = "Input") bool bPush = false;
	UPROPERTY(BlueprintReadWrite, Category = "Input") bool bKick = false;
	UPROPERTY(BlueprintReadWrite, Category = "Input") bool bMenu = false;
};

UCLASS()
class FALL_API AFallPlayer : public AFallBody
{
	GENERATED_BODY()

public:
	AFallPlayer();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fall") TObjectPtr<USpringArmComponent> CameraArm;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fall") TObjectPtr<UCameraComponent> Camera;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fall") TObjectPtr<UAudioComponent> WindAudio;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fall") TObjectPtr<class UPointLightComponent> FillLight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fall")
	TObjectPtr<UFallMovementData> Movement;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fall")
	TObjectPtr<UFallWeaponData> Weapon;

	/** Rounds left in the magazine; reload is automatic when empty. */
	UPROPERTY(BlueprintReadOnly, Category = "Fall") int32 Ammo = 24;
	UPROPERTY(BlueprintReadOnly, Category = "Fall") bool bHasParachute = false;
	/** Under an open canopy (set by the director). */
	UPROPERTY(BlueprintReadOnly, Category = "Fall") bool bCanopy = false;

	/** Where the crosshair points in the world (updated every frame). */
	FVector AimPoint = FVector::ZeroVector;

	/** Called by the director to know a death happened. */
	bool bDied = false;

	/** Current virtual fall speed, m/s. */
	UPROPERTY(BlueprintReadOnly, Category = "Fall") float FallSpeed = 55.f;

	/** Horizontal velocity, cm/s. */
	UPROPERTY(BlueprintReadOnly, Category = "Fall") FVector VelocityH = FVector::ZeroVector;

	/** When set, input comes from here instead of the devices (review bot). */
	TFunction<void(float /*Dt*/, FFallInputFrame& /*Out*/)> BotInput;

	/** Last input used, and the previous one (edges). */
	FFallInputFrame InputNow, InputPrev;

	/** Extra fall speed impulse, m/s (push-off, hits), decays into the stance speed. */
	void AddFallImpulse(float MetersPerSecond) { FallSpeed = FMath::Max(5.f, FallSpeed + MetersPerSecond); }
	void AddHorizontalImpulse(const FVector& CmPerSecond) { VelocityH += CmPerSecond; }

	/** Fall speed the current stance wants, m/s. */
	float StanceSpeed() const;

	/** Camera shake impulse (hits, explosions, canopy). */
	void AddShake(float Amount) { ShakeImpulse = FMath::Max(ShakeImpulse, Amount); }

	/** Hit-stop: freeze time for a few frames on impact. */
	void HitStop(float Seconds);

	/** Debris piece the player holds, if any. */
	TWeakObjectPtr<AFallDebris> Held;
	bool IsHolding() const { return Held.IsValid(); }
	void Grab(AFallDebris* Piece);
	void Release();
	void PushOff();
	/** The grab button was pressed recently (generous window). */
	bool WantsGrab() const { return GetWorld() && GetWorld()->GetTimeSeconds() - GrabIntentTime < 0.8f; }

	/** Best grabbable piece in reach, or null. */
	AFallDebris* FindGrabbable() const;

	virtual void ResetBody() override;
	virtual bool ReceiveHit(const FVector& PushDirWorld, float Strength, float Damage) override;

	void Fire();
	void StartReload();
	void TryKick();
	void TakeParachute(AFallEnemy* Target);
	/** Puts the parachute container on the back (finisher, review runs). */
	void GiveParachute();
	/** Enemy closest to a direction within range, for kicks and finishers. */
	AFallEnemy* FindEnemyInReach(float Range) const;
	virtual void Tick(float DeltaSeconds) override;

	bool Pressed(bool FFallInputFrame::*Field) const { return InputNow.*Field && !(InputPrev.*Field); }

protected:
	virtual void BeginPlay() override;
	void GatherInput(float Dt);
	void UpdateMovement(float Dt);
	void UpdateCamera(float Dt);
	void UpdateHold(float Dt);
	void UpdateCombat(float Dt);
	virtual void BuildOutfit() override;

	UPROPERTY() TObjectPtr<UStaticMeshComponent> Gun;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> ParachutePack;
	TWeakObjectPtr<AFallEnemy> KickTarget;
	TWeakObjectPtr<AFallEnemy> RobTarget;
	float FireCooldown = 0.f;
	float ReloadLeft = 0.f;
	float Spread = 0.f;
	float LastShot = -10.f;
	float LastDamage = -10.f;
	float DamagePulse = 0.f;
	float RobTime = -1.f;
	bool bKickApplied = false;

	UPROPERTY() TObjectPtr<UFallWindWave> WindWave;

	float ShakeImpulse = 0.f;
	float ShakeClock = 0.f;
	float CamAccelOffset = 0.f;
	float PrevFallSpeed = 55.f;
	float HitStopLeft = 0.f;
	float YawVel = 0.f;
	FVector HeldAnchorLocal = FVector::ZeroVector;
	FVector HeldNormalLocal = FVector::UpVector;
	float GrabCooldown = 0.f;
	float GrabIntentTime = -10.f;
	float FovKick = 0.f;
	float ArmPunch = 0.f;
	float ArmPunchTarget = 0.f;
	float CanopyCam = 0.f;
	float ChaseBias = 0.f;
	float ChaseShoulder = 0.f;
};
