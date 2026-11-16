#include "FallStraps.h"
#include "ProceduralMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "FallAnimInstance.h"

UFallStrapsComponent::UFallStrapsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork; // after the animation of this frame
}

void UFallStrapsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	Simulate(DeltaTime);
}

void UFallStrapsComponent::Setup(USkeletalMeshComponent* InMesh, UMaterialInterface* Material)
{
	Mesh = InMesh;
	AActor* Owner = GetOwner();
	if (!Owner || !Mesh) return;
	Ribbons = NewObject<UProceduralMeshComponent>(Owner, TEXT("StrapRibbons"));
	Ribbons->SetupAttachment(Owner->GetRootComponent());
	Ribbons->SetUsingAbsoluteLocation(true);
	Ribbons->SetUsingAbsoluteRotation(true);
	Ribbons->SetUsingAbsoluteScale(true);
	Ribbons->SetWorldTransform(FTransform::Identity);
	Ribbons->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ribbons->SetCastShadow(true);
	Ribbons->RegisterComponent();
	Ribbons->bUseAsyncCooking = true;

	// Authoring is in body space of the reference pose; convert to each bone's local space once.
	TArray<FTransform> RefCS;
	FVector Basis[3];
	FallComputeRefCS(Mesh->GetSkeletalMeshAsset()->GetRefSkeleton(), RefCS, Basis);
	auto ToCS = [&Basis](const FVector& V) { return Basis[0] * V.X + Basis[1] * V.Y + Basis[2] * V.Z; };
	Local = Straps;
	for (FFallStrapDef& D : Local)
	{
		const int32 B = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton().FindBoneIndex(D.Bone);
		if (B < 0) continue;
		const FTransform& T = RefCS[B];
		D.Offset = T.InverseTransformPosition(T.GetLocation() + ToCS(D.Offset));
		D.RestDir = T.InverseTransformVectorNoScale(ToCS(D.RestDir).GetSafeNormal());
		D.WidthDir = T.InverseTransformVectorNoScale(ToCS(D.WidthDir).GetSafeNormal());
	}

	for (int32 s = 0; s < Straps.Num(); ++s)
	{
		TArray<FVector> V;
		TArray<int32> Tri;
		const int32 N = Straps[s].Segments + 1;
		V.SetNumZeroed(N * 2);
		for (int32 k = 0; k < N - 1; ++k)
		{
			const int32 a = k * 2, b = a + 1, c = a + 2, d = a + 3;
			Tri.Append({a, c, b, b, c, d});
		}
		Ribbons->CreateMeshSection_LinearColor(s, V, Tri, {}, {}, {}, {}, false);
		if (Material)
		{
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Material, this);
			MID->SetVectorParameterValue("Color", Straps[s].Color);
			Ribbons->SetMaterial(s, MID);
		}
	}
	ResetSim();
}

void UFallStrapsComponent::ResetSim()
{
	bValid = false;
}

void UFallStrapsComponent::Simulate(float DeltaTime)
{
	if (!Mesh || !Ribbons || DeltaTime <= 0.f || Local.Num() != Straps.Num()) return;
	Clock += DeltaTime;

	if (!bValid || Points.Num() != Straps.Num())
	{
		Points.SetNum(Straps.Num());
		Prev.SetNum(Straps.Num());
		for (int32 s = 0; s < Straps.Num(); ++s)
		{
			const FFallStrapDef& D = Local[s];
			const FTransform BT = Mesh->GetSocketTransform(D.Bone);
			const FVector Root = BT.TransformPosition(D.Offset);
			const FVector Dir = BT.TransformVectorNoScale(D.RestDir).GetSafeNormal();
			Points[s].SetNum(D.Segments + 1);
			for (int32 k = 0; k <= D.Segments; ++k) Points[s][k] = Root + Dir * D.SegmentLength * k;
			Prev[s] = Points[s];
		}
		bValid = true;
	}

	// Body collision spheres: keep straps out of the torso.
	struct FSphere { FVector C; float R; };
	TArray<FSphere, TInlineAllocator<6>> Body;
	auto AddS = [&](const TCHAR* Bone, float R) { const FName N(Bone); if (Mesh->DoesSocketExist(N)) Body.Add({Mesh->GetSocketLocation(N), R}); };
	AddS(TEXT("pelvis"), 17.f);
	AddS(TEXT("spine_03"), 19.f);
	AddS(TEXT("spine_05"), 17.f);
	AddS(TEXT("head"), 12.f);

	const int32 Steps = FMath::Clamp(FMath::CeilToInt(DeltaTime * 120.f), 1, 8);
	const float H = DeltaTime / Steps;

	for (int32 s = 0; s < Straps.Num(); ++s)
	{
		const FFallStrapDef& D = Local[s];
		TArray<FVector>& P = Points[s];
		TArray<FVector>& Q = Prev[s];
		const FTransform BT = Mesh->GetSocketTransform(D.Bone);
		const FVector Root = BT.TransformPosition(D.Offset);
		const FVector RestDir = BT.TransformVectorNoScale(D.RestDir).GetSafeNormal();
		const FVector WidthW = BT.TransformVectorNoScale(D.WidthDir).GetSafeNormal();

		for (int32 st = 0; st < Steps; ++st)
		{
			const float T = Clock - DeltaTime + H * (st + 1);
			P[0] = Root;
			Q[0] = Root;
			for (int32 k = 1; k < P.Num(); ++k)
			{
				const FVector Vel = (P[k] - Q[k]) / H;
				// Air drag pulls the point towards the airflow velocity, flutter whips it sideways (noise, not a sine).
				FVector Acc = (AirVelocity - Vel) * D.Drag;
				const float Tip = (float)k / (P.Num() - 1);
				const float Fl = D.Flutter * WindAmount * Tip;
				Acc += FVector(FMath::PerlinNoise1D(T * 11.f + s * 3.1f + k * 0.7f),
					FMath::PerlinNoise1D(T * 13.f + s * 5.3f + k * 0.9f + 10.f),
					FMath::PerlinNoise1D(T * 9.f + s * 7.7f + k * 1.3f + 20.f)) * Fl;
				// Flag wave travelling down the strap, perpendicular to its plane, speeding up with the wind.
				const FVector Flap = (AirVelocity.GetSafeNormal() ^ WidthW).GetSafeNormal();
				const float Omega = 2.f * PI * (6.f + 10.f * WindAmount) * (1.f + 0.25f * FMath::PerlinNoise1D(T * 0.7f + s));
				Acc += Flap * FMath::Sin(k * 1.9f - T * Omega) * Fl * 0.8f;
				const FVector Next = P[k] + (P[k] - Q[k]) * 0.995f + Acc * H * H;
				Q[k] = P[k];
				P[k] = Next;
			}
			// Distance constraints, with the first segment biased towards the rest direction.
			for (int32 it = 0; it < 3; ++it)
			{
				for (int32 k = 1; k < P.Num(); ++k)
				{
					FVector Seg = P[k] - P[k - 1];
					if (k == 1)
					{
						Seg = FMath::Lerp(Seg.GetSafeNormal(), RestDir, D.RootStiffness) * D.SegmentLength;
					}
					const FVector Want = P[k - 1] + Seg.GetSafeNormal(UE_SMALL_NUMBER, RestDir) * D.SegmentLength;
					P[k] = (k == 1) ? Want : FMath::Lerp(P[k], Want, 0.85f);
				}
				for (int32 k = 1; k < P.Num(); ++k)
				{
					for (const FSphere& B : Body)
					{
						const FVector Off = P[k] - B.C;
						const float Dist = Off.Size();
						if (Dist < B.R && Dist > KINDA_SMALL_NUMBER) P[k] = B.C + Off / Dist * B.R;
					}
				}
			}
		}

		// Ribbon vertices.
		TArray<FVector> V;
		TArray<FVector> N;
		V.SetNum(P.Num() * 2);
		N.SetNum(P.Num() * 2);
		const FVector WidthBase = BT.TransformVectorNoScale(D.WidthDir).GetSafeNormal();
		for (int32 k = 0; k < P.Num(); ++k)
		{
			const FVector Dir = (k == 0 ? (P[1] - P[0]) : (P[k] - P[k - 1])).GetSafeNormal(UE_SMALL_NUMBER, RestDir);
			FVector Wd = WidthBase - Dir * (WidthBase | Dir);
			const float Tw = FMath::PerlinNoise1D(Clock * 7.f + s * 2.3f + k * 0.5f) * 0.9f * WindAmount * ((float)k / P.Num());
			Wd = FQuat(Dir, Tw).RotateVector(Wd.GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector));
			const float Wh = FMath::Lerp(D.Width.X, D.Width.Y, (float)k / (P.Num() - 1)) * 0.5f;
			V[k * 2] = P[k] - Wd * Wh;
			V[k * 2 + 1] = P[k] + Wd * Wh;
			N[k * 2] = N[k * 2 + 1] = (Dir ^ Wd).GetSafeNormal();
		}
		Ribbons->UpdateMeshSection_LinearColor(s, V, N, {}, {}, {});
	}
}
