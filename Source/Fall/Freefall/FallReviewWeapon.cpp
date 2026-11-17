// Weapon-in-hand check (every frame of every review run) and the scripted weapon test (-FallWeaponTest):
// the player goes through every state (after the cutscene, fall, dive, brake, fire, reload, grab, after a segment
// restart); in each the pistol must be the art set's pistol, visible, attached to hand_r and touching the hand.
// Four close-ups of the gun hand per state go to <dir>/closeup_<state>_<n>.png.
#include "FallReview.h"
#include "FallArt.h"
#include "FallWorld.h"
#include "FallDebris.h"
#include "FallDirector.h"
#include "FallEnemy.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimInstance.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Animation/Skeleton.h"

FString AFallReview::WeaponProblem(AFallPlayer* P)
{
	const UFallArtData* Art = UFallArtData::Get();
	if (P->Mesh->GetSkeletalMeshAsset() != Art->CharacterMesh.Get()) return TEXT("character is not the art set's mesh");
	UStaticMeshComponent* W = P->WeaponMesh;
	if (!W) return TEXT("no weapon component");
	if (!W->GetStaticMesh() || W->GetStaticMesh() != Art->Pistol.Get()) return TEXT("weapon is not the art set's pistol");
	if (!W->IsVisible() || W->bHiddenInGame || P->IsHidden()) return TEXT("weapon hidden");
	if (W->GetAttachParent() != P->Mesh || W->GetAttachSocketName() != FName("hand_r")) return TEXT("weapon not attached to hand_r");
	if (W->GetComponentScale().GetMin() < 0.5f || W->GetComponentScale().GetMax() > 2.f) return TEXT("weapon scale");
	FBox Hand(ForceInit);
	for (const TCHAR* B : {TEXT("hand_r"), TEXT("index_01_r"), TEXT("middle_01_r"), TEXT("pinky_01_r"), TEXT("thumb_01_r"), TEXT("middle_02_r")})
	{
		Hand += P->Mesh->GetSocketLocation(B);
	}
	if (!W->Bounds.GetBox().Intersect(Hand.ExpandBy(1.f))) return TEXT("weapon box does not touch the hand");
	return FString();
}

void AFallReview::CheckWeapon(AFallPlayer* P, UFallWorldSubsystem* FW)
{
	// Labels are not exclusive: a frame in a dive while reloading after a restart counts for all three.
	if (P->bInIntro) { bWasIntro = true; return; } // the cutscene shows the gun only from the moment it is picked up
	if (bWasIntro) { bWasIntro = false; AfterIntroUntil = T + 5.f; }
	if (FW)
	{
		for (; SeenWeaponEvents < FW->Events.Num(); ++SeenWeaponEvents)
		{
			if (FW->Events[SeenWeaponEvents].Name == FName("Restart") && T > 1.f) AfterRestartUntil = T + 5.f;
		}
	}
	if (P->IsDead()) return;
	if (P->bCanopy)
	{
		// Under the canopy the hands hold the carrier's harness: only the close-ups run.
		if (Bot == TEXT("weapon") || bCloseups)
		{
			TArray<FString> L;
			for (TActorIterator<AFallEnemy> It(GetWorld()); It; ++It) if (It->IsTandem()) L.Add(TEXT("tandem_canopy"));
			for (const FString& S : L) { float& Since = LabelSince.FindOrAdd(S); if (Since < 0.f) Since = T; }
			TickCloseups(P, L);
		}
		return;
	}
	TArray<FString> Labels;
	Labels.Add(P->Dive > 0.5f ? TEXT("dive") : (P->Dive < -0.5f ? TEXT("brake") : TEXT("fall")));
	if (P->TriggerPull > 0.3f) Labels.Add(TEXT("fire"));
	if (P->IsReloading()) Labels.Add(TEXT("reload"));
	if (P->IsHolding()) Labels.Add(TEXT("grab"));
	if (T < AfterIntroUntil) Labels.Add(TEXT("after_intro"));
	if (T < AfterRestartUntil) Labels.Add(TEXT("after_restart"));
	const FString Why = WeaponProblem(P);
	for (const FString& L : Labels)
	{
		FWeaponStat& S = WeaponStats.FindOrAdd(L);
		++S.Frames;
		if (!Why.IsEmpty())
		{
			++S.Bad;
			if (S.FirstBad.IsEmpty()) S.FirstBad = FString::Printf(TEXT("%.2f s: %s"), T, *Why);
		}
		float& Since = LabelSince.FindOrAdd(L);
		if (Since < 0.f) Since = T;
	}
	for (TPair<FString, float>& Pr : LabelSince) if (!Labels.Contains(Pr.Key)) Pr.Value = -1.f;
	if (Bot == TEXT("weapon") || bCloseups)
	{
		for (TActorIterator<AFallEnemy> It(GetWorld()); It; ++It)
		{
			if (!It->IsTandem()) continue;
			Labels.Add(P->bCanopy ? TEXT("tandem_canopy") : TEXT("tandem_hook"));
			float& Since = LabelSince.FindOrAdd(Labels.Last());
			if (Since < 0.f) Since = T;
		}
		TickCloseups(P, Labels);
	}
}

void AFallReview::TickCloseups(AFallPlayer* P, const TArray<FString>& Labels)
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC) return;
	// The third reload is filmed whole from one angle, every second frame: every phase of the montage in a row.
	if (CloseStep < 0 && !bSeqDone && P->IsReloading() && ReloadResults.Num() >= 2)
	{
		if (!CloseCam.IsValid()) { CloseStep = 0; CloseState = TEXT("seq"); }
		else
		{
			bSeqActive = true;
			const FVector Chest = P->Mesh->GetSocketLocation("spine_05");
			const FVector Up = (P->Mesh->GetSocketLocation("neck_01") - P->Mesh->GetSocketLocation("pelvis")).GetSafeNormal();
			const FVector Right = (P->Mesh->GetSocketLocation("upperarm_r") - P->Mesh->GetSocketLocation("upperarm_l")).GetSafeNormal();
			const FVector D = ((Right ^ Up) * 1.f - Right * 0.6f + Up * 0.15f).GetSafeNormal(); // front-left, the reloading hand's side
			CloseCam->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			const FVector Aim = (Chest + P->Mesh->GetSocketLocation("hand_r")) * 0.5f;
			CloseCam->SetActorLocationAndRotation(Aim + D * 200.f, (-D).Rotation());
			PC->SetViewTarget(CloseCam.Get());
			if ((SeqFrame++ % 2) == 1) Shot(FString::Printf(TEXT("reloadseq_%02d"), SeqFrame / 2));
			return;
		}
	}
	if (bSeqActive && !P->IsReloading()) { bSeqActive = false; bSeqDone = true; PC->SetViewTarget(P); }
	if (CloseStep < 0)
	{
		for (const FString& L : Labels)
		{
			if (CloseDone.Contains(L) || T - LabelSince.FindRef(L) < 0.4f) continue;
			CloseState = L;
			CloseStep = 0;
			CloseDone.Add(L);
			break;
		}
		if (CloseStep < 0) return;
	}
	if (!CloseCam.IsValid())
	{
		CloseCam = GetWorld()->SpawnActor<ACameraActor>();
		CloseCam->GetCameraComponent()->SetFieldOfView(40.f);
		CloseCam->GetCameraComponent()->bConstrainAspectRatio = false;
		FPostProcessSettings& PP = CloseCam->GetCameraComponent()->PostProcessSettings;
		PP.bOverride_MotionBlurAmount = true; PP.MotionBlurAmount = 0.f; // the camera rides the hand: no smear from the world rushing past
	}
	// Even steps move the camera, odd steps take the shot of the frame rendered with it.
	const int32 View = CloseStep / 2;
	if (View >= 4)
	{
		PC->SetViewTarget(P);
		CloseCam->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		CloseStep = -1;
		return;
	}
	FVector Hand = P->Mesh->GetSocketLocation("hand_r");
	float CamDist = 70.f;
	if (CloseState.StartsWith(TEXT("tandem")))
	{
		for (TActorIterator<AFallEnemy> It(GetWorld()); It; ++It)
			if (It->IsTandem()) Hand = (P->Mesh->GetSocketLocation("spine_05") + It->Mesh->GetSocketLocation("spine_04")) * 0.5f;
		CamDist = 420.f;
	}
	const FVector Up = (P->Mesh->GetSocketLocation("neck_01") - P->Mesh->GetSocketLocation("pelvis")).GetSafeNormal();
	const FVector Right = (P->Mesh->GetSocketLocation("upperarm_r") - P->Mesh->GetSocketLocation("upperarm_l")).GetSafeNormal();
	const FVector Front = (Right ^ Up).GetSafeNormal();
	static const float Dirs[4][3] = {{1.f, 0.3f, 0.3f}, {0.3f, 1.f, 0.2f}, {-0.5f, 0.6f, 0.8f}, {0.4f, -0.3f, -0.9f}}; // front, outside, over the shoulder, from below
	const FVector D = (Front * Dirs[View][0] + Right * Dirs[View][1] + Up * Dirs[View][2]).GetSafeNormal();
	if (CloseStep % 2 == 0)
	{
		CloseCam->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		CloseCam->SetActorLocationAndRotation(Hand + D * CamDist, (-D).Rotation());
		CloseCam->AttachToComponent(P->Mesh, FAttachmentTransformRules::KeepWorldTransform, "hand_r");
		PC->SetViewTarget(CloseCam.Get());
	}
	else Shot(FString::Printf(TEXT("closeup_%s_%d"), *CloseState, View));
	++CloseStep;
}

void AFallReview::WeaponTestInput(float Dt, FFallInputFrame& Out)
{
	AFallPlayer* P = Cast<AFallPlayer>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!P || P->bInIntro) return;
	WT += Dt;
	auto In = [&](float A, float B) { return WT >= A && WT < B; };
	auto Pulse = [&](float A) { return WT >= A && WT - Dt < A; };
	if (In(3.f, 6.f) || In(28.f, 30.f)) Out.Dive = 1.f;
	if (In(6.f, 9.f)) Out.Dive = -1.f;
	const bool bShooting = In(4.f, 5.f) || In(7.f, 8.f) || In(9.f, 12.f) || In(18.f, 19.f) || In(26.f, 27.5f);
	if (bShooting) { Out.bAim = true; Out.bFire = FMath::Fmod(WT, 0.3f) < Dt * 1.5f; Out.Look = FVector2D(6.f, 0.f) * Dt; }
	if (Pulse(12.5f) || Pulse(19.5f) || Pulse(28.f)) Out.bReload = true;
	if (Pulse(14.f))
	{
		if (AFallDebrisSpawner* Sp = AFallDebrisSpawner::Get(this))
		{
			GrabPiece = Sp->SpawnPiece(EFallDebrisType::Plate, FVector(0, 0, -600.f), FVector(0, 0, 3.f), 1.f, false);
		}
	}
	if (AFallDebris* D = GrabPiece.Get())
	{
		D->Mover->VelocityH = FVector::ZeroVector;
		if (!P->IsHolding() && WT < 23.f && D->DistanceTo(P->GetActorLocation()) < 120.f) Out.bGrab = true;
	}
	if (Pulse(23.f))
	{
		if (P->IsHolding()) P->Release();
		if (AFallDebris* D = GrabPiece.Get()) D->Destroy();
	}
	if (Pulse(24.f))
	{
		if (AFallDirector* Dir_ = AFallDirector::Get(this)) Dir_->JumpTo(20.f);
	}
	if (WT > 33.f) Duration = 0.f;
}

void AFallReview::WriteWeaponMetrics(TSharedPtr<FJsonObject> Root)
{
	TSharedPtr<FJsonObject> WO = MakeShared<FJsonObject>();
	int32 Bad = 0;
	for (const TPair<FString, FWeaponStat>& Pr : WeaponStats)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("frames"), Pr.Value.Frames);
		O->SetNumberField(TEXT("bad"), Pr.Value.Bad);
		O->SetStringField(TEXT("first_bad"), Pr.Value.FirstBad);
		WO->SetObjectField(Pr.Key, O);
		Bad += Pr.Value.Bad;
	}
	Root->SetObjectField(TEXT("weapon_in_hand"), WO);
	Root->SetArrayField(TEXT("reloads"), ReloadResults);
	Root->SetNumberField(TEXT("reload_fails"), ReloadFails);
	TSharedPtr<FJsonObject> RO = MakeShared<FJsonObject>();
	int32 NoRecoil = 0;
	for (const TPair<FString, FIntPoint>& Pr : RecoilStats)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("shots"), Pr.Value.X);
		O->SetNumberField(TEXT("visible_recoil"), Pr.Value.Y);
		RO->SetObjectField(Pr.Key, O);
		NoRecoil += Pr.Value.X - Pr.Value.Y;
	}
	Root->SetObjectField(TEXT("recoil"), RO);
	TSharedPtr<FJsonObject> TO = MakeShared<FJsonObject>();
	TO->SetNumberField(TEXT("frames"), TandemFrames);
	TO->SetNumberField(TEXT("frames_line_through_player"), LineHits);
	TO->SetNumberField(TEXT("min_line_to_player_cm"), LineMinCm);
	TO->SetStringField(TEXT("closest_line_bone"), LineWorst);
	TO->SetNumberField(TEXT("frames_bodies_overlap"), BodyOverlap);
	TO->SetNumberField(TEXT("min_body_to_body_cm"), BodyMinCm);
	TO->SetNumberField(TEXT("frames_hands_off_straps"), HandsOff);
	TO->SetNumberField(TEXT("mean_hand_to_strap_cm"), TandemFrames ? HandGapSum / (2.f * TandemFrames) : -1.f);
	TO->SetNumberField(TEXT("max_up_angle_between_bodies_deg"), UpAngleMax);
	TO->SetNumberField(TEXT("mean_carrier_ahead_of_player_chest_cm"), TandemFrames ? AheadSum / TandemFrames : 0.f);
	TO->SetNumberField(TEXT("mean_facing_dot"), TandemFrames ? FacingSum / TandemFrames : 0.f);
	TO->SetNumberField(TEXT("mean_pack_behind_carrier_cm"), TandemFrames ? PackBackSum / TandemFrames : 0.f);
	TO->SetNumberField(TEXT("mean_pack_pivot_behind_carrier_cm"), TandemFrames ? PackPivotBackSum / TandemFrames : 0.f);
	TO->SetNumberField(TEXT("frames_pack_between_bodies"), PackBetween);
	TO->SetNumberField(TEXT("mean_left_ik_target_to_strap_cm"), TandemFrames ? TargetGapSum / TandemFrames : -1.f);
	TO->SetNumberField(TEXT("mean_left_ik_weight"), TandemFrames ? WeightSum / TandemFrames : -1.f);
	TO->SetNumberField(TEXT("mean_left_shoulder_to_strap_cm"), TandemFrames ? ReachSum / TandemFrames : -1.f);
	Root->SetObjectField(TEXT("tandem"), TO);
	Root->SetNumberField(TEXT("shots_without_recoil"), NoRecoil);
	Root->SetNumberField(TEXT("weapon_in_hand_bad_frames"), Bad);
	if (Bot == TEXT("weapon"))
	{
		static const TCHAR* Need[] = {TEXT("after_intro"), TEXT("fall"), TEXT("dive"), TEXT("brake"), TEXT("fire"), TEXT("reload"), TEXT("grab"), TEXT("after_restart")};
		TArray<TSharedPtr<FJsonValue>> Missing;
		for (const TCHAR* N : Need) if (WeaponStats.FindRef(N).Frames == 0) Missing.Add(MakeShared<FJsonValueString>(N));
		Root->SetArrayField(TEXT("weapon_states_missing"), Missing);
		Root->SetBoolField(TEXT("weapon_test_pass"), Bad == 0 && Missing.Num() == 0 && ReloadFails == 0 && ReloadResults.Num() >= 3 && NoRecoil == 0);
	}
}

void AFallReview::TickBake(AFallPlayer* P)
{
	if (P->IsReloading())
	{
		bBakeStarted = true;
		TArray<TSharedPtr<FJsonValue>> Bones;
		for (const FTransform& X : P->Mesh->GetBoneSpaceTransforms())
		{
			const FQuat Q = X.GetRotation();
			const FVector L = X.GetLocation();
			for (double V : {Q.X, Q.Y, Q.Z, Q.W, L.X, L.Y, L.Z}) Bones.Add(MakeShared<FJsonValueNumber>(V));
		}
		BakeFrames.Add(MakeShared<FJsonValueArray>(Bones));
		return;
	}
	if (!bBakeStarted || Duration < 1e8f) return;
	// One full reload recorded: bone names, frame rate and frames (quat xyzw, translation xyz per bone).
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	const FReferenceSkeleton& Ref = P->Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
	TArray<TSharedPtr<FJsonValue>> Names;
	for (int32 i = 0; i < Ref.GetNum(); ++i) Names.Add(MakeShared<FJsonValueString>(Ref.GetBoneName(i).ToString()));
	Root->SetArrayField(TEXT("bones"), Names);
	Root->SetNumberField(TEXT("fps"), 30);
	Root->SetStringField(TEXT("skeleton"), P->Mesh->GetSkeletalMeshAsset()->GetSkeleton()->GetPathName());
	Root->SetArrayField(TEXT("frames"), BakeFrames);
	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root.ToSharedRef(), W);
	FFileHelper::SaveStringToFile(Out, *(Dir / TEXT("reload_bake.json")));
	Duration = 0.f;
}

void AFallReview::CheckReloadAndRecoil(AFallPlayer* P, float SelfPen)
{
	if (P->bInIntro || P->bCanopy || P->IsDead()) return;
	const FString Ctx = P->IsHolding() ? TEXT("grab") : (P->Dive > 0.5f ? TEXT("dive") : (P->Dive < -0.5f ? TEXT("brake") : TEXT("fall")));
	// Reload: one record per reload, judged when it ends.
	if (P->IsReloading())
	{
		if (CurReload.Start < 0.f) { CurReload = FReloadRec(); CurReload.Start = T; CurReload.Expected = P->ReloadDuration(); CurReload.Ctx = Ctx; }
		++CurReload.Frames;
		if (P->IsReloadMontagePlaying()) ++CurReload.MontageFrames;
		const FVector HL = P->Mesh->GetSocketLocation("hand_l");
		CurReload.MinPouch = FMath::Min(CurReload.MinPouch, (float)FVector::Dist(HL, P->ReloadPouchPoint()));
		CurReload.MinWell = FMath::Min(CurReload.MinWell, (float)FVector::Dist(HL, P->ReloadWellPoint()));
		if (SelfPen > 1.f) ++CurReload.SelfInt;
	}
	else if (CurReload.Start >= 0.f)
	{
		const float Dur = T - CurReload.Start;
		const bool bMontage = CurReload.MontageFrames >= CurReload.Frames * 0.8f;
		const bool bOk = bMontage && FMath::Abs(Dur - CurReload.Expected) < 0.15f && CurReload.MinPouch < 10.f && CurReload.MinWell < 8.f && CurReload.SelfInt == 0;
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("t"), CurReload.Start);
		O->SetStringField(TEXT("state"), CurReload.Ctx);
		O->SetNumberField(TEXT("duration_s"), Dur);
		O->SetNumberField(TEXT("expected_s"), CurReload.Expected);
		O->SetNumberField(TEXT("montage_frames"), CurReload.MontageFrames);
		O->SetNumberField(TEXT("frames"), CurReload.Frames);
		O->SetNumberField(TEXT("left_hand_min_to_pouch_cm"), CurReload.MinPouch);
		O->SetNumberField(TEXT("left_hand_min_to_well_cm"), CurReload.MinWell);
		O->SetNumberField(TEXT("self_intersection_frames"), CurReload.SelfInt);
		O->SetBoolField(TEXT("pass"), bOk);
		ReloadResults.Add(MakeShared<FJsonValueObject>(O));
		if (!bOk) ++ReloadFails;
		CurReload.Start = -1.f;
	}
	// Recoil: the muzzle must turn at least 1 degree within 8 frames of a shot, in every state.
	const FVector Muz = P->MuzzleForward();
	if (P->TriggerPull > PrevTrigger + 0.5f && P->WeaponMesh)
	{
		FRecoilRec R; R.Dir = Muz; R.Left = 8; R.Ctx = Ctx;
		RecoilOpen.Add(R);
	}
	PrevTrigger = P->TriggerPull;
	for (int32 i = RecoilOpen.Num() - 1; i >= 0; --i)
	{
		FRecoilRec& R = RecoilOpen[i];
		R.MaxDeg = FMath::Max(R.MaxDeg, FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp((float)(R.Dir | Muz), -1.f, 1.f))));
		if (--R.Left > 0) continue;
		FIntPoint& S = RecoilStats.FindOrAdd(R.Ctx);
		++S.X;
		if (R.MaxDeg >= 1.f) ++S.Y;
		RecoilOpen.RemoveAt(i);
	}
}

void AFallReview::CheckTandem(AFallPlayer* P)
{
	AFallEnemy* C = nullptr;
	for (TActorIterator<AFallEnemy> It(GetWorld()); It; ++It) if (It->IsTandem()) C = *It;
	if (!C) return;
	++TandemFrames;
	// Lines vs the player's head, torso and arms (capsule radii roughly the body's thickness).
	TArray<TPair<FVector, FVector>> Lines;
	if (const AFallDirector* D = AFallDirector::Get(this)) D->LineSegments(Lines);
	struct FPart { const TCHAR* A; const TCHAR* B; float R; };
	static const FPart Parts[] = {{TEXT("head"), TEXT("head"), 13.f}, {TEXT("spine_02"), TEXT("neck_01"), 16.f}, {TEXT("upperarm_l"), TEXT("lowerarm_l"), 6.f}, {TEXT("upperarm_r"), TEXT("lowerarm_r"), 6.f},
		{TEXT("lowerarm_l"), TEXT("hand_l"), 5.f}, {TEXT("lowerarm_r"), TEXT("hand_r"), 5.f}};
	bool bHit = false;
	for (const TPair<FVector, FVector>& L : Lines)
	{
		for (const FPart& Pt : Parts)
		{
			const FVector A = P->Mesh->GetSocketLocation(Pt.A), B = P->Mesh->GetSocketLocation(Pt.B);
			FVector X, Y;
			FMath::SegmentDistToSegmentSafe(L.Key, L.Value, A, B, X, Y);
			const float Gap = (float)FVector::Dist(X, Y) - Pt.R;
			if (Gap < LineMinCm) { LineMinCm = Gap; LineWorst = Pt.A; }
			if (Gap < 0.f) bHit = true;
		}
	}
	if (bHit) ++LineHits;
	// Bodies: torso capsules (pelvis to neck, ~17 cm radius each) must not overlap.
	FVector X, Y;
	FMath::SegmentDistToSegmentSafe(P->Mesh->GetSocketLocation("pelvis"), P->Mesh->GetSocketLocation("neck_01"), C->Mesh->GetSocketLocation("pelvis"), C->Mesh->GetSocketLocation("neck_01"), X, Y);
	const float BodyGap = (float)FVector::Dist(X, Y) - 34.f;
	BodyMinCm = FMath::Min(BodyMinCm, BodyGap);
	if (BodyGap < 0.f) ++BodyOverlap;
	// Hands on the harness: the left on its strap, the right on its strap or at the ripcord on the container.
	const FVector Pel = C->Mesh->GetSocketLocation("pelvis");
	const FVector U = (C->Mesh->GetSocketLocation("neck_01") - Pel).GetSafeNormal();
	const FVector R = (C->Mesh->GetSocketLocation("upperarm_r") - C->Mesh->GetSocketLocation("upperarm_l")).GetSafeNormal();
	const FVector Back = -(R ^ U).GetSafeNormal();
	const FVector CC = C->Mesh->GetSocketLocation("spine_04");
	const FVector SL = CC - R * 30.f + Back * 22.f + U * 4.f, SR = CC + R * 30.f + Back * 22.f + U * 4.f;
	const FVector Pack = C->Pack ? C->Pack->Bounds.Origin : CC;
	const bool bLeft = FVector::Dist(P->Mesh->GetSocketLocation("hand_l"), SL) < 15.f;
	const bool bRight = FVector::Dist(P->Mesh->GetSocketLocation("hand_r"), SR) < 15.f || FVector::Dist(P->Mesh->GetSocketLocation("hand_r"), Pack) < 70.f;
	if (!bLeft || !bRight) ++HandsOff;
	HandGapSum += FVector::Dist(P->Mesh->GetSocketLocation("hand_l"), SL) + FVector::Dist(P->Mesh->GetSocketLocation("hand_r"), SR);
	// Diagnostics: do the bodies share an attitude, and where do the IK targets point.
	const FVector PU = (P->Mesh->GetSocketLocation("neck_01") - P->Mesh->GetSocketLocation("pelvis")).GetSafeNormal();
	UpAngleMax = FMath::Max(UpAngleMax, FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp((float)(PU | U), -1.f, 1.f))));
	TargetGapSum += FVector::Dist(P->Control.HandTarget[0], SL);
	WeightSum += P->Control.HandWeight[0];
	ReachSum += FVector::Dist(P->Mesh->GetSocketLocation("upperarm_l"), SL);
	const FVector PR = (P->Mesh->GetSocketLocation("upperarm_r") - P->Mesh->GetSocketLocation("upperarm_l")).GetSafeNormal();
	const FVector PF = (PR ^ PU).GetSafeNormal();
	AheadSum += (C->Mesh->GetSocketLocation("spine_04") - P->Mesh->GetSocketLocation("spine_04")) | PF;
	FacingSum += PF | (R ^ U).GetSafeNormal();
	const FVector PackOff = (C->Pack ? C->Pack->Bounds.Origin : CC) - CC;
	PackBackSum += PackOff | Back;
	if (C->Pack)
	{
		const FVector PC = P->Mesh->GetSocketLocation("spine_05");
		PackPivotBackSum += (C->Pack->GetComponentLocation() - CC) | Back;
		if (FVector::Dist(PC, C->Pack->GetComponentLocation()) < FVector::Dist(PC, CC)) ++PackBetween;
	}
}
