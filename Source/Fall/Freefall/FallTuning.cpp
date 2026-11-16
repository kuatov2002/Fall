#include "FallTuning.h"

namespace
{
	FVector NLerp(const FVector& A, const FVector& B, float T)
	{
		return FMath::Lerp(A, B, T).GetSafeNormal(UE_SMALL_NUMBER, A);
	}

	FFallLimb BlendLimb(const FFallLimb& A, const FFallLimb& B, float T)
	{
		FFallLimb R;
		R.Upper = NLerp(A.Upper, B.Upper, T);
		R.Lower = NLerp(A.Lower, B.Lower, T);
		R.EndFlex = FMath::Lerp(A.EndFlex, B.EndFlex, T);
		R.EndTwist = FMath::Lerp(A.EndTwist, B.EndTwist, T);
		R.Curl = FMath::Lerp(A.Curl, B.Curl, T);
		R.RootRaise = FMath::Lerp(A.RootRaise, B.RootRaise, T);
		R.RootForward = FMath::Lerp(A.RootForward, B.RootForward, T);
		return R;
	}

	FFallLimb AddLimb(const FFallLimb& A, const FFallLimb& B, const FFallLimb& Base, float W)
	{
		FFallLimb R;
		R.Upper = (A.Upper + (B.Upper - Base.Upper) * W).GetSafeNormal(UE_SMALL_NUMBER, A.Upper);
		R.Lower = (A.Lower + (B.Lower - Base.Lower) * W).GetSafeNormal(UE_SMALL_NUMBER, A.Lower);
		R.EndFlex = A.EndFlex + (B.EndFlex - Base.EndFlex) * W;
		R.EndTwist = A.EndTwist + (B.EndTwist - Base.EndTwist) * W;
		R.Curl = FMath::Clamp(A.Curl + (B.Curl - Base.Curl) * W, 0.f, 1.f);
		R.RootRaise = A.RootRaise + (B.RootRaise - Base.RootRaise) * W;
		R.RootForward = A.RootForward + (B.RootForward - Base.RootForward) * W;
		return R;
	}

	FFallLimb Limb(FVector Upper, FVector Lower, float EndFlex = 0.f, float Curl = 0.25f, float RootRaise = 0.f, float RootForward = 0.f, float EndTwist = 0.f)
	{
		FFallLimb L;
		L.Upper = Upper.GetSafeNormal();
		L.Lower = Lower.GetSafeNormal();
		L.EndFlex = EndFlex;
		L.Curl = Curl;
		L.RootRaise = RootRaise;
		L.RootForward = RootForward;
		L.EndTwist = EndTwist;
		return L;
	}

	FFallBoneGroupTuning MakeGroup(float Hz, float Damping, FVector2D Noise, float Drag)
	{
		FFallBoneGroupTuning G;
		G.Frequency = Hz;
		G.Damping = Damping;
		G.NoiseDeg = Noise;
		G.Drag = Drag;
		return G;
	}

	FFallPose Pose(FName Name, const FFallLimb& Arm, const FFallLimb& Leg, float Arch, float HeadPitch, float BodyPitch)
	{
		FFallPose P;
		P.Name = Name;
		P.ArmL = P.ArmR = Arm;
		P.LegL = P.LegR = Leg;
		P.bMirror = true;
		P.Arch = Arch;
		P.HeadPitch = HeadPitch;
		P.BodyPitch = BodyPitch;
		return P;
	}
}

FFallPose FFallPose::Resolved() const
{
	FFallPose R = *this;
	if (bMirror)
	{
		R.ArmR = ArmL;
		R.LegR = LegL;
		R.bMirror = false;
	}
	return R;
}

FFallPose FFallPose::Blend(const FFallPose& InA, const FFallPose& InB, float T)
{
	const FFallPose A = InA.Resolved();
	const FFallPose B = InB.Resolved();
	FFallPose R = A;
	R.bMirror = false;
	R.ArmL = BlendLimb(A.ArmL, B.ArmL, T);
	R.ArmR = BlendLimb(A.ArmR, B.ArmR, T);
	R.LegL = BlendLimb(A.LegL, B.LegL, T);
	R.LegR = BlendLimb(A.LegR, B.LegR, T);
	R.Arch = FMath::Lerp(A.Arch, B.Arch, T);
	R.Side = FMath::Lerp(A.Side, B.Side, T);
	R.Twist = FMath::Lerp(A.Twist, B.Twist, T);
	R.HeadPitch = FMath::Lerp(A.HeadPitch, B.HeadPitch, T);
	R.BodyPitch = FMath::Lerp(A.BodyPitch, B.BodyPitch, T);
	R.Stiffness = FMath::Lerp(A.Stiffness, B.Stiffness, T);
	R.NoiseScale = FMath::Lerp(A.NoiseScale, B.NoiseScale, T);
	return R;
}

FFallPose FFallPose::Additive(const FFallPose& InA, const FFallPose& InB, const FFallPose& InBase, float W)
{
	const FFallPose A = InA.Resolved();
	const FFallPose B = InB.Resolved();
	const FFallPose Base = InBase.Resolved();
	FFallPose R = A;
	R.ArmL = AddLimb(A.ArmL, B.ArmL, Base.ArmL, W);
	R.ArmR = AddLimb(A.ArmR, B.ArmR, Base.ArmR, W);
	R.LegL = AddLimb(A.LegL, B.LegL, Base.LegL, W);
	R.LegR = AddLimb(A.LegR, B.LegR, Base.LegR, W);
	R.Arch += (B.Arch - Base.Arch) * W;
	R.Side += (B.Side - Base.Side) * W;
	R.Twist += (B.Twist - Base.Twist) * W;
	R.HeadPitch += (B.HeadPitch - Base.HeadPitch) * W;
	R.BodyPitch += (B.BodyPitch - Base.BodyPitch) * W;
	return R;
}

FFallPose FFallPose::Mirrored() const
{
	const FFallPose A = Resolved();
	FFallPose R = A;
	R.ArmL = A.ArmR;
	R.ArmR = A.ArmL;
	R.LegL = A.LegR;
	R.LegR = A.LegL;
	R.Side = -A.Side;
	R.Twist = -A.Twist;
	return R;
}

UFallAnimData::UFallAnimData()
{
	// Body space: X out, Y front (belly), Z up (head). Left side authored, right mirrored unless stated.
	// Belly-to-earth box: arms ~90 deg at the shoulder, elbows 90-120, hands near head level; hips low, shins up into the sky.
	Poses.Add(Pose("Belly", Limb({0.93f, -0.22f, 0.30f}, {0.10f, -0.30f, 0.95f}, -10.f, 0.3f, 10.f), Limb({0.30f, -0.20f, -0.93f}, {0.12f, -0.97f, 0.12f}, 30.f), 16.f, 38.f, 90.f));
	// Brake: bigger box, elbows open, shins pushed into the wind, head up.
	{
		FFallPose P = Pose("Brake", Limb({0.88f, -0.40f, 0.26f}, {0.55f, -0.30f, 0.78f}, -25.f, 0.1f, 25.f), Limb({0.50f, -0.25f, -0.83f}, {0.40f, -0.65f, -0.65f}, 15.f), 14.f, 48.f, 84.f);
		P.NoiseScale = 1.8f;
		Poses.Add(P);
	}
	// Delta track: arms swept back to the hips, legs straight, toes pointed, head-low attitude.
	Poses.Add(Pose("Track", Limb({0.34f, -0.25f, -0.91f}, {0.30f, -0.22f, -0.93f}, 10.f, 0.1f, -6.f, 14.f), Limb({0.09f, -0.08f, -0.99f}, {0.07f, -0.10f, -0.99f}, 60.f), 4.f, 22.f, 118.f));
	// Head-down: body vertical, arms out at shoulder height with hands ahead of the shoulders, legs apart with soft knees.
	Poses.Add(Pose("HeadDown", Limb({0.92f, 0.25f, 0.30f}, {0.45f, 0.80f, -0.40f}, -5.f, 0.2f, 5.f), Limb({0.36f, 0.20f, -0.91f}, {0.30f, -0.55f, -0.78f}, 40.f), 2.f, 32.f, 166.f));
	// Bank left: left arm drops into the flow (rudder), right arm rises, chest twists, legs trail.
	{
		FFallPose P;
		P.Name = "BankLeft";
		P.bMirror = false;
		P.ArmL = Limb({0.80f, 0.15f, 0.58f}, {0.25f, 0.10f, 0.96f}, -5.f, 0.3f, 6.f);
		P.ArmR = Limb({0.86f, -0.50f, 0.10f}, {0.45f, -0.45f, 0.77f}, -15.f, 0.2f, 20.f);
		P.LegL = Limb({0.22f, -0.22f, -0.95f}, {0.08f, -0.97f, 0.12f}, 30.f);
		P.LegR = Limb({0.38f, -0.22f, -0.90f}, {0.16f, -0.97f, 0.12f}, 30.f);
		P.Arch = 20.f; P.Side = 10.f; P.Twist = -12.f; P.HeadPitch = 34.f; P.BodyPitch = 90.f;
		Poses.Add(P);
	}
	// Grab: reach forward to the debris, knees tucked, chest rounded towards the surface.
	Poses.Add(Pose("Grab", Limb({0.75f, 0.35f, 0.55f}, {0.05f, 0.70f, 0.70f}, 0.f, 0.75f, 10.f, 10.f), Limb({0.30f, 0.55f, -0.78f}, {0.10f, -0.60f, -0.80f}, 25.f), -8.f, 30.f, 80.f));
	// Kick: sit-fly base, chamber (knee to chest = anticipation), extend (front kick), arms counter-swing.
	{
		FFallPose P;
		P.Name = "KickChamber";
		P.bMirror = false;
		P.ArmL = Limb({0.95f, -0.15f, 0.25f}, {0.30f, -0.25f, 0.92f}, -10.f, 0.4f, 12.f);
		P.ArmR = Limb({0.80f, -0.20f, 0.55f}, {0.15f, -0.10f, 0.98f}, -10.f, 0.7f, 15.f);
		P.LegL = Limb({0.25f, -0.20f, -0.95f}, {0.10f, -0.95f, 0.30f}, 30.f);
		P.LegR = Limb({0.80f, -0.30f, 0.10f}, {0.25f, -0.55f, -0.80f}, 20.f);
		P.Arch = 8.f; P.Side = 14.f; P.Twist = -8.f; P.HeadPitch = 40.f; P.BodyPitch = 86.f; P.Stiffness = 1.6f;
		Poses.Add(P);
		P.Name = "KickExtend";
		P.ArmL = Limb({0.90f, -0.10f, 0.40f}, {0.50f, -0.10f, 0.86f}, -10.f, 0.4f, 12.f);
		P.ArmR = Limb({0.70f, -0.45f, 0.55f}, {0.25f, -0.40f, 0.88f}, -10.f, 0.7f, 15.f);
		P.LegL = Limb({0.20f, -0.20f, -0.96f}, {0.10f, -0.90f, 0.40f}, 30.f);
		P.LegR = Limb({0.97f, -0.12f, -0.10f}, {0.97f, -0.08f, -0.15f}, 15.f);
		P.Arch = 12.f; P.Side = -10.f; P.Twist = 6.f; P.HeadPitch = 40.f; P.BodyPitch = 82.f; P.Stiffness = 2.2f;
		Poses.Add(P);
	}
	// Wingsuit: arms out and slightly back, legs spread, straight body.
	Poses.Add(Pose("Wingsuit", Limb({0.95f, -0.12f, -0.28f}, {0.96f, -0.10f, -0.25f}, 0.f, 0.05f, 0.f), Limb({0.40f, -0.05f, -0.92f}, {0.38f, -0.06f, -0.92f}, 60.f), 6.f, 35.f, 100.f));
	// Limp: stunned or dead. Loose springs and a lot of wind noise.
	{
		FFallPose P = Pose("Limp", Limb({0.70f, -0.50f, 0.20f}, {0.60f, -0.60f, -0.20f}, 20.f, 0.4f, 0.f), Limb({0.35f, -0.30f, -0.88f}, {0.25f, -0.60f, -0.75f}, 10.f), -10.f, -20.f, 100.f);
		P.Stiffness = 0.18f;
		P.NoiseScale = 3.5f;
		Poses.Add(P);
	}
	// Under canopy: upright, hands up on the toggles, legs hanging.
	{
		FFallPose P = Pose("Canopy", Limb({0.55f, 0.35f, 0.30f}, {0.10f, 0.25f, 0.96f}, 0.f, 0.9f, 15.f), Limb({0.15f, 0.45f, -0.88f}, {0.10f, -0.15f, -0.98f}, 25.f), -4.f, 5.f, 0.f);
		P.NoiseScale = 0.8f;
		Poses.Add(P);
	}

	SpineGroup = MakeGroup(3.2f, 0.6f, FVector2D(1.0f, 3.5f), 0.15f);
	HeadGroup = MakeGroup(3.6f, 0.65f, FVector2D(1.5f, 5.0f), 0.20f);
	ClavicleGroup = MakeGroup(3.5f, 0.70f, FVector2D(0.5f, 2.0f), 0.15f);
	UpperLimbGroup = MakeGroup(2.8f, 0.5f, FVector2D(2.0f, 9.0f), 0.35f);
	LowerLimbGroup = MakeGroup(2.4f, 0.45f, FVector2D(3.0f, 12.0f), 0.45f);
	ExtremityGroup = MakeGroup(2.2f, 0.4f, FVector2D(4.0f, 15.0f), 0.50f);
}

const FFallBoneGroupTuning& UFallAnimData::Group(EFallBoneGroup G) const
{
	switch (G)
	{
	case EFallBoneGroup::Spine: return SpineGroup;
	case EFallBoneGroup::Head: return HeadGroup;
	case EFallBoneGroup::Clavicle: return ClavicleGroup;
	case EFallBoneGroup::UpperLimb: return UpperLimbGroup;
	case EFallBoneGroup::LowerLimb: return LowerLimbGroup;
	default: return ExtremityGroup;
	}
}

const FFallPose* UFallAnimData::FindPose(FName Name) const
{
	for (const FFallPose& P : Poses)
	{
		if (P.Name == Name)
		{
			return &P;
		}
	}
	return nullptr;
}
