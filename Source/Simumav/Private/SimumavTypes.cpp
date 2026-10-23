#include "SimumavTypes.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Policies/PrettyJsonPrintPolicy.h"

// ---------------------------------------------------------------------------
// SmvJson
// ---------------------------------------------------------------------------

double SmvJson::Num(const TSharedPtr<FJsonObject>& O, const TCHAR* K, double Def)
{
	if (!O.IsValid()) return Def;
	const TSharedPtr<FJsonValue> V = O->TryGetField(K);
	if (!V.IsValid() || V->IsNull()) return Def;
	if (V->Type == EJson::Number) return V->AsNumber();
	if (V->Type == EJson::Boolean) return V->AsBool() ? 1.0 : 0.0;
	if (V->Type == EJson::String) { double D = Def; LexFromString(D, *V->AsString()); return D; }
	return Def;
}

FString SmvJson::Str(const TSharedPtr<FJsonObject>& O, const TCHAR* K, const FString& Def)
{
	if (!O.IsValid()) return Def;
	const TSharedPtr<FJsonValue> V = O->TryGetField(K);
	if (!V.IsValid() || V->IsNull()) return Def;
	if (V->Type == EJson::String) return V->AsString();
	if (V->Type == EJson::Number) return LexToString(V->AsNumber());
	if (V->Type == EJson::Boolean) return V->AsBool() ? TEXT("true") : TEXT("false");
	return Def;
}

bool SmvJson::Bool(const TSharedPtr<FJsonObject>& O, const TCHAR* K, bool Def)
{
	if (!O.IsValid()) return Def;
	const TSharedPtr<FJsonValue> V = O->TryGetField(K);
	if (!V.IsValid() || V->IsNull()) return Def;
	if (V->Type == EJson::Boolean) return V->AsBool();
	if (V->Type == EJson::Number) return V->AsNumber() != 0.0;
	return Def;
}

TSharedPtr<FJsonObject> SmvJson::Obj(const TSharedPtr<FJsonObject>& O, const TCHAR* K)
{
	if (!O.IsValid()) return nullptr;
	const TSharedPtr<FJsonObject>* P = nullptr;
	if (O->TryGetObjectField(K, P) && P) return *P;
	return nullptr;
}

FString SmvJson::Id(const TSharedPtr<FJsonObject>& O, const TCHAR* K)
{
	if (!O.IsValid()) return FString();
	const TSharedPtr<FJsonValue> V = O->TryGetField(K);
	if (!V.IsValid() || V->IsNull()) return FString();
	if (V->Type == EJson::Number) return FString::Printf(TEXT("%lld"), (long long)V->AsNumber());
	return V->AsString();
}

FString SmvJson::ToString(const TSharedRef<FJsonObject>& O, bool bPretty)
{
	FString Out;
	if (bPretty)
	{
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> W = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(O, W);
	}
	else
	{
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(O, W);
	}
	return Out;
}

TSharedPtr<FJsonObject> SmvJson::Parse(const FString& S)
{
	TSharedPtr<FJsonObject> O;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(S);
	if (!FJsonSerializer::Deserialize(R, O)) return nullptr;
	return O;
}

bool SmvJson::IsServerId(const FString& InId)
{
	// parametre adı SmvJson::Id işleviyle aynı olmasın (gölgeleme uyarısı C4459 UE'de hata)
	return !InId.IsEmpty() && InId.IsNumeric();
}

static void SmvSetIdField(const TSharedRef<FJsonObject>& O, const TCHAR* K, const FString& InId)
{
	if (SmvJson::IsServerId(InId)) O->SetNumberField(K, FCString::Atod(*InId));
	else if (!InId.IsEmpty()) O->SetStringField(K, InId);
}

// ---------------------------------------------------------------------------

FSmvUser FSmvUser::FromJson(const TSharedPtr<FJsonObject>& O)
{
	FSmvUser U;
	U.Id = (int64)SmvJson::Num(O, TEXT("id"));
	U.Email = SmvJson::Str(O, TEXT("email"));
	U.DisplayName = SmvJson::Str(O, TEXT("display_name"), SmvJson::Str(O, TEXT("name")));
	U.Lang = SmvJson::Str(O, TEXT("lang"));
	return U;
}

TSharedRef<FJsonObject> FSmvPowerSettings::ToJson() const
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	if (MaxSpeedMs > 0) O->SetNumberField(TEXT("max_speed_ms"), MaxSpeedMs);
	if (FuelL >= 0) O->SetNumberField(TEXT("fuel_l"), FuelL);
	if (FuelKmPerL > 0) O->SetNumberField(TEXT("fuel_km_per_l"), FuelKmPerL);
	if (ElecCells > 0) O->SetNumberField(TEXT("elec_cells"), ElecCells);
	if (ElecMah > 0) O->SetNumberField(TEXT("elec_mah"), ElecMah);
	if (ElecW >= 0) O->SetNumberField(TEXT("elec_w"), ElecW);
	return O;
}

FSmvPowerSettings FSmvPowerSettings::FromJson(const TSharedPtr<FJsonObject>& O)
{
	FSmvPowerSettings P;
	if (!O.IsValid()) return P;
	P.MaxSpeedMs = SmvJson::Num(O, TEXT("max_speed_ms"), -1);
	P.FuelL = SmvJson::Num(O, TEXT("fuel_l"), -1);
	P.FuelKmPerL = SmvJson::Num(O, TEXT("fuel_km_per_l"), -1);
	P.ElecCells = (int32)SmvJson::Num(O, TEXT("elec_cells"), -1);
	P.ElecMah = SmvJson::Num(O, TEXT("elec_mah"), -1);
	P.ElecW = SmvJson::Num(O, TEXT("elec_w"), -1);
	return P;
}

TSharedRef<FJsonObject> FSmvDrone::ToJson(bool bForCreate) const
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	if (!bForCreate) SmvSetIdField(O, TEXT("id"), Id);
	O->SetStringField(TEXT("name"), Name.Left(40));
	if (bForCreate) O->SetStringField(TEXT("type"), Type);
	O->SetStringField(TEXT("model_key"), ModelKey);
	O->SetStringField(TEXT("color"), Color);
	if (Type == TEXT("plane")) O->SetStringField(TEXT("airframe"), Airframe);
	TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
	for (const TPair<FString, double>& Kv : Params) P->SetNumberField(Kv.Key, Kv.Value);
	O->SetObjectField(TEXT("params"), P);
	O->SetObjectField(TEXT("power"), Power.ToJson());
	if (!bForCreate) O->SetBoolField(TEXT("is_default"), bIsDefault);
	return O;
}

FSmvDrone FSmvDrone::FromJson(const TSharedPtr<FJsonObject>& O)
{
	FSmvDrone D;
	D.Id = SmvJson::Id(O, TEXT("id"));
	D.Name = SmvJson::Str(O, TEXT("name"));
	D.Type = SmvJson::Str(O, TEXT("type"), TEXT("multirotor"));
	D.ModelKey = SmvJson::Str(O, TEXT("model_key"), TEXT("glb:iris"));
	D.Color = SmvJson::Str(O, TEXT("color"), TEXT("#e53935"));
	D.Airframe = SmvJson::Str(O, TEXT("airframe"), TEXT("classic"));
	D.Drive = SmvJson::Str(O, TEXT("drive"));
	D.bIsDefault = SmvJson::Bool(O, TEXT("is_default"));
	if (const TSharedPtr<FJsonObject> P = SmvJson::Obj(O, TEXT("params")))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Kv : P->Values)
		{
			if (Kv.Value.IsValid() && Kv.Value->Type == EJson::Number) D.Params.Add(Kv.Key, Kv.Value->AsNumber());
		}
	}
	D.Power = FSmvPowerSettings::FromJson(SmvJson::Obj(O, TEXT("power")));
	return D;
}

TSharedRef<FJsonObject> FSmvAvatar::ToJson() const
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetNumberField(TEXT("v"), 1);
	O->SetStringField(TEXT("helmet"), Helmet);
	O->SetStringField(TEXT("visor"), Visor);
	O->SetBoolField(TEXT("mirror"), bMirror);
	O->SetStringField(TEXT("suit"), Suit);
	O->SetStringField(TEXT("skin"), Skin);
	O->SetStringField(TEXT("patch"), Patch);
	O->SetStringField(TEXT("patch_color"), PatchColor);
	O->SetStringField(TEXT("accessory"), Accessory);
	return O;
}

FSmvAvatar FSmvAvatar::FromJson(const TSharedPtr<FJsonObject>& O)
{
	FSmvAvatar A;
	if (!O.IsValid()) return A;
	A.Helmet = SmvJson::Str(O, TEXT("helmet"), A.Helmet);
	A.Visor = SmvJson::Str(O, TEXT("visor"), A.Visor);
	A.bMirror = SmvJson::Bool(O, TEXT("mirror"), A.bMirror);
	A.Suit = SmvJson::Str(O, TEXT("suit"), A.Suit);
	A.Skin = SmvJson::Str(O, TEXT("skin"), A.Skin);
	A.Patch = SmvJson::Str(O, TEXT("patch"), A.Patch);
	A.PatchColor = SmvJson::Str(O, TEXT("patch_color"), A.PatchColor);
	A.Accessory = SmvJson::Str(O, TEXT("accessory"), A.Accessory);
	return A;
}

TSharedRef<FJsonObject> FSmvPilot::ToJson() const
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("name"), Name.Left(40));
	O->SetStringField(TEXT("character"), Character);
	O->SetObjectField(TEXT("avatar"), Avatar.ToJson());
	return O;
}

FSmvPilot FSmvPilot::FromJson(const TSharedPtr<FJsonObject>& O)
{
	FSmvPilot P;
	P.Id = SmvJson::Id(O, TEXT("id"));
	P.Name = SmvJson::Str(O, TEXT("name"));
	P.Character = SmvJson::Str(O, TEXT("character"), TEXT("p01"));
	P.bIsDefault = SmvJson::Bool(O, TEXT("is_default"));
	P.Flights = (int32)SmvJson::Num(O, TEXT("flights"));
	P.Hours = SmvJson::Num(O, TEXT("hours"));
	P.Avatar = FSmvAvatar::FromJson(SmvJson::Obj(O, TEXT("avatar")));
	return P;
}

TSharedRef<FJsonObject> FSmvTrack::ToJson() const
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("name"), Name.Left(60));
	O->SetNumberField(TEXT("lat"), Lat);
	O->SetNumberField(TEXT("lon"), Lon);
	O->SetNumberField(TEXT("radius_m"), FMath::Clamp(RadiusM, 200.0, 5000.0));
	TSharedRef<FJsonObject> W = MakeShared<FJsonObject>();
	W->SetNumberField(TEXT("speed_ms"), FMath::Clamp(WindMs, 0.0, 30.0));
	W->SetNumberField(TEXT("dir_deg"), FMath::Clamp(WindDir, 0.0, 359.99));
	W->SetNumberField(TEXT("gust"), FMath::Clamp(WindGust, 0.0, 20.0));
	O->SetObjectField(TEXT("wind"), W);
	O->SetStringField(TEXT("time_of_day"), TimeOfDay);
	O->SetStringField(TEXT("weather"), Weather);
	O->SetStringField(TEXT("world_mode"), WorldMode);
	TSharedRef<FJsonObject> Wd = MakeShared<FJsonObject>();
	Wd->SetNumberField(TEXT("seed"), World.Seed);
	Wd->SetNumberField(TEXT("size_m"), World.SizeM);
	Wd->SetNumberField(TEXT("tree_density"), World.TreeDensity);
	Wd->SetNumberField(TEXT("mountain_height"), World.MountainHeight);
	Wd->SetNumberField(TEXT("mountain_count"), World.MountainCount);
	Wd->SetNumberField(TEXT("building_count"), World.BuildingCount);
	Wd->SetBoolField(TEXT("runway"), World.bRunway);
	Wd->SetNumberField(TEXT("runway_heading"), World.RunwayHeading);
	Wd->SetNumberField(TEXT("water"), World.Water);
	Wd->SetNumberField(TEXT("fog"), World.Fog);
	if (World.LandingCircleM > 0) Wd->SetNumberField(TEXT("landing_circle_m"), World.LandingCircleM);
	O->SetObjectField(TEXT("world"), Wd);
	TArray<TSharedPtr<FJsonValue>> G;
	for (int32 i = 0; i < Gates.Num() && i < 50; ++i)
	{
		const FSmvGate& Gt = Gates[i];
		TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
		J->SetNumberField(TEXT("i"), i);
		J->SetNumberField(TEXT("lat"), Gt.Lat);
		J->SetNumberField(TEXT("lon"), Gt.Lon);
		J->SetNumberField(TEXT("agl_m"), FMath::Clamp(Gt.AglM, 1.0, 120.0));
		J->SetNumberField(TEXT("yaw_deg"), Gt.YawDeg);
		J->SetNumberField(TEXT("radius_m"), FMath::Clamp(Gt.RadiusM, 1.0, 20.0));
		J->SetStringField(TEXT("kind"), Gt.Kind);
		G.Add(MakeShared<FJsonValueObject>(J));
	}
	O->SetArrayField(TEXT("gates"), G);
	return O;
}

FSmvTrack FSmvTrack::FromJson(const TSharedPtr<FJsonObject>& O)
{
	FSmvTrack T;
	T.Id = SmvJson::Id(O, TEXT("id"));
	T.Name = SmvJson::Str(O, TEXT("name"));
	T.Lat = SmvJson::Num(O, TEXT("lat"), T.Lat);
	T.Lon = SmvJson::Num(O, TEXT("lon"), T.Lon);
	T.GroundMsl = SmvJson::Num(O, TEXT("ground_msl"), SmvJson::Num(O, TEXT("alt"), -10000));
	T.RadiusM = SmvJson::Num(O, TEXT("radius_m"), 2000);
	const TSharedPtr<FJsonObject> W = SmvJson::Obj(O, TEXT("wind"));
	T.WindMs = SmvJson::Num(W, TEXT("speed_ms"), 0);
	T.WindDir = SmvJson::Num(W, TEXT("dir_deg"), 0);
	T.WindGust = SmvJson::Num(W, TEXT("gust"), 0);
	T.TimeOfDay = SmvJson::Str(O, TEXT("time_of_day"), TEXT("day"));
	T.Weather = SmvJson::Str(O, TEXT("weather"), TEXT("clear"));
	T.WorldMode = SmvJson::Str(O, TEXT("world_mode"), TEXT("real"));
	if (const TSharedPtr<FJsonObject> Wd = SmvJson::Obj(O, TEXT("world")))
	{
		T.World.Seed = (int32)SmvJson::Num(Wd, TEXT("seed"), T.World.Seed);
		T.World.SizeM = SmvJson::Num(Wd, TEXT("size_m"), T.World.SizeM);
		T.World.TreeDensity = SmvJson::Num(Wd, TEXT("tree_density"), T.World.TreeDensity);
		T.World.MountainHeight = SmvJson::Num(Wd, TEXT("mountain_height"), T.World.MountainHeight);
		T.World.MountainCount = (int32)SmvJson::Num(Wd, TEXT("mountain_count"), T.World.MountainCount);
		T.World.BuildingCount = (int32)SmvJson::Num(Wd, TEXT("building_count"), T.World.BuildingCount);
		T.World.bRunway = SmvJson::Bool(Wd, TEXT("runway"), T.World.bRunway);
		T.World.RunwayHeading = SmvJson::Num(Wd, TEXT("runway_heading"), T.World.RunwayHeading);
		T.World.Water = SmvJson::Num(Wd, TEXT("water"), T.World.Water);
		T.World.Fog = SmvJson::Num(Wd, TEXT("fog"), T.World.Fog);
		T.World.LandingCircleM = SmvJson::Num(Wd, TEXT("landing_circle_m"), 0);
	}
	const TArray<TSharedPtr<FJsonValue>>* G = nullptr;
	if (O.IsValid() && O->TryGetArrayField(TEXT("gates"), G) && G)
	{
		for (const TSharedPtr<FJsonValue>& V : *G)
		{
			const TSharedPtr<FJsonObject> J = V.IsValid() ? V->AsObject() : nullptr;
			if (!J.IsValid()) continue;
			FSmvGate Gt;
			Gt.I = (int32)SmvJson::Num(J, TEXT("i"));
			Gt.Lat = SmvJson::Num(J, TEXT("lat"));
			Gt.Lon = SmvJson::Num(J, TEXT("lon"));
			Gt.AglM = SmvJson::Num(J, TEXT("agl_m"), 10);
			Gt.YawDeg = SmvJson::Num(J, TEXT("yaw_deg"));
			Gt.RadiusM = SmvJson::Num(J, TEXT("radius_m"), 5);
			Gt.Kind = SmvJson::Str(J, TEXT("kind"), TEXT("ring"));
			T.Gates.Add(Gt);
		}
	}
	T.bIsDefault = SmvJson::Bool(O, TEXT("is_default"));
	return T;
}

TSharedRef<FJsonObject> FSmvMission::ToJson() const
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	SmvSetIdField(O, TEXT("track_id"), TrackId);
	O->SetStringField(TEXT("name"), Name.Left(60));
	O->SetStringField(TEXT("kind"), Kind);
	const TSharedPtr<FJsonObject> P = SmvJson::Parse(PlanJson);
	// üçlü işleçte TSharedPtr/TSharedRef karışımı yerine açık tür
	O->SetObjectField(TEXT("plan"), P.IsValid() ? P : TSharedPtr<FJsonObject>(MakeShared<FJsonObject>()));
	return O;
}

FSmvMission FSmvMission::FromJson(const TSharedPtr<FJsonObject>& O)
{
	FSmvMission M;
	M.Id = SmvJson::Id(O, TEXT("id"));
	M.TrackId = SmvJson::Id(O, TEXT("track_id"));
	M.Name = SmvJson::Str(O, TEXT("name"));
	M.Kind = SmvJson::Str(O, TEXT("kind"), TEXT("free"));
	const TSharedPtr<FJsonObject> P = SmvJson::Obj(O, TEXT("plan"));
	M.PlanJson = P.IsValid() ? SmvJson::ToString(P.ToSharedRef()) : TEXT("{}");
	return M;
}

TSharedRef<FJsonObject> FSmvSettings::ToServerJson() const
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	if (!Lang.IsEmpty()) O->SetStringField(TEXT("lang"), Lang);
	O->SetStringField(TEXT("theme"), Theme);
	O->SetBoolField(TEXT("sound"), bSound);
	O->SetBoolField(TEXT("analog_fpv"), bAnalogFpv);
	O->SetBoolField(TEXT("music"), bMusic);
	O->SetNumberField(TEXT("volume"), FMath::Clamp(Volume, 0.0, 1.0));
	O->SetBoolField(TEXT("reduced_motion"), bReducedMotion);
	O->SetStringField(TEXT("units"), Units);
	O->SetBoolField(TEXT("leaderboard_hidden"), bLeaderboardHidden);
	return O;
}

void FSmvSettings::ApplyServerJson(const TSharedPtr<FJsonObject>& O)
{
	if (!O.IsValid()) return;
	Lang = SmvJson::Str(O, TEXT("lang"), Lang);
	Theme = SmvJson::Str(O, TEXT("theme"), Theme);
	bSound = SmvJson::Bool(O, TEXT("sound"), bSound);
	bAnalogFpv = SmvJson::Bool(O, TEXT("analog_fpv"), bAnalogFpv);
	bMusic = SmvJson::Bool(O, TEXT("music"), bMusic);
	Volume = SmvJson::Num(O, TEXT("volume"), Volume);
	bReducedMotion = SmvJson::Bool(O, TEXT("reduced_motion"), bReducedMotion);
	Units = SmvJson::Str(O, TEXT("units"), Units);
	bLeaderboardHidden = SmvJson::Bool(O, TEXT("leaderboard_hidden"), bLeaderboardHidden);
}

TSharedPtr<FJsonObject> FSmvLaunch::ToJson() const
{
	if (Method.IsEmpty()) return nullptr;
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("method"), Method);
	if (Method == TEXT("ramp")) O->SetNumberField(TEXT("angle_deg"), FMath::Clamp(AngleDeg, 10.0, 30.0));
	if (Method == TEXT("ramp") || Method == TEXT("vehicle")) O->SetNumberField(TEXT("release_kmh"), FMath::Clamp(ReleaseKmh, 40.0, 110.0));
	if (Method == TEXT("hand"))
	{
		O->SetNumberField(TEXT("hand_speed_ms"), FMath::Clamp(HandSpeedMs, 6.0, 8.0));
		O->SetNumberField(TEXT("hand_pitch_deg"), FMath::Clamp(HandPitchDeg, 10.0, 15.0));
	}
	if (Method == TEXT("pod4") || Method == TEXT("pod8") || Method == TEXT("tube") || Method == TEXT("handheld"))
	{
		O->SetNumberField(TEXT("loaded"), FMath::Clamp(Loaded, 1, 8));
		O->SetNumberField(TEXT("aim_az_deg"), FMath::Clamp(AimAzDeg, 0.0, 360.0));
		O->SetNumberField(TEXT("aim_el_deg"), FMath::Clamp(AimElDeg, 10.0, 85.0));
	}
	return O;
}

TSharedRef<FJsonObject> FSmvFlightRequest::ToStartJson() const
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	SmvSetIdField(O, TEXT("drone_id"), Drone.Id);
	SmvSetIdField(O, TEXT("pilot_id"), Pilot.Id);
	SmvSetIdField(O, TEXT("track_id"), Track.Id);
	if (bHasMission) SmvSetIdField(O, TEXT("mission_id"), Mission.Id);
	if (const TSharedPtr<FJsonObject> L = Launch.ToJson()) O->SetObjectField(TEXT("launch"), L);
	TSharedRef<FJsonObject> Rec = MakeShared<FJsonObject>();
	Rec->SetBoolField(TEXT("video"), false);
	Rec->SetBoolField(TEXT("bin"), bRecordBin);
	O->SetObjectField(TEXT("record"), Rec);
	O->SetObjectField(TEXT("power"), Power.ToJson());
	if (Speedup != 1.0) O->SetNumberField(TEXT("speedup"), Speedup);
	O->SetStringField(TEXT("camera"), Camera);
	if (Targets.Num())
	{
		TSharedRef<FJsonObject> T = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Items;
		for (int32 i = 0; i < Targets.Num() && i < 8; ++i)
		{
			const FSmvTarget& Tg = Targets[i];
			TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
			J->SetStringField(TEXT("model"), Tg.Model);
			J->SetNumberField(TEXT("alt_m"), Tg.AltM);
			J->SetNumberField(TEXT("diameter_m"), Tg.DiameterM);
			J->SetNumberField(TEXT("speed_ms"), Tg.SpeedMs);
			J->SetNumberField(TEXT("east_m"), Tg.EastM);
			J->SetNumberField(TEXT("north_m"), Tg.NorthM);
			J->SetStringField(TEXT("dir"), Tg.bCw ? TEXT("cw") : TEXT("ccw"));
			Items.Add(MakeShared<FJsonValueObject>(J));
		}
		T->SetArrayField(TEXT("items"), Items);
		T->SetNumberField(TEXT("radius_m"), FMath::Clamp(TargetRadiusM, 5.0, 200.0));
		O->SetObjectField(TEXT("targets"), T);
	}
	if (Drone.Type == TEXT("rover") && !Drone.Drive.IsEmpty()) O->SetStringField(TEXT("drive"), Drone.Drive);
	return O;
}

FSmvFlightInfo FSmvFlightInfo::FromJson(const TSharedPtr<FJsonObject>& O)
{
	FSmvFlightInfo F;
	F.FlightId = SmvJson::Id(O, TEXT("flight_id"));
	F.SimId = SmvJson::Id(O, TEXT("sim_id"));
	F.WsPath = SmvJson::Str(O, TEXT("ws"));
	F.State = SmvJson::Str(O, TEXT("state"));
	if (const TSharedPtr<FJsonObject> Mp = SmvJson::Obj(O, TEXT("mp")))
	{
		F.MpHost = SmvJson::Str(Mp, TEXT("host"));
		F.MpPort = (int32)SmvJson::Num(Mp, TEXT("port"));
		F.MpProto = SmvJson::Str(Mp, TEXT("proto"), TEXT("tcp"));
	}
	return F;
}

FSmvTeleExtra FSmvTeleExtra::FromJson(const TSharedPtr<FJsonObject>& O)
{
	FSmvTeleExtra E;
	E.Speedup = SmvJson::Num(O, TEXT("speedup"), 1);
	if (const TSharedPtr<FJsonObject> F = SmvJson::Obj(O, TEXT("fuel")))
	{
		E.bFuel = true;
		E.FuelL = SmvJson::Num(F, TEXT("l"));
		E.FuelPct = SmvJson::Num(F, TEXT("pct"));
		E.RangeKm = SmvJson::Num(F, TEXT("range_km"));
		E.bFuelEmpty = SmvJson::Bool(F, TEXT("empty"));
	}
	if (const TSharedPtr<FJsonObject> L = SmvJson::Obj(O, TEXT("elec")))
	{
		E.bElec = true;
		E.ElecWh = SmvJson::Num(L, TEXT("wh"));
		E.ElecPct = SmvJson::Num(L, TEXT("pct"));
		E.ElecW = SmvJson::Num(L, TEXT("w"));
		E.ElecMinLeft = SmvJson::Num(L, TEXT("min_left"), -1);
		E.ElecV = SmvJson::Num(L, TEXT("v"));
		E.bElecEmpty = SmvJson::Bool(L, TEXT("empty"));
	}
	E.WindSpd = SmvJson::Num(O, TEXT("wind_spd"));
	E.WindDir = SmvJson::Num(O, TEXT("wind_dir"));
	E.bMotorCut = SmvJson::Bool(O, TEXT("motor_cut"));
	E.SensBad = SmvJson::Num(O, TEXT("sens_bad"));
	E.Ekf = SmvJson::Num(O, TEXT("ekf"), -1);
	E.Vibe = SmvJson::Num(O, TEXT("vibe"), -1);
	E.TerrainAgl = SmvJson::Num(O, TEXT("terrain_agl"), -100000);
	E.bGcsConnected = SmvJson::Bool(O, TEXT("gcs_connected"));
	const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
	if (O.IsValid() && O->TryGetArrayField(TEXT("servo"), A) && A)
	{
		for (const TSharedPtr<FJsonValue>& V : *A) E.Servo.Add(V.IsValid() && !V->IsNull() ? (int32)V->AsNumber() : 0);
	}
	if (O.IsValid() && O->TryGetArrayField(TEXT("servo_fn"), A) && A)
	{
		for (const TSharedPtr<FJsonValue>& V : *A) E.ServoFn.Add(V.IsValid() && !V->IsNull() ? (int32)V->AsNumber() : 0);
	}
	return E;
}

FSmvFlightSummary FSmvFlightSummary::FromServerFlight(const TSharedPtr<FJsonObject>& F)
{
	FSmvFlightSummary S;
	S.FlightId = SmvJson::Id(F, TEXT("id"));
	S.Result = SmvJson::Str(F, TEXT("result"), TEXT("ok"));
	S.DurationS = SmvJson::Num(F, TEXT("duration_s"));
	const TSharedPtr<FJsonObject> Sum = SmvJson::Obj(F, TEXT("summary"));
	if (Sum.IsValid())
	{
		S.RawJson = SmvJson::ToString(Sum.ToSharedRef());
		if (const TSharedPtr<FJsonObject> A = SmvJson::Obj(Sum, TEXT("alerts")))
		{
			S.AlertScore = (int32)SmvJson::Num(A, TEXT("score"), -1);
			S.AlertGrade = SmvJson::Str(A, TEXT("grade"));
			S.Warnings = (int32)SmvJson::Num(A, TEXT("warnings"));
			S.Cautions = (int32)SmvJson::Num(A, TEXT("cautions"));
		}
		if (const TSharedPtr<FJsonObject> G = SmvJson::Obj(Sum, TEXT("gates")))
		{
			S.GatesPassed = (int32)SmvJson::Num(G, TEXT("passed"));
			S.GatesMissed = (int32)SmvJson::Num(G, TEXT("missed"));
			S.Laps = (int32)SmvJson::Num(G, TEXT("laps"));
			S.BestLapS = SmvJson::Num(G, TEXT("best_lap_s"));
		}
		if (const TSharedPtr<FJsonObject> P = SmvJson::Obj(Sum, TEXT("power")))
		{
			S.DistM = SmvJson::Num(P, TEXT("dist_m"));
			S.MaxSpeedMs = SmvJson::Num(P, TEXT("max_speed_seen"));
			S.ArmedS = SmvJson::Num(P, TEXT("armed_s"));
			S.FuelUsedL = SmvJson::Num(SmvJson::Obj(P, TEXT("fuel")), TEXT("used_l"));
			S.WhUsed = SmvJson::Num(SmvJson::Obj(P, TEXT("elec")), TEXT("wh_used"));
		}
		if (const TSharedPtr<FJsonObject> T = SmvJson::Obj(Sum, TEXT("targets")))
		{
			S.TargetScore = (int32)SmvJson::Num(T, TEXT("score"));
			const TArray<TSharedPtr<FJsonValue>>* B = nullptr;
			if (T->TryGetArrayField(TEXT("badges"), B) && B) for (const TSharedPtr<FJsonValue>& V : *B) S.Badges.Add(V->AsString());
		}
	}
	return S;
}

TSharedRef<FJsonObject> FSmvFlightSummary::ToJson() const
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("id"), FlightId);
	O->SetStringField(TEXT("result"), Result);
	O->SetStringField(TEXT("end_reason"), EndReason);
	O->SetNumberField(TEXT("duration_s"), DurationS);
	O->SetNumberField(TEXT("dist_m"), DistM);
	O->SetNumberField(TEXT("max_speed_seen"), MaxSpeedMs);
	O->SetNumberField(TEXT("armed_s"), ArmedS);
	if (AlertScore >= 0) { O->SetNumberField(TEXT("alert_score"), AlertScore); O->SetStringField(TEXT("alert_grade"), AlertGrade); }
	O->SetNumberField(TEXT("warnings"), Warnings);
	O->SetNumberField(TEXT("cautions"), Cautions);
	O->SetNumberField(TEXT("gates_passed"), GatesPassed);
	O->SetNumberField(TEXT("gates_missed"), GatesMissed);
	O->SetNumberField(TEXT("laps"), Laps);
	O->SetNumberField(TEXT("best_lap_s"), BestLapS);
	O->SetNumberField(TEXT("fuel_used_l"), FuelUsedL);
	O->SetNumberField(TEXT("wh_used"), WhUsed);
	O->SetNumberField(TEXT("target_score"), TargetScore);
	TArray<TSharedPtr<FJsonValue>> B;
	for (const FString& X : Badges) B.Add(MakeShared<FJsonValueString>(X));
	O->SetArrayField(TEXT("badges"), B);
	O->SetBoolField(TEXT("servo_trigger"), bServoTriggerFired);
	return O;
}

FSmvLeaderRow FSmvLeaderRow::FromJson(const TSharedPtr<FJsonObject>& O)
{
	FSmvLeaderRow R;
	R.Rank = (int32)SmvJson::Num(O, TEXT("rank"));
	R.Name = SmvJson::Str(O, TEXT("name"));
	R.Score = (int32)SmvJson::Num(O, TEXT("score"));
	R.Wins = (int32)SmvJson::Num(O, TEXT("wins"));
	R.Matches = (int32)SmvJson::Num(O, TEXT("matches"));
	R.Flights = (int32)SmvJson::Num(O, TEXT("flights"));
	R.bDemo = SmvJson::Bool(O, TEXT("demo"));
	return R;
}

FSmvMatchPlayer FSmvMatchPlayer::FromJson(const TSharedPtr<FJsonObject>& O)
{
	FSmvMatchPlayer P;
	P.Id = (int64)SmvJson::Num(O, TEXT("id"));
	P.Name = SmvJson::Str(O, TEXT("name"));
	P.bHost = SmvJson::Bool(O, TEXT("host"));
	P.bBot = SmvJson::Bool(O, TEXT("is_bot"));
	P.bReady = SmvJson::Bool(O, TEXT("ready"));
	P.bOnline = SmvJson::Bool(O, TEXT("online"));
	P.Status = SmvJson::Str(O, TEXT("status"));
	P.Role = SmvJson::Str(O, TEXT("role"));
	P.Color = SmvJson::Str(O, TEXT("color"));
	P.Rings = (int32)SmvJson::Num(O, TEXT("rings"));
	P.Total = (int32)SmvJson::Num(O, TEXT("total"));
	P.FlightId = SmvJson::Id(O, TEXT("flight_id"));
	return P;
}

FSmvMatch FSmvMatch::FromJson(const TSharedPtr<FJsonObject>& O)
{
	FSmvMatch M;
	if (!O.IsValid()) return M;
	M.RawJson = SmvJson::ToString(O.ToSharedRef());
	M.Id = SmvJson::Id(O, TEXT("id"));
	M.Code = SmvJson::Str(O, TEXT("code"));
	M.State = SmvJson::Str(O, TEXT("state"));
	M.Kind = SmvJson::Str(O, TEXT("kind"), TEXT("rings"));
	M.HostId = (int64)SmvJson::Num(O, TEXT("host_id"));
	M.MaxPlayers = (int32)SmvJson::Num(O, TEXT("max_players"), 8);
	M.bCanStart = SmvJson::Bool(O, TEXT("can_start"));
	M.StartAt = SmvJson::Num(O, TEXT("start_at"));
	M.Winner = (int64)SmvJson::Num(O, TEXT("winner"));
	const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
	if (O->TryGetArrayField(TEXT("players"), A) && A)
	{
		for (const TSharedPtr<FJsonValue>& V : *A) if (V.IsValid() && V->AsObject().IsValid()) M.Players.Add(FSmvMatchPlayer::FromJson(V->AsObject()));
	}
	if (O->TryGetArrayField(TEXT("rings"), A) && A)
	{
		for (const TSharedPtr<FJsonValue>& V : *A)
		{
			const TSharedPtr<FJsonObject> J = V.IsValid() ? V->AsObject() : nullptr;
			if (!J.IsValid()) continue;
			FSmvGate G;
			G.I = (int32)SmvJson::Num(J, TEXT("i"));
			G.Lat = SmvJson::Num(J, TEXT("lat"));
			G.Lon = SmvJson::Num(J, TEXT("lon"));
			G.AglM = SmvJson::Num(J, TEXT("agl_m"));
			G.YawDeg = SmvJson::Num(J, TEXT("yaw_deg"));
			G.RadiusM = SmvJson::Num(J, TEXT("radius_m"), 6);
			M.Rings.Add(G);
		}
	}
	if (const TSharedPtr<FJsonObject> Me = SmvJson::Obj(O, TEXT("me"))) M.MyFlightId = SmvJson::Id(Me, TEXT("flight_id"));
	return M;
}
