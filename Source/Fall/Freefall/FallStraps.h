// Secondary motion: harness straps, scarf and sleeve flaps as Verlet ribbons driven by the relative airflow.
// Plays the role of AnimDynamics/Chaos Cloth for a mannequin that has no cloth: cheap, stable, always moving.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FallStraps.generated.h"

class UProceduralMeshComponent;
class USkeletalMeshComponent;
class UMaterialInterface;

USTRUCT(BlueprintType)
struct FFallStrapDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strap")
	FName Bone;

	/** Attachment offset from the bone, body space of the reference pose (X right, Y front, Z up), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strap")
	FVector Offset = FVector::ZeroVector;

	/** Direction the strap hangs in calm air, body space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strap")
	FVector RestDir = FVector(0, 0, -1);

	/** Width direction of the ribbon, body space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strap")
	FVector WidthDir = FVector(1, 0, 0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strap")
	int32 Segments = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strap")
	float SegmentLength = 10.f;

	/** Ribbon width at the root and at the tip, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strap")
	FVector2D Width = FVector2D(4.f, 3.f);

	/** How strongly the airflow drags the strap, 1/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strap")
	float Drag = 6.f;

	/** Flutter acceleration at full airspeed, cm/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strap")
	float Flutter = 9000.f;

	/** Pull of the first segment towards the rest direction (0..1 per step). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strap")
	float RootStiffness = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strap")
	FLinearColor Color = FLinearColor(0.05f, 0.05f, 0.05f);
};

UCLASS(ClassGroup = (Fall), meta = (BlueprintSpawnableComponent))
class FALL_API UFallStrapsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFallStrapsComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Straps")
	TArray<FFallStrapDef> Straps;

	/** Air velocity relative to the body, world space, cm/s. Set by the owner every frame. */
	FVector AirVelocity = FVector(0, 0, 5500.f);

	/** 0..1, scales flutter. */
	float WindAmount = 0.5f;

	void Setup(USkeletalMeshComponent* InMesh, UMaterialInterface* Material);
	void Simulate(float DeltaTime);
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	void ResetSim();

	/** Ribbon point positions, for debug drawing and checks. */
	const TArray<TArray<FVector>>& GetPoints() const { return Points; }

private:
	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Ribbons;

	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> Mesh;

	TArray<TArray<FVector>> Points, Prev;
	TArray<FFallStrapDef> Local; // Straps converted to bone space
	float Clock = 0.f;
	bool bValid = false;
};
