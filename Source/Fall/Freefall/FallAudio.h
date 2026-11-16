// Procedural audio: wind noise whose loudness, brightness and whistle follow the airspeed. No sound files needed.
#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundWaveProcedural.h"
#include <atomic>
#include "FallAudio.generated.h"

UCLASS()
class FALL_API UFallWindWave : public USoundWaveProcedural
{
	GENERATED_BODY()

public:
	UFallWindWave(const FObjectInitializer& ObjectInitializer);

	/** 0..1 overall level. */
	std::atomic<float> Gain{0.f};
	/** 0..1 brightness (filter cutoff). */
	std::atomic<float> Brightness{0.3f};
	/** 0..1 resonant whistle amount. */
	std::atomic<float> Whistle{0.f};
	/** 0..1 low-frequency buffeting. */
	std::atomic<float> Buffet{0.f};

	virtual int32 OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples) override;

private:
	uint32 Noise = 22222;
	float Lp1 = 0.f, Lp2 = 0.f, Rumble = 0.f;
	float SvfLow = 0.f, SvfBand = 0.f;
	float GainS = 0.f, BrightS = 0.3f, WhistleS = 0.f;
	float Gust = 0.f, GustTarget = 0.f;
	int32 GustCounter = 0;
	float WhistlePhase = 0.f;
};

/** Synthesised one-shots (gunshot, hits, whoosh) rendered once into PCM and played as procedural waves. */
UCLASS()
class FALL_API UFallOneShotWave : public USoundWaveProcedural
{
	GENERATED_BODY()

public:
	UFallOneShotWave(const FObjectInitializer& ObjectInitializer);

	void SetPCM(const TArray<int16>& Samples);
	virtual int32 OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples) override;

private:
	TArray<int16> PCM;
	int32 Cursor = 0;
};

namespace FallSynth
{
	/** Kinds of synthesised sounds. */
	enum class EKind : uint8 { Gunshot, EnemyShot, HitBody, HitMetal, Kick, Whoosh, Whistle, Charge, Explosion, Canopy, Grab, Ricochet };
	/** Renders a sound to 16-bit mono PCM at 48 kHz. Seed varies the take. */
	FALL_API void Render(EKind Kind, int32 Seed, TArray<int16>& Out);
	/** Plays a synthesised sound at a location (2D when Location is zero). */
	FALL_API void Play(UObject* WorldContext, EKind Kind, const FVector& Location = FVector::ZeroVector, float Volume = 1.f, float Pitch = 1.f);
}
