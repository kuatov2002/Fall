#include "FallFX.h"
#include "Components/PointLightComponent.h"
#include "FallWorld.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "EngineUtils.h"
#include "FallArt.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

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
	const UFallArtData* Art = UFallArtData::Get();
	UStaticMesh* Plane = Art->FxQuad.LoadSynchronous();
	UStaticMesh* Cube = Art->FxBeam.LoadSynchronous();
	UStaticMesh* Sphere = Art->FxBall.LoadSynchronous();
	AirAnchor = NewObject<USceneComponent>(this, TEXT("AirAnchor"));
	AirAnchor->SetupAttachment(RootComponent);
	AirAnchor->SetUsingAbsoluteLocation(true);
	AirAnchor->SetUsingAbsoluteRotation(true);
	AirAnchor->RegisterComponent();
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
	Sprites.SetNum(280); // smoke trails need the overlap: with 140 the oldest puffs were recycled and trails broke into beads
	for (FPart& P : Sprites) P.Comp = Make(Plane, SpriteMat, P.MID);
	Flashes.SetNum(24);
	for (FPart& P : Flashes) P.Comp = Make(Sphere, AddMat, P.MID);
	for (int32 i = 0; i < Flashes.Num(); ++i)
	{
		UPointLightComponent* L = NewObject<UPointLightComponent>(this);
		L->SetupAttachment(RootComponent);
		L->SetUsingAbsoluteLocation(true);
		L->SetIntensityUnits(ELightUnits::Candelas);
		L->SetIntensity(0.f);
		L->SetCastShadows(false);
		L->SetVisibility(false); // an idle light still costs a deferred lighting pass
		L->RegisterComponent();
		FlashLights.Add(L);
	}
	SparkPool.SetNum(120);
	for (FPart& P : SparkPool) P.Comp = Make(Cube, AddMat, P.MID);
	if (UStaticMesh* Shell = Art->Casing.LoadSynchronous())
	{
		Casings.SetNum(24);
		for (FPart& P : Casings)
		{
			UMaterialInstanceDynamic* Unused = nullptr;
			P.Comp = Make(Shell, nullptr, Unused);
			P.Comp->SetMaterial(0, Shell->GetMaterial(0));
		}
	}
	if (UStaticMesh* Mag = Art->Magazine.LoadSynchronous())
	{
		Mags.SetNum(6);
		for (FPart& P : Mags)
		{
			UMaterialInstanceDynamic* Unused = nullptr;
			P.Comp = Make(Mag, nullptr, Unused);
			P.Comp->SetMaterial(0, Mag->GetMaterial(0));
		}
	}
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
		// Fire: the puff's thin edges go dark red, only the dense core burns bright.
		P->MID->SetScalarParameterValue("Shadow", Emissive > 2.f ? 0.12f : 0.55f);
	}
	P->Comp->SetVisibility(true);
}

void AFallFX::Flash(const FVector& Pos, float Size, const FLinearColor& Color, float Life)
{
	FPart* P = Take(Flashes, FlashCursor);
	if (!P || !P->Comp) return;
	P->Pos = Pos; P->Vel = FVector::ZeroVector; P->Age = 0.f; P->Life = Life; P->Size0 = Size; P->Size1 = Size * 1.6f;
	P->Color = Color; P->Opacity = 1.f; P->bAir = false; P->bActive = true;
	const int32 Idx = (int32)(P - Flashes.GetData());
	if (FlashLights.IsValidIndex(Idx))
	{
		FlashLights[Idx]->SetWorldLocation(Pos);
		FlashLights[Idx]->SetLightColor(Color);
		FlashLights[Idx]->SetAttenuationRadius(FMath::Max(Size * 4.f, 600.f));
		FlashLights[Idx]->SetVisibility(true);
	}
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
	SetSolid(*L, false);
	const FVector Dir = (B - A).GetSafeNormal();
	const float Len = (B - A).Size();
	const float Start = FMath::FRandRange(0.f, FMath::Max(Len - 900.f, 0.f));
	L->A = A + Dir * Start; L->B = A + Dir * FMath::Min(Start + 900.f, Len); L->Age = 0.f; L->Life = 0.06f; L->Width = Width; L->bActive = true; L->bPersistent = false; L->Intensity = 6.f;
	if (L->MID) L->MID->SetVectorParameterValue("Color", Color);
	L->Comp->SetVisibility(true);
}

void AFallFX::SetSolid(FLine& L, bool bSolid)
{
	if (L.bSolid == bSolid) return;
	L.bSolid = bSolid;
	if (bSolid && !L.SolidMID)
	{
		if (UMaterialInterface* Lit = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Freefall/Materials/M_FF_Lit.M_FF_Lit")))
		{
			L.SolidMID = UMaterialInstanceDynamic::Create(Lit, this);
			L.SolidMID->SetScalarParameterValue("Roughness", 0.8f);
			L.SolidMID->SetScalarParameterValue("Metallic", 0.f);
		}
	}
	L.Comp->SetMaterial(0, bSolid && L.SolidMID ? L.SolidMID : L.MID);
}

bool AFallFX::BeamEnds(int32 Handle, FVector& OutA, FVector& OutB) const
{
	if (!Lines.IsValidIndex(Handle) || !Lines[Handle].bActive) return false;
	OutA = Lines[Handle].A;
	OutB = Lines[Handle].B;
	return true;
}

int32 AFallFX::Beam(int32 Handle, const FVector& A, const FVector& B, const FLinearColor& Color, float Width, bool bVisible, bool bSolid)
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
	SetSolid(*L, bSolid);
	if (L->MID) L->MID->SetVectorParameterValue("Color", Color);
	if (L->SolidMID) L->SolidMID->SetVectorParameterValue("Color", Color);
	L->Comp->SetVisibility(bVisible);
	return bVisible ? Handle : INDEX_NONE;
}

UNiagaraComponent* AFallFX::SpawnInAir(UNiagaraSystem* System, const FVector& Pos, float Scale, const FRotator& Rot)
{
	if (!System || !AirAnchor) return nullptr;
	return UNiagaraFunctionLibrary::SpawnSystemAttached(System, AirAnchor, NAME_None, Pos, Rot, FVector(Scale), EAttachLocation::KeepWorldPosition, true, ENCPoolMethod::AutoRelease);
}

UNiagaraComponent* AFallFX::SpawnOn(UNiagaraSystem* System, USceneComponent* Parent, const FVector& WorldPos, float Scale, const FRotator& WorldRot)
{
	if (!System) return nullptr;
	if (!Parent) return SpawnInAir(System, WorldPos, Scale, WorldRot);
	return UNiagaraFunctionLibrary::SpawnSystemAttached(System, Parent, NAME_None, WorldPos, WorldRot, FVector(Scale), EAttachLocation::KeepWorldPosition, true, ENCPoolMethod::AutoRelease);
}

void AFallFX::Explosion(const FVector& Pos, float Scale)
{
	const UFallArtData* Art = UFallArtData::Get();
	Flash(Pos, 900.f * Scale, FLinearColor(1.f, 0.6f, 0.25f), 0.25f);
	// Close to the camera the burst is scaled down so its core never fills the screen.
	APlayerCameraManager* CamM = UGameplayStatics::GetPlayerCameraManager(this, 0);
	const float Near = CamM ? FMath::Clamp((float)FVector::Dist(Pos, CamM->GetCameraLocation()) / 3000.f, 0.25f, 1.f) : 1.f;
	SpawnInAir((Scale > 2.f ? Art->ExplosionLarge : Art->Explosion).LoadSynchronous(), Pos, Scale * (Scale > 2.f ? 1.2f : 1.6f) * Near, FRotator(FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f), 0.f));
	for (int32 i = 0; i < 4; ++i)
	{
		Sprite(Pos + FMath::VRand() * 200.f * Scale, FMath::VRand() * 900.f * Scale, 500.f * Scale, 1300.f * Scale, FMath::FRandRange(0.5f, 0.9f), FLinearColor(1.f, 0.45f, 0.1f), 0.9f, true, 6.f);
	}
	for (int32 i = 0; i < 12; ++i)
	{
		Sprite(Pos + FMath::VRand() * 300.f * Scale, FMath::VRand() * 500.f * Scale, 700.f * Scale, 2200.f * Scale, FMath::FRandRange(2.f, 4.f), FLinearColor(0.08f, 0.07f, 0.07f), 0.85f, true, 1.f);
	}
	Sparks(Pos, FVector::UpVector, 30, FLinearColor(1.f, 0.7f, 0.3f), 5000.f * Scale);
}

void AFallFX::Casing(const FVector& Pos, const FVector& Vel)
{
	if (!Casings.Num()) return;
	FPart* P = Take(Casings, CasingCursor);
	P->Pos = Pos; P->Vel = Vel; P->Age = 0.f; P->Life = 2.5f; P->bActive = true;
	P->Size0 = FMath::FRandRange(-1.f, 1.f); // tumble direction
	P->Comp->SetWorldScale3D(FVector(1.6f)); // readable at gameplay distance
	P->Comp->SetVisibility(true);
}

void AFallFX::DropMagazine(const FVector& Pos, const FQuat& Rot, const FVector& Vel)
{
	if (!Mags.Num()) return;
	FPart* P = Take(Mags, MagCursor);
	P->Pos = Pos; P->Vel = Vel; P->Age = 0.f; P->Life = 3.f; P->bActive = true;
	P->Size0 = FMath::FRandRange(-1.f, 1.f);
	P->Size1 = 0.f;
	P->Color = FLinearColor(Rot.X, Rot.Y, Rot.Z, Rot.W); // start orientation
	P->Comp->SetWorldScale3D(FVector(1.f));
	P->Comp->SetWorldLocationAndRotation(Pos, Rot);
	P->Comp->SetVisibility(true);
}

void AFallFX::Tick(float Dt)
{
	Super::Tick(Dt);
	const UFallWorldSubsystem* FW = UFallWorldSubsystem::Get(this);
	const FVector Air = FW ? FVector(-FW->PlayerVelocityH.X, -FW->PlayerVelocityH.Y, FW->PlayerFallSpeed) : FVector::ZeroVector;
	APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0);
	const FVector C = Cam ? Cam->GetCameraLocation() : FVector::ZeroVector;
	if (AirAnchor) AirAnchor->SetWorldLocation(AirAnchor->GetComponentLocation() + Air * Dt);

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
			const float Fade = FMath::Min(1.f, X * 8.f) * (1.f - X * X) * FMath::SmoothStep(S * 0.35f, S * 1.2f, (float)D.Size()); // a puff never fills the lens
			P.MID->SetScalarParameterValue("Opacity", P.Opacity * Fade);
			P.MID->SetScalarParameterValue("Emissive", P.Emissive * FMath::Lerp(1.f, 0.4f, X));
		}
	}
	for (FPart& P : Casings)
	{
		if (!P.bActive) continue;
		P.Age += Dt;
		if (P.Age >= P.Life) { P.bActive = false; P.Comp->SetVisibility(false); continue; }
		// The casing leaves at the ejection speed, then the airstream takes it: it rises past the shooter.
		P.Vel *= FMath::Exp(-2.5f * Dt);
		P.Pos += (P.Vel + Air * FMath::Min(P.Age * 1.5f, 1.f)) * Dt;
		P.Comp->SetWorldLocation(P.Pos);
		P.Comp->SetWorldRotation(FRotator(P.Age * 1400.f * P.Size0, P.Age * 900.f, P.Age * 600.f));
	}
	for (FPart& P : Mags)
	{
		if (!P.bActive) continue;
		P.Age += Dt;
		if (P.Age >= P.Life) { P.bActive = false; P.Comp->SetVisibility(false); continue; }
		// Falls out of the grip for a moment, then the air wins; tumbles slower than a casing (heavier, longer).
		P.Vel *= FMath::Exp(-2.f * Dt);
		P.Pos += (P.Vel + Air * FMath::Min(P.Age * 1.2f, 1.f)) * Dt;
		const FQuat Q0(P.Color.R, P.Color.G, P.Color.B, P.Color.A);
		P.Comp->SetWorldLocationAndRotation(P.Pos, FQuat(FVector(1.f, 0.3f * P.Size0, 0.2f).GetSafeNormal(), P.Age * 6.f * (0.6f + 0.4f * FMath::Abs(P.Size0))) * Q0);
	}
	for (FPart& P : Flashes)
	{
		if (!P.bActive) continue;
		P.Age += Dt;
		const int32 Idx = (int32)(&P - Flashes.GetData());
		UPointLightComponent* L = FlashLights.IsValidIndex(Idx) ? FlashLights[Idx].Get() : nullptr;
		if (P.Age >= P.Life) { P.bActive = false; if (L) L->SetVisibility(false); continue; }
		const float X = P.Age / P.Life;
		if (L) L->SetIntensity(FMath::Square(P.Size0) * 0.0025f * FMath::Square(1.f - X)); // sun is 9 lux: a 9 m blast outshines it threefold at its own radius
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
		const float W = L.bSolid ? L.Width : FMath::Max(L.Width, Dist * 0.0012f); // glowing beams stay at least ~1 px wide far away
		L.Comp->SetWorldLocation(Mid);
		L.Comp->SetWorldRotation(FRotationMatrix::MakeFromZ(Dir.GetSafeNormal()).Rotator());
		L.Comp->SetWorldScale3D(FVector(W / 100.f, W / 100.f, Dir.Size() / 100.f));
		if (L.MID) L.MID->SetScalarParameterValue("Intensity", L.bPersistent ? L.Intensity : L.Intensity * (1.f - L.Age / L.Life));
	}
}
