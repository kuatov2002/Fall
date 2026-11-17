#include "FreefallGameMode.h"
#include "FallPlayer.h"
#include "FallWorld.h"
#include "FallReview.h"
#include "FallIntro.h"
#include "FallAnimGallery.h"
#include "FallDebris.h"
#include "FallFX.h"
#include "FallDirector.h"
#include "EngineUtils.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/App.h"

AFreefallGameMode::AFreefallGameMode()
{
	DefaultPawnClass = AFallPlayer::StaticClass();
	HUDClass = AFallHUD::StaticClass();
}

bool AFreefallGameMode::IsGallery() const
{
	return GetWorld() && GetWorld()->GetMapName().Contains(TEXT("AnimGallery"));
}

void AFreefallGameMode::RestartPlayer(AController* NewPlayer)
{
	if (IsGallery())
	{
		return; // the gallery drives its own camera
	}
	RestartPlayerAtTransform(NewPlayer, FTransform(FVector::ZeroVector));
}

void AFreefallGameMode::StartPlay()
{
	UWorld* W = GetWorld();
	int32 Seed = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("FallSeed="), Seed)) { FMath::RandInit(Seed); FMath::SRandInit(Seed); }
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (IsGallery())
	{
		W->SpawnActor<AFallAnimGallery>(AFallAnimGallery::StaticClass(), FTransform::Identity, P);
	}
	else
	{
		W->SpawnActor<AFallPlanet>(AFallPlanet::StaticClass(), FTransform::Identity, P);
		W->SpawnActor<AFallSpeedField>(AFallSpeedField::StaticClass(), FTransform::Identity, P);
		W->SpawnActor<AFallFX>(AFallFX::StaticClass(), FTransform::Identity, P);
		W->SpawnActor<AFallDebrisSpawner>(AFallDebrisSpawner::StaticClass(), FTransform::Identity, P);
	}
	Super::StartPlay();
	if (!IsGallery() && !AFallDirector::Get(this))
	{
		W->SpawnActor<AFallDirector>(AFallDirector::StaticClass(), FTransform::Identity, P);
	}

	FString Dummy;
	if (!IsGallery() && AFallIntro::ShouldPlay())
	{
		W->SpawnActor<AFallIntro>(AFallIntro::StaticClass(), FTransform::Identity, P);
	}
	if (!IsGallery() && FParse::Value(FCommandLine::Get(), TEXT("FallReview="), Dummy))
	{
		W->SpawnActor<AFallReview>(AFallReview::StaticClass(), FTransform::Identity, P);
	}
}

void AFallHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas) return;
	const float Dt = FMath::Min(FApp::GetDeltaTime(), 0.1);
	FadeAlpha = FMath::Clamp(FadeAlpha - FadeSpeed * Dt, 0.f, 1.f);
	if (FadeAlpha > 0.001f)
	{
		FLinearColor C = FadeColor;
		C.A = FadeAlpha;
		DrawRect(C, 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
	}
	if (!bVisible || FadeAlpha > 0.6f) return;
	// No crosshair in the cutscene or under the canopy: nothing to shoot there.
	if (const AFallPlayer* PP = Cast<AFallPlayer>(GetOwningPawn())) if (PP->bInIntro || PP->bCanopy) return;
	Spread = FMath::FInterpTo(Spread, 0.f, Dt, 8.f);
	HitPulse = FMath::FInterpTo(HitPulse, 0.f, Dt, 10.f);
	KillPulse = FMath::FInterpTo(KillPulse, 0.f, Dt, 5.f);

	const float S = Canvas->ClipY / 1080.f;
	const FVector2D C(Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f);
	const float Gap = (7.f + Spread * 14.f) * S;
	const float Len = 9.f * S;
	const FLinearColor Col = FMath::Lerp(FLinearColor(1.f, 1.f, 1.f, 0.85f), FLinearColor(1.f, 0.25f, 0.1f, 1.f), FMath::Max(HitPulse, KillPulse));
	const float Th = 2.5f * S;
	auto Line = [&](FVector2D A, FVector2D B)
	{
		// Axis-aligned ticks as filled rectangles (crisp at any resolution), on a dark underlay for white clouds.
		const FVector2D Mn(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y)), Mx(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y));
		const bool bH = FMath::Abs(A.X - B.X) > FMath::Abs(A.Y - B.Y);
		const float W = bH ? Mx.X - Mn.X : Th, H = bH ? Th : Mx.Y - Mn.Y;
		const float X = bH ? Mn.X : Mn.X - Th * 0.5f, Y = bH ? Mn.Y - Th * 0.5f : Mn.Y;
		DrawRect(FLinearColor(0, 0, 0, 0.4f), FMath::RoundToFloat(X - 1.f), FMath::RoundToFloat(Y - 1.f), FMath::RoundToFloat(W + 2.f), FMath::RoundToFloat(H + 2.f));
		DrawRect(Col, FMath::RoundToFloat(X), FMath::RoundToFloat(Y), FMath::RoundToFloat(W), FMath::RoundToFloat(H));
	};
	Line(C + FVector2D(Gap, 0), C + FVector2D(Gap + Len, 0));
	Line(C - FVector2D(Gap, 0), C - FVector2D(Gap + Len, 0));
	Line(C + FVector2D(0, Gap), C + FVector2D(0, Gap + Len));
	Line(C - FVector2D(0, Gap), C - FVector2D(0, Gap + Len));
	if (HitPulse > 0.05f)
	{
		// Hit confirmation: short diagonal ticks.
		const float D = (Gap + 2.f) * 0.8f, E = D + 7.f * S * HitPulse;
		for (int32 sx = -1; sx <= 1; sx += 2)
			for (int32 sy = -1; sy <= 1; sy += 2)
				DrawLine(C.X + sx * D, C.Y + sy * D, C.X + sx * E, C.Y + sy * E, Col, Th);
	}
}
