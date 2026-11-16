#include "FreefallGameMode.h"
#include "FallPlayer.h"
#include "FallWorld.h"
#include "FallReview.h"
#include "FallAnimGallery.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

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
	}
	Super::StartPlay();

	FString Dummy;
	if (!IsGallery() && FParse::Value(FCommandLine::Get(), TEXT("FallReview="), Dummy))
	{
		W->SpawnActor<AFallReview>(AFallReview::StaticClass(), FTransform::Identity, P);
	}
}

void AFallHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas || !bVisible) return;
	const float Dt = GetWorld()->GetDeltaSeconds();
	Spread = FMath::FInterpTo(Spread, 0.f, Dt, 8.f);
	HitPulse = FMath::FInterpTo(HitPulse, 0.f, Dt, 10.f);
	KillPulse = FMath::FInterpTo(KillPulse, 0.f, Dt, 5.f);

	const float S = Canvas->ClipY / 1080.f;
	const FVector2D C(Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f);
	const float Gap = (7.f + Spread * 14.f) * S;
	const float Len = 9.f * S;
	const FLinearColor Col = FMath::Lerp(FLinearColor(1.f, 1.f, 1.f, 0.85f), FLinearColor(1.f, 0.25f, 0.1f, 1.f), FMath::Max(HitPulse, KillPulse));
	const float Th = 2.f * S;
	auto Line = [&](FVector2D A, FVector2D B)
	{
		// Dark outline keeps the crosshair readable against white clouds.
		DrawLine(A.X + 1, A.Y + 1, B.X + 1, B.Y + 1, FLinearColor(0, 0, 0, 0.5f), Th + 1.f);
		DrawLine(A.X, A.Y, B.X, B.Y, Col, Th);
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
				Line(C + FVector2D(sx * D, sy * D), C + FVector2D(sx * E, sy * E));
	}
}
