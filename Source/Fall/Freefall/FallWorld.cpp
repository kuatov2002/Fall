#include "FallWorld.h"
#include "FallArt.h"
#include "FallPlayer.h"
#include "ProceduralMeshComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"

// ---------------------------------------------------------------- subsystem

UFallWorldSubsystem* UFallWorldSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	return W ? W->GetSubsystem<UFallWorldSubsystem>() : nullptr;
}

void UFallWorldSubsystem::NoteEvent(FName Name)
{
	Events.Add({FallTime, Name});
}

FVector UFallWorldSubsystem::AirVelocityFor(float OwnFallSpeed, const FVector& OwnVelH) const
{
	return FVector(-OwnVelH.X, -OwnVelH.Y, OwnFallSpeed);
}

float UFallWorldSubsystem::GroundHeightAt(const FVector& WorldPos) const
{
	return AFallPlanet::HeightAt(WorldPos.X / 100.f, WorldPos.Y / 100.f);
}

float UFallWorldSubsystem::HeightAboveGround() const
{
	const FVector P = Player.IsValid() ? Player->GetActorLocation() : FVector::ZeroVector;
	return Altitude - GroundHeightAt(P);
}

// ---------------------------------------------------------------- mover

UFallMoverComponent::UFallMoverComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_DuringPhysics; // after the player updated its fall speed
}

void UFallMoverComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AActor* Owner = GetOwner();
	const UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this);
	if (!Owner || !FW) return;
	if (FallAccel > 0.f) FallSpeed = FMath::Min(FallSpeed + FallAccel * DeltaTime, TerminalSpeed);
	const FVector Delta(VelocityH.X * DeltaTime, VelocityH.Y * DeltaTime, (FW->PlayerFallSpeed - FallSpeed) * DeltaTime);
	Owner->AddActorWorldOffset(Delta);
	if (!Spin.IsZero()) Owner->AddActorLocalRotation(Spin * DeltaTime);
	const FVector P = FW->Player.IsValid() ? FW->Player->GetActorLocation() : FVector::ZeroVector;
	if (FMath::Abs(Owner->GetActorLocation().Z - P.Z) > KillDistance || FVector::DistSquared2D(Owner->GetActorLocation(), P) > FMath::Square(KillDistance * 2.f))
	{
		Owner->Destroy();
	}
}

// ---------------------------------------------------------------- planet

namespace
{
	float Ridged(float X, float Y)
	{
		float Sum = 0.f, Amp = 1.f, Freq = 1.f / 9000.f, Norm = 0.f;
		for (int32 o = 0; o < 6; ++o)
		{
			const float N = 1.f - FMath::Abs(FMath::PerlinNoise2D(FVector2D(X * Freq + o * 17.3f, Y * Freq - o * 9.1f)));
			Sum += N * N * Amp;
			Norm += Amp;
			Amp *= 0.5f;
			Freq *= 2.03f;
		}
		return Sum / Norm;
	}

	FLinearColor GroundColor(float H, float Slope, float Noise)
	{
		const FLinearColor Meadow(0.10f, 0.16f, 0.05f), Forest(0.035f, 0.07f, 0.03f), Rock(0.16f, 0.14f, 0.12f), Snow(0.82f, 0.85f, 0.9f), Scree(0.24f, 0.22f, 0.2f);
		FLinearColor C = FMath::Lerp(Meadow, Forest, FMath::SmoothStep(380.f, 650.f, H + Noise * 120.f));
		C = FMath::Lerp(C, Scree, FMath::SmoothStep(1500.f, 1900.f, H + Noise * 200.f));
		C = FMath::Lerp(C, Rock * (0.75f + 0.5f * FMath::Abs(Noise)), FMath::SmoothStep(0.3f, 0.6f, Slope));
		C = FMath::Lerp(C, Snow, FMath::SmoothStep(2550.f, 2850.f, H + Noise * 300.f) * (1.f - FMath::SmoothStep(0.45f, 0.75f, Slope)));
		return C;
	}
}

float AFallPlanet::HeightAt(float Xm, float Ym)
{
	// Domain warp: bends the ridge lines so the same shapes do not repeat.
	const float Wx = FMath::PerlinNoise2D(FVector2D(Xm / 6000.f + 3.1f, Ym / 6000.f - 1.7f)) * 2500.f;
	const float Wy = FMath::PerlinNoise2D(FVector2D(Xm / 6000.f - 5.3f, Ym / 6000.f + 2.9f)) * 2500.f;
	const float R = Ridged(Xm + Wx, Ym + Wy);
	const float Mountains = 250.f + 2900.f * FMath::Pow(R, 1.7f);
	// Landing valley around the origin: low rolling meadow.
	const float Dist = FMath::Sqrt(Xm * Xm + Ym * Ym);
	const float Valley = 320.f + 35.f * FMath::PerlinNoise2D(FVector2D(Xm / 700.f, Ym / 700.f)) + 12.f * FMath::PerlinNoise2D(FVector2D(Xm / 120.f, Ym / 120.f));
	return FMath::Lerp(Valley, Mountains, FMath::SmoothStep(1800.f, 7000.f, Dist));
}

AFallPlanet::AFallPlanet()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Anchor = CreateDefaultSubobject<USceneComponent>(TEXT("Anchor"));
	Anchor->SetupAttachment(RootComponent);
	Anchor->SetUsingAbsoluteLocation(true);

	Atmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("Atmosphere"));
	Atmosphere->SetupAttachment(Anchor);
	Atmosphere->TransformMode = ESkyAtmosphereTransformMode::PlanetTopAtComponentTransform;
	Atmosphere->AerialPespectiveViewDistanceScale = 1.2f;
	// The planet beyond the 80 km terrain: dark olive like the land, so it fades into haze instead of glowing pale blue.
	Atmosphere->GroundAlbedo = FColor(34, 32, 22);

	Clouds = CreateDefaultSubobject<UVolumetricCloudComponent>(TEXT("Clouds"));
	Clouds->SetupAttachment(Anchor);

	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(RootComponent);
	Sun->SetUsingAbsoluteRotation(true);
	// Golden hour: the sun 7 degrees above the horizon, off to the side of the usual view.
	Sun->SetWorldRotation(FRotator(-7.f, 35.f, 0.f));
	Sun->SetIntensity(9.f);
	Sun->SetLightColor(FLinearColor(1.f, 0.74f, 0.5f));
	Sun->SetAtmosphereSunLight(true);
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->bCastCloudShadows = true;

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(RootComponent);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->bRealTimeCapture = true;

	Haze = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Haze"));
	Haze->SetupAttachment(Anchor);
	// Haze reaches up past the cloud deck: the distant horizon blends into warm air instead of a hard seam.
	Haze->SetFogDensity(0.025f);
	Haze->SetFogHeightFalloff(0.2f);
	Haze->SetFogMaxOpacity(0.92f);
	Haze->SetFogInscatteringColor(FLinearColor(0.32f, 0.42f, 0.62f));
	Haze->SetDirectionalInscatteringExponent(6.f);
	Haze->SetDirectionalInscatteringColor(FLinearColor(1.f, 0.55f, 0.28f));
	Haze->SetVolumetricFog(false);

	Terrain = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Terrain"));
	Terrain->SetupAttachment(Anchor);
	Terrain->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TerrainFar = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TerrainFar"));
	TerrainFar->SetupAttachment(Anchor);
	TerrainFar->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TerrainDetail = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TerrainDetail"));
	TerrainDetail->SetupAttachment(Anchor);
	TerrainDetail->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Trees = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Trees"));
	Trees->SetupAttachment(Anchor);
	Trees->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AFallPlanet::BeginPlay()
{
	Super::BeginPlay();
	if (UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this)) FW->Planet = this;
	if (UMaterialInterface* CloudMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst.m_SimpleVolumetricCloud_Inst")))
	{
		Clouds->SetMaterial(CloudMat);
		if (UMaterialInterface* Broken = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Freefall/Materials/MI_FF_Clouds.MI_FF_Clouds"))) Clouds->SetMaterial(Broken);
	}
	Clouds->SetLayerBottomAltitude(CloudDeck.X);
	Clouds->SetLayerHeight(CloudDeck.Y);
	BuildTerrain();
	Tick(0.f);
}

void AFallPlanet::BuildTerrain()
{
	UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Freefall/Materials/M_FF_Terrain.M_FF_Terrain"));
	const float DetailHalf = 2600.f; // metres
	auto Build = [&](UProceduralMeshComponent* C, float HalfSize, int32 Cells, bool bCoarse)
	{
		const int32 N = Cells + 1;
		const float Step = HalfSize * 2.f / Cells;
		TArray<FVector> V; TArray<FVector> Nrm; TArray<FLinearColor> Col; TArray<int32> Tri; TArray<FVector2D> UV;
		V.Reserve(N * N); Nrm.Reserve(N * N); Col.Reserve(N * N);
		auto H = [&](float X, float Y)
		{
			float Z = HeightAt(X, Y);
			if (bCoarse)
			{
				// Sink the coarse mesh under the detail patch so the two never z-fight.
				const float Inside = 1.f - FMath::Max(FMath::SmoothStep(DetailHalf - 600.f, DetailHalf, FMath::Abs(X)), FMath::SmoothStep(DetailHalf - 600.f, DetailHalf, FMath::Abs(Y)));
				Z -= 40.f * Inside;
				if (C == TerrainFar)
				{
					// The far ring sinks under the 80 km mesh wherever that one exists.
					const float InMid = 1.f - FMath::Max(FMath::SmoothStep(38000.f, 40000.f, FMath::Abs(X)), FMath::SmoothStep(38000.f, 40000.f, FMath::Abs(Y)));
					Z -= 150.f * InMid;
				}
			}
			// Earth curvature, so the far terrain meets the atmosphere's horizon instead of floating above it.
			return Z - (X * X + Y * Y) / (2.f * 6371000.f);
		};
		for (int32 y = 0; y < N; ++y)
		{
			for (int32 x = 0; x < N; ++x)
			{
				const float X = -HalfSize + x * Step, Y = -HalfSize + y * Step;
				const float Z = H(X, Y);
				const float E = Step * 0.5f;
				const FVector G(H(X - E, Y) - H(X + E, Y), H(X, Y - E) - H(X, Y + E), 2.f * E);
				const FVector Nn = G.GetSafeNormal();
				V.Add(FVector(X, Y, Z) * 100.f);
				Nrm.Add(Nn);
				const float Noise = FMath::PerlinNoise2D(FVector2D(X / 300.f, Y / 300.f));
				Col.Add(GroundColor(Z, 1.f - Nn.Z * Nn.Z * 1.2f, Noise));
				UV.Add(FVector2D(X, Y) / 100.f);
			}
		}
		for (int32 y = 0; y < Cells; ++y)
		{
			for (int32 x = 0; x < Cells; ++x)
			{
				const int32 a = y * N + x, b = a + 1, c = a + N, d = c + 1;
				Tri.Append({a, c, b, b, c, d});
			}
		}
		C->CreateMeshSection_LinearColor(0, V, Tri, Nrm, UV, Col, {}, false);
		if (Mat) C->SetMaterial(0, Mat);
	};
	Build(Terrain, 40000.f, 330, true);
	Build(TerrainFar, 330000.f, 260, true); // out towards the horizon (340 km from 9 km up); the planet beyond is haze
	Build(TerrainDetail, DetailHalf, 208, false);

	// Forest on the valley slopes.
	const UFallArtData* Art = UFallArtData::Get();
	if (UStaticMesh* Tree = Art->Trees.Num() ? Art->Trees[0].LoadSynchronous() : nullptr)
	{
		Trees->SetStaticMesh(Tree);
		const float TreeH = FMath::Max(Tree->GetBoundingBox().GetSize().Z, 100.f) / 100.f; // metres
		FRandomStream R(7);
		TArray<FTransform> Xf;
		for (int32 i = 0; i < 9000; ++i)
		{
			const float X = R.FRandRange(-DetailHalf, DetailHalf), Y = R.FRandRange(-DetailHalf, DetailHalf);
			const float Clump = FMath::PerlinNoise2D(FVector2D(X / 400.f, Y / 400.f));
			if (Clump < 0.05f || FMath::Sqrt(X * X + Y * Y) < 250.f) continue;
			const float Hgt = R.FRandRange(9.f, 20.f);
			const float Sc = Hgt / TreeH;
			Xf.Add(FTransform(FRotator(0, R.FRandRange(0, 360), 0), FVector(X, Y, HeightAt(X, Y)) * 100.f, FVector(Sc)));
		}
		Trees->AddInstances(Xf, false);
	}
}

void AFallPlanet::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this);
	const float Alt = FW ? FW->Altitude : 9000.f;
	Anchor->SetWorldLocation(FVector(0.f, 0.f, -Alt * 100.f));
}

// ---------------------------------------------------------------- speed field

AFallSpeedField::AFallSpeedField()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Streaks = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Streaks"));
	Streaks->SetupAttachment(RootComponent);
	Streaks->SetUsingAbsoluteLocation(true);
	Streaks->SetUsingAbsoluteRotation(true);
	Streaks->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Streaks->SetCastShadow(false);
	Specks = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Specks"));
	Specks->SetupAttachment(RootComponent);
	Specks->SetUsingAbsoluteLocation(true);
	Specks->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Specks->SetCastShadow(false);
	Rng.Initialize(1234);
}

void AFallSpeedField::BeginPlay()
{
	Super::BeginPlay();
	const UFallArtData* Art = UFallArtData::Get();
	UStaticMesh* Cube = Art->FxBeam.LoadSynchronous();
	UStaticMesh* Sphere = Art->FxBall.LoadSynchronous();
	UMaterialInterface* Add = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Freefall/Materials/M_FF_Streak.M_FF_Streak"));
	UMaterialInterface* Cloud = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Freefall/Materials/M_FF_Sprite.M_FF_Sprite"));
	UStaticMesh* Plane = Art->FxQuad.LoadSynchronous();

	Streaks->SetStaticMesh(Cube);
	if (Add)
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Add, this);
		MID->SetVectorParameterValue("Color", FLinearColor(0.85f, 0.9f, 1.f));
		MID->SetScalarParameterValue("Intensity", 0.35f);
		Streaks->SetMaterial(0, MID);
	}
	StreakPos.SetNum(NumStreaks);
	TArray<FTransform> Xf;
	for (int32 i = 0; i < NumStreaks; ++i)
	{
		const float R = FMath::Sqrt(Rng.FRandRange(FMath::Square(StreakRadius.X), FMath::Square(StreakRadius.Y)));
		const float A = Rng.FRandRange(0.f, 2.f * PI);
		StreakPos[i] = FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, Rng.FRandRange(-StreakHalfHeight, StreakHalfHeight));
		Xf.Add(FTransform(StreakPos[i]));
	}
	Streaks->AddInstances(Xf, false);

	Specks->SetStaticMesh(Cube);
	if (Add)
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Add, this);
		MID->SetVectorParameterValue("Color", FLinearColor(1.f, 0.82f, 0.62f));
		MID->SetScalarParameterValue("Intensity", 0.9f);
		Specks->SetMaterial(0, MID);
	}
	SpeckPos.SetNum(NumSpecks);
	TArray<FTransform> Sx;
	for (int32 i = 0; i < NumSpecks; ++i)
	{
		SpeckPos[i] = FVector(Rng.FRandRange(-700.f, 700.f), Rng.FRandRange(-700.f, 700.f), Rng.FRandRange(-1500.f, 1500.f));
		Sx.Add(FTransform(SpeckPos[i]));
	}
	Specks->AddInstances(Sx, false);

	UMaterialInterface* ScrapMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Freefall/Materials/MI_Scrap.MI_Scrap"));
	for (int32 i = 0; i < NumScraps; ++i)
	{
		FScrap S;
		S.Comp = NewObject<UStaticMeshComponent>(this);
		S.Comp->SetStaticMesh(Plane);
		S.Comp->SetupAttachment(RootComponent);
		S.Comp->SetUsingAbsoluteLocation(true);
		S.Comp->SetUsingAbsoluteRotation(true);
		S.Comp->SetUsingAbsoluteScale(true);
		S.Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		S.Comp->SetCastShadow(false);
		if (ScrapMat) S.Comp->SetMaterial(0, ScrapMat);
		S.Comp->RegisterComponent();
		S.Pos = FVector(Rng.FRandRange(-2500.f, 2500.f), Rng.FRandRange(-2500.f, 2500.f), Rng.FRandRange(-6000.f, 6000.f));
		S.Spin = FRotator(Rng.FRandRange(-400.f, 400.f), Rng.FRandRange(-400.f, 400.f), Rng.FRandRange(-400.f, 400.f));
		S.Drag = Rng.FRandRange(0.6f, 1.f);
		S.Comp->SetWorldScale3D(FVector(Rng.FRandRange(0.15f, 0.6f), Rng.FRandRange(0.1f, 0.4f), 1.f));
		Scraps.Add(S);
	}

	for (int32 i = 0; i < 48; ++i)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(Plane);
		C->SetUsingAbsoluteRotation(true);
		C->SetUsingAbsoluteScale(true);
		C->SetupAttachment(RootComponent);
		C->SetUsingAbsoluteLocation(true);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->SetVisibility(false);
		C->RegisterComponent();
		FPuff P;
		P.Comp = C;
		if (Cloud)
		{
			P.MID = UMaterialInstanceDynamic::Create(Cloud, this);
			C->SetMaterial(0, P.MID);
		}
		Puffs.Add(P);
		PuffComps.Add(C);
	}
}

void AFallSpeedField::Burst(int32 Count)
{
	for (int32 i = 0; i < Count; ++i) ForceClose = 2;
	SinceClose = 0.f;
}

void AFallSpeedField::Tick(float Dt)
{
	Super::Tick(Dt);
	UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this);
	APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (!FW || !Cam || Dt <= 0.f) return;
	const FVector C = Cam->GetCameraLocation();
	// Standing on the deck in the cutscene: no airflow to show.
	const AFallPlayer* IntroP = Cast<AFallPlayer>(FW->Player.Get());
	const bool bStill = IntroP && IntroP->bInIntro;
	Streaks->SetVisibility(!bStill); Specks->SetVisibility(!bStill);
	for (FScrap& Sc : Scraps) Sc.Comp->SetVisibility(!bStill);
	const FVector Rel(-FW->PlayerVelocityH.X, -FW->PlayerVelocityH.Y, FW->PlayerFallSpeed); // air relative to the player
	const float Speed = Rel.Size();
	const float Len = FMath::Max(Speed * StreakBlur, 50.f);
	const FQuat Align = FQuat::FindBetweenNormals(FVector::UpVector, Rel.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector));

	TArray<FTransform> Xf;
	Xf.SetNum(NumStreaks);
	for (int32 i = 0; i < NumStreaks; ++i)
	{
		FVector& P = StreakPos[i];
		P += Rel * Dt;
		if (P.Z > StreakHalfHeight || FVector2D(P.X, P.Y).Size() > StreakRadius.Y * 1.3f)
		{
			const float R = FMath::Sqrt(Rng.FRandRange(FMath::Square(StreakRadius.X), FMath::Square(StreakRadius.Y)));
			const float A = Rng.FRandRange(0.f, 2.f * PI);
			P = FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, -StreakHalfHeight + Rng.FRandRange(0.f, 400.f));
		}
		if (FW->Player.IsValid())
		{
			const FVector ToBody = (C + P) - FW->Player->GetActorLocation();
			if (FVector2D(ToBody.X, ToBody.Y).Size() < 260.f) P.Z = StreakHalfHeight + 1.f; // respawn next frame, away from the body
		}
		const float Dist = P.Size();
		const float Thick = 0.008f + Dist * 0.000006f;
		Xf[i] = FTransform(Align, C + P, FVector(Thick, Thick, Len / 100.f));
	}
	if (Xf.Num()) Streaks->BatchUpdateInstancesTransforms(0, Xf, true, true);

	// Specks: tiny bright streaks 1.5-7 m from the lens. Their number and length follow the speed, so a still frame
	// at the top of a dive reads as fast.
	const float SpeedN = FMath::Clamp((Speed - 3500.f) / 5500.f, 0.f, 1.f);
	const int32 Active = FMath::RoundToInt(FMath::Lerp(NumSpecks * 0.3f, (float)NumSpecks, SpeedN));
	const float SpeckLen = FMath::Max(Speed * 0.006f, 12.f);
	TArray<FTransform> Sx;
	Sx.SetNum(NumSpecks);
	for (int32 i = 0; i < NumSpecks; ++i)
	{
		FVector& P = SpeckPos[i];
		P += Rel * Dt;
		if (P.Z > 1500.f || FVector2D(P.X, P.Y).Size() > 900.f)
		{
			const float A = Rng.FRandRange(0.f, 2.f * PI), R = Rng.FRandRange(150.f, 700.f);
			P = FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, -1500.f + Rng.FRandRange(0.f, 300.f));
		}
		const float Th = i < Active ? 0.006f + 0.004f * (i % 3) : 0.f;
		Sx[i] = FTransform(Align, C + P, FVector(Th, Th, i < Active ? SpeckLen / 100.f : 0.f));
	}
	Specks->BatchUpdateInstancesTransforms(0, Sx, true, true);

	// Scraps of envelope fabric: flutter, lag the air (they are falling too, just slower).
	const float ScrapShare = FMath::Clamp(1.f - FW->FallTime / 70.f, 0.25f, 1.f);
	for (int32 i = 0; i < Scraps.Num(); ++i)
	{
		FScrap& S = Scraps[i];
		S.Pos += Rel * S.Drag * Dt;
		S.Rot += S.Spin * Dt;
		if (S.Pos.Z > 6000.f || i >= FMath::CeilToInt(Scraps.Num() * ScrapShare))
		{
			S.Pos = FVector(Rng.FRandRange(-2500.f, 2500.f), Rng.FRandRange(-2500.f, 2500.f), -6000.f - Rng.FRandRange(0.f, 2000.f));
		}
		S.Comp->SetWorldLocation(C + S.Pos);
		S.Comp->SetWorldRotation(S.Rot);
	}

	// Cloud wisps rising past the camera.
	NextPuff -= Dt * FMath::Max(FW->CloudDensity, 0.f) * FMath::Clamp(FW->PlayerFallSpeed / 5500.f, 0.85f, 1.8f);
	// Never more than ~0.8 s without something passing close: the next cluster is forced close and soon.
	SinceClose += Dt;
	if (SinceClose > 0.55f && ForceClose == 0) { ForceClose = 1; NextPuff = FMath::Min(NextPuff, 0.f); }
	if (NextPuff <= 0.f || ForceClose > 1)
	{
		NextPuff = Rng.FRandRange(PuffInterval.X, PuffInterval.Y);
		// A wisp is a small cluster of sprites; every third one passes right next to the camera.
		const bool bClose = ForceClose > 0 || Rng.FRand() < 0.45f;
		ForceClose = 0;
		if (bClose) SinceClose = 0.f;
		const float BaseSize = Rng.FRandRange(PuffSize.X, PuffSize.Y) * (bClose ? 0.35f : 1.f) * FMath::Lerp(1.f, 3.f, FMath::Clamp((FW->CloudDensity - 1.f) / 2.f, 0.f, 1.f));
		const float A0 = Rng.FRandRange(0.f, 2.f * PI);
		const float R0 = bClose ? Rng.FRandRange(650.f, 1300.f) : Rng.FRandRange(BaseSize * 0.3f + 400.f, 1600.f);
		++ClusterId;
		const float Depth = bClose ? FMath::Clamp(FW->PlayerFallSpeed * 0.3f, 1500.f, 3000.f) : 9000.f + Rng.FRandRange(0.f, 3000.f);
		const FVector Lead = FW->PlayerVelocityH * (Depth / FMath::Max(FW->PlayerFallSpeed, 1000.f)); // meets the player where it will be
		const FVector Center = C + Lead + FVector(FMath::Cos(A0) * R0, FMath::Sin(A0) * R0, -Depth);
		const int32 Count = Rng.RandRange(3, 5);
		int32 Spawned = 0;
		for (FPuff& P : Puffs)
		{
			if (P.bActive) continue;
			if (Spawned >= Count) break;
			const float Size = BaseSize * Rng.FRandRange(0.45f, 1.f);
			const FVector Pos = Center + FVector(Rng.FRandRange(-0.5f, 0.5f) * BaseSize, Rng.FRandRange(-0.5f, 0.5f) * BaseSize, Rng.FRandRange(-0.8f, 0.8f) * BaseSize);
			P.Comp->SetWorldLocation(Pos);
			P.BaseScale = FVector(Size, Size * Rng.FRandRange(0.5f, 0.9f), 100.f) / 100.f;
			P.Comp->SetWorldScale3D(P.BaseScale);
			P.Comp->SetVisibility(true);
			P.Opacity = Rng.FRandRange(0.2f, 0.45f) * (bClose ? 0.6f : 1.f); // the volumetric deck does the heavy lifting
			if (P.MID)
			{
				P.MID->SetScalarParameterValue("Opacity", P.Opacity);
				P.MID->SetScalarParameterValue("FadeDistance", Size * 0.3f);
				P.MID->SetScalarParameterValue("Seed", Rng.FRandRange(0.f, 100.f));
				P.MID->SetScalarParameterValue("NoiseScale", Rng.FRandRange(2.f, 3.5f));
			}
			P.bActive = true;
			P.bCounted = false;
			P.Cluster = ClusterId;
			++Spawned;
		}
	}
	for (FPuff& P : Puffs)
	{
		if (!P.bActive) continue;
		const FVector Pos = P.Comp->GetComponentLocation() + Rel * Dt;
		P.Comp->SetWorldLocation(Pos);
		const FVector D = Pos - C;
		// Billboard towards the camera, keep a stable roll; fade out when the camera is about to pass through.
		// Billboard towards the camera, long axis along the airflow so fast wisps streak past.
		P.Comp->SetWorldRotation(FRotationMatrix::MakeFromZX(-D.GetSafeNormal(), Rel.GetSafeNormal()).Rotator());
		const float Stretch = 1.f + Speed / 14000.f;
		P.Comp->SetWorldScale3D(FVector(P.BaseScale.X * Stretch, P.BaseScale.Y / FMath::Sqrt(Stretch), 1.f));
		if (P.MID) P.MID->SetScalarParameterValue("Opacity", P.Opacity * FMath::SmoothStep(250.f, 1400.f, (float)D.Size()));
		if (!P.bCounted && D.Z > 0.f)
		{
			P.bCounted = true;
			if (FVector2D(D.X, D.Y).Size() - P.Comp->GetComponentScale().X * 50.f < NearPassRadius && P.Cluster != LastCountedCluster)
			{
				LastCountedCluster = P.Cluster;
				++NearPasses;
				++FW->NearPasses;
				FW->NoteEvent("NearPass");
			}
		}
		if (D.Z > 9000.f)
		{
			P.bActive = false;
			P.Comp->SetVisibility(false);
		}
	}
}
