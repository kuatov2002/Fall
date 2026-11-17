#include "FallTerrain.h"

float FFallHeightGrid::Sample(float Xm, float Ym) const
{
	const float S = (float)(Size - 1);
	const float C = FMath::Clamp((Xm / Extent + 0.5f) * S, 0.f, S - 0.001f);
	const float R = FMath::Clamp((Ym / Extent + 0.5f) * S, 0.f, S - 0.001f);
	const int32 c = (int32)C, r = (int32)R;
	const float fx = C - c, fy = R - r;
	const float* H = Heights.GetData();
	const float a = H[r * Size + c], b = H[r * Size + c + 1], d = H[(r + 1) * Size + c], e = H[(r + 1) * Size + c + 1];
	return FMath::Lerp(FMath::Lerp(a, b, fx), FMath::Lerp(d, e, fx), fy);
}

const UFallTerrainData* UFallTerrainData::Get()
{
	static TWeakObjectPtr<UFallTerrainData> Cached;
	static bool bTried = false;
	if (!Cached.IsValid() && !bTried)
	{
		bTried = true;
		UFallTerrainData* T = LoadObject<UFallTerrainData>(nullptr, TEXT("/Game/Freefall/Data/DA_FallTerrain.DA_FallTerrain"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (T && T->Far.IsValid())
		{
			T->AddToRoot(); // read from the height function every frame, never collected
			Cached = T;
		}
	}
	return Cached.Get();
}

float UFallTerrainData::HeightAt(float Xm, float Ym) const
{
	// Far everywhere, mid and near blended in towards the centre, each fading out before its own edge.
	const float Edge = FMath::Max(FMath::Abs(Xm), FMath::Abs(Ym));
	float H = Far.Sample(Xm, Ym);
	if (Mid.IsValid()) H = FMath::Lerp(Mid.Sample(Xm, Ym), H, FMath::SmoothStep(Mid.Extent * 0.42f, Mid.Extent * 0.49f, Edge));
	if (Near.IsValid()) H = FMath::Lerp(Near.Sample(Xm, Ym), H, FMath::SmoothStep(Near.Extent * 0.38f, Near.Extent * 0.48f, Edge));
	// Roughness below the grid spacing, stronger on the mountains than on the valley floor.
	const float Rough = FMath::Clamp(H / 600.f, 0.15f, 1.f);
	H += Rough * (22.f * FMath::PerlinNoise2D(FVector2D(Xm / 160.f, Ym / 160.f)) + 7.f * FMath::PerlinNoise2D(FVector2D(Xm / 45.f, Ym / 45.f)));
	return FMath::Max(H, -150.f) + ValleyFloor;
}
