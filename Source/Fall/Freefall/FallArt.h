// Art set: every mesh, material, effect and sound the slice shows, in one designer-facing data asset.
// Code asks this asset for content instead of naming paths. Defaults are filled in C++ so a fresh DA_FallArt is
// complete; designers swap entries in the editor.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FallDebris.h"
#include "FallArt.generated.h"

class UStaticMesh;
class USkeletalMesh;
class UMaterialInterface;
class UNiagaraSystem;
class USoundBase;

/** Look and handling of one debris type. A piece picks one of the meshes. */
USTRUCT(BlueprintType)
struct FFallDebrisArt
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debris") EFallDebrisType Type = EFallDebrisType::Plate;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debris") TArray<TSoftObjectPtr<UStaticMesh>> Meshes;
	/** Multiplies the mesh's own size (and the wave's scale). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debris") float Scale = 1.f;
	/** Has handholds (rails, rims, rungs, straps): the player can hold on and use it as cover. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debris") bool bGrabbable = false;
	/** Open frame (ring, propeller): the player flies through it; it neither blocks bullets nor pushes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debris") bool bHollow = false;
	/** Spins around its thin axis (propeller). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debris") bool bSpins = false;
	/** Hit strength multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debris") float Mass = 1.f;
};

UCLASS(BlueprintType)
class FALL_API UFallArtData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UFallArtData();

	/** DA_FallArt if it exists, else the class defaults. */
	static const UFallArtData* Get();

	const FFallDebrisArt* FindDebris(EFallDebrisType Type) const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debris") TArray<FFallDebrisArt> Debris;

	/** Airship pieces, modelled in place around one origin; each becomes a falling chunk at the breakup. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Airship") TArray<TSoftObjectPtr<UStaticMesh>> AirshipChunks;
	/** Airship scale (the source model is 76 m long). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Airship") float AirshipScale = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Characters") TSoftObjectPtr<USkeletalMesh> CharacterMesh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Characters") TSoftObjectPtr<UMaterialInterface> PlayerMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Characters") TSoftObjectPtr<UMaterialInterface> ShooterMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Characters") TSoftObjectPtr<UMaterialInterface> WingsuitMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Characters") TSoftObjectPtr<UMaterialInterface> HeavyMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Characters") TSoftObjectPtr<UMaterialInterface> CarrierMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Characters") TSoftObjectPtr<UStaticMesh> ParachuteContainer;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Characters") TSoftObjectPtr<UStaticMesh> Canopy;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Characters") TSoftObjectPtr<UStaticMesh> Turret;

	/** Pistol: mesh, and points in its own space (cm). Barrel along -X, grip down -Z. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon") TSoftObjectPtr<UStaticMesh> Pistol;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon") TSoftObjectPtr<UStaticMesh> Magazine;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon") TSoftObjectPtr<UStaticMesh> Casing;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon") FVector PistolGrip = FVector(3.5f, 0.f, -2.5f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon") FVector PistolMuzzle = FVector(-15.8f, 0.f, 4.2f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon") FVector PistolMagWell = FVector(5.8f, 0.f, -8.f);
	/** The pistol's forward axis in its own space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon") FVector PistolForward = FVector(-1.f, 0.f, 0.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment") TArray<TSoftObjectPtr<UStaticMesh>> Trees;

	/** Effect geometry for the pooled sprites, tracers and lasers (textured, never shown as an object). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects") TSoftObjectPtr<UStaticMesh> FxQuad;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects") TSoftObjectPtr<UStaticMesh> FxBeam;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects") TSoftObjectPtr<UStaticMesh> FxBall;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects") TSoftObjectPtr<UNiagaraSystem> Explosion;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects") TSoftObjectPtr<UNiagaraSystem> ExplosionSmall;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects") TSoftObjectPtr<UNiagaraSystem> ExplosionLarge;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects") TSoftObjectPtr<UNiagaraSystem> Fire;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects") TSoftObjectPtr<UNiagaraSystem> FireLarge;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects") TSoftObjectPtr<UNiagaraSystem> Sparks;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects") TSoftObjectPtr<UNiagaraSystem> MuzzleFlash;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects") TSoftObjectPtr<UNiagaraSystem> ImpactMetal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound") TMap<FName, TSoftObjectPtr<USoundBase>> Sounds;
};
