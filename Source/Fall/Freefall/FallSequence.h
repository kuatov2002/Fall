// The fall as data: a timeline of events by fall time. A designer builds the whole 3-minute fall here without code.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Curves/CurveFloat.h"
#include "FallDebris.h"
#include "FallCombatData.h"
#include "FallSequence.generated.h"

UENUM(BlueprintType)
enum class EFallEventType : uint8
{
	SegmentStart,   // a new segment begins; may be a restart point
	SegmentEnd,     // marks the end of a segment (pacing, review)
	DebrisWave,     // a wave of debris for a while
	DebrisPiece,    // one specific piece at a relative position and velocity
	SpawnEnemy,     // an enemy at a position relative to the player
	OneShot,        // a scripted moment (airship breakup, engine explosion, big flyby)
	CloudDensity,   // how much cloud passes the camera from now on
	AltitudeLive,   // from now on the altitude follows the player's real fall speed
	TargetFlee,     // the parachute carrier starts the escape dive
};

UENUM(BlueprintType)
enum class EFallOneShot : uint8
{
	AirshipBreakup,   // the airship above the player explodes into falling chunks
	EngineExplosion,  // a burning engine pod blows up near the player
	BigFlyby,         // a large hull section drops past the player
};

USTRUCT(BlueprintType)
struct FFallSequenceEvent
{
	GENERATED_BODY()

	/** Fall time, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event", meta = (ClampMin = 0))
	float Time = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event")
	EFallEventType Type = EFallEventType::DebrisWave;

	/** Free note for the designer (shown in the editor preview). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event")
	FString Note;

	/** Segment name (SegmentStart / SegmentEnd). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event", meta = (EditCondition = "Type == EFallEventType::SegmentStart || Type == EFallEventType::SegmentEnd", EditConditionHides))
	FName Segment;

	/** A failure restarts from here (SegmentStart). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event", meta = (EditCondition = "Type == EFallEventType::SegmentStart", EditConditionHides))
	bool bRestartPoint = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event", meta = (EditCondition = "Type == EFallEventType::DebrisWave", EditConditionHides))
	FFallDebrisWave Wave;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event", meta = (EditCondition = "Type == EFallEventType::DebrisPiece", EditConditionHides))
	EFallDebrisType PieceType = EFallDebrisType::Section;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event", meta = (EditCondition = "Type == EFallEventType::SpawnEnemy", EditConditionHides))
	EFallEnemyType EnemyType = EFallEnemyType::Shooter;

	/** Optional tuning for this enemy; type defaults when empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event", meta = (EditCondition = "Type == EFallEventType::SpawnEnemy", EditConditionHides))
	TObjectPtr<UFallEnemyData> EnemyData;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event", meta = (EditCondition = "Type == EFallEventType::OneShot", EditConditionHides))
	EFallOneShot OneShot = EFallOneShot::EngineExplosion;

	/** Position relative to the player, metres (X forward, Y right, Z up, in the player's heading). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event", meta = (EditCondition = "Type == EFallEventType::SpawnEnemy || Type == EFallEventType::DebrisPiece || Type == EFallEventType::OneShot", EditConditionHides))
	FVector Position = FVector(20.f, 0.f, -5.f);

	/** Velocity relative to the player, m/s (DebrisPiece; positive Z rises past the player). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event", meta = (EditCondition = "Type == EFallEventType::DebrisPiece", EditConditionHides))
	FVector Velocity = FVector(0.f, 0.f, 5.f);

	/** Scale (DebrisPiece), density (CloudDensity), altitude in metres (AltitudeLive). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event", meta = (EditCondition = "Type == EFallEventType::DebrisPiece || Type == EFallEventType::CloudDensity || Type == EFallEventType::AltitudeLive", EditConditionHides))
	float Value = 1.f;
};

UCLASS(BlueprintType)
class FALL_API UFallSequence : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UFallSequence();

	/** Total length of the designed fall, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sequence")
	float Duration = 180.f;

	/** Altitude above sea level the fall follows until an AltitudeLive event, metres over seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sequence")
	FRuntimeFloatCurve NominalAltitude;

	/** Height above the ground at which a carried parachute opens, metres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sequence")
	float DeployHeight = 320.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sequence", meta = (TitleProperty = "{Time} {Type} {Note}"))
	TArray<FFallSequenceEvent> Events;

	float AltitudeAt(float Time) const;
	/** Nominal fall speed (m/s) at a time: the slope of the altitude curve. */
	float SpeedAt(float Time) const;
	/** Latest restart point at or before a time. */
	float RestartPointBefore(float Time) const;

	void SortEvents();

	/** Builds the shipped 3-minute design. */
	void BuildDefault();
};
