#include "FallAudio.h"
#include "FallArt.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	constexpr int32 SR = 48000;

	struct FRand32
	{
		uint32 S;
		explicit FRand32(uint32 InS) : S(InS ? InS : 1u) {}
		float Next() { S ^= S << 13; S ^= S >> 17; S ^= S << 5; return (S & 0xffffff) / float(0x7fffff) - 1.f; }
	};

	int16 ToPCM(float V) { return (int16)FMath::Clamp(FMath::RoundToInt(FMath::Tanh(V) * 32000.f), -32767, 32767); }
}

// ---------------------------------------------------------------- wind

UFallWindWave::UFallWindWave(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	SetSampleRate(SR);
	NumChannels = 1;
	Duration = INDEFINITELY_LOOPING_DURATION;
	bLooping = true;
	bProcedural = true;
}

int32 UFallWindWave::OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples)
{
	OutAudio.SetNumUninitialized(NumSamples * 2);
	int16* Out = reinterpret_cast<int16*>(OutAudio.GetData());
	FRand32 R(Noise);
	const float G = Gain.load(), B = Brightness.load(), W = Whistle.load(), Bu = Buffet.load();
	for (int32 i = 0; i < NumSamples; ++i)
	{
		GainS += (G - GainS) * 0.0005f;
		BrightS += (B - BrightS) * 0.0005f;
		WhistleS += (W - WhistleS) * 0.0005f;
		if (++GustCounter > 2400)
		{
			GustCounter = 0;
			GustTarget = 0.65f + 0.55f * (R.Next() * 0.5f + 0.5f);
		}
		Gust += (GustTarget - Gust) * 0.0002f;

		const float White = R.Next();
		const float Fc = 250.f + 3800.f * FMath::Pow(FMath::Clamp(BrightS, 0.f, 1.f), 1.5f);
		const float A = 1.f - FMath::Exp(-2.f * PI * Fc / SR);
		Lp1 += A * (White - Lp1);
		Lp2 += A * (Lp1 - Lp2);
		Rumble += 0.008f * (White - Rumble);

		const float F = 2.f * FMath::Sin(PI * FMath::Clamp(800.f + 1700.f * WhistleS, 100.f, 6000.f) / SR);
		const float High = White - SvfLow - 0.08f * SvfBand;
		SvfBand += F * High;
		SvfLow += F * SvfBand;

		const float V = (Lp2 * 2.2f + Rumble * 6.f * Bu * Gust + SvfBand * 0.05f * WhistleS) * GainS * Gust;
		Out[i] = ToPCM(V);
	}
	Noise = R.S;
	return NumSamples;
}

// ---------------------------------------------------------------- one-shots

UFallOneShotWave::UFallOneShotWave(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	SetSampleRate(SR);
	NumChannels = 1;
	bLooping = false;
	bProcedural = true;
}

void UFallOneShotWave::SetPCM(const TArray<int16>& Samples)
{
	PCM = Samples;
	Cursor = 0;
	Duration = PCM.Num() / float(SR);
}

int32 UFallOneShotWave::OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples)
{
	OutAudio.SetNumZeroed(NumSamples * 2);
	int16* Out = reinterpret_cast<int16*>(OutAudio.GetData());
	const int32 N = FMath::Min(NumSamples, PCM.Num() - Cursor);
	if (N > 0)
	{
		FMemory::Memcpy(Out, PCM.GetData() + Cursor, N * sizeof(int16));
		Cursor += N;
	}
	return NumSamples;
}

void FallSynth::Render(EKind Kind, int32 Seed, TArray<int16>& Out)
{
	FRand32 R(0x9e3779b9u ^ (uint32)(Seed * 2654435761u) ^ ((uint32)Kind << 20));
	auto Len = [](float Sec) { return FMath::RoundToInt(Sec * SR); };
	float Lp = 0.f, Lp2 = 0.f, Hp = 0.f, Ph = 0.f, Ph2 = 0.f, Ph3 = 0.f, SvL = 0.f, SvB = 0.f;
	int32 N = 0;
	switch (Kind)
	{
	case EKind::Gunshot: case EKind::EnemyShot: N = Len(0.45f); break;
	case EKind::HitBody: N = Len(0.22f); break;
	case EKind::HitMetal: case EKind::Grab: N = Len(0.5f); break;
	case EKind::Kick: N = Len(0.35f); break;
	case EKind::Whoosh: N = Len(0.45f); break;
	case EKind::Whistle: N = Len(1.6f); break;
	case EKind::Charge: N = Len(0.65f); break;
	case EKind::Explosion: N = Len(3.0f); break;
	case EKind::Canopy: N = Len(1.0f); break;
	case EKind::Ricochet: N = Len(0.35f); break;
	}
	Out.SetNumUninitialized(N);
	const float Var = 1.f + R.Next() * 0.06f;
	for (int32 i = 0; i < N; ++i)
	{
		const float T = i / float(SR);
		const float W = R.Next();
		float V = 0.f;
		switch (Kind)
		{
		case EKind::Gunshot:
		case EKind::EnemyShot:
		{
			const bool bE = Kind == EKind::EnemyShot;
			Hp = W - Lp; Lp += 0.25f * (W - Lp);
			Lp2 += 0.05f * (W - Lp2);
			Ph += 2.f * PI * (bE ? 70.f : 95.f) * Var * (1.f - T) / SR;
			const float Crack = Hp * FMath::Exp(-T * 120.f) * 1.6f;
			const float Body = Lp2 * FMath::Exp(-T * (bE ? 9.f : 14.f)) * 3.5f;
			const float Thump = FMath::Sin(Ph) * FMath::Exp(-T * 25.f) * 0.9f;
			V = (Crack + Body + Thump) * (bE ? 0.8f : 1.f);
			break;
		}
		case EKind::HitBody:
			Lp += 0.06f * (W - Lp);
			Ph += 2.f * PI * FMath::Lerp(140.f, 55.f, T / 0.22f) * Var / SR;
			V = (FMath::Sin(Ph) * 1.2f + Lp * 3.f) * FMath::Exp(-T * 22.f);
			break;
		case EKind::HitMetal:
		case EKind::Grab:
		{
			const float K = Kind == EKind::Grab ? 0.6f : 1.f;
			Ph += 2.f * PI * 1230.f * K * Var / SR; Ph2 += 2.f * PI * 2710.f * K * Var / SR; Ph3 += 2.f * PI * 4130.f * K * Var / SR;
			Hp = W - Lp; Lp += 0.3f * (W - Lp);
			V = (FMath::Sin(Ph) * 0.5f + FMath::Sin(Ph2) * 0.3f * FMath::Exp(-T * 6.f) + FMath::Sin(Ph3) * 0.2f * FMath::Exp(-T * 12.f)) * FMath::Exp(-T * 7.f)
				+ Hp * FMath::Exp(-T * 200.f) * 1.5f;
			break;
		}
		case EKind::Kick:
			Lp += 0.04f * (W - Lp);
			Ph += 2.f * PI * FMath::Lerp(110.f, 45.f, T / 0.35f) / SR;
			V = FMath::Sin(Ph) * FMath::Exp(-FMath::Square((T - 0.12f) * 18.f)) * 1.5f + Lp * 4.f * FMath::Sin(PI * FMath::Clamp(T / 0.35f, 0.f, 1.f));
			break;
		case EKind::Whoosh:
		{
			const float F = 2.f * FMath::Sin(PI * FMath::Lerp(400.f, 1500.f, T / 0.45f) / SR);
			const float H = W - SvL - 0.5f * SvB; SvB += F * H; SvL += F * SvB;
			V = SvB * FMath::Sin(PI * FMath::Clamp(T / 0.45f, 0.f, 1.f)) * 1.2f;
			break;
		}
		case EKind::Whistle:
		{
			const float X = T / 1.6f;
			const float F0 = FMath::Lerp(500.f, 2600.f, X * X);
			Ph += 2.f * PI * F0 / SR;
			const float F = 2.f * FMath::Sin(PI * F0 / SR);
			const float H = W - SvL - 0.05f * SvB; SvB += F * H; SvL += F * SvB;
			V = (FMath::Sin(Ph) * 0.35f + SvB * 0.04f) * X * FMath::Min(1.f, (1.f - X) * 12.f) * 1.4f;
			break;
		}
		case EKind::Charge:
		{
			const float X = T / 0.65f;
			Ph += 2.f * PI * FMath::Lerp(700.f, 1900.f, X) / SR;
			const float Pulse = FMath::Sin(2.f * PI * FMath::Lerp(8.f, 26.f, X) * T) > 0.f ? 1.f : 0.3f;
			V = FMath::Sin(Ph) * Pulse * 0.45f * FMath::Min(1.f, (1.f - X) * 20.f);
			break;
		}
		case EKind::Explosion:
			Lp += 0.02f * (W - Lp); Lp2 += 0.004f * (W - Lp2);
			V = (Lp * 5.f * FMath::Exp(-T * 2.5f) + Lp2 * 14.f * FMath::Exp(-T * 0.9f) + W * FMath::Exp(-T * 40.f)) * 1.3f;
			break;
		case EKind::Canopy:
		{
			Lp += 0.15f * (W - Lp);
			const float Flap = 0.5f + 0.5f * FMath::Sin(2.f * PI * FMath::Lerp(28.f, 9.f, T) * T);
			V = Lp * 3.f * Flap * FMath::Exp(-T * 3.f) + Lp * 6.f * FMath::Exp(-FMath::Square((T - 0.35f) * 14.f));
			break;
		}
		case EKind::Ricochet:
			Ph += 2.f * PI * FMath::Lerp(3200.f, 900.f, T / 0.35f) * Var / SR;
			Hp = W - Lp; Lp += 0.3f * (W - Lp);
			V = FMath::Sin(Ph) * 0.4f * FMath::Exp(-T * 9.f) + Hp * FMath::Exp(-T * 150.f);
			break;
		}
		// Short fade in/out to avoid clicks.
		const float Edge = FMath::Min(1.f, FMath::Min(i / 48.f, (N - i) / 480.f));
		Out[i] = ToPCM(V * 0.7f * Edge);
	}
}

void FallSynth::Play(UObject* WorldContext, EKind Kind, const FVector& Location, float Volume, float Pitch)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_DedicatedServer) return;

	static const TCHAR* Names[] = {TEXT("Gunshot"), TEXT("EnemyShot"), TEXT("HitBody"), TEXT("HitMetal"), TEXT("Kick"), TEXT("Whoosh"), TEXT("Whistle"), TEXT("Charge"), TEXT("Explosion"), TEXT("Canopy"), TEXT("Grab"), TEXT("Ricochet")};
	if (const TSoftObjectPtr<USoundBase>* Rec = UFallArtData::Get()->Sounds.Find(Names[(int32)Kind]))
	{
		if (USoundBase* S = Rec->LoadSynchronous())
		{
			if (Location.IsZero()) UGameplayStatics::PlaySound2D(World, S, Volume, Pitch);
			else UGameplayStatics::PlaySoundAtLocation(World, S, Location, Volume, Pitch);
			return;
		}
	}

	static TMap<int32, TArray<int16>> Cache;
	static int32 Take = 0;
	const int32 Key = (int32)Kind * 8 + (Take++ % 4);
	TArray<int16>* PCM = Cache.Find(Key);
	if (!PCM)
	{
		PCM = &Cache.Add(Key);
		Render(Kind, Key, *PCM);
	}
	UFallOneShotWave* Wave = NewObject<UFallOneShotWave>(World);
	Wave->SetPCM(*PCM);

	static TStrongObjectPtr<USoundAttenuation> Atten;
	if (!Atten.IsValid())
	{
		Atten.Reset(NewObject<USoundAttenuation>());
		Atten->Attenuation.bAttenuate = true;
		Atten->Attenuation.bSpatialize = true;
		Atten->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
		Atten->Attenuation.AttenuationShapeExtents = FVector(600.f);
		Atten->Attenuation.FalloffDistance = 9000.f;
	}
	if (Location.IsZero())
	{
		UGameplayStatics::PlaySound2D(World, Wave, Volume, Pitch);
	}
	else
	{
		UGameplayStatics::PlaySoundAtLocation(World, Wave, Location, Volume, Pitch, 0.f, Atten.Get());
	}
}
