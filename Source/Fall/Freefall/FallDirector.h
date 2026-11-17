// Plays a UFallSequence: fires timeline events, owns altitude, restart points, the canopy ending and the crash.
// In the editor it previews the sequence around itself: a time slider (PreviewTime) shows what spawns where,
// and a panel draws the nominal altitude and fall speed curves with segment and event marks.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FallSequence.h"
#include "FallDirector.generated.h"

class AFallPlayer;
class UTextRenderComponent;
class UStaticMeshComponent;

UCLASS()
class FALL_API AFallDirector : public AActor
{
	GENERATED_BODY()

public:
	AFallDirector();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fall")
	TObjectPtr<UFallSequence> Sequence;

	/** Editor preview: drag to scrub through the fall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview", meta = (ClampMin = 0, ClampMax = 200, UIMin = 0, UIMax = 180))
	float PreviewTime = 0.f;

	/** Editor preview: how many seconds ahead of PreviewTime are drawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview", meta = (ClampMin = 1, ClampMax = 60))
	float PreviewWindow = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview")
	bool bPreview = true;

	/** Current fall time, seconds. */
	UPROPERTY(BlueprintReadOnly, Category = "Fall")
	float Time = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Fall")
	float Checkpoint = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Fall")
	FName CurrentSegment;

	static AFallDirector* Get(const UObject* WorldContext);

	/** Restart the fall at a time (restart points, review runs). */
	void JumpTo(float T, bool bFromIntro = false);

	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }

	bool IsUnderCanopy() const { return bCanopy; }
	/** Seconds since the ripcord, -1 without a canopy. Bag out, lines stretch, then the opening shock at CanopyShockTime. */
	float CanopyClock() const { return bCanopy ? CanopyTime : -1.f; }
	static constexpr float CanopyShockTime = 0.9f;
	/** Suspension lines currently drawn (review: lines must not pass through the bodies). */
	void LineSegments(TArray<TPair<FVector, FVector>>& Out) const;
	/** Ripcord pulled: the canopy comes out of the carrier's container (or the player's own, review restarts). */
	void Deploy();
	/** The player took the parachute: restarts from now keep it. */
	void MarkParachuteCheckpoint() { bCheckpointParachute = true; Checkpoint = Time; }
	bool HasEnded() const { return bEnded; }

protected:
	virtual void BeginPlay() override;
	void Fire(const FFallSequenceEvent& E);
	void Crash();
	void DrawPreview();
	FVector RelToWorld(const FVector& RelMetres) const;
	const UFallSequence* Seq() const;

	UPROPERTY() TObjectPtr<UFallSequence> FallbackSequence;
	UPROPERTY() TArray<TObjectPtr<UTextRenderComponent>> Labels;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Canopy;

	int32 NextEvent = 0;
	bool bCanopy = false;
	float CanopyTime = 0.f;
	bool bEnded = false;
	float EndTime = 0.f;
	float CrashTime = -1.f;
	float Fade = 0.f;
	static constexpr int32 NumLines = 16; // 4 risers x 4 lines fanning out to the cells
	int32 Risers[NumLines];
	bool bShockDone = false;
	float CanopyDescent = 14.f;
	bool bEventsEnabled = true;
	bool bStarted = false;
	bool bCheckpointParachute = false;
	float DeathTime = -1.f;
	bool bLanded = false;
	float LandTime = 0.f;
	float StartTime = 0.f;
};
