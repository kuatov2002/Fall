// Enemy soldiers in the fall. One class, four behaviours (shooter, wingsuiter, heavy on a turret, parachute target).
// Every attack is announced: laser and charge sound (shooter, heavy), rising whistle and trail (wingsuiter).
#pragma once

#include "CoreMinimal.h"
#include "FallBody.h"
#include "FallCombatData.h"
#include "FallEnemy.generated.h"

class AFallDebris;
class AFallPlayer;
class UProceduralMeshComponent;
class UBoxComponent;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EFallEnemyState : uint8
{
	Approach, Hold, Telegraph, Burst, Cooldown, Pass, Stunned, Flee, Dead, Robbed
};

UCLASS()
class FALL_API AFallEnemy : public AFallBody
{
	GENERATED_BODY()

public:
	AFallEnemy();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy")
	TObjectPtr<UFallEnemyData> Data;

	UPROPERTY(BlueprintReadOnly, Category = "Enemy")
	EFallEnemyState State = EFallEnemyState::Approach;

	/** Spawns an enemy at a position relative to the player (cm). Data may be null for type defaults. */
	static AFallEnemy* Spawn(UWorld* World, EFallEnemyType Type, const FVector& RelPos, UFallEnemyData* Data = nullptr);

	EFallEnemyType GetType() const { return Data ? Data->Type : EFallEnemyType::Shooter; }

	virtual bool ReceiveHit(const FVector& PushDirWorld, float Strength, float Damage) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Own absolute fall speed (cm/s) and horizontal velocity (cm/s). */
	float OwnFall = 5500.f;
	FVector VelH = FVector::ZeroVector;

	/** Target only: the director starts the escape dive. */
	void StartFlee();
	/** Target only: stunned and in reach of a finisher. */
	bool CanBeRobbed() const { return GetType() == EFallEnemyType::Target && State == EFallEnemyState::Stunned; }
	/** Target only: the parachute is taken. */
	void Rob(const FVector& PushDir);
	/** Knocked out of the fight (kick on a heavy, death). */
	void Knockout(const FVector& PushDir);

	/** Is an attack being telegraphed right now (for review metrics). */
	bool IsTelegraphing() const { return State == EFallEnemyState::Telegraph; }

	UPROPERTY() TObjectPtr<UStaticMeshComponent> Pack;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void BuildOutfit() override;

	void TickShooter(float Dt, AFallPlayer* P);
	void TickWingsuiter(float Dt, AFallPlayer* P);
	void TickHeavy(float Dt, AFallPlayer* P);
	void TickTarget(float Dt, AFallPlayer* P);
	void StationKeep(float Dt, const FVector& Desired, float Agility, float FallAgility);
	void FireShot(const FVector& From, const FVector& AimPoint, float Spread, float Damage, bool bTurret);
	FVector Muzzle() const;
	void SetLaser(bool bOn, const FVector& From, const FVector& To);
	void UpdateMembrane();
	void BuildTurret();

	UPROPERTY() TObjectPtr<UStaticMeshComponent> Gun;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> Membrane;
	TWeakObjectPtr<AFallDebris> Platform;
	UPROPERTY() TObjectPtr<USceneComponent> TurretHead;
	UPROPERTY() TObjectPtr<UBoxComponent> Shield;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> TurretParts;

	float StateTime = 0.f;
	float NextAction = 0.f;
	int32 ShotsLeft = 0;
	float ShotTimer = 0.f;
	float Orbit = 0.f;
	float OrbitSpeed = 0.f;
	float Dist = 1800.f;
	int32 LaserHandle = INDEX_NONE;
	FVector LaggedAim = FVector::ZeroVector;
	float StunMeter = 0.f;
	float DeadTime = 0.f;
	float TrailTimer = 0.f;
	int32 Passes = 0;
	FVector PassFrom, PassTo, PassStartRel;
	FVector PrevRel = FVector::ZeroVector;
	float TurretYaw = 0.f;
	float TurretPitch = 0.f;
	float JinkSeed = 0.f;
	bool bWhistled = false;
};
