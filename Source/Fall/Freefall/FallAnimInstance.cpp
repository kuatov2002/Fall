#include "FallAnimInstance.h"
#include "Animation/AnimNodeBase.h"
#include "BoneContainer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformTime.h"

namespace
{
	enum { kRight = 0, kFront = 1, kUp = 2 };

	float Smooth(float X) { X = FMath::Clamp(X, 0.f, 1.f); return X * X * (3.f - 2.f * X); }

	// Axes[0] is the subject's LEFT: (left, front, up) keeps cross(a0, a1) == a2 in Unreal's convention,
	// which the rotation signs below rely on. "Out" of the right side is therefore -Axes[0].
	FVector ToCS(const FVector& Body, bool bRight, const FVector Axes[3])
	{
		return (Axes[kRight] * (bRight ? -Body.X : Body.X) + Axes[kFront] * Body.Y + Axes[kUp] * Body.Z).GetSafeNormal();
	}

	FQuat FrameXZ(const FVector& X, const FVector& Z)
	{
		return FRotationMatrix::MakeFromXZ(X, Z).ToQuat();
	}

	FVector RotateToward(const FVector& V, const FVector& Toward, float Deg)
	{
		const FVector Axis = (V ^ Toward).GetSafeNormal();
		return Axis.IsNearlyZero() ? V : FQuat(Axis, FMath::DegreesToRadians(Deg)).RotateVector(V);
	}

	const FFallPose& PoseOr(const UFallAnimData& D, FName Name)
	{
		if (const FFallPose* P = D.FindPose(Name)) return *P;
		if (const FFallPose* P = D.FindPose("Belly")) return *P;
		static FFallPose Empty;
		return Empty;
	}
}

FFallPose FallComputeTargetPose(const UFallAnimData& D, const FFallAnimInput& In)
{
	const FFallPose& Belly = PoseOr(D, "Belly");
	FFallPose P;
	const float Dv = FMath::Clamp(In.Dive, -1.f, 1.f);
	if (Dv < 0.f) P = FFallPose::Blend(Belly, PoseOr(D, "Brake"), Smooth(-Dv));
	else if (In.bDiveExit) P = FFallPose::Blend(Belly, PoseOr(D, "HeadDown"), Smooth(Dv)); // arms come forward first, no track on the way out
	else if (Dv < 0.5f) P = FFallPose::Blend(Belly, PoseOr(D, "Track"), Smooth(Dv / 0.5f));
	else P = FFallPose::Blend(PoseOr(D, "Track"), PoseOr(D, "HeadDown"), Smooth((Dv - 0.5f) / 0.5f));

	const float B = FMath::Clamp(In.Bank, -1.f, 1.f);
	if (FMath::Abs(B) > 0.01f)
	{
		const FFallPose& Left = PoseOr(D, "BankLeft");
		const FFallPose Bank = B < 0.f ? Left.Resolved() : Left.Mirrored();
		P = FFallPose::Additive(P, Bank, Belly, FMath::Abs(B) * (1.f - 0.5f * FMath::Max(Dv, 0.f)));
	}

	if (In.OverrideWeight > 0.f && !In.OverridePose.IsNone())
	{
		P = FFallPose::Blend(P, PoseOr(D, In.OverridePose), Smooth(In.OverrideWeight));
	}

	if (In.KickTime >= 0.f)
	{
		const FFallPose Ch = In.bKickRight ? PoseOr(D, "KickChamber").Resolved() : PoseOr(D, "KickChamber").Mirrored();
		const FFallPose Ex = In.bKickRight ? PoseOr(D, "KickExtend").Resolved() : PoseOr(D, "KickExtend").Mirrored();
		const FVector K = D.KickTimes;
		const float T = In.KickTime;
		if (T < K.X) P = FFallPose::Blend(P, Ch, Smooth(T / (K.X * 0.6f)));
		else if (T < K.X + K.Y) P = FFallPose::Blend(Ch, Ex, Smooth((T - K.X) / K.Y));
		else if (T < K.X + K.Y + K.Z) P = FFallPose::Blend(Ex, P, Smooth((T - K.X - K.Y) / K.Z));
	}
	return P.Resolved();
}

void FallComputeRefCS(const FReferenceSkeleton& Ref, TArray<FTransform>& OutCS, FVector OutBasis[3])
{
	const int32 N = Ref.GetNum();
	const TArray<FTransform>& Local = Ref.GetRefBonePose();
	OutCS.SetNum(N);
	for (int32 i = 0; i < N; ++i)
	{
		const int32 P = Ref.GetParentIndex(i);
		OutCS[i] = P >= 0 ? Local[i] * OutCS[P] : Local[i];
	}
	auto Pos = [&](const TCHAR* Name) { const int32 I = Ref.FindBoneIndex(FName(Name)); return I >= 0 ? OutCS[I].GetLocation() : FVector::ZeroVector; };
	const FVector Up = (Pos(TEXT("head")) - Pos(TEXT("pelvis"))).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	FVector Right = Pos(TEXT("upperarm_r")) - Pos(TEXT("upperarm_l"));
	Right = (Right - Up * (Right | Up)).GetSafeNormal(UE_SMALL_NUMBER, FVector(1, 0, 0)); // subject's right
	OutBasis[0] = Right;
	OutBasis[1] = Right ^ Up; // front: (right, front, up) with Unreal's cross product
	OutBasis[2] = Up;
}

void FFallAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	if (UFallAnimInstance* Inst = Cast<UFallAnimInstance>(InAnimInstance))
	{
		// Accumulate one-shot impulses if evaluation was skipped since the last copy.
		const FVector PrevHit = In.HitCS;
		const float PrevRecoil = In.Recoil;
		const bool bPending = PendingDt > 0.f;
		In = Inst->Input;
		if (bPending)
		{
			In.HitCS += PrevHit;
			In.Recoil += PrevRecoil;
		}
		Inst->Input.HitCS = FVector::ZeroVector;
		Inst->Input.Recoil = 0.f;
		Inst->Input.CompDeltaInv = FQuat::Identity;
		DataPtr = UFallAnimData::Resolve(Inst->AnimData);
	}
	PendingDt += DeltaSeconds;
}

void FFallAnimProxy::Init(const FReferenceSkeleton& Ref)
{
	NumBones = Ref.GetNum();
	Parents.SetNum(NumBones);
	RefLocal = Ref.GetRefBonePose();
	RefCS.SetNum(NumBones);
	OutCS.SetNum(NumBones);
	Roles.Init(ERole::None, NumBones);
	IndexFinger.Init(false, NumBones);
	RoleIndex.Init(0, NumBones);
	SpringQ.Init(FQuat::Identity, NumBones);
	SpringW.Init(FVector::ZeroVector, NumBones);
	SpringValid.Init(false, NumBones);
	for (int32 i = 0; i < NumBones; ++i)
	{
		Parents[i] = Ref.GetParentIndex(i);
		RefCS[i] = Parents[i] >= 0 ? RefLocal[i] * RefCS[Parents[i]] : RefLocal[i];
	}
	auto Find = [&Ref](const TCHAR* N) { return Ref.FindBoneIndex(FName(N)); };
	auto Pos = [this](int32 I) { return I >= 0 ? RefCS[I].GetLocation() : FVector::ZeroVector; };

	const int32 Pelvis = Find(TEXT("pelvis"));
	if (Pelvis >= 0) Roles[Pelvis] = ERole::Pelvis;
	const int32 Head = Find(TEXT("head"));
	const FVector Up = (Pos(Head) - Pos(Pelvis)).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	// Lateral axis from the shoulders: exact, unlike the feet, which splay outwards in the reference pose.
	FVector Right = Pos(Find(TEXT("upperarm_l"))) - Pos(Find(TEXT("upperarm_r")));
	Right = (Right - Up * (Right | Up)).GetSafeNormal(UE_SMALL_NUMBER, FVector(1, 0, 0)); // subject's left, see ToCS
	FVector Front = Up ^ Right;
	const FVector Feet = (Pos(Find(TEXT("ball_l"))) - Pos(Find(TEXT("foot_l")))) + (Pos(Find(TEXT("ball_r"))) - Pos(Find(TEXT("foot_r"))));
	if ((Front | Feet) < 0.f) { UE_LOG(LogTemp, Warning, TEXT("FallAnim: unexpected skeleton handedness")); }
	Basis[kRight] = Right;
	Basis[kFront] = Front;
	Basis[kUp] = Up;
	if (((Pos(Find(TEXT("upperarm_l"))) - Pos(Find(TEXT("upperarm_r")))) | Right) < 0.f)
	{
		UE_LOG(LogTemp, Warning, TEXT("FallAnim: skeleton right side does not match the computed basis"));
	}

	const TCHAR* SpineNames[5] = {TEXT("spine_01"), TEXT("spine_02"), TEXT("spine_03"), TEXT("spine_04"), TEXT("spine_05")};
	for (int32 k = 0; k < 5; ++k)
	{
		Spine[k] = Find(SpineNames[k]);
		if (Spine[k] >= 0) { Roles[Spine[k]] = ERole::Spine; RoleIndex[Spine[k]] = k; }
	}
	const TCHAR* NeckNames[3] = {TEXT("neck_01"), TEXT("neck_02"), TEXT("head")};
	for (int32 k = 0; k < 3; ++k)
	{
		Neck[k] = Find(NeckNames[k]);
		if (Neck[k] >= 0) { Roles[Neck[k]] = ERole::Neck; RoleIndex[Neck[k]] = k; }
	}

	const TCHAR* Names[4][4] = {
		{TEXT("clavicle_l"), TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l")},
		{TEXT("clavicle_r"), TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r")},
		{TEXT(""), TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l")},
		{TEXT(""), TEXT("thigh_r"), TEXT("calf_r"), TEXT("foot_r")}};
	for (int32 l = 0; l < 4; ++l)
	{
		FLimbRef& L = Limbs[l];
		L.bArm = l < 2;
		L.bRight = (l % 2) == 1;
		L.Root = L.bArm ? Find(Names[l][0]) : INDEX_NONE;
		L.Upper = Find(Names[l][1]);
		L.Lower = Find(Names[l][2]);
		L.End = Find(Names[l][3]);
		if (L.Upper < 0 || L.Lower < 0 || L.End < 0) continue;
		const FVector PU = Pos(L.Upper), PL = Pos(L.Lower), PE = Pos(L.End);
		L.DirU = (PL - PU).GetSafeNormal();
		L.DirL = (PE - PL).GetSafeNormal();
		L.LenU = (PL - PU).Size();
		L.LenL = (PE - PL).Size();
		const FVector FlexDir = L.bArm ? Front : -Front; // forearm folds forward, shin folds back
		const FVector Anat = (L.DirU ^ FlexDir).GetSafeNormal();
		const FVector FromRef = L.DirU ^ L.DirL;
		L.Hinge = (FromRef.Size() > 0.04f && (FromRef | Anat) > 0.f) ? FromRef.GetSafeNormal() : Anat;
		if (L.Root >= 0)
		{
			L.RootDir = (PU - Pos(L.Root)).GetSafeNormal();
			Roles[L.Root] = ERole::Clavicle; RoleIndex[L.Root] = l;
		}
		Roles[L.Upper] = ERole::Upper; RoleIndex[L.Upper] = l;
		Roles[L.Lower] = ERole::Lower; RoleIndex[L.Lower] = l;
		Roles[L.End] = ERole::End; RoleIndex[L.End] = l;
	}

	// Fingers: curl axis moves each finger towards the palm.
	for (int32 s = 0; s < 2; ++s)
	{
		const TCHAR* Sfx = s ? TEXT("_r") : TEXT("_l");
		const FVector H = Pos(Limbs[s].End);
		const FVector Mid = Pos(Find(*FString::Printf(TEXT("middle_01%s"), Sfx)));
		const FVector Idx = Pos(Find(*FString::Printf(TEXT("index_01%s"), Sfx)));
		const FVector Pnk = Pos(Find(*FString::Printf(TEXT("pinky_01%s"), Sfx)));
		const FVector D = (Mid - H).GetSafeNormal();
		const FVector Across = (Idx - Pnk).GetSafeNormal();
		PalmRef[s] = (s ? (D ^ Across) : (Across ^ D)).GetSafeNormal();
		CurlAxisRef[s] = (D ^ PalmRef[s]).GetSafeNormal();
		for (const TCHAR* F : {TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky")})
		{
			for (int32 j = 1; j <= 3; ++j)
			{
				const int32 B = Find(*FString::Printf(TEXT("%s_0%d%s"), F, j, Sfx));
				if (B >= 0) { Roles[B] = ERole::Finger; RoleIndex[B] = s; IndexFinger[B] = FCString::Strcmp(F, TEXT("index")) == 0; }
			}
		}
	}
	bInit = true;
}

FQuat FFallAnimProxy::Noise(int32 Key, float AmpDeg) const
{
	const UFallAnimData* D = DataPtr.Get();
	const float W = In.Wind;
	const float Fd = FMath::Lerp(D->DriftHz.X, D->DriftHz.Y, W);
	const float Ff = FMath::Lerp(D->FlutterHz.X, D->FlutterHz.Y, W);
	const float S = Key * 7.31f + (In.Seed % 97) * 1.37f;
	auto Oct = [&](float Off)
	{
		return FMath::PerlinNoise1D(Clock * Fd + S + Off) * (1.f - D->FlutterShare)
			+ FMath::PerlinNoise1D(Clock * Ff + S * 1.7f + Off * 3.1f) * D->FlutterShare;
	};
	const FVector RV(Oct(0.f), Oct(31.4f), Oct(77.7f));
	return FQuat::MakeFromRotationVector(RV * FMath::DegreesToRadians(AmpDeg) * 1.6f);
}

FQuat FFallAnimProxy::Flutter(int32 Key, float AmpDeg) const
{
	const UFallAnimData* D = DataPtr.Get();
	const float F = FMath::Lerp(D->FlutterHz.X, D->FlutterHz.Y, In.Wind) * 1.6f;
	const float S = Key * 3.77f + (In.Seed % 89) * 2.11f;
	const FVector RV(FMath::PerlinNoise1D(Clock * F + S), FMath::PerlinNoise1D(Clock * F * 1.13f + S + 41.f), FMath::PerlinNoise1D(Clock * F * 0.91f + S + 83.f));
	return FQuat::MakeFromRotationVector(RV * FMath::DegreesToRadians(AmpDeg) * 1.6f);
}

FQuat FFallAnimProxy::Spring(int32 Bone, const FQuat& Target, const FFallBoneGroupTuning& G, float StiffMul, float Dt)
{
	if (!SpringValid[Bone] || Dt <= 0.f)
	{
		SpringValid[Bone] = true;
		SpringQ[Bone] = Target;
		SpringW[Bone] = FVector::ZeroVector;
		return Target;
	}
	FQuat Q = SpringQ[Bone];
	// Drag: the limb keeps part of its world orientation while the body turns under it.
	Q = FQuat::Slerp(Q, In.CompDeltaInv * Q, G.Drag).GetNormalized();
	FVector W = SpringW[Bone];
	const float Omega = 2.f * PI * G.Frequency * FMath::Sqrt(FMath::Max(StiffMul, 0.05f));
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(Dt * 240.f), 1, 16);
	const float H = Dt / Steps;
	for (int32 s = 0; s < Steps; ++s)
	{
		FQuat Err = Target * Q.Inverse();
		Err.EnforceShortestArcWith(FQuat::Identity);
		const FVector E = Err.ToRotationVector();
		W += (E * (Omega * Omega) - W * (2.f * G.Damping * Omega)) * H;
		Q = (FQuat::MakeFromRotationVector(W * H) * Q).GetNormalized();
	}
	SpringQ[Bone] = Q;
	SpringW[Bone] = W;
	return Q;
}

void FFallAnimProxy::SolveLimb(FLimbRef& L, const FFallLimb& P, const FVector Axes[3], const FVector& RootPos, float Dt)
{
	FVector DU = ToCS(P.Upper, L.bRight, Axes);
	FVector DL = ToCS(P.Lower, L.bRight, Axes);

	// Aim: right arm points at the target, elbow almost straight.
	if (L.bArm && L.bRight && In.AimWeight > 0.f)
	{
		// A belly-to-earth body cannot point a gun at its own feet, over its back or across to the left:
		// the arm stops at the edge of that cone (Axes[kRight] is the subject's left inside the proxy).
		FVector Aim = (In.AimPointCS - RootPos).GetSafeNormal(UE_SMALL_NUMBER, DU);
		{
			const float Au = FMath::Max((float)(Aim | Axes[kUp]), 0.15f); // always a little forward, never back along the body
			const float Af = FMath::Clamp((float)(Aim | Axes[kFront]), -0.3f, 0.7f); // not straight under the chest either
			const float Al = FMath::Min((float)(Aim | Axes[kRight]), 0.35f);
			Aim = (Axes[kUp] * Au + Axes[kFront] * Af + Axes[kRight] * Al).GetSafeNormal(UE_SMALL_NUMBER, Aim);
		}
		const FVector AU = (Aim - Axes[kUp] * 0.22f - Axes[kRight] * 0.3f).GetSafeNormal(); // soft elbow, ~160 deg, upper arm out to the side
		const float W = Smooth(In.AimWeight);
		DU = FMath::Lerp(DU, AU, W).GetSafeNormal();
		DL = FMath::Lerp(DL, Aim, W).GetSafeNormal();
	}

	// Limp: the limbs trail with the airflow.
	if (In.Trail > 0.f)
	{
		const FVector Out = Axes[kRight] * (L.bRight ? -1.f : 1.f);
		const float W = Smooth(In.Trail);
		DU = FMath::Lerp(DU, (In.WindDirCS + Out * 1.1f).GetSafeNormal(), W * 0.75f).GetSafeNormal(); // trails outward, never across the chest
		DL = FMath::Lerp(DL, (In.WindDirCS + Out * 0.8f).GetSafeNormal(), W * 0.85f).GetSafeNormal();
	}

	// Two-bone IK to a hand target (grab), elbow keeps the side of the posed elbow.
	const int32 Side = L.bRight ? 1 : 0;
	if (L.bArm && In.HandIKWeight[Side] > 0.f)
	{
		const FVector ToT = In.HandIKCS[Side] - RootPos;
		const float Dist = FMath::Clamp(ToT.Size(), 1.f, (L.LenU + L.LenL) * 0.999f);
		const FVector Dir = ToT.GetSafeNormal();
		const float CosA = FMath::Clamp((L.LenU * L.LenU + Dist * Dist - L.LenL * L.LenL) / (2.f * L.LenU * Dist), -1.f, 1.f);
		FVector Pole = DU - Dir * (DU | Dir);
		if (Pole.SizeSquared() < 1e-4f) Pole = -Axes[kUp];
		Pole.Normalize();
		const FVector IU = Dir * CosA + Pole * FMath::Sqrt(1.f - CosA * CosA);
		const FVector Elbow = RootPos + IU * L.LenU;
		const FVector IL = (RootPos + Dir * Dist - Elbow).GetSafeNormal(UE_SMALL_NUMBER, IU);
		const float W = Smooth(In.HandIKWeight[Side]);
		DU = FMath::Lerp(DU, IU, W).GetSafeNormal();
		DL = FMath::Lerp(DL, IL, W).GetSafeNormal();
	}

	// Hinge from the two directions, never bending the joint backwards.
	// The hinge is the plane of the two segments; the upper bone twists to put its own hinge there, so the joint
	// always flexes the anatomical way. Only a nearly straight limb borrows the swing of the reference hinge.
	const FVector Fallback = FQuat::FindBetweenNormals(L.DirU, DU).RotateVector(L.Hinge);
	FVector Raw = DU ^ DL;
	if ((Raw | Fallback) < 0.f && Raw.Size() < 0.2f) Raw = FVector::ZeroVector; // tiny reverse bend: treat as straight
	const float A = Smooth((Raw.Size() - 0.03f) / 0.2f);
	FVector H = FMath::Lerp(Fallback, Raw.GetSafeNormal(), A);
	H = (H - DU * (H | DU)).GetSafeNormal(UE_SMALL_NUMBER, Fallback);

	L.TU = DU;
	L.TL = DL;
	L.TH = H;
	L.Flex = P.EndFlex;
	L.Twist = P.EndTwist;
	L.Curl = P.Curl;
	if (L.bArm && L.bRight && In.AimWeight > 0.f)
	{
		L.Flex = FMath::Lerp(L.Flex, 0.f, In.AimWeight);
		L.Curl = FMath::Lerp(L.Curl, 0.85f, In.AimWeight);
	}
	if (L.bArm && In.HandIKWeight[Side] > 0.f)
	{
		L.Curl = FMath::Lerp(L.Curl, 0.8f, In.HandIKWeight[Side]);
	}
}

bool FFallAnimProxy::Evaluate(FPoseContext& Output)
{
	const FBoneContainer& BC = Output.Pose.GetBoneContainer();
	const FReferenceSkeleton& Ref = BC.GetReferenceSkeleton();
	const UFallAnimData* D = DataPtr.Get();
	if (!D || Ref.GetNum() == 0)
	{
		Output.ResetToRefPose();
		return true;
	}
	if (!bInit || NumBones != Ref.GetNum())
	{
		Init(Ref);
	}

	const float Dt = FMath::Min(PendingDt, 0.1f);
	PendingDt = 0.f;
	Clock += Dt;

	// Layered timing: the head leads stance changes, arms and spine follow, legs trail behind.
	static const float Tau[3] = {0.03f, 0.2f, 0.38f};
	if (!bLagInit)
	{
		for (int32 g = 0; g < 3; ++g) { LagDive[g] = In.Dive; LagBank[g] = In.Bank; }
		bLagInit = true;
	}
	for (int32 g = 0; g < 3; ++g)
	{
		const float K = 1.f - FMath::Exp(-Dt / Tau[g]);
		LagDive[g] += (In.Dive - LagDive[g]) * K;
		LagBank[g] += (In.Bank - LagBank[g]) * K;
	}
	FFallAnimInput Lag = In;
	Lag.Dive = LagDive[1]; Lag.Bank = LagBank[1];
	FFallPose P = FallComputeTargetPose(*D, Lag);
	Lag.Dive = LagDive[2]; Lag.Bank = LagBank[2];
	const FFallPose PLegs = FallComputeTargetPose(*D, Lag);
	Lag.Dive = LagDive[0]; Lag.Bank = LagBank[0];
	const FFallPose PHead = FallComputeTargetPose(*D, Lag);
	P.LegL = PLegs.LegL;
	P.LegR = PLegs.LegR;
	{
		const float LegLag = FMath::Clamp(FMath::Abs(LagDive[1] - LagDive[2]) * 3.f, 0.f, 1.f);
		P.LegL.Lower = (P.LegL.Lower + FVector(0.f, -0.9f, 0.3f) * LegLag).GetSafeNormal();
		P.LegR.Lower = (P.LegR.Lower + FVector(0.f, -0.9f, 0.3f) * LegLag).GetSafeNormal();
	}
	// Chin tucks while the head is ahead of the body going into the dive, lifts first coming out.
	P.HeadPitch = PHead.HeadPitch - 45.f * FMath::Clamp((LagDive[0] - LagDive[1]) * 2.5f, -1.f, 1.f);
	P.Arch += 10.f * FMath::Clamp((LagDive[1] - LagDive[2]) * 2.5f, -1.f, 1.f);

	// Lean from body acceleration (m/s^2): pressed into an arch when slowing, bent away from lateral pushes.
	const FVector AccM = In.AccelCS / 100.f;
	P.Arch += FMath::Clamp(-(AccM | Basis[kFront]) * D->LeanPerAccel, -D->LeanMaxDeg, D->LeanMaxDeg);
	P.Side += FMath::Clamp(-(AccM | Basis[kRight]) * D->LeanPerAccel, -D->LeanMaxDeg, D->LeanMaxDeg);

	// Look target in body terms: yaw is shared between spine and head.
	float LookYaw = 0.f, LookPitch = P.HeadPitch;
	{
		const FVector L = In.LookDirCS.GetSafeNormal(UE_SMALL_NUMBER, Basis[kFront]);
		const float Y = FMath::RadiansToDegrees(FMath::Atan2(-(L | Basis[kRight]), (L | Basis[kFront])));
		const float Lim = D->LookYawPitchLimit.X;
		LookReach = FMath::SmoothStep(150.f, 105.f, FMath::Abs(Y));
		const float W = FMath::Clamp(In.LookWeight, 0.f, 1.f) * LookReach;
		LookYaw = FMath::Clamp(Y, -60.f, 60.f) * FMath::Clamp(In.LookWeight, 0.f, 1.f) * FMath::SmoothStep(100.f, 60.f, FMath::Abs(Y));
		(void)Lim; (void)W;
		if (In.AimWeight > 0.f)
		{
			const FVector A = (In.AimPointCS - RefCS[Limbs[1].Upper].GetLocation()).GetSafeNormal();
			LookYaw = FMath::Lerp(LookYaw, FMath::RadiansToDegrees(FMath::Atan2(-(A | Basis[kRight]), (A | Basis[kFront]))) * 0.6f, In.AimWeight);
		}
		P.Twist += LookYaw * D->LookSpineShare;
	}

	// Gusts and hits kick the springs.
	const float StiffMul = P.Stiffness;
	const float NoiseMul = P.NoiseScale;
	const float FlutterMul = FMath::Min(P.NoiseScale, 1.2f) * FMath::Lerp(1.f, 0.2f, FMath::Clamp(1.f - P.Stiffness, 0.f, 1.f));
	FVector GustKick = FVector::ZeroVector;
	if (Clock >= NextGust)
	{
		const float Rate = FMath::Max(D->GustsPerSecond * In.Wind, 0.05f);
		NextGust = Clock + Rng.FRandRange(0.5f, 1.5f) / Rate;
		GustKick = Rng.GetUnitVector() * D->GustStrength * In.Wind * NoiseMul;
	}
	const FVector HitAxis = (Basis[kUp] ^ In.HitCS) * D->HitKick;

	const FQuat SpineTotal = FQuat(Basis[kRight], FMath::DegreesToRadians(P.Arch))
		* FQuat(Basis[kFront], FMath::DegreesToRadians(P.Side))
		* FQuat(Basis[kUp], FMath::DegreesToRadians(P.Twist));

	FVector Chest[3] = {Basis[kRight], Basis[kFront], Basis[kUp]};
	FQuat ChestDelta = FQuat::Identity;

	TArray<FTransform> OutLocal;
	OutLocal.SetNum(NumBones);

	for (int32 i = 0; i < NumBones; ++i)
	{
		const int32 Par = Parents[i];
		const FTransform Inherit = Par >= 0 ? RefLocal[i] * OutCS[Par] : RefLocal[i];
		FQuat Rot = Inherit.GetRotation();
		const int32 RI = RoleIndex[i];

		switch (Roles[i])
		{
		case ERole::Pelvis:
		{
			const FFallBoneGroupTuning& G = D->Group(EFallBoneGroup::Spine);
			Rot = Spring(i, Noise(i, FMath::Lerp(G.NoiseDeg.X, G.NoiseDeg.Y, In.Wind) * 0.8f * NoiseMul) * RefCS[i].GetRotation(), G, StiffMul, Dt);
			break;
		}
		case ERole::Spine:
		{
			const FQuat T = FQuat::Slerp(FQuat::Identity, SpineTotal, (RI + 1) / 5.f) * RefCS[i].GetRotation();
			const FFallBoneGroupTuning& G = D->Group(EFallBoneGroup::Spine);
			if (SpringValid[i]) SpringW[i] += (GustKick * 0.4f + HitAxis * (0.4f + 0.15f * RI) + Chest[kUp] * In.Recoil * 3.f);
			Rot = Flutter(i, G.NoiseDeg.Y * 0.3f * In.Wind * FlutterMul) * Spring(i, Noise(i, FMath::Lerp(G.NoiseDeg.X, G.NoiseDeg.Y, In.Wind) * NoiseMul) * T, G, StiffMul, Dt);
			if (RI == 4)
			{
				ChestDelta = Rot * RefCS[i].GetRotation().Inverse();
				for (int32 a = 0; a < 3; ++a) Chest[a] = ChestDelta.RotateVector(Basis[a]);
			}
			break;
		}
		case ERole::Neck:
		{
			static const float Share[3] = {0.3f, 0.6f, 1.f};
			const FVector L = In.LookDirCS.GetSafeNormal(UE_SMALL_NUMBER, Chest[kFront]);
			const float W = FMath::Clamp(In.LookWeight, 0.f, 1.f) * LookReach;
			const float Yaw = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(-(L | Chest[kRight]), (L | Chest[kFront]))), -D->LookYawPitchLimit.X, D->LookYawPitchLimit.X);
			const float Pit = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2((L | Chest[kUp]), FVector2D((L | Chest[kRight]), (L | Chest[kFront])).Size())), -D->LookYawPitchLimit.Y, D->LookYawPitchLimit.Y);
			const float FYaw = Yaw * W * Share[RI];
			const float FPit = FMath::Lerp(P.HeadPitch, Pit, W) * Share[RI];
			const FQuat T = FQuat(Chest[kUp], FMath::DegreesToRadians(FYaw)) * FQuat(Chest[kRight], FMath::DegreesToRadians(FPit)) * ChestDelta * RefCS[i].GetRotation();
			const FFallBoneGroupTuning& G = D->Group(EFallBoneGroup::Head);
			if (SpringValid[i]) SpringW[i] += (GustKick * 0.3f + HitAxis * 0.8f);
			Rot = Flutter(i, G.NoiseDeg.Y * 0.3f * In.Wind * FlutterMul) * Spring(i, Noise(i, FMath::Lerp(G.NoiseDeg.X, G.NoiseDeg.Y, In.Wind) * NoiseMul) * T, G, StiffMul, Dt);
			break;
		}
		case ERole::Clavicle:
		{
			const FFallLimb Arm = P.GetArm(Limbs[RI].bRight);
			const FVector Dc = ChestDelta.RotateVector(Limbs[RI].RootDir);
			const FVector Dr = RotateToward(RotateToward(Dc, Chest[kUp], Arm.RootRaise), Chest[kFront], Arm.RootForward);
			const FQuat T = FQuat::FindBetweenNormals(Dc, Dr) * ChestDelta * RefCS[i].GetRotation();
			const FFallBoneGroupTuning& G = D->Group(EFallBoneGroup::Clavicle);
			if (SpringValid[i] && Limbs[RI].bRight && In.Recoil > 0.f) SpringW[i] += (Dc ^ Chest[kUp]).GetSafeNormal() * In.Recoil * 8.f;
			Rot = Spring(i, T, G, StiffMul, Dt);
			break;
		}
		case ERole::Upper:
		{
			FLimbRef& L = Limbs[RI];
			const FFallLimb LP = L.bArm ? P.GetArm(L.bRight) : P.GetLeg(L.bRight);
			SolveLimb(L, LP, L.bArm ? Chest : Basis, Inherit.GetLocation(), Dt);
			const FQuat T = FrameXZ(L.TU, L.TH) * FrameXZ(L.DirU, L.Hinge).Inverse() * RefCS[i].GetRotation();
			const FFallBoneGroupTuning& G = D->Group(EFallBoneGroup::UpperLimb);
			const bool bKickLeg = !L.bArm && In.KickTime >= 0.f && L.bRight == In.bKickRight;
			if (SpringValid[i])
			{
				SpringW[i] += GustKick * 1.2f + HitAxis * 1.4f;
				if (L.bArm && L.bRight && In.Recoil > 0.f) SpringW[i] += (L.TU ^ Chest[kUp]).GetSafeNormal() * In.Recoil * 2.f; // the arm rises with the shot
			}
			Rot = Flutter(i, G.NoiseDeg.Y * 0.3f * In.Wind * FlutterMul) * Spring(i, Noise(i, FMath::Lerp(G.NoiseDeg.X, G.NoiseDeg.Y, In.Wind) * NoiseMul) * T, G, StiffMul * (bKickLeg ? 3.f : 1.f), Dt);
			break;
		}
		case ERole::Lower:
		{
			FLimbRef& L = Limbs[RI];
			// Hinge of the actual (lagging) upper bone, so the joint always bends in its plane.
			const FQuat UpperDelta = OutCS[L.Upper].GetRotation() * RefCS[L.Upper].GetRotation().Inverse();
			const FVector HingeNow = UpperDelta.RotateVector(L.Hinge);
			FVector DL = L.TL;
			const FVector UNow = UpperDelta.RotateVector(L.DirU);
			if (((UNow ^ DL) | HingeNow) < 0.f) DL = UNow;
			const FQuat T = FrameXZ(DL, HingeNow) * FrameXZ(L.DirL, L.Hinge).Inverse() * RefCS[i].GetRotation();
			const FFallBoneGroupTuning& G = D->Group(EFallBoneGroup::LowerLimb);
			const bool bKickLeg = !L.bArm && In.KickTime >= 0.f && L.bRight == In.bKickRight;
			if (SpringValid[i]) SpringW[i] += GustKick * 1.5f + HitAxis * 1.2f;
			Rot = Flutter(i, G.NoiseDeg.Y * 0.3f * In.Wind * FlutterMul) * Spring(i, Noise(i, FMath::Lerp(G.NoiseDeg.X, G.NoiseDeg.Y, In.Wind) * NoiseMul) * T, G, StiffMul * (bKickLeg ? 3.f : 1.f), Dt);
			break;
		}
		case ERole::End:
		{
			FLimbRef& L = Limbs[RI];
			const FQuat LowerDelta = OutCS[L.Lower].GetRotation() * RefCS[L.Lower].GetRotation().Inverse();
			const FVector HingeNow = LowerDelta.RotateVector(L.Hinge);
			const FVector DirNow = LowerDelta.RotateVector(L.DirL);
			// The gun hand keeps a straight wrist: the barrel continues the forearm, no wind noise on it.
			const bool bGunHand = L.bArm && L.bRight && In.bGunInRight;
			const FQuat T = bGunHand ? Inherit.GetRotation() : FQuat(DirNow, FMath::DegreesToRadians(L.Twist)) * FQuat(HingeNow, FMath::DegreesToRadians(L.Flex)) * Inherit.GetRotation();
			const FFallBoneGroupTuning& G = D->Group(EFallBoneGroup::Extremity);
			if (SpringValid[i])
			{
				SpringW[i] += GustKick * 2.f;
				if (L.bArm && L.bRight && In.Recoil > 0.f) SpringW[i] += (L.TL ^ Chest[kUp]).GetSafeNormal() * In.Recoil * 3.f; // muzzle flips up a few degrees, wrist returns on the spring
			}
			const float HandNoise = bGunHand ? 0.1f : 1.f;
			Rot = Flutter(i, G.NoiseDeg.Y * 0.3f * In.Wind * FlutterMul * HandNoise) * Spring(i, Noise(i, FMath::Lerp(G.NoiseDeg.X, G.NoiseDeg.Y, In.Wind) * NoiseMul * HandNoise) * T, G, StiffMul * (bGunHand ? 2.f : 1.f), Dt);
			break;
		}
		case ERole::Finger:
		{
			const FLimbRef& L = Limbs[RI];
			const FQuat HandDelta = OutCS[L.End].GetRotation() * RefCS[L.End].GetRotation().Inverse();
			const float Flutter = FMath::PerlinNoise1D(Clock * 5.f + i * 0.37f) * 0.08f * In.Wind;
			float Curl = L.Curl + Flutter;
			if (RI == 1 && In.bGunInRight)
			{
				// Wrapped round the grip; the index finger lies along the frame and moves onto the trigger to fire.
				Curl = IndexFinger[i] ? FMath::Lerp(0.18f, 0.5f, In.TriggerPull) : FMath::Max(Curl, 0.95f);
			}
			Rot = FQuat(HandDelta.RotateVector(CurlAxisRef[RI]), FMath::DegreesToRadians(55.f * FMath::Clamp(Curl, 0.f, 1.f))) * Rot;
			break;
		}
		default:
			break;
		}

		OutCS[i] = FTransform(Rot, Inherit.GetLocation(), Inherit.GetScale3D());
		OutLocal[i] = Par >= 0 ? OutCS[i].GetRelativeTransform(OutCS[Par]) : OutCS[i];
	}

	static double LastLog = 0.0;
	if (FPlatformTime::Seconds() - LastLog > 1.0 && FParse::Param(FCommandLine::Get(), TEXT("FallAnimDebug")))
	{
		LastLog = FPlatformTime::Seconds();
		for (int32 l = 0; l < 4; ++l)
		{
			const FLimbRef& L = Limbs[l];
			auto B = [&](const FVector& V) { return FVector(V | Basis[kRight], V | Basis[kFront], V | Basis[kUp]); };
			const FVector UA = (OutCS[L.Lower].GetLocation() - OutCS[L.Upper].GetLocation()).GetSafeNormal();
			const FVector LA = (OutCS[L.End].GetLocation() - OutCS[L.Lower].GetLocation()).GetSafeNormal();
			UE_LOG(LogTemp, Display, TEXT("FallAnimDebug limb %d: targetU %s actualU %s | targetL %s actualL %s | hinge %s refHinge %s"), l,
				*B(L.TU).ToCompactString(), *B(UA).ToCompactString(), *B(L.TL).ToCompactString(), *B(LA).ToCompactString(), *B(L.TH).ToCompactString(), *B(L.Hinge).ToCompactString());
		}
	}

	for (FCompactPoseBoneIndex CI : Output.Pose.ForEachBoneIndex())
	{
		const int32 MeshIdx = BC.MakeMeshPoseIndex(CI).GetInt();
		if (OutLocal.IsValidIndex(MeshIdx))
		{
			Output.Pose[CI] = OutLocal[MeshIdx];
		}
	}
	return true;
}
