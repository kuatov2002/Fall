// Airship debris: pieces that fall at their own speed (so they rise past or drop past the player), can hit the
// player, block bullets (cover) and can be grabbed. Waves of debris are described by data (FFallDebrisWave).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FallDebris.generated.h"

class UBoxComponent;
class UFallMoverComponent;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EFallDebrisType : uint8
{
	Plate,    // hull panel, grabbable cover
	Girder,   // long beam, grabbable
	Crate,    // cargo box, hits hard, too small to hold
	Engine,   // burning engine pod with a propeller, grabbable
	Section,  // big hull section, grabbable, carries the turret
	Shards,   // small fragments, near misses
};

/** A wave of debris. Used by the fall sequence; one wave spawns one type. */
USTRUCT(BlueprintType)
struct FFallDebrisWave
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debris")
	EFallDebrisType Type = EFallDebrisType::Plate;

	/** Pieces per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debris", meta = (ClampMin = 0))
	float Density = 0.5f;

	/** Speed relative to the player, m/s, range. Positive rises past the player from below, negative falls past from above. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debris")
	FVector2D RelativeSpeed = FVector2D(4.f, 12.f);

	/** Horizontal spread around the player, metres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debris", meta = (ClampMin = 0))
	float Spread = 25.f;

	/** Share of pieces aimed to pass within a few metres of the player (dodge or grab opportunities). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debris", meta = (ClampMin = 0, ClampMax = 1))
	float CloseCall = 0.3f;

	/** Size multiplier range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debris")
	FVector2D Scale = FVector2D(0.8f, 1.2f);

	/** Chance that a piece burns (fire and smoke trail). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debris", meta = (ClampMin = 0, ClampMax = 1))
	float BurnChance = 0.25f;

	/** Seconds the wave lasts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debris", meta = (ClampMin = 0))
	float Duration = 10.f;
};

UCLASS()
class FALL_API AFallDebris : public AActor
{
	GENERATED_BODY()

public:
	AFallDebris();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Debris") TObjectPtr<UBoxComponent> Box;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Debris") TObjectPtr<UFallMoverComponent> Mover;

	UPROPERTY(BlueprintReadOnly, Category = "Debris") EFallDebrisType Type = EFallDebrisType::Plate;
	UPROPERTY(BlueprintReadOnly, Category = "Debris") bool bGrabbable = true;
	UPROPERTY(BlueprintReadOnly, Category = "Debris") bool bBurning = false;

	/** Build the visual and collision for a type. */
	void Build(EFallDebrisType InType, float Scale, bool bInBurning, int32 Seed);

	/** Closest point on the box surface to P, and the outward normal of that face. */
	FVector ClosestSurfacePoint(const FVector& P, FVector* OutNormal = nullptr) const;
	/** Distance from P to the box (0 inside). */
	float DistanceTo(const FVector& P) const;

	/** Damage multiplier when it hits a body (mass feel). */
	float Mass = 1.f;

	/** Set while a body holds it (stabilises the spin). */
	TWeakObjectPtr<AActor> Holder;

	/** Seconds since a body last touched it (no repeated hits). */
	float LastHit = -10.f;

	virtual void Tick(float DeltaSeconds) override;

protected:
	UStaticMeshComponent* AddPart(UStaticMesh* Mesh, const FVector& Loc, const FRotator& Rot, const FVector& Scale, const FLinearColor& Color, float Metal = 0.4f, float Rough = 0.6f);
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Propeller;
	float SmokeTimer = 0.f;
	float Age = 0.f;
};

/** Runs debris waves around the player and checks collisions with bodies. */
UCLASS()
class FALL_API AFallDebrisSpawner : public AActor
{
	GENERATED_BODY()

public:
	AFallDebrisSpawner();
	virtual void Tick(float DeltaSeconds) override;

	static AFallDebrisSpawner* Get(const UObject* WorldContext);

	void StartWave(const FFallDebrisWave& Wave);
	void ClearWaves() { Waves.Reset(); }
	/** Spawns one piece at a position relative to the player with a relative velocity (m/s). */
	AFallDebris* SpawnPiece(EFallDebrisType Type, const FVector& RelPos, const FVector& RelVelMS, float Scale = 1.f, bool bBurning = false);
	void DestroyAll();

	/** Vertical distance at which pieces appear, cm. */
	UPROPERTY(EditAnywhere, Category = "Debris") float SpawnDepth = 16000.f;

protected:
	struct FActiveWave { FFallDebrisWave Wave; float Age = 0.f; float Accum = 0.f; };
	TArray<FActiveWave> Waves;
	FRandomStream Rng;
	int32 Seed = 1;
};
