// The faked fall. The player stays near the origin; its vertical speed is a number. Everything nearby moves up
// relative to it; the far layer (atmosphere, cloud deck, terrain) is a planet anchor placed at -altitude.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "FallWorld.generated.h"

class AFallPlayer;
class UProceduralMeshComponent;
class USkyAtmosphereComponent;
class UVolumetricCloudComponent;
class UDirectionalLightComponent;
class USkyLightComponent;
class UInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

USTRUCT(BlueprintType)
struct FFallEventRecord
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Fall") float Time = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Fall") FName Name;
};

UCLASS()
class FALL_API UFallWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Player fall speed, cm/s (virtual). */
	UPROPERTY(BlueprintReadOnly, Category = "Fall")
	float PlayerFallSpeed = 5500.f;

	/** Player horizontal velocity, cm/s (real, the player actor moves horizontally). */
	UPROPERTY(BlueprintReadOnly, Category = "Fall")
	FVector PlayerVelocityH = FVector::ZeroVector;

	/** Height above sea level, metres (virtual). */
	UPROPERTY(BlueprintReadOnly, Category = "Fall")
	float Altitude = 9200.f;

	/** When false, altitude follows the director's nominal curve; when true, it integrates the real fall speed. */
	UPROPERTY(BlueprintReadOnly, Category = "Fall")
	bool bAltitudeLive = false;

	/** Fall time, seconds since the start of the fall (director time). */
	UPROPERTY(BlueprintReadOnly, Category = "Fall")
	float FallTime = 0.f;

	/** Things that passed close to the camera (cloud wisps, debris), for metrics. */
	int32 NearPasses = 0;

	/** Density of cloud puffs passing the camera, 0..3. */
	UPROPERTY(BlueprintReadWrite, Category = "Fall")
	float CloudDensity = 1.f;

	TWeakObjectPtr<AFallPlayer> Player;
	TWeakObjectPtr<class AFallPlanet> Planet;

	/** Interesting things that happened, for pacing metrics. */
	TArray<FFallEventRecord> Events;
	void NoteEvent(FName Name);

	/** Velocity of the air relative to a body that falls at OwnFallSpeed (cm/s) and moves with VelH. */
	FVector AirVelocityFor(float OwnFallSpeed, const FVector& OwnVelH = FVector::ZeroVector) const;

	/** Ground height under a world XY, metres above sea level. */
	float GroundHeightAt(const FVector& WorldPos) const;

	/** Height of the player above the ground, metres. */
	float HeightAboveGround() const;

	static UFallWorldSubsystem* Get(const UObject* WorldContext);
};

/** Moves its owner with the relative fall: up by (player fall speed - own fall speed). */
UCLASS(ClassGroup = (Fall), meta = (BlueprintSpawnableComponent))
class FALL_API UFallMoverComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFallMoverComponent();

	/** Own absolute fall speed, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fall")
	float FallSpeed = 5000.f;

	/** Own horizontal velocity, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fall")
	FVector VelocityH = FVector::ZeroVector;

	/** Gravity on the own fall speed until TerminalSpeed (chunks that start at rest), cm/s^2. 0 = constant speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fall")
	float FallAccel = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fall")
	float TerminalSpeed = 6000.f;

	/** Spin, degrees per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fall")
	FRotator Spin = FRotator::ZeroRotator;

	/** Destroy the owner when it gets this far above or below the player, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fall")
	float KillDistance = 40000.f;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
};

/** The far layer: atmosphere, cloud deck, sun, terrain. Placed at -altitude every frame. */
UCLASS()
class FALL_API AFallPlanet : public AActor
{
	GENERATED_BODY()

public:
	AFallPlanet();

	UPROPERTY(VisibleAnywhere, Category = "Fall") TObjectPtr<USceneComponent> Anchor;
	UPROPERTY(VisibleAnywhere, Category = "Fall") TObjectPtr<USkyAtmosphereComponent> Atmosphere;
	UPROPERTY(VisibleAnywhere, Category = "Fall") TObjectPtr<UVolumetricCloudComponent> Clouds;
	UPROPERTY(VisibleAnywhere, Category = "Fall") TObjectPtr<UDirectionalLightComponent> Sun;
	UPROPERTY(VisibleAnywhere, Category = "Fall") TObjectPtr<USkyLightComponent> SkyLight;
	UPROPERTY(VisibleAnywhere, Category = "Fall") TObjectPtr<UProceduralMeshComponent> Terrain;
	UPROPERTY(VisibleAnywhere, Category = "Fall") TObjectPtr<UProceduralMeshComponent> TerrainDetail;
	UPROPERTY(VisibleAnywhere, Category = "Fall") TObjectPtr<UProceduralMeshComponent> TerrainFar;
	UPROPERTY(VisibleAnywhere, Category = "Fall") TObjectPtr<UInstancedStaticMeshComponent> Trees;

	/** Cloud deck bottom and thickness, km. */
	UPROPERTY(EditAnywhere, Category = "Fall") FVector2D CloudDeck = FVector2D(1.9f, 1.3f); // a deep deck reads as three-dimensional from above
	/** Ground haze: dense in the valleys, warm towards the sun. */
	UPROPERTY(VisibleAnywhere, Category = "Fall") TObjectPtr<class UExponentialHeightFogComponent> Haze;

	/** Ground height function, metres. Valley floor around the landing zone at the origin. */
	static float HeightAt(float Xm, float Ym);

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	void BuildTerrain();
};

/** Speed reference near the camera: streaks of air, ice crystals and cloud wisps flying up past the player. */
UCLASS()
class FALL_API AFallSpeedField : public AActor
{
	GENERATED_BODY()

public:
	AFallSpeedField();

	UPROPERTY(VisibleAnywhere, Category = "Fall") TObjectPtr<UInstancedStaticMeshComponent> Streaks;
	/** Moisture and dust right by the camera: more and longer with speed. */
	UPROPERTY(VisibleAnywhere, Category = "Fall") TObjectPtr<UInstancedStaticMeshComponent> Specks;
	UPROPERTY(EditAnywhere, Category = "Fall") int32 NumSpecks = 120;
	/** Bits of envelope fabric tumbling past, mostly near the airship. */
	UPROPERTY(EditAnywhere, Category = "Fall") int32 NumScraps = 6;

	UPROPERTY(EditAnywhere, Category = "Fall") int32 NumStreaks = 0; // long streaks read as scratches on the image; the specks carry the speed
	UPROPERTY(EditAnywhere, Category = "Fall") FVector2D StreakRadius = FVector2D(280.f, 1300.f);
	UPROPERTY(EditAnywhere, Category = "Fall") float StreakHalfHeight = 2600.f;
	/** Streak length = fall speed * this, seconds of motion blur. */
	UPROPERTY(EditAnywhere, Category = "Fall") float StreakBlur = 0.03f;
	/** Seconds between cloud wisps at density 1. */
	UPROPERTY(EditAnywhere, Category = "Fall") FVector2D PuffInterval = FVector2D(0.3f, 0.7f);
	/** Wisp size range, cm. */
	UPROPERTY(EditAnywhere, Category = "Fall") FVector2D PuffSize = FVector2D(400.f, 1800.f);
	/** Distance at which a wisp counts as passing close to the camera, cm. */
	UPROPERTY(EditAnywhere, Category = "Fall") float NearPassRadius = 1500.f;

	virtual void Tick(float DeltaSeconds) override;

	/** Number of close passes so far (metrics). */
	int32 NearPasses = 0;

	/** Spawn a few wisps right in front of the camera (air shock on brake). */
	void Burst(int32 Count);

protected:
	virtual void BeginPlay() override;

	TArray<FVector> SpeckPos;
	struct FScrap { TObjectPtr<UStaticMeshComponent> Comp; FVector Pos; FRotator Spin; FRotator Rot; float Drag = 1.f; };
	TArray<FScrap> Scraps;

	struct FPuff
	{
		TObjectPtr<UStaticMeshComponent> Comp;
		TObjectPtr<UMaterialInstanceDynamic> MID;
		bool bActive = false;
		bool bCounted = false;
		float Opacity = 0.5f;
		int32 Cluster = 0;
		FVector BaseScale = FVector::OneVector;
	};
	TArray<FPuff> Puffs;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> PuffComps;
	TArray<FVector> StreakPos;
	float NextPuff = 0.f;
	int32 ClusterId = 0;
	int32 LastCountedCluster = -1;
	float SinceClose = 0.f;
	int32 ForceClose = 0;
	FRandomStream Rng;
};
