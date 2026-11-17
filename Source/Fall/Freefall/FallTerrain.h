// Real elevation for the landing region (Rhone valley at Martigny, Swiss Alps), baked from open terrain tiles by
// Tools/fetch_terrain.py and Tools/make_terrain.py. Three nested square grids in metres above the valley floor:
// near 6.6 km, mid 84 km, far 440 km. World X is east, world Y is south.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FallTerrain.generated.h"

USTRUCT(BlueprintType)
struct FFallHeightGrid
{
	GENERATED_BODY()

	/** Samples per side. Rows run north to south, columns west to east. */
	UPROPERTY(EditAnywhere, Category = "Terrain") int32 Size = 0;
	/** Side length, metres. */
	UPROPERTY(EditAnywhere, Category = "Terrain") float Extent = 0.f;
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Terrain") TArray<float> Heights;

	bool IsValid() const { return Size > 1 && Heights.Num() == Size * Size; }
	/** Bilinear height at a point in metres from the centre (X east, Y south). */
	float Sample(float Xm, float Ym) const;
};

UCLASS(BlueprintType)
class FALL_API UFallTerrainData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Terrain") FFallHeightGrid Near;
	UPROPERTY(EditAnywhere, Category = "Terrain") FFallHeightGrid Mid;
	UPROPERTY(EditAnywhere, Category = "Terrain") FFallHeightGrid Far;
	/** Game height of the valley floor (the fall's altitude curve was tuned for 320 m). */
	UPROPERTY(EditAnywhere, Category = "Terrain") float ValleyFloor = 320.f;
	/** Where the data comes from, for the credits. */
	UPROPERTY(EditAnywhere, Category = "Terrain") FString Source;

	/** DA_FallTerrain, or null (the procedural mountains are used then). */
	static const UFallTerrainData* Get();
	/** Height in game metres at a point in metres from the centre. */
	float HeightAt(float Xm, float Ym) const;
};
