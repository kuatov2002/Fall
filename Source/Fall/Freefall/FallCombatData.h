// Weapon and enemy tuning as data assets. Class defaults are complete; assets override them per design.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FallCombatData.generated.h"

UCLASS(BlueprintType)
class FALL_API UFallWeaponData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Seconds between shots while the trigger is held. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon") float FireInterval = 0.12f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon") int32 MagazineSize = 24;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon") float ReloadTime = 1.3f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon") float Damage = 14.f;
	/** Random spread cone, degrees; grows with each shot and recovers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon") FVector2D SpreadDeg = FVector2D(0.6f, 3.f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon") float SpreadPerShot = 0.35f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon") float Range = 20000.f;
	/** Hit stop on a confirmed hit and on a kill, seconds (40-80 ms reads as impact). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon") FVector2D HitStop = FVector2D(0.045f, 0.08f);
	/** Push given to a body per hit (hit reaction strength). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon") float HitPush = 0.7f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon") FLinearColor TracerColor = FLinearColor(1.f, 0.8f, 0.45f);

	/** Kick: reach to start it, reach at impact, damage, push. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kick") float KickRange = 420.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kick") float KickDamage = 35.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kick") float KickPush = 2600.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kick") float KickHitStop = 0.075f;

	static const UFallWeaponData* Resolve(const UFallWeaponData* Maybe) { return Maybe ? Maybe : ::GetDefault<UFallWeaponData>(); }
};

UENUM(BlueprintType)
enum class EFallEnemyType : uint8
{
	Shooter,     // falls nearby, keeps distance, fires bursts after a 0.6 s laser telegraph
	Wingsuiter,  // fast strafing runs announced by a rising whistle and a trail 1.5 s ahead
	Heavy,       // holds a big debris piece with a shielded turret; vulnerable from behind or kicked off
	Target,      // carries the only parachute; dives away, must be stunned and robbed
};

UCLASS(BlueprintType)
class FALL_API UFallEnemyData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy") EFallEnemyType Type = EFallEnemyType::Shooter;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy") float Health = 60.f;
	/** Warning before an attack, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy") float TelegraphTime = 0.6f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy") int32 BurstCount = 5;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy") float ShotInterval = 0.1f;
	/** Pause between bursts, seconds (range). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy") FVector2D Cooldown = FVector2D(2.2f, 3.5f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy") float Damage = 7.f;
	/** Aim error cone, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy") float SpreadDeg = 2.2f;
	/** How late the aim follows the player during a burst, seconds (moving dodges it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy") float AimLag = 0.3f;
	/** Preferred horizontal distance to the player, metres (range). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy") FVector2D Distance = FVector2D(14.f, 26.f);
	/** Preferred height relative to the player, metres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy") float Height = 0.f;
	/** Max horizontal speed, m/s, and max fall speed difference it can make, m/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy") FVector2D Agility = FVector2D(9.f, 18.f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy") FLinearColor SuitColor = FLinearColor(0.09f, 0.03f, 0.025f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy") FLinearColor AccentColor = FLinearColor(1.f, 0.1f, 0.05f);

	/** Complete defaults for a type. */
	static void ApplyDefaults(UFallEnemyData* D, EFallEnemyType Type);

	/** Editor button: reset every value to the defaults of the selected Type. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Enemy")
	void ResetToTypeDefaults() { ApplyDefaults(this, Type); }
};
