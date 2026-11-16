// The burning airship the fall starts from. It hangs above the player for a moment, then breaks into chunks that
// accelerate from rest; heavier chunks overtake the player a few seconds later and plunge past.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FallAirship.generated.h"

class UFallMoverComponent;
class UStaticMeshComponent;

UCLASS()
class FALL_API AFallAirship : public AActor
{
	GENERATED_BODY()

public:
	AFallAirship();
	virtual void Tick(float DeltaSeconds) override;

	/** Seconds before it breaks apart. */
	UPROPERTY(EditAnywhere, Category = "Airship") float BreakupDelay = 1.6f;
	/** The ship of the opening cutscene: the fall reuses it instead of spawning another. */
	bool bIntroShip = false;
	float GetAge() const { return Age; }
	UStaticMeshComponent* AnyPart() const { return Parts.Num() ? Parts[0].Get() : nullptr; }

protected:
	virtual void BeginPlay() override;
	void Breakup();

	UPROPERTY() TObjectPtr<UFallMoverComponent> Mover;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	UPROPERTY() TObjectPtr<class UPointLightComponent> Glow;
	UPROPERTY() TArray<TObjectPtr<class UNiagaraComponent>> Fires;
	float Age = 0.f;
	float FireTimer = 0.f;
	bool bBroken = false;
};
