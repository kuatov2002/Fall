// Opening cutscene on the deck of the airship, 17 s, no text. Plays once per session on a fresh start.
// The camera is the level sequence LS_Intro, authored in deck space: its transform origin follows the ship every
// frame (roll and shake included). This actor runs the beats on the sequence clock: alarm, first explosion and
// roll, the character runs for his pistol, second explosion throws him over the rail. Control is given at that
// moment and the camera eases into the gameplay camera without a cut. Any key skips to the throw.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FallIntro.generated.h"

class AFallAirship;
class AFallPlayer;
class ALevelSequenceActor;
class ULevelSequencePlayer;
class UPointLightComponent;
class UAudioComponent;

UCLASS()
class FALL_API AFallIntro : public AActor
{
	GENERATED_BODY()

public:
	AFallIntro();
	virtual void Tick(float DeltaSeconds) override;

	/** True while an intro holds the fall back (the director waits). */
	static bool IsBlocking(const UObject* WorldContext);
	/** Whether this session should play the intro (fresh start, not a bot run unless -FallIntro). */
	static bool ShouldPlay();
	/** True once an intro has thrown the player out (the fall starts where he is, no fade). */
	static bool HandedOver(const UObject* WorldContext);

	/** Seconds into the sequence when the second explosion throws the player out. */
	UPROPERTY(EditAnywhere, Category = "Intro") float ThrowTime = 14.5f;

protected:
	virtual void BeginPlay() override;
	void Begin(AFallPlayer* P);
	void Throw();
	/** Deck space (airship mesh space, cm) to world. */
	FTransform DeckTransform() const;
	FVector DeckToWorld(const FVector& Local) const;

	UPROPERTY() TObjectPtr<AFallAirship> Ship;
	UPROPERTY() TObjectPtr<ALevelSequenceActor> SeqActor;
	UPROPERTY() TObjectPtr<ULevelSequencePlayer> SeqPlayer;
	UPROPERTY() TObjectPtr<UPointLightComponent> Alarm;
	UPROPERTY() TArray<TObjectPtr<UAudioComponent>> Ambience;

	bool bStarted = false;
	bool bThrown = false;
	bool bDone = false;
	bool bExplosion1 = false;
	bool bGunShown = false;
	float T = 0.f;
	float AfterThrow = 0.f;
	float Shake = 0.f;
	float Roll = 0.f;
	FName Clip;
	FVector ShakeOffset = FVector::ZeroVector;
	FRotator ShipRot0 = FRotator::ZeroRotator;
};
