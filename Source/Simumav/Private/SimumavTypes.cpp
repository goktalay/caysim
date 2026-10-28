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

bool FSmvEquipment::AddonFits(const FString& Addon, const FString& Type)
{
	// equipment.ADDONS types: ilaçlama yalnız kopter ailesi; paket bırakma uçan araçlar + kara aracı; paraşüt uçan araçlar
	const bool bCopter = Type == TEXT("multirotor") || Type == TEXT("fpv") || Type == TEXT("heli");
	const bool bFlying = bCopter || Type == TEXT("plane") || Type == TEXT("interceptor") || Type == TEXT("vtol");
	if (Addon == TEXT("sprayer")) return bCopter;
	if (Addon == TEXT("gripper")) return bFlying || Type == TEXT("rover");
	if (Addon == TEXT("parachute")) return bFlying;
	return false;
}

TMap<FString, double> FSmvEquipment::SitlParams(const FString& Type) const
{
	// app/simumav/equipment.py sitl_params ile aynı kanallar: ilaçlama SERVO9/10, tutucu SERVO11, paraşüt SERVO12
	TMap<FString, double> P;
	if (bSprayer && AddonFits(TEXT("sprayer"), Type))
	{
		P.Add(TEXT("SPRAY_ENABLE"), 1); P.Add(TEXT("SERVO9_FUNCTION"), 22); P.Add(TEXT("SERVO10_FUNCTION"), 23);
		P.Add(TEXT("SPRAY_PUMP_RATE"), FMath::Clamp(SprayPumpRate, 0.0, 100.0));
	}
	if (bGripper && AddonFits(TEXT("gripper"), Type))
	{
		P.Add(TEXT("GRIP_ENABLE"), 1); P.Add(TEXT("GRIP_TYPE"), 1); P.Add(TEXT("SERVO11_FUNCTION"), 28);
	}
	if (bParachute && AddonFits(TEXT("parachute"), Type))
	{
		P.Add(TEXT("CHUTE_ENABLED"), 1); P.Add(TEXT("CHUTE_TYPE"), 10); P.Add(TEXT("SERVO12_FUNCTION"), 27);
		P.Add(TEXT("CHUTE_ALT_MIN"), FMath::Clamp(ChuteAltMinM, 0.0, 100.0));
	}
	if (MaxAltM > 0 && Type != TEXT("rover"))
	{
		// azami irtifa: irtifa çiti (Copter/Plane FENCE_TYPE bit 0 = azami irtifa)
		P.Add(TEXT("FENCE_ENABLE"), 1); P.Add(TEXT("FENCE_TYPE"), 1); P.Add(TEXT("FENCE_ALT_MAX"), FMath::Clamp(MaxAltM, 10.0, 5000.0));
	}
	return P;
}

TSharedRef<FJsonObject> FSmvEquipment::ToJson(bool bForServer) const
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("camera"), Camera);
	TSharedRef<FJsonObject> A = MakeShared<FJsonObject>();
	if (bSprayer)
	{
		TSharedRef<FJsonObject> X = MakeShared<FJsonObject>();
		X->SetNumberField(TEXT("tank_l"), FMath::Clamp(SprayTankL, 1.0, 40.0));
		X->SetNumberField(TEXT("width_m"), FMath::Clamp(SprayWidthM, 1.0, 12.0));
		X->SetNumberField(TEXT("pump_rate"), FMath::Clamp(SprayPumpRate, 0.0, 100.0));
		A->SetObjectField(TEXT("sprayer"), X);
	}
	if (bGripper)
	{
		TSharedRef<FJsonObject> X = MakeShared<FJsonObject>();
		X->SetNumberField(TEXT("payload_kg"), FMath::Clamp(PayloadKg, 0.1, 20.0));
		A->SetObjectField(TEXT("gripper"), X);
	}
	if (bParachute)
	{
		TSharedRef<FJsonObject> X = MakeShared<FJsonObject>();
		X->SetNumberField(TEXT("alt_min_m"), FMath::Clamp(ChuteAltMinM, 0.0, 100.0));
		A->SetObjectField(TEXT("parachute"), X);
	}
	O->SetObjectField(TEXT("addons"), A);
	TSharedRef<FJsonObject> L = MakeShared<FJsonObject>();
	L->SetStringField(TEXT("pattern"), Pattern.IsEmpty() ? FString(TEXT("solid")) : Pattern);
	if (Color2.Len() == 7) L->SetStringField(TEXT("color2"), Color2.ToLower()); else L->SetField(TEXT("color2"), MakeShared<FJsonValueNull>());
	if (!Callsign.IsEmpty()) L->SetStringField(TEXT("callsign"), Callsign.Left(8).ToUpper()); else L->SetField(TEXT("callsign"), MakeShared<FJsonValueNull>());
	TArray<TSharedPtr<FJsonValue>> D;
	for (const FSmvDecal& X : Decals)
	{
		if (bForServer && X.Id <= 0) continue;   // yalnız yerel çıkartma sunucuya gönderilmez (yüklenince kimlik alır)
		TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
		J->SetNumberField(TEXT("id"), (double)X.Id);
		J->SetStringField(TEXT("slot"), X.Slot);
		J->SetNumberField(TEXT("scale"), FMath::Clamp(X.Scale, 0.2, 2.0));
		J->SetNumberField(TEXT("rot"), FMath::Clamp(X.Rot, 0.0, 360.0));
		if (!bForServer) { J->SetStringField(TEXT("file"), X.File); J->SetStringField(TEXT("kind"), X.Kind); }
		D.Add(MakeShared<FJsonValueObject>(J));
		if (D.Num() >= 6) break;
	}
	L->SetArrayField(TEXT("decals"), D);
	O->SetObjectField(TEXT("livery"), L);
	if (MaxAltM > 0) O->SetNumberField(TEXT("max_alt_m"), FMath::Clamp(MaxAltM, 10.0, 5000.0)); else O->SetField(TEXT("max_alt_m"), MakeShared<FJsonValueNull>());
	if (!bForServer) O->SetStringField(TEXT("finish"), Finish);
	return O;
}

FSmvEquipment FSmvEquipment::FromJson(const TSharedPtr<FJsonObject>& O)
{
	FSmvEquipment E;
	if (!O.IsValid()) return E;
	E.Camera = SmvJson::Str(O, TEXT("camera"), TEXT("day"));
	if (const TSharedPtr<FJsonObject> A = SmvJson::Obj(O, TEXT("addons")))
	{
		if (const TSharedPtr<FJsonObject> X = SmvJson::Obj(A, TEXT("sprayer")))
		{
			E.bSprayer = true;
			E.SprayTankL = SmvJson::Num(X, TEXT("tank_l"), E.SprayTankL);
			E.SprayWidthM = SmvJson::Num(X, TEXT("width_m"), E.SprayWidthM);
			E.SprayPumpRate = SmvJson::Num(X, TEXT("pump_rate"), E.SprayPumpRate);
		}
		else E.bSprayer = SmvJson::Bool(A, TEXT("sprayer"), false);
		if (const TSharedPtr<FJsonObject> X = SmvJson::Obj(A, TEXT("gripper"))) { E.bGripper = true; E.PayloadKg = SmvJson::Num(X, TEXT("payload_kg"), E.PayloadKg); }
		else E.bGripper = SmvJson::Bool(A, TEXT("gripper"), false);
		if (const TSharedPtr<FJsonObject> X = SmvJson::Obj(A, TEXT("parachute"))) { E.bParachute = true; E.ChuteAltMinM = SmvJson::Num(X, TEXT("alt_min_m"), E.ChuteAltMinM); }
		else E.bParachute = SmvJson::Bool(A, TEXT("parachute"), false);
	}
	if (const TSharedPtr<FJsonObject> L = SmvJson::Obj(O, TEXT("livery")))
	{
		E.Pattern = SmvJson::Str(L, TEXT("pattern"), TEXT("solid"));
		E.Color2 = SmvJson::Str(L, TEXT("color2"));
		E.Callsign = SmvJson::Str(L, TEXT("callsign"));
		const TArray<TSharedPtr<FJsonValue>>* D = nullptr;
		if (L->TryGetArrayField(TEXT("decals"), D) && D)
		{
			for (const TSharedPtr<FJsonValue>& V : *D)
			{
				const TSharedPtr<FJsonObject> J = V.IsValid() ? V->AsObject() : nullptr;
				if (!J.IsValid()) continue;
				FSmvDecal X;
				X.Id = (int64)SmvJson::Num(J, TEXT("id"), 0);
				X.File = SmvJson::Str(J, TEXT("file"));
				X.Slot = SmvJson::Str(J, TEXT("slot"), TEXT("top"));
				X.Scale = SmvJson::Num(J, TEXT("scale"), 1.0);
				X.Rot = SmvJson::Num(J, TEXT("rot"), 0.0);
				X.Kind = SmvJson::Str(J, TEXT("kind"), TEXT("sticker"));
				E.Decals.Add(X);
			}
		}
	}
	E.MaxAltM = SmvJson::Num(O, TEXT("max_alt_m"), -1.0);
	E.Finish = SmvJson::Str(O, TEXT("finish"), TEXT("gloss"));
	return E;
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
	for (const TPair<FString, double>& Kv : Params) P->SetNumberField(FString(*Kv.Key), Kv.Value);
	O->SetObjectField(TEXT("params"), P);
	O->SetObjectField(TEXT("power"), Power.ToJson());
	O->SetObjectField(TEXT("equipment"), Equipment.ToJson(true));
	if (!Script.IsEmpty()) O->SetStringField(TEXT("script"), Script.Left(64000));
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
		for (const auto& Kv : P->Values)
		{
			if (Kv.Value.IsValid() && Kv.Value->Type == EJson::Number) D.Params.Add(FString(*Kv.Key), Kv.Value->AsNumber());
		}
	}
	D.Power = FSmvPowerSettings::FromJson(SmvJson::Obj(O, TEXT("power")));
	D.Equipment = FSmvEquipment::FromJson(SmvJson::Obj(O, TEXT("equipment")));
	D.Script = SmvJson::Str(O, TEXT("script"));
	return D;
}

TSharedRef<FJsonObject> FSmvAvatar::ToJson() const
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetNumberField(TEXT("v"), 2);
	O->SetStringField(TEXT("helmet"), Helmet);
	O->SetStringField(TEXT("visor"), Visor);
	O->SetBoolField(TEXT("mirror"), bMirror);
	O->SetStringField(TEXT("suit"), Suit);
	O->SetStringField(TEXT("skin"), Skin);
	O->SetStringField(TEXT("patch"), Patch);
	O->SetStringField(TEXT("patch_color"), PatchColor);
	O->SetStringField(TEXT("accessory"), Accessory);
	O->SetStringField(TEXT("body"), Body);
	O->SetStringField(TEXT("build"), Build);
	O->SetStringField(TEXT("face"), Face);
	O->SetStringField(TEXT("eyes"), Eyes);
	O->SetStringField(TEXT("eye_color"), EyeColor);
	O->SetStringField(TEXT("brows"), Brows);
	O->SetStringField(TEXT("brow_w"), BrowW);
	O->SetStringField(TEXT("nose"), Nose);
	O->SetStringField(TEXT("mouth"), Mouth);
	O->SetStringField(TEXT("beard"), Beard);
	O->SetStringField(TEXT("glasses"), Glasses);
	O->SetStringField(TEXT("hair"), Hair);
	O->SetStringField(TEXT("hair_color"), HairColor);
	O->SetStringField(TEXT("headgear"), Headgear);
	O->SetBoolField(TEXT("hg2"), true);
	O->SetStringField(TEXT("outfit"), Outfit);
	O->SetBoolField(TEXT("freckles"), bFreckles);
	O->SetBoolField(TEXT("mole"), bMole);
	O->SetBoolField(TEXT("visor_open"), bVisorOpen);
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
	A.Body = SmvJson::Str(O, TEXT("body"), A.Body);
	A.Build = SmvJson::Str(O, TEXT("build"), A.Build);
	A.Face = SmvJson::Str(O, TEXT("face"), A.Face);
	A.Eyes = SmvJson::Str(O, TEXT("eyes"), A.Eyes);
	A.EyeColor = SmvJson::Str(O, TEXT("eye_color"), A.EyeColor);
	A.Brows = SmvJson::Str(O, TEXT("brows"), A.Brows);
	A.BrowW = SmvJson::Str(O, TEXT("brow_w"), A.BrowW);
	A.Nose = SmvJson::Str(O, TEXT("nose"), A.Nose);
	A.Mouth = SmvJson::Str(O, TEXT("mouth"), A.Mouth);
	A.Beard = SmvJson::Str(O, TEXT("beard"), A.Beard);
	A.Glasses = SmvJson::Str(O, TEXT("glasses"), A.Glasses);
	A.Hair = SmvJson::Str(O, TEXT("hair"), A.Hair);
	A.HairColor = SmvJson::Str(O, TEXT("hair_color"), A.HairColor);
	A.Headgear = SmvJson::Str(O, TEXT("headgear"), A.Headgear);
	// eski kayıtlar: kask varsayılandı (seçilmeden geliyordu) → hg2 işareti olmayan "helmet" kaskısız sayılır
	if (!SmvJson::Bool(O, TEXT("hg2"), false) && (A.Headgear == TEXT("helmet") || A.Headgear.IsEmpty())) A.Headgear = TEXT("none");
	A.Outfit = SmvJson::Str(O, TEXT("outfit"), A.Outfit);
	A.bFreckles = SmvJson::Bool(O, TEXT("freckles"), A.bFreckles);
	A.bMole = SmvJson::Bool(O, TEXT("mole"), A.bMole);
	A.bVisorOpen = SmvJson::Bool(O, TEXT("visor_open"), A.bVisorOpen);
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
	P.Xp = (int64)SmvJson::Num(O, TEXT("xp"), 0);
	P.XpUnverified = (int64)SmvJson::Num(O, TEXT("xp_unverified"), 0);
	P.Level = FMath::Clamp((int32)SmvJson::Num(O, TEXT("level"), 1), 1, 100);
	const TArray<TSharedPtr<FJsonValue>>* L = nullptr;
	if (O.IsValid() && O->TryGetArrayField(TEXT("xp_log"), L) && L)
	{
		for (const TSharedPtr<FJsonValue>& V : *L)
		{
			const TSharedPtr<FJsonObject> J = V.IsValid() ? V->AsObject() : nullptr;
			if (!J.IsValid()) continue;
			FSmvXpEntry E;
			E.Xp = (int32)SmvJson::Num(J, TEXT("xp"));
			E.At = SmvJson::Str(J, TEXT("at"));
			E.Mode = SmvJson::Str(J, TEXT("mode"));
			E.DurationS = (int32)SmvJson::Num(J, TEXT("duration_s"));
			E.bImported = SmvJson::Bool(J, TEXT("imported"));
			P.XpLog.Add(E);
		}
	}
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
	if (!World.Biome.IsEmpty()) Wd->SetStringField(TEXT("biome"), World.Biome);
	if (World.Vehicles > 0) Wd->SetNumberField(TEXT("vehicles"), FMath::Clamp(World.Vehicles, 0, 400));
	if (World.People > 0) Wd->SetNumberField(TEXT("people"), FMath::Clamp(World.People, 0, 400));
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
	if (!ParkurKind.IsEmpty()) O->SetStringField(TEXT("category"), ParkurKind);
	if (GroundMsl > -1000) O->SetNumberField(TEXT("ground_msl"), GroundMsl);
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
		T.World.Biome = SmvJson::Str(Wd, TEXT("biome"));
		T.World.Vehicles = (int32)SmvJson::Num(Wd, TEXT("vehicles"), 0);
		T.World.People = (int32)SmvJson::Num(Wd, TEXT("people"), 0);
	}
	T.ParkurKind = SmvJson::Str(O, TEXT("category"));
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
	if (!Track.Id.StartsWith(TEXT("fab:"))) SmvSetIdField(O, TEXT("track_id"), Track.Id);   // Fab haritası: sanal yerel parkur
	if (bHasMission) SmvSetIdField(O, TEXT("mission_id"), Mission.Id);
	if (const TSharedPtr<FJsonObject> L = Launch.ToJson()) O->SetObjectField(TEXT("launch"), L);
	TSharedRef<FJsonObject> Rec = MakeShared<FJsonObject>();
	Rec->SetBoolField(TEXT("video"), false);
	Rec->SetBoolField(TEXT("bin"), bRecordBin);
	O->SetObjectField(TEXT("record"), Rec);
	O->SetObjectField(TEXT("power"), Power.ToJson());
	if (Speedup != 1.0) O->SetNumberField(TEXT("speedup"), Speedup);
	// multispektral çekirdekte gündüz kamerası olarak koşar (equipment.CAMERA_CORE); bantları istemci işler
	O->SetStringField(TEXT("camera"), Camera == TEXT("multispectral") ? FString(TEXT("day")) : Camera);
	if (!GameMode.IsEmpty() && GameMode != TEXT("rings") && GameMode != TEXT("dogfight")) O->SetStringField(TEXT("mode"), GameMode);
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
	if (Site.Any())
	{
		TSharedRef<FJsonObject> Si = MakeShared<FJsonObject>();
		Si->SetBoolField(TEXT("station"), Site.bStation);
		Si->SetBoolField(TEXT("radar"), Site.bRadar);
		Si->SetNumberField(TEXT("radar_km"), FMath::RoundToDouble(FMath::Clamp(Site.RadarKm, 1.0, 50.0) * 100.0) / 100.0);
		Si->SetBoolField(TEXT("eo"), Site.bEo);
		Si->SetNumberField(TEXT("eo_km"), FMath::RoundToDouble(FMath::Clamp(Site.EoKm, 0.5, 15.0) * 100.0) / 100.0);
		O->SetObjectField(TEXT("site"), Si);
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
	if (!Label.IsEmpty()) O->SetStringField(TEXT("label"), Label);
	if (!When.IsEmpty()) O->SetStringField(TEXT("when"), When);
	if (!GameMode.IsEmpty()) O->SetStringField(TEXT("game_mode"), GameMode);
	if (GameScore) O->SetNumberField(TEXT("game_score"), GameScore);
	if (Xp) O->SetNumberField(TEXT("xp"), Xp);
	if (Maneuvers.Num())
	{
		TArray<TSharedPtr<FJsonValue>> Mv;
		for (const FString& X : Maneuvers) Mv.Add(MakeShared<FJsonValueString>(X));
		O->SetArrayField(TEXT("maneuvers"), Mv);
	}
	if (!PilotId.IsEmpty()) O->SetStringField(TEXT("pilot_id"), PilotId);
	if (!PilotName.IsEmpty()) O->SetStringField(TEXT("pilot_name"), PilotName);
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
	R.Email = SmvJson::Str(O, TEXT("email"));
	const TSharedPtr<FJsonObject>* Av = nullptr;
	if (O.IsValid() && O->TryGetObjectField(TEXT("avatar"), Av) && Av && Av->IsValid()) { R.Avatar = FSmvAvatar::FromJson(*Av); R.bHasAvatar = true; }
	const TSharedPtr<FJsonObject>* Pt = nullptr;
	if (O.IsValid() && O->TryGetObjectField(TEXT("parts"), Pt) && Pt && Pt->IsValid())
	{
		for (const TCHAR* K : { TEXT("match"), TEXT("pace"), TEXT("discipline"), TEXT("intercept"), TEXT("fpv") })
		{
			double V = 0;
			if ((*Pt)->TryGetNumberField(K, V) && (V > 0 || FCString::Strcmp(K, TEXT("fpv")) == 0)) R.Parts.Add(K, FMath::RoundToInt(V));
		}
	}
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
