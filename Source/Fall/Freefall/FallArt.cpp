#include "FallArt.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"

namespace
{
	// "/Game/A/B" -> soft path "/Game/A/B.B"
	FSoftObjectPath P(const TCHAR* Package)
	{
		const FString S(Package);
		return FSoftObjectPath(S + TEXT(".") + FPaths::GetBaseFilename(S));
	}
	// Poly Haven model: /Game/Freefall/Assets/Models/<id>/<id>_2k/StaticMeshes/<mesh>
	FSoftObjectPath PH(const TCHAR* Id, const TCHAR* Mesh)
	{
		return P(*FString::Printf(TEXT("/Game/Freefall/Assets/Models/%s/%s_2k/StaticMeshes/%s"), Id, Id, Mesh));
	}
	FSoftObjectPath Zep(const TCHAR* Name) { return P(*FString::Printf(TEXT("/Game/Freefall/Assets/Debris/SM_Zep_%s"), Name)); }

	void Add(TArray<FFallDebrisArt>& Out, EFallDebrisType T, std::initializer_list<FSoftObjectPath> Meshes, float Scale, bool bGrab, float Mass, bool bHollow = false, bool bSpins = false)
	{
		FFallDebrisArt A;
		A.Type = T;
		for (const FSoftObjectPath& M : Meshes) A.Meshes.Add(TSoftObjectPtr<UStaticMesh>(M));
		A.Scale = Scale;
		A.bGrabbable = bGrab;
		A.Mass = Mass;
		A.bHollow = bHollow;
		A.bSpins = bSpins;
		Out.Add(A);
	}
}

UFallArtData::UFallArtData()
{
	using T = EFallDebrisType;
	// From pocket-sized trash to 25 m frame rings. Grabbable pieces are the ones with visible handholds.
	Add(Debris, T::Shards, {PH(TEXT("metal_jerrycan_green"), TEXT("metal_jerrycan_green_body")), PH(TEXT("vintage_flashlight"), TEXT("vintage_flashlight_2k")),
		PH(TEXT("korean_fire_extinguisher_01"), TEXT("korean_fire_extinguisher_01_2k")), PH(TEXT("modular_pipes"), TEXT("pipe_100cm_metal")),
		PH(TEXT("modular_pipes"), TEXT("pipe_thin_200cm_metal")), PH(TEXT("industrial_wall_lamp"), TEXT("industrial_wall_lamp_2k")),
		PH(TEXT("caged_hanging_light"), TEXT("caged_hanging_light_2k")), PH(TEXT("power_box_01"), TEXT("power_box_01_door")),
		PH(TEXT("lifebuoy"), TEXT("lifebuoy_2k")), PH(TEXT("ammo_box"), TEXT("ammo_box")), PH(TEXT("metal_stool_02"), TEXT("metal_stool_02_2k")),
		PH(TEXT("fire_alarm"), TEXT("fire_alarm")), Zep(TEXT("AnchorPoint"))}, 1.4f, false, 0.3f);
	Add(Debris, T::Crate, {Zep(TEXT("Crate")), PH(TEXT("wooden_military_crate"), TEXT("wooden_military_crate")), PH(TEXT("old_military_crate"), TEXT("old_military_crate_a")),
		PH(TEXT("old_military_crate"), TEXT("old_military_crate_b")), PH(TEXT("medical_box"), TEXT("medical_box")), PH(TEXT("metal_tool_chest"), TEXT("metal_tool_chest_chest"))}, 1.3f, false, 1.2f);
	Add(Debris, T::Barrel, {Zep(TEXT("Barrel")), PH(TEXT("Barrel_01"), TEXT("Barrel_01_2k")), PH(TEXT("barrel_03"), TEXT("barrel_03_2k")),
		PH(TEXT("propane_tank"), TEXT("propane_tank_2k")), PH(TEXT("small_lpg_tank"), TEXT("small_lpg_tank_2k"))}, 1.3f, false, 1.f);
	Add(Debris, T::Rack, {PH(TEXT("worn_metal_rack"), TEXT("worn_metal_rack_2k")), PH(TEXT("industrial_storage_cart"), TEXT("industrial_storage_cart_2k")),
		PH(TEXT("metal_office_desk"), TEXT("metal_office_desk")), Zep(TEXT("VentBox")), PH(TEXT("utility_box_01"), TEXT("utility_box_01_2k"))}, 1.3f, false, 1.5f);
	Add(Debris, T::Plate, {Zep(TEXT("BlastDoor")), PH(TEXT("rollershutter_door"), TEXT("rollershutter_door")), Zep(TEXT("Pallet"))}, 1.7f, true, 1.f);
	Add(Debris, T::Girder, {Zep(TEXT("Railing")), PH(TEXT("ladder_sectioned_01"), TEXT("ladder_section_01")), PH(TEXT("modular_fire_escape"), TEXT("modular_fire_escape_ladder_bottom"))}, 1.f, true, 1.5f);
	Add(Debris, T::Ductwork, {PH(TEXT("modular_airduct_circular_01"), TEXT("modular_airduct_circular_triple")), PH(TEXT("modular_airduct_rectangular_01"), TEXT("modular_airduct_rectangular_01_tripple_01")),
		PH(TEXT("modular_airduct_rectangular_01"), TEXT("modular_airduct_rectangular_01_transition_double_01")), PH(TEXT("modular_industrial_pipes_01"), TEXT("modular_industrial_pipes_01_pipe02")),
		PH(TEXT("modular_industrial_pipes_01"), TEXT("modular_industrial_pipes_01_pipe03")), Zep(TEXT("SupplyPipe"))}, 1.6f, true, 1.2f);
	Add(Debris, T::Engine, {Zep(TEXT("Generator")), PH(TEXT("old_military_compressor"), TEXT("old_military_compressor_2k")), PH(TEXT("portable_generator"), TEXT("portable_generator")),
		PH(TEXT("exterior_aircon_unit"), TEXT("exterior_aircon_unit_rusted"))}, 1.4f, true, 3.f);
	Add(Debris, T::Searchlight, {Zep(TEXT("Searchlight")), PH(TEXT("hanging_industrial_lamp"), TEXT("hanging_industrial_lamp_2k"))}, 1.f, false, 2.f);
	Add(Debris, T::Lifeboat, {Zep(TEXT("Lifeboat"))}, 1.3f, true, 2.f);
	Add(Debris, T::Stairs, {Zep(TEXT("Stairs")), PH(TEXT("modular_fire_escape"), TEXT("modular_fire_escape_stairs"))}, 1.f, true, 3.f);
	Add(Debris, T::Tarp, {Zep(TEXT("Tarp"))}, 1.f, true, 2.5f);
	Add(Debris, T::Section, {PH(TEXT("modular_fire_escape"), TEXT("modular_fire_escape_platform_middle")), PH(TEXT("modular_fire_escape"), TEXT("modular_fire_escape_platform_bottom"))}, 1.45f, true, 6.f);
	Add(Debris, T::Crane, {Zep(TEXT("Crane")), PH(TEXT("overhead_crane"), TEXT("overhead_crane"))}, 1.f, true, 5.f);
	Add(Debris, T::FrameRing, {Zep(TEXT("FrameRing")), Zep(TEXT("FrameRingWide"))}, 1.f, false, 8.f, true);
	Add(Debris, T::Propeller, {Zep(TEXT("Propeller"))}, 0.6f, false, 4.f, true, true);

	for (const TCHAR* C : {TEXT("Envelope_L"), TEXT("Envelope_R"), TEXT("Propeller_L"), TEXT("Propeller_R"), TEXT("Engine_L"), TEXT("Engine_R"),
		TEXT("Hull_Bow"), TEXT("Hull_Mid"), TEXT("Hull_Stern"), TEXT("Deckhouse")})
	{
		AirshipChunks.Add(TSoftObjectPtr<UStaticMesh>(P(*FString::Printf(TEXT("/Game/Freefall/Assets/Airship/SM_Airship_%s"), C))));
	}

	CharacterMesh = TSoftObjectPtr<USkeletalMesh>(P(TEXT("/Game/RetroFuturisticSoldier/Meshes/SKM_Soldier_Manny")));
	PlayerMaterial = TSoftObjectPtr<UMaterialInterface>(P(TEXT("/Game/Freefall/Materials/MI_Char_Player")));
	ShooterMaterial = TSoftObjectPtr<UMaterialInterface>(P(TEXT("/Game/Freefall/Materials/MI_Char_Shooter")));
	WingsuitMaterial = TSoftObjectPtr<UMaterialInterface>(P(TEXT("/Game/Freefall/Materials/MI_Char_Wingsuit")));
	HeavyMaterial = TSoftObjectPtr<UMaterialInterface>(P(TEXT("/Game/Freefall/Materials/MI_Char_Heavy")));
	CarrierMaterial = TSoftObjectPtr<UMaterialInterface>(P(TEXT("/Game/Freefall/Materials/MI_Char_Carrier")));
	ParachuteContainer = TSoftObjectPtr<UStaticMesh>(P(TEXT("/Game/Freefall/Assets/Meshes/SM_ParachuteContainer")));
	Canopy = TSoftObjectPtr<UStaticMesh>(P(TEXT("/Game/Freefall/Assets/Meshes/SM_Canopy")));
	Turret = TSoftObjectPtr<UStaticMesh>(P(TEXT("/Game/Freefall/Assets/Meshes/SM_Turret")));

	Pistol = TSoftObjectPtr<UStaticMesh>(P(TEXT("/Game/M1911/Meshes/NonRigged_M1911")));
	Magazine = TSoftObjectPtr<UStaticMesh>(P(TEXT("/Game/M1911/Meshes/NonRigged_M1911_Magazine")));
	Casing = TSoftObjectPtr<UStaticMesh>(P(TEXT("/Game/M1911/Meshes/NonRigged_45ACP_Ammo")));

	for (const TCHAR* T : {TEXT("spruce_full_01_low"), TEXT("spruce_full_02_low"), TEXT("spruce_full_03_low")})
	{
		Trees.Add(TSoftObjectPtr<UStaticMesh>(P(*FString::Printf(TEXT("/Game/PN_interactiveSpruceForest/Meshes/full/low/%s"), T))));
	}
	auto Snd = [this](const TCHAR* Key, const TCHAR* Package) { Sounds.Add(Key, TSoftObjectPtr<USoundBase>(P(Package))); };
	Snd(TEXT("Gunshot"), TEXT("/Game/FreeWeaponSFX/Cue/Handgun/Gunshots/sfx_wpn_hgn_gunshot_01_Cue"));
	Snd(TEXT("EnemyShot"), TEXT("/Game/FreeWeaponSFX/Cue/AssaultRifle/Gunshots/sfx_wpn_ar_gunshot_01_Cue"));
	Snd(TEXT("Explosion"), TEXT("/Game/FreeWeaponSFX/Cue/GrenadeLauncher/sfx_wpn_gl_explosion_Cue"));
	Snd(TEXT("MagOut"), TEXT("/Game/FreeWeaponSFX/Cue/Handgun/Reload/sfx_wpn_hgn_reload_02_magOut_Cue"));
	Snd(TEXT("MagIn"), TEXT("/Game/FreeWeaponSFX/Cue/Handgun/Reload/sfx_wpn_hgn_reload_04_magIn_Cue"));
	Snd(TEXT("Charge"), TEXT("/Game/FreeWeaponSFX/Cue/Handgun/Reload/sfx_wpn_hgn_reload_05_charge_Cue"));

	FxQuad = TSoftObjectPtr<UStaticMesh>(P(TEXT("/Game/Freefall/Assets/Meshes/SM_FF_Quad")));
	FxBeam = TSoftObjectPtr<UStaticMesh>(P(TEXT("/Game/Freefall/Assets/Meshes/SM_FF_Beam")));
	FxBall = TSoftObjectPtr<UStaticMesh>(P(TEXT("/Game/Freefall/Assets/Meshes/SM_FF_Ball")));
	Explosion = TSoftObjectPtr<UNiagaraSystem>(P(TEXT("/Game/Freefall/FX/FX_ExplosionMid")));
	ExplosionSmall = TSoftObjectPtr<UNiagaraSystem>(P(TEXT("/Game/Freefall/FX/FX_ExplosionMedium")));
	ExplosionLarge = TSoftObjectPtr<UNiagaraSystem>(P(TEXT("/Game/Freefall/FX/FX_ExplosionLarge")));
	Fire = TSoftObjectPtr<UNiagaraSystem>(P(TEXT("/Game/Freefall/FX/FX_Fire")));
	FireLarge = TSoftObjectPtr<UNiagaraSystem>(P(TEXT("/Game/Freefall/FX/FX_FireLarge")));
	Sparks = TSoftObjectPtr<UNiagaraSystem>(P(TEXT("/Game/Freefall/FX/FX_Sparks")));
	MuzzleFlash = TSoftObjectPtr<UNiagaraSystem>(P(TEXT("/Game/Freefall/FX/FX_Muzzle")));
	ImpactMetal = TSoftObjectPtr<UNiagaraSystem>(P(TEXT("/Game/Freefall/FX/FX_ImpactMetal")));
}

const UFallArtData* UFallArtData::Get()
{
	static TWeakObjectPtr<UFallArtData> Cached;
	if (!Cached.IsValid())
	{
		Cached = LoadObject<UFallArtData>(nullptr, TEXT("/Game/Freefall/Data/DA_FallArt.DA_FallArt"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Cached.IsValid()) return GetDefault<UFallArtData>();
	}
	return Cached.Get();
}

const FFallDebrisArt* UFallArtData::FindDebris(EFallDebrisType Type) const
{
	return Debris.FindByPredicate([Type](const FFallDebrisArt& A) { return A.Type == Type && A.Meshes.Num() > 0; });
}
