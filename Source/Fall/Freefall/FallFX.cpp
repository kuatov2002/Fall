#include "FallFX.h"
#include "FallWorld.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "EngineUtils.h"

AFallFX::AFallFX()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

AFallFX* AFallFX::Get(const UObject* WorldContext)
{
	UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!W) return nullptr;
	for (TActorIterator<AFallFX> It(W); It; ++It) return *It;
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return W->SpawnActor<AFallFX>(AFallFX::StaticClass(), FTransform::Identity, P);
}

void AFallFX::BeginPlay()
{
	Super::BeginPlay();
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* SpriteMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Freefall/Materials/M_FF_Sprite.M_FF_Sprite"));
	UMaterialInterface* AddMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Freefall/Materials/M_FF_Additive.M_FF_Additive"));

	auto Make = [&](UStaticMesh* Mesh, UMaterialInterface* Mat, UMaterialInstanceDynamic*& OutMID)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(Mesh);
		C->SetupAttachment(RootComponent);
		C->SetUsingAbsoluteLocation(true);
		C->SetUsingAbsoluteRotation(true);
		C->SetUsingAbsoluteScale(true);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->SetVisibility(false);
		C->RegisterComponent();
		OutMID = Mat ? UMaterialInstanceDynamic::Create(Mat, this) : nullptr;
		if (OutMID) C->SetMaterial(0, OutMID);
		Owned.Add(C);
		return C;
	};
	Sprites.SetNum(140);
	for (FPart& P : Sprites) P.Comp = Make(Plane, SpriteMat, P.MID);
	Flashes.SetNum(24);
	for (FPart& P : Flashes) P.Comp = Make(Sphere, AddMat, P.MID);
	SparkPool.SetNum(120);
	for (FPart& P : SparkPool) P.Comp = Make(Cube, AddMat, P.MID);
	Lines.SetNum(48);
	for (FLine& L : Lines) L.Comp = Make(Cube, AddMat, L.MID);
}

AFallFX::FPart* AFallFX::Take(TArray<FPart>& Pool, int32& Cursor)
{
	if (Pool.Num() == 0) return nullptr;
	for (int32 i = 0; i < Pool.Num(); ++i)
	{
		FPart& P = Pool[(Cursor + i) % Pool.Num()];
		if (!P.bActive) { Cursor = (Cursor + i + 1) % Pool.Num(); return &P; }
	}
	FPart& P = Pool[Cursor]; // steal the oldest slot
	Cursor = (Cursor + 1) % Pool.Num();
	return &P;
}

AFallFX::FLine* AFallFX::TakeLine()
{
	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		FLine& L = Lines[(LineCursor + i) % Lines.Num()];
		if (!L.bActive) { LineCursor = (LineCursor + i + 1) % Lines.Num(); return &L; }
	}
	return nullptr;
}

void AFallFX::Sprite(const FVector& Pos, const FVector& Vel, float Size0, float Size1, float Life, const FLinearColor& Color, float Opacity, bool bAir, float Emissive)
{
	FPart* P = Take(Sprites, SpriteCursor);
	if (!P || !P->Comp) return;
	P->Pos = Pos; P->Vel = Vel; P->Age = 0.f; P->Life = Life; P->Size0 = Size0; P->Size1 = Size1;
	P->Color = Color; P->Opacity = Opacity; P->bAir = bAir; P->bActive = true; P->Emissive = Emissive;
	if (P->MID)
	{
		P->MID->SetVectorParameterValue("Color", Color);
		P->MID->SetScalarParameterValue("Seed", FMath::FRandRange(0.f, 100.f));
		P->MID->SetScalarParameterValue("Emissive", Emissive);
		P->MID->SetScalarParameterValue("FadeDistance", Size0 * 0.5f);
		P->MID->SetScalarParameterValue("NoiseSpeed", 0.6f);
	}
	P->Comp->SetVisibility(true);
}

void AFallFX::Flash(const FVector& Pos, float Size, const FLinearColor& Color, float Life)
{
	FPart* P = Take(Flashes, FlashCursor);
	if (!P || !P->Comp) return;
	P->Pos = Pos; P->Vel = FVector::ZeroVector; P->Age = 0.f; P->Life = Life; P->Size0 = Size; P->Size1 = Size * 1.6f;
	P->Color = Color; P->Opacity = 1.f; P->bAir = false; P->bActive = true;
	if (P->MID) P->MID->SetVectorParameterValue("Color", Color);
	P->Comp->SetVisibility(true);
}

void AFallFX::Sparks(const FVector& Pos, const FVector& Dir, int32 Count, const FLinearColor& Color, float Speed)
{
	for (int32 i = 0; i < Count; ++i)
	{
		FPart* P = Take(SparkPool, SparkCursor);
		if (!P || !P->Comp) return;
		const FVector D = (Dir.GetSafeNormal() + FMath::VRand() * 0.8f).GetSafeNormal();
		P->Pos = Pos; P->Vel = D * Speed * FMath::FRandRange(0.4f, 1.2f); P->Age = 0.f; P->Life = FMath::FRandRange(0.15f, 0.4f);
		P->Size0 = FMath::FRandRange(2.f, 4.f); P->Size1 = 1.f; P->Color = Color; P->Opacity = 1.f; P->bAir = true; P->bActive = true;
		if (P->MID) P->MID->SetVectorParameterValue("Color", Color);
		P->Comp->SetVisibility(true);
	}
}

void AFallFX::Tracer(const FVector& A, const FVector& B, const FLinearColor& Color, float Width)
{
	FLine* L = TakeLine();
	if (!L) return;
	L->A = A; L->B = B; L->Age = 0.f; L->Life = 0.07f; L->Width = Width; L->bActive = true; L->bPersistent = false; L->Intensity = 3.f;
	if (L->MID) L->MID->SetVectorParameterValue("Color", Color);
	L->Comp->SetVisibility(true);
}

int32 AFallFX::Beam(int32 Handle, const FVector& A, const FVector& B, const FLinearColor& Color, float Width, bool bVisible)
{
	FLine* L = Lines.IsValidIndex(Handle) ? &Lines[Handle] : nullptr;
	if (!L)
	{
		if (!bVisible) return INDEX_NONE;
		L = TakeLine();
		if (!L) return INDEX_NONE;
		Handle = (int32)(L - Lines.GetData());
	}
	L->bActive = bVisible;
	L->bPersistent = true;
	L->A = A; L->B = B; L->Width = Width; L->Intensity = 2.5f;
	if (L->MID) L->MID->SetVectorParameterValue("Color", Color);
	L->Comp->SetVisibility(bVisible);
	return bVisible ? Handle : INDEX_NONE;
}

void AFallFX::Explosion(const FVector& Pos, float Scale)
{
	Flash(Pos, 900.f * Scale, FLinearColor(1.f, 0.6f, 0.25f), 0.25f);
	for (int32 i = 0; i < 10; ++i)
	{
		Sprite(Pos + FMath::VRand() * 200.f * Scale, FMath::VRand() * 900.f * Scale, 500.f * Scale, 1300.f * Scale, FMath::FRandRange(0.5f, 0.9f), FLinearColor(1.f, 0.45f, 0.1f), 0.9f, true, 6.f);
	}
	for (int32 i = 0; i < 12; ++i)
	{
		Sprite(Pos + FMath::VRand() * 300.f * Scale, FMath::VRand() * 500.f * Scale, 700.f * Scale, 2200.f * Scale, FMath::FRandRange(2.f, 4.f), FLinearColor(0.08f, 0.07f, 0.07f), 0.85f, true, 1.f);
	}
	Sparks(Pos, FVector::UpVector, 30, FLinearColor(1.f, 0.7f, 0.3f), 5000.f * Scale);
}

void AFallFX::Tick(float Dt)
{
	Super::Tick(Dt);
	const UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this);
	const FVector Air = FW ? FVector(-FW->PlayerVelocityH.X, -FW->PlayerVelocityH.Y, FW->PlayerFallSpeed) : FVector::ZeroVector;
	APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0);
	const FVector C = Cam ? Cam->GetCameraLocation() : FVector::ZeroVector;

	for (FPart& P : Sprites)
	{
		if (!P.bActive) continue;
		P.Age += Dt;
		if (P.Age >= P.Life) { P.bActive = false; P.Comp->SetVisibility(false); continue; }
		P.Vel *= FMath::Exp(-1.5f * Dt); // drag on the initial velocity
		P.Pos += (P.Vel + (P.bAir ? Air : FVector::ZeroVector)) * Dt;
		const float X = P.Age / P.Life;
		const float S = FMath::Lerp(P.Size0, P.Size1, FMath::Sqrt(X));
		const FVector D = P.Pos - C;
		P.Comp->SetWorldLocation(P.Pos);
		P.Comp->SetWorldRotation(FRotationMatrix::MakeFromZ(-D.GetSafeNormal()).Rotator());
		P.Comp->SetWorldScale3D(FVector(S / 100.f, S / 100.f, 1.f));
		if (P.MID)
		{
			const float Fade = FMath::Min(1.f, X * 8.f) * (1.f - X * X) * FMath::SmoothStep(80.f, 600.f, (float)D.Size());
			P.MID->SetScalarParameterValue("Opacity", P.Opacity * Fade);
			P.MID->SetScalarParameterValue("Emissive", P.Emissive * FMath::Lerp(1.f, 0.4f, X));
		}
	}
	for (FPart& P : Flashes)
	{
		if (!P.bActive) continue;
		P.Age += Dt;
		if (P.Age >= P.Life) { P.bActive = false; P.Comp->SetVisibility(false); continue; }
		const float X = P.Age / P.Life;
		P.Comp->SetWorldLocation(P.Pos);
		P.Comp->SetWorldScale3D(FVector(FMath::Lerp(P.Size0, P.Size1, X) / 100.f));
		if (P.MID) P.MID->SetScalarParameterValue("Intensity", 12.f * (1.f - X));
	}
	for (FPart& P : SparkPool)
	{
		if (!P.bActive) continue;
		P.Age += Dt;
		if (P.Age >= P.Life) { P.bActive = false; P.Comp->SetVisibility(false); continue; }
		P.Pos += (P.Vel + Air) * Dt;
		const FVector V = P.Vel + Air * 0.2f;
		const float Len = FMath::Clamp((float)V.Size() * 0.02f, 5.f, 80.f);
		P.Comp->SetWorldLocation(P.Pos);
		P.Comp->SetWorldRotation(FRotationMatrix::MakeFromZ(V.GetSafeNormal()).Rotator());
		P.Comp->SetWorldScale3D(FVector(P.Size0 / 100.f, P.Size0 / 100.f, Len / 100.f));
		if (P.MID) P.MID->SetScalarParameterValue("Intensity", 20.f * (1.f - P.Age / P.Life));
	}
	for (FLine& L : Lines)
	{
		if (!L.bActive) continue;
		if (!L.bPersistent)
		{
			L.Age += Dt;
			if (L.Age >= L.Life) { L.bActive = false; L.Comp->SetVisibility(false); continue; }
		}
		const FVector Mid = (L.A + L.B) * 0.5f;
		const FVector Dir = L.B - L.A;
		const float Dist = FMath::Max((float)FMath::PointDistToSegment(C, L.A, L.B), 50.f);
		const float W = FMath::Max(L.Width, Dist * 0.0012f); // stays at least ~1 px wide far away
		L.Comp->SetWorldLocation(Mid);
		L.Comp->SetWorldRotation(FRotationMatrix::MakeFromZ(Dir.GetSafeNormal()).Rotator());
		L.Comp->SetWorldScale3D(FVector(W / 100.f, W / 100.f, Dir.Size() / 100.f));
		if (L.MID) L.MID->SetScalarParameterValue("Intensity", L.bPersistent ? L.Intensity : L.Intensity * (1.f - L.Age / L.Life));
	}
}
