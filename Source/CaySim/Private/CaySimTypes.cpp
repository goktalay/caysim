#include "CaySimTypes.h"
#include "Dom/JsonValue.h"

namespace
{
	double NumOr(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, double Def)
	{
		double V;
		return (O.IsValid() && O->TryGetNumberField(Key, V)) ? V : Def;
	}
	FString StrOr(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, const FString& Def = FString())
	{
		FString V;
		return (O.IsValid() && O->TryGetStringField(Key, V)) ? V : Def;
	}
	bool BoolOr(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, bool Def)
	{
		bool V;
		return (O.IsValid() && O->TryGetBoolField(Key, V)) ? V : Def;
	}
	TSharedPtr<FJsonObject> ObjOr(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		const TSharedPtr<FJsonObject>* P = nullptr;
		return (O.IsValid() && O->TryGetObjectField(Key, P) && P) ? *P : TSharedPtr<FJsonObject>();
	}
	/** `null` ya da eksik alan → false */
	bool HasNum(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		double V;
		return O.IsValid() && O->TryGetNumberField(Key, V);
	}
}

// ---- enum yardımcıları --------------------------------------------------

ECaySimDroneType CaySimTypes::DroneTypeFromKey(const FString& Key)
{
	const FString K = Key.ToLower();
	if (K == TEXT("sabit_kanat") || K == TEXT("plane") || K == TEXT("fixed_wing") || K == TEXT("fixedwing")) return ECaySimDroneType::FixedWing;
	if (K == TEXT("hexarotor") || K == TEXT("hexa")) return ECaySimDroneType::Hexarotor;
	if (K == TEXT("vtol") || K == TEXT("quadplane")) return ECaySimDroneType::Vtol;
	if (K == TEXT("helikopter") || K == TEXT("heli") || K == TEXT("helicopter")) return ECaySimDroneType::Helicopter;
	return ECaySimDroneType::Multirotor;
}

FString CaySimTypes::DroneTypeToKey(ECaySimDroneType Type)
{
	switch (Type)
	{
	case ECaySimDroneType::FixedWing:  return TEXT("sabit_kanat");
	case ECaySimDroneType::Hexarotor:  return TEXT("hexarotor");
	case ECaySimDroneType::Vtol:       return TEXT("vtol");
	case ECaySimDroneType::Helicopter: return TEXT("helikopter");
	default:                           return TEXT("multirotor");
	}
}

ECaySimSessionState CaySimTypes::StateFromString(const FString& S)
{
	if (S == TEXT("BASLIYOR")) return ECaySimSessionState::Starting;
	if (S == TEXT("HAZIR"))    return ECaySimSessionState::Ready;
	if (S == TEXT("HATA"))     return ECaySimSessionState::Error;
	if (S == TEXT("KAPANDI"))  return ECaySimSessionState::Closed;
	return ECaySimSessionState::Unknown;
}

ECaySimLaunchMethod CaySimTypes::LaunchMethodFromString(const FString& S)
{
	const FString K = S.ToLower();
	if (K == TEXT("ramp") || K == TEXT("rampa") || K == TEXT("catapult")) return ECaySimLaunchMethod::Ramp;
	// "wheels" (HEPGO §13) → pist koşusu
	if (K == TEXT("vehicle") || K == TEXT("arac") || K == TEXT("pickup"))  return ECaySimLaunchMethod::Vehicle;
	if (K == TEXT("vertical") || K == TEXT("vtol") || K == TEXT("dikey"))  return ECaySimLaunchMethod::Vertical;
	return ECaySimLaunchMethod::Runway;
}

FString CaySimTypes::LaunchMethodToString(ECaySimLaunchMethod M)
{
	switch (M)
	{
	case ECaySimLaunchMethod::Ramp:     return TEXT("ramp");
	case ECaySimLaunchMethod::Vehicle:  return TEXT("vehicle");
	case ECaySimLaunchMethod::Vertical: return TEXT("vertical");
	default:                            return TEXT("wheels"); // HEPGO §13 adı
	}
}

// ---- FCaySimWorld -------------------------------------------------------

TSharedRef<FJsonObject> FCaySimWorld::ToJson() const
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("mode"), Mode);
	O->SetNumberField(TEXT("seed"), Seed);
	O->SetNumberField(TEXT("size_m"), SizeM);
	O->SetNumberField(TEXT("tree_density"), TreeDensity);
	O->SetNumberField(TEXT("mountain_height"), MountainHeight);
	O->SetNumberField(TEXT("mountain_count"), MountainCount);
	O->SetNumberField(TEXT("building_count"), BuildingCount);
	O->SetBoolField(TEXT("runway"), bRunway);
	O->SetNumberField(TEXT("runway_heading"), RunwayHeading);
	O->SetNumberField(TEXT("water"), Water);
	O->SetStringField(TEXT("time"), Time);
	O->SetNumberField(TEXT("wind_ms"), WindMs);
	O->SetNumberField(TEXT("wind_dir"), WindDir);
	O->SetNumberField(TEXT("fog"), Fog);
	if (bHasLaunch)
	{
		TSharedRef<FJsonObject> L = MakeShared<FJsonObject>();
		L->SetStringField(TEXT("method"), LaunchMethod);
		if (LaunchMethod == TEXT("ramp")) L->SetNumberField(TEXT("angle_deg"), FMath::Clamp(LaunchAngleDeg, 10.f, 30.f));
		if (LaunchMethod == TEXT("vehicle")) L->SetNumberField(TEXT("release_kmh"), FMath::Clamp(LaunchReleaseKmh, 40.f, 110.f));
		O->SetObjectField(TEXT("launch"), L);
	}
	return O;
}

FCaySimWorld FCaySimWorld::FromJson(const TSharedPtr<FJsonObject>& O)
{
	FCaySimWorld W;
	if (!O.IsValid()) return W;
	W.Mode = StrOr(O, TEXT("mode"), W.Mode);
	W.Seed = (int32)NumOr(O, TEXT("seed"), W.Seed);
	W.SizeM = (float)NumOr(O, TEXT("size_m"), W.SizeM);
	W.TreeDensity = (float)NumOr(O, TEXT("tree_density"), W.TreeDensity);
	W.MountainHeight = (float)NumOr(O, TEXT("mountain_height"), W.MountainHeight);
	W.MountainCount = (int32)NumOr(O, TEXT("mountain_count"), W.MountainCount);
	W.BuildingCount = (int32)NumOr(O, TEXT("building_count"), W.BuildingCount);
	W.bRunway = BoolOr(O, TEXT("runway"), W.bRunway);
	W.RunwayHeading = (float)NumOr(O, TEXT("runway_heading"), W.RunwayHeading);
	W.Water = (float)NumOr(O, TEXT("water"), W.Water);
	W.Time = StrOr(O, TEXT("time"), W.Time);
	W.WindMs = (float)NumOr(O, TEXT("wind_ms"), W.WindMs);
	W.WindDir = (float)NumOr(O, TEXT("wind_dir"), W.WindDir);
	W.Fog = (float)NumOr(O, TEXT("fog"), W.Fog);
	const TSharedPtr<FJsonObject> L = ObjOr(O, TEXT("launch"));
	if (L.IsValid())
	{
		W.bHasLaunch = true;
		W.LaunchMethod = StrOr(L, TEXT("method"), W.LaunchMethod);
		W.LaunchAngleDeg = (float)NumOr(L, TEXT("angle_deg"), W.LaunchAngleDeg);
		W.LaunchReleaseKmh = (float)NumOr(L, TEXT("release_kmh"), W.LaunchReleaseKmh);
	}
	return W;
}

// ---- FCaySimModelOrient -------------------------------------------------

TSharedRef<FJsonObject> FCaySimModelOrient::ToJson() const
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("forward"), Forward);
	O->SetStringField(TEXT("up"), Up);
	O->SetNumberField(TEXT("scale"), Scale);
	TArray<TSharedPtr<FJsonValue>> Off;
	Off.Add(MakeShared<FJsonValueNumber>(Offset.X));
	Off.Add(MakeShared<FJsonValueNumber>(Offset.Y));
	Off.Add(MakeShared<FJsonValueNumber>(Offset.Z));
	O->SetArrayField(TEXT("offset"), Off);
	TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
	P->SetStringField(TEXT("mode"), PropMode);
	TArray<TSharedPtr<FJsonValue>> Pos;
	Pos.Add(MakeShared<FJsonValueNumber>(PropPos.X));
	Pos.Add(MakeShared<FJsonValueNumber>(PropPos.Y));
	Pos.Add(MakeShared<FJsonValueNumber>(PropPos.Z));
	P->SetArrayField(TEXT("pos"), Pos);
	P->SetNumberField(TEXT("r"), PropRadius);
	O->SetObjectField(TEXT("prop"), P);
	return O;
}

FCaySimModelOrient FCaySimModelOrient::FromJson(const TSharedPtr<FJsonObject>& O)
{
	FCaySimModelOrient R;
	if (!O.IsValid()) return R;
	R.Forward = StrOr(O, TEXT("forward"), R.Forward);
	R.Up = StrOr(O, TEXT("up"), R.Up);
	R.Scale = (float)NumOr(O, TEXT("scale"), R.Scale);
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (O->TryGetArrayField(TEXT("offset"), Arr) && Arr && Arr->Num() >= 3)
	{
		R.Offset = FVector((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber(), (*Arr)[2]->AsNumber());
	}
	const TSharedPtr<FJsonObject> P = ObjOr(O, TEXT("prop"));
	if (P.IsValid())
	{
		R.PropMode = StrOr(P, TEXT("mode"), R.PropMode);
		R.PropRadius = (float)NumOr(P, TEXT("r"), R.PropRadius);
		if (P->TryGetArrayField(TEXT("pos"), Arr) && Arr && Arr->Num() >= 3)
		{
			R.PropPos = FVector((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber(), (*Arr)[2]->AsNumber());
		}
	}
	return R;
}

// ---- FCaySimLaunchEvent -------------------------------------------------

FCaySimLaunchEvent FCaySimLaunchEvent::FromJson(const TSharedPtr<FJsonObject>& O)
{
	FCaySimLaunchEvent L;
	if (!O.IsValid()) return L;
	L.Method = CaySimTypes::LaunchMethodFromString(StrOr(O, TEXT("method"), TEXT("wheels")));
	L.Phase = StrOr(O, TEXT("phase"));
	L.AngleDeg = (float)NumOr(O, TEXT("angle_deg"), 0.0);
	L.ReleaseKmh = (float)NumOr(O, TEXT("release_kmh"), 0.0);
	L.SpeedKmh = (float)NumOr(O, TEXT("speed_kmh"), 0.0);
	L.AltM = (float)NumOr(O, TEXT("alt"), -1.0);
	L.Ts = NumOr(O, TEXT("ts"), 0.0);
	L.bValid = true;
	return L;
}

// ---- FCaySimSession -----------------------------------------------------

static FCaySimModelInfo ModelFromJson(const TSharedPtr<FJsonObject>& O)
{
	FCaySimModelInfo M;
	if (!O.IsValid()) return M;
	// id tam sayı ya da dizge gelebilir
	double IdNum;
	if (O->TryGetNumberField(TEXT("id"), IdNum)) M.Id = FString::Printf(TEXT("%d"), (int32)IdNum);
	else M.Id = StrOr(O, TEXT("id"));
	M.Name = StrOr(O, TEXT("name"));
	M.Source = StrOr(O, TEXT("source"));
	M.TypeKey = StrOr(O, TEXT("type"));
	M.ThumbUrl = StrOr(O, TEXT("thumb_url"));
	M.GlbUrl = StrOr(O, TEXT("glb_url"));
	M.Format = StrOr(O, TEXT("format"));
	M.Size = (int64)NumOr(O, TEXT("size"), 0);
	M.bReady = BoolOr(O, TEXT("ready"), false);
	M.bHasProp = BoolOr(O, TEXT("has_prop"), false);
	M.Orient = FCaySimModelOrient::FromJson(ObjOr(O, TEXT("orient")));
	return M;
}

static FCaySimCenter CenterFromJson(const TSharedPtr<FJsonObject>& O)
{
	FCaySimCenter C;
	if (!O.IsValid()) return C;
	C.Id = (int32)NumOr(O, TEXT("id"), 0);
	C.Name = StrOr(O, TEXT("name"));
	C.Lat = NumOr(O, TEXT("lat"), 0.0);
	C.Lon = NumOr(O, TEXT("lon"), 0.0);
	C.bHasAlt = HasNum(O, TEXT("alt"));
	C.Alt = NumOr(O, TEXT("alt"), 0.0);
	C.LocationName = StrOr(O, TEXT("location_name"));
	return C;
}

FCaySimSession FCaySimSession::FromJson(const TSharedPtr<FJsonObject>& O)
{
	FCaySimSession S;
	if (!O.IsValid()) return S;
	double IdNum;
	if (O->TryGetNumberField(TEXT("id"), IdNum)) S.Id = FString::Printf(TEXT("%d"), (int32)IdNum);
	else S.Id = StrOr(O, TEXT("id"));
	S.Mode = StrOr(O, TEXT("mode"), TEXT("sitl")) == TEXT("replay") ? ECaySimSessionMode::Replay : ECaySimSessionMode::Sitl;
	S.TypeKey = StrOr(O, TEXT("type"));
	S.Type = CaySimTypes::DroneTypeFromKey(S.TypeKey);
	S.Model = ModelFromJson(ObjOr(O, TEXT("model")));
	S.Center = CenterFromJson(ObjOr(O, TEXT("center")));
	S.World = FCaySimWorld::FromJson(ObjOr(O, TEXT("world")));
	S.State = CaySimTypes::StateFromString(StrOr(O, TEXT("state")));
	S.UdpPort = (int32)NumOr(O, TEXT("udp_port"), 0);
	S.Host = StrOr(O, TEXT("host"));
	S.OwnerName = StrOr(ObjOr(O, TEXT("owner")), TEXT("name"));
	S.ClientName = StrOr(ObjOr(O, TEXT("client")), TEXT("name"));
	S.CreatedAt = StrOr(O, TEXT("created_at"));
	const TSharedPtr<FJsonObject> G = ObjOr(O, TEXT("gcs"));
	S.Gcs.bConnected = BoolOr(G, TEXT("connected"), false);
	S.Gcs.Addr = StrOr(G, TEXT("addr"));
	S.SitlError = StrOr(ObjOr(O, TEXT("sitl")), TEXT("error"));
	S.Launch = FCaySimLaunchEvent::FromJson(ObjOr(O, TEXT("launch")));
	const TSharedPtr<FJsonObject> R = ObjOr(O, TEXT("replay"));
	if (R.IsValid())
	{
		S.Replay.LogId = (int32)NumOr(R, TEXT("log_id"), 0);
		S.Replay.FileName = StrOr(R, TEXT("file_name"));
		S.Replay.Progress = (float)NumOr(R, TEXT("progress"), 0);
		S.Replay.Frames = (int32)NumOr(R, TEXT("frames"), 0);
		S.Replay.DurationS = (float)NumOr(R, TEXT("duration_s"), 0);
		S.Replay.Hz = (float)NumOr(R, TEXT("hz"), 25);
		S.Replay.TakeoffT = (float)NumOr(R, TEXT("takeoff_t"), 0);
		S.Replay.Vehicle = StrOr(R, TEXT("vehicle"));
	}
	return S;
}

// ---- FCaySimTelemetry ---------------------------------------------------

FCaySimTelemetry FCaySimTelemetry::FromJson(const TSharedPtr<FJsonObject>& O)
{
	FCaySimTelemetry T;
	if (!O.IsValid()) return T;
	T.Ts = NumOr(O, TEXT("ts"), 0.0);
	T.Tbm = (int64)NumOr(O, TEXT("tbm"), 0.0);
	T.Lat = NumOr(O, TEXT("lat"), 0.0);
	T.Lon = NumOr(O, TEXT("lon"), 0.0);
	T.AltMsl = NumOr(O, TEXT("alt_msl"), 0.0);
	T.AltRel = NumOr(O, TEXT("alt_rel"), 0.0);
	T.Roll = (float)NumOr(O, TEXT("roll"), 0.0);
	T.Pitch = (float)NumOr(O, TEXT("pitch"), 0.0);
	T.Yaw = (float)NumOr(O, TEXT("yaw"), 0.0);
	T.RollSpeed = (float)NumOr(O, TEXT("rs"), 0.0);
	T.PitchSpeed = (float)NumOr(O, TEXT("ps"), 0.0);
	T.YawSpeed = (float)NumOr(O, TEXT("ys"), 0.0);
	T.Vn = (float)NumOr(O, TEXT("vn"), 0.0);
	T.Ve = (float)NumOr(O, TEXT("ve"), 0.0);
	T.Vd = (float)NumOr(O, TEXT("vd"), 0.0);
	T.Airspeed = (float)NumOr(O, TEXT("airspeed"), 0.0);
	T.Groundspeed = (float)NumOr(O, TEXT("groundspeed"), 0.0);
	T.Heading = (float)NumOr(O, TEXT("heading"), FMath::RadiansToDegrees(T.Yaw));
	T.Climb = (float)NumOr(O, TEXT("climb"), -T.Vd);
	T.Mode = StrOr(O, TEXT("mode"));
	T.bArmed = BoolOr(O, TEXT("armed"), false);
	T.GpsFix = (int32)NumOr(O, TEXT("gps_fix"), 0);
	T.Sats = (int32)NumOr(O, TEXT("sats"), -1);
	T.BattV = (float)NumOr(O, TEXT("batt_v"), -1.0);
	T.BattPct = (float)NumOr(O, TEXT("batt_pct"), -1.0);
	T.Throttle = (float)NumOr(O, TEXT("throttle"), 0.0);
	const TArray<TSharedPtr<FJsonValue>>* Rc = nullptr;
	T.Rc.Init(0, 8);
	if (O->TryGetArrayField(TEXT("rc"), Rc) && Rc)
	{
		for (int32 i = 0; i < FMath::Min(8, Rc->Num()); ++i)
		{
			T.Rc[i] = (int32)(*Rc)[i]->AsNumber();
		}
	}
	const TSharedPtr<FJsonObject> H = ObjOr(O, TEXT("home"));
	if (H.IsValid())
	{
		T.bHasHome = true;
		T.HomeLat = NumOr(H, TEXT("lat"), 0.0);
		T.HomeLon = NumOr(H, TEXT("lon"), 0.0);
		T.HomeAlt = NumOr(H, TEXT("alt"), 0.0);
	}
	T.bGcsConnected = BoolOr(O, TEXT("gcs_connected"), false);
	return T;
}
