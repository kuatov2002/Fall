// Lightweight pooled effects driven from C++: smoke and fire sprites, sparks, flashes, tracers, laser beams.
// Particles can be carried by the air: in the faked fall the air rises past the player at its fall speed.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FallFX.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UInstancedStaticMeshComponent;

UCLASS()
class FALL_API AFallFX : public AActor
{
	GENERATED_BODY()

public:
	AFallFX();
	virtual void Tick(float DeltaSeconds) override;

	static AFallFX* Get(const UObject* WorldContext);

	/** Soft sprite (smoke, fire, dust). Vel in cm/s; bAir = carried up by the relative air flow. */
	void Sprite(const FVector& Pos, const FVector& Vel, float Size0, float Size1, float Life, const FLinearColor& Color, float Opacity, bool bAir = true, float Emissive = 1.f);
	/** Short bright additive flash. */
	void Flash(const FVector& Pos, float Size, const FLinearColor& Color, float Life = 0.08f);
	/** Burst of sparks along a direction. */
	void Sparks(const FVector& Pos, const FVector& Dir, int32 Count, const FLinearColor& Color, float Speed = 2500.f);
	/** Bullet tracer from A to B. */
	void Tracer(const FVector& A, const FVector& B, const FLinearColor& Color, float Width = 3.f);
	/** Explosion: flash, fire and smoke, sparks. */
	void Explosion(const FVector& Pos, float Scale);

	/** Persistent beam (telegraph laser); returns a handle. Hide with Beam(handle, ..., false). */
	int32 Beam(int32 Handle, const FVector& A, const FVector& B, const FLinearColor& Color, float Width, bool bVisible);

protected:
	virtual void BeginPlay() override;

	struct FPart
	{
		UStaticMeshComponent* Comp = nullptr;
		UMaterialInstanceDynamic* MID = nullptr;
		FVector Pos, Vel;
		float Age = 0.f, Life = 0.f, Size0 = 0.f, Size1 = 0.f, Opacity = 1.f, Emissive = 1.f;
		FLinearColor Color;
		bool bAir = true;
		bool bActive = false;
	};
	struct FLine
	{
		UStaticMeshComponent* Comp = nullptr;
		UMaterialInstanceDynamic* MID = nullptr;
		FVector A, B;
		float Age = 0.f, Life = 0.f, Width = 1.f, Intensity = 1.f;
		bool bActive = false;
		bool bPersistent = false;
	};

	FPart* Take(TArray<FPart>& Pool, int32& Cursor);
	FLine* TakeLine();

	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Owned;
	TArray<FPart> Sprites, Flashes, SparkPool;
	TArray<FLine> Lines;
	int32 SpriteCursor = 0, FlashCursor = 0, SparkCursor = 0, LineCursor = 0;
};
