#include "FallSequence.h"

UFallSequence::UFallSequence()
{
	BuildDefault();
}

float UFallSequence::AltitudeAt(float Time) const
{
	const FRichCurve* C = NominalAltitude.GetRichCurveConst();
	return C && C->GetNumKeys() > 0 ? C->Eval(Time) : 9000.f - Time * 50.f;
}

float UFallSequence::SpeedAt(float Time) const
{
	return FMath::Max(0.f, (AltitudeAt(Time - 0.5f) - AltitudeAt(Time + 0.5f)));
}

float UFallSequence::RestartPointBefore(float Time) const
{
	float Best = 0.f;
	for (const FFallSequenceEvent& E : Events)
	{
		if (E.Type == EFallEventType::SegmentStart && E.bRestartPoint && E.Time <= Time + KINDA_SMALL_NUMBER) Best = FMath::Max(Best, E.Time);
	}
	return Best;
}

void UFallSequence::SortEvents()
{
	Events.StableSort([](const FFallSequenceEvent& A, const FFallSequenceEvent& B) { return A.Time < B.Time; });
}

void UFallSequence::BuildDefault()
{
	Events.Reset();
	FRichCurve* Alt = NominalAltitude.GetRichCurve();
	Alt->Reset();
	Alt->AddKey(0.f, 9300.f);
	Alt->AddKey(120.f, 3700.f);
	Alt->AddKey(175.f, 350.f);

	auto Ev = [this](float T, EFallEventType Type, const TCHAR* Note) -> FFallSequenceEvent&
	{
		FFallSequenceEvent& E = Events.AddDefaulted_GetRef();
		E.Time = T;
		E.Type = Type;
		E.Note = Note;
		return E;
	};
	auto Seg = [&](float T, FName Name, bool bRestart, const TCHAR* Note) { FFallSequenceEvent& E = Ev(T, EFallEventType::SegmentStart, Note); E.Segment = Name; E.bRestartPoint = bRestart; };
	auto End = [&](float T, FName Name) { FFallSequenceEvent& E = Ev(T, EFallEventType::SegmentEnd, TEXT("")); E.Segment = Name; };
	auto Wave = [&](float T, EFallDebrisType Type, float Density, FVector2D Rel, float Spread, float Close, float Dur, float Burn, const TCHAR* Note, FVector2D Scale = FVector2D(0.8f, 1.2f))
	{
		FFallSequenceEvent& E = Ev(T, EFallEventType::DebrisWave, Note);
		E.Wave.Type = Type; E.Wave.Density = Density; E.Wave.RelativeSpeed = Rel; E.Wave.Spread = Spread;
		E.Wave.CloseCall = Close; E.Wave.Duration = Dur; E.Wave.BurnChance = Burn; E.Wave.Scale = Scale;
	};
	auto Enemy = [&](float T, EFallEnemyType Type, FVector Pos, const TCHAR* Note) { FFallSequenceEvent& E = Ev(T, EFallEventType::SpawnEnemy, Note); E.EnemyType = Type; E.Position = Pos; };
	auto Shot = [&](float T, EFallOneShot Type, FVector Pos, const TCHAR* Note) { FFallSequenceEvent& E = Ev(T, EFallEventType::OneShot, Note); E.OneShot = Type; E.Position = Pos; };
	auto Clouds = [&](float T, float D) { FFallSequenceEvent& E = Ev(T, EFallEventType::CloudDensity, TEXT("")); E.Value = D; };

	// 1. Breakout, 0-20 s: the airship explodes above; chunks plunge past, so diving away and braking behind cover come naturally. No enemies.
	Seg(0.f, "Breakout", true, TEXT("Airship explodes above the player"));
	Clouds(0.f, 0.7f);
	Shot(0.f, EFallOneShot::AirshipBreakup, FVector(62.f, 6.f, 8.f), TEXT("Burning airship ahead and above, breaks apart after 1.6 s"));
	{
		FFallSequenceEvent& E = Ev(5.5f, EFallEventType::DebrisPiece, TEXT("A hull plate tumbles right past the player"));
		E.PieceType = EFallDebrisType::Plate; E.Position = FVector(6.f, 2.f, 45.f); E.Velocity = FVector(0.f, 0.f, -22.f); E.Value = 1.3f;
	}
	Wave(12.f, EFallDebrisType::Shards, 3.f, FVector2D(-14.f, -8.f), 6.f, 0.8f, 5.f, 0.f, TEXT("A shower of fragments from above: brake behind the big section"));
	Wave(3.f, EFallDebrisType::Shards, 3.f, FVector2D(-20.f, -10.f), 12.f, 0.5f, 6.f, 0.f, TEXT("Fragments rain from above: dive to outrun them"));
	Wave(3.5f, EFallDebrisType::Plate, 0.6f, FVector2D(-16.f, -9.f), 10.f, 0.6f, 6.f, 0.3f, TEXT("Hull plates falling onto the player"));
	Shot(6.f, EFallOneShot::EngineExplosion, FVector(10.f, -7.f, 14.f), TEXT("Engine pod blows up close"));
	{
		FFallSequenceEvent& E = Ev(8.f, EFallEventType::DebrisPiece, TEXT("Big hull section rises slowly from below: brake to meet it, grab it"));
		E.PieceType = EFallDebrisType::Section; E.Position = FVector(6.f, 0.f, -70.f); E.Velocity = FVector(0.f, 0.f, 6.f); E.Value = 0.85f;
	}
	Wave(11.f, EFallDebrisType::Plate, 0.5f, FVector2D(3.f, 8.f), 14.f, 0.4f, 9.f, 0.2f, TEXT("Slow plates to grab and push off"));
	Wave(14.f, EFallDebrisType::Shards, 2.f, FVector2D(6.f, 14.f), 16.f, 0.4f, 6.f, 0.f, TEXT("Speed reference"));
	End(19.9f, "Breakout");

	// 2. Debris field, 20-70 s: dense cover, the first shooters one at a time, then in pairs.
	Seg(20.f, "DebrisField", true, TEXT("Dense field of debris, first shooters"));
	Clouds(20.f, 1.f);
	Wave(20.f, EFallDebrisType::Plate, 0.8f, FVector2D(3.f, 12.f), 22.f, 0.35f, 50.f, 0.2f, TEXT("Cover plates"), FVector2D(1.1f, 1.6f));
	Wave(30.f, EFallDebrisType::Section, 0.08f, FVector2D(2.f, 5.f), 18.f, 0.5f, 40.f, 0.3f, TEXT("Big hull sections: real cover"), FVector2D(0.7f, 0.9f));
	Wave(20.f, EFallDebrisType::Girder, 0.35f, FVector2D(4.f, 12.f), 24.f, 0.3f, 50.f, 0.f, TEXT("Girders"));
	Wave(20.f, EFallDebrisType::Crate, 0.4f, FVector2D(6.f, 15.f), 20.f, 0.3f, 50.f, 0.f, TEXT("Cargo: hits hard"));
	Wave(20.f, EFallDebrisType::Shards, 1.5f, FVector2D(8.f, 16.f), 18.f, 0.5f, 50.f, 0.f, TEXT("Fragments"));
	Enemy(24.f, EFallEnemyType::Shooter, FVector(25.f, -12.f, -6.f), TEXT("First shooter, alone, in front"));
	Enemy(33.f, EFallEnemyType::Shooter, FVector(24.f, 14.f, 4.f), TEXT("Second shooter from the right"));
	Enemy(44.f, EFallEnemyType::Shooter, FVector(28.f, -18.f, -3.f), TEXT("Pair"));
	Enemy(46.f, EFallEnemyType::Shooter, FVector(26.f, 18.f, -8.f), TEXT("Pair"));
	Wave(52.f, EFallDebrisType::Engine, 0.2f, FVector2D(5.f, 10.f), 20.f, 0.3f, 15.f, 1.f, TEXT("Burning engines"));
	Enemy(56.f, EFallEnemyType::Shooter, FVector(22.f, 0.f, -10.f), TEXT("Shooter below"));
	Shot(62.f, EFallOneShot::BigFlyby, FVector(10.f, 8.f, 60.f), TEXT("Huge section drops past"));
	End(69.9f, "DebrisField");

	// 3. Attack from above, 70-120 s: a heavy turret holds the centre, wingsuiters make runs.
	Seg(70.f, "AttackFromAbove", true, TEXT("Heavy turret and wingsuit runs"));
	Wave(70.f, EFallDebrisType::Plate, 0.45f, FVector2D(3.f, 10.f), 22.f, 0.3f, 50.f, 0.2f, TEXT("Cover"), FVector2D(1.1f, 1.6f));
	Wave(70.f, EFallDebrisType::Shards, 1.f, FVector2D(8.f, 16.f), 18.f, 0.5f, 50.f, 0.f, TEXT("Fragments"));
	Enemy(72.f, EFallEnemyType::Heavy, FVector(26.f, 0.f, -4.f), TEXT("Heavy on a hull section with a shielded turret"));
	Enemy(78.f, EFallEnemyType::Wingsuiter, FVector(40.f, -50.f, 20.f), TEXT("First wingsuiter: three runs"));
	Enemy(90.f, EFallEnemyType::Wingsuiter, FVector(40.f, 50.f, 25.f), TEXT("Second wingsuiter"));
	Enemy(96.f, EFallEnemyType::Shooter, FVector(24.f, -16.f, 0.f), TEXT("Support shooter"));
	Enemy(104.f, EFallEnemyType::Wingsuiter, FVector(-40.f, 40.f, 20.f), TEXT("Wingsuiter from behind"));
	Shot(108.f, EFallOneShot::EngineExplosion, FVector(15.f, 10.f, 8.f), TEXT("Explosion"));
	End(119.9f, "AttackFromAbove");

	// 4. Chase, 120-170 s: the parachute carrier dives through the cloud deck; the ground is coming.
	Seg(120.f, "Chase", true, TEXT("Catch the parachute carrier"));
	{
		FFallSequenceEvent& E = Ev(120.f, EFallEventType::AltitudeLive, TEXT("Altitude now follows the real fall speed"));
		E.Value = 3700.f;
	}
	Enemy(120.f, EFallEnemyType::Target, FVector(18.f, 4.f, -6.f), TEXT("The one with the parachute"));
	Ev(122.f, EFallEventType::TargetFlee, TEXT("Target dives away"));
	Clouds(123.f, 2.5f);
	Enemy(127.f, EFallEnemyType::Shooter, FVector(20.f, -10.f, -15.f), TEXT("Escort shooter"));
	Enemy(138.f, EFallEnemyType::Wingsuiter, FVector(40.f, 40.f, 10.f), TEXT("Escort wingsuiter"));
	Clouds(142.f, 0.8f);
	Wave(150.f, EFallDebrisType::Shards, 1.f, FVector2D(8.f, 14.f), 16.f, 0.4f, 15.f, 0.f, TEXT("Last fragments"));
	End(169.9f, "Chase");

	// 5. Finale, 170-180 s: the parachute opens, or the ground.
	Seg(170.f, "Finale", false, TEXT("Canopy or impact"));
	SortEvents();
}
