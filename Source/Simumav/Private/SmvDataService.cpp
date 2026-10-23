#include "SmvDataService.h"
#include "SimumavModule.h"
#include "SimumavClient.h"
#include "SimumavSecrets.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "SmvVehicles.h"
#include "SmvSitlConfig.h"

// ---------------------------------------------------------------------------
// katalog — app/simumav/bridge.py GLB_MODELS + PROC_MODELS (glb:/proc: anahtarlar birebir) + Simumav'a özgü çeşitler (smv:*, yalnız yerel SITL)
// + Fab gövde seçenekleri (fab:*). GLB'ler pakete gömülüdür (Content/Simumav/ModelsSrc, UFS; kaynak/lisans: aynı klasörde LICENSES.md);
// paket dışı modelde simumav.com'dan indirilir. Fizik (SITL ikilisi/çerçeve/JSON/parametre seti) ve istatistikler çekirdek tablodan:
// Source/Simumav/Private/Core/SmvVehicles.cpp (tek kaynak). Belge: docs/arac-katalogu.md
// ---------------------------------------------------------------------------

static TMap<FString, double> SmvQuad250()
{
	return { { TEXT("ATC_RAT_RLL_P"), 0.18 }, { TEXT("ATC_RAT_PIT_P"), 0.18 }, { TEXT("ATC_RAT_YAW_P"), 0.25 }, { TEXT("ATC_ACCEL_R_MAX"), 220000 },
		{ TEXT("ATC_ACCEL_P_MAX"), 220000 }, { TEXT("ATC_ACCEL_Y_MAX"), 60000 }, { TEXT("ANGLE_MAX"), 6500 }, { TEXT("PILOT_SPEED_UP"), 800 },
		{ TEXT("PILOT_SPEED_DN"), 600 }, { TEXT("WPNAV_SPEED"), 2500 }, { TEXT("WPNAV_ACCEL"), 500 }, { TEXT("LOIT_SPEED"), 2500 }, { TEXT("MOT_THST_EXPO"), 0.7 },
		{ TEXT("ACRO_RP_RATE"), 540 }, { TEXT("ACRO_Y_RATE"), 360 }, { TEXT("SIM_BATT_VOLTAGE"), 16.8 }, { TEXT("SIM_BATT_CAP_AH"), 1.5 } };
}

static TMap<FString, double> SmvJetParams()
{
	return { { TEXT("AIRSPEED_MAX"), 170 }, { TEXT("AIRSPEED_MIN"), 24 }, { TEXT("AIRSPEED_CRUISE"), 95 }, { TEXT("THR_MAX"), 100 }, { TEXT("THR_MIN"), 0 },
		{ TEXT("THR_SLEWRATE"), 0 }, { TEXT("TKOFF_THR_MAX"), 100 }, { TEXT("TKOFF_THR_MINSPD"), 0 }, { TEXT("TKOFF_ROTATE_SPD"), 22 }, { TEXT("LIM_ROLL_CD"), 7500 },
		{ TEXT("LIM_PITCH_MAX"), 5000 }, { TEXT("LIM_PITCH_MIN"), -3500 }, { TEXT("NAVL1_PERIOD"), 11 }, { TEXT("NAVL1_DAMPING"), 0.8 }, { TEXT("PTCH_RATE_P"), 0.35 },
		{ TEXT("PTCH_RATE_I"), 0.25 }, { TEXT("PTCH_RATE_D"), 0.01 }, { TEXT("PTCH_RATE_FF"), 0.5 }, { TEXT("RLL_RATE_P"), 0.3 }, { TEXT("RLL_RATE_I"), 0.2 },
		{ TEXT("RLL_RATE_FF"), 0.45 }, { TEXT("YAW_RATE_ENABLE"), 1 }, { TEXT("TECS_CLMB_MAX"), 30 }, { TEXT("TECS_SINK_MAX"), 20 }, { TEXT("TECS_SINK_MIN"), 4 },
		{ TEXT("TECS_PITCH_MAX"), 45 }, { TEXT("TECS_PITCH_MIN"), -30 }, { TEXT("SCALING_SPEED"), 60 }, { TEXT("STALL_PREVENTION"), 1 }, { TEXT("TRIM_THROTTLE"), 70 },
		{ TEXT("KFF_RDDRMIX"), 0.3 }, { TEXT("ACRO_ROLL_RATE"), 360 }, { TEXT("ACRO_PITCH_RATE"), 240 } };
}

const TArray<FSmvCatalogModel>& USmvDataService::Catalog()
{
	static TArray<FSmvCatalogModel> C;
	if (C.Num()) return C;
	auto Add = [](const TCHAR* Key, const TCHAR* Label, const TCHAR* Type, const TCHAR* File, const TCHAR* Airframe, const TCHAR* Drive, const TCHAR* Desc,
		TMap<FString, double> Defaults = TMap<FString, double>(), bool bProp = true)
	{
		FSmvCatalogModel M;
		M.Key = Key; M.Label = Label; M.Type = Type;
		M.GlbPath = File && *File ? FString::Printf(TEXT("/static/simumav/models/%s.glb"), File) : FString();
		M.Airframe = Airframe; M.Drive = Drive; M.Desc = Desc; M.Defaults = MoveTemp(Defaults); M.bHasProp = bProp;
		C.Add(M);
	};
	// ---- FPV ----
	Add(TEXT("glb:iris_fpv"), TEXT("Iris FPV"), TEXT("fpv"), TEXT("iris_fpv"), TEXT(""), TEXT(""), TEXT("3DR Iris + FPV kamera; akro/hız ağırlıklı oran döngüleri"), SmvQuad250());
	Add(TEXT("proc:quad250"), TEXT("FPV Quad 250"), TEXT("fpv"), TEXT(""), TEXT(""), TEXT(""), TEXT("5\" FPV yarış quad (250 mm, 4S)"), SmvQuad250());
	Add(TEXT("fab:fpv"), TEXT("FPV Interceptor (Fab gövdesi)"), TEXT("fpv"), TEXT(""), TEXT(""), TEXT(""), TEXT("Fab FPV Interceptor gövdesi; 6S 0,9 kg yarışçı fiziği"));
	// ---- çok rotor ----
	Add(TEXT("glb:iris"), TEXT("3DR Iris"), TEXT("multirotor"), TEXT("iris"), TEXT(""), TEXT(""), TEXT("3DR Iris quadrotor (0,5 m)"));
	Add(TEXT("glb:x500"), TEXT("X500"), TEXT("multirotor"), TEXT("x500"), TEXT(""), TEXT(""), TEXT("Karbon çerçeve quad (0,6 m)"));
	Add(TEXT("fab:quad"), TEXT("Quadcopter (Fab gövdesi)"), TEXT("multirotor"), TEXT(""), TEXT(""), TEXT(""), TEXT("Fab Quadcopter gövdesi (pervane kemikleri döner); 1,8 kg X quad fiziği"));
	Add(TEXT("glb:typhoon_h480"), TEXT("Typhoon H480 (hexa)"), TEXT("multirotor"), TEXT("typhoon_h480"), TEXT(""), TEXT(""), TEXT("Altı rotor + gimbal kamera (0,7 m)"));
	Add(TEXT("smv:octa"), TEXT("Octa 8 (sinema/haritalama)"), TEXT("multirotor"), TEXT("smv_octa"), TEXT(""), TEXT(""), TEXT("Sekiz kollu düz octa (1,3 m, 5 kg): yedekli, sakin ve ağır uçuş; 3 kg faydalı yük"));
	Add(TEXT("smv:y6"), TEXT("Y6 koaksiyel"), TEXT("multirotor"), TEXT("smv_y6"), TEXT(""), TEXT(""), TEXT("Üç kol, kol başına üst/alt karşıt dönen pervane (0,9 m, 2,5 kg)"));
	Add(TEXT("smv:cargo_x8"), TEXT("Kargo X8 (ağır yük)"), TEXT("multirotor"), TEXT("smv_cargo_x8"), TEXT(""), TEXT(""), TEXT("Koaksiyel X8, 30\" pervaneler, 32,5 kg, 10 kg yük kutusu; ArduPilot Callisto modeli"));
	// ---- sabit kanat ----
	Add(TEXT("smv:caymaz"), TEXT("CAYMAZ V3"), TEXT("plane"), TEXT("caymaz"), TEXT("elevon"), TEXT(""), TEXT("Ürettiğimiz uçak (3,03 m açıklık, arkadan itişli delta); iki elevon hem yunuslama hem yatış — HEPGO ile aynı SITL modeli"));
	Add(TEXT("glb:rc_cessna"), TEXT("RC Cessna"), TEXT("plane"), TEXT("rc_cessna"), TEXT("classic"), TEXT(""), TEXT("RC Cessna 172 (1,1 m); aileron + elevatör + rudder"));
	Add(TEXT("smv:trainer"), TEXT("Eğitim uçağı"), TEXT("plane"), TEXT("smv_trainer"), TEXT("classic"), TEXT(""), TEXT("Yüksek kanatlı eğitim uçağı (1,6 m): yavaş (≈50 km/h), affedici, yumuşak yatış/yunuslama sınırları"));
	Add(TEXT("glb:believer"), TEXT("Believer"), TEXT("plane"), TEXT("believer"), TEXT("classic"), TEXT(""), TEXT("Çift motorlu haritalama uçağı (1,9 m)"));
	Add(TEXT("glb:talon_vtail"), TEXT("Talon V-kuyruk"), TEXT("plane"), TEXT("talon_vtail"), TEXT("vtail"), TEXT(""), TEXT("Tek itici pervane, V-kuyruk (1,72 m)"));
	Add(TEXT("glb:jet"), TEXT("Jet (yüksek hız)"), TEXT("plane"), TEXT("jet"), TEXT("classic"), TEXT(""), TEXT("170 m/s (~610 km/h), sert manevra"), SmvJetParams());
	Add(TEXT("glb:zephyr"), TEXT("Zephyr (delta kanat)"), TEXT("plane"), TEXT("zephyr"), TEXT("elevon"), TEXT(""), TEXT("Delta uçan kanat (1,5 m)"), SmvJetParams());
	// ---- VTOL ----
	Add(TEXT("glb:standard_vtol"), TEXT("Standard VTOL"), TEXT("vtol"), TEXT("standard_vtol"), TEXT(""), TEXT(""), TEXT("X8 kanat + 4 dikey rotor + itici (2,2 m)"));
	Add(TEXT("smv:caymaz_vtol"), TEXT("VTOL-CAYMAZ V3"), TEXT("vtol"), TEXT("caymaz"), TEXT(""), TEXT(""), TEXT("Caymaz V3 + dört dikey kaldırma rotoru (quadplane-elevon, 8,5 kg)"));
	Add(TEXT("smv:tailsitter"), TEXT("Tailsitter (kanatlı)"), TEXT("vtol"), TEXT("smv_tailsitter"), TEXT(""), TEXT(""), TEXT("Kuyruğu üstünde kalkan dört rotorlu uçan kanat (1,2 m); askıdan yatay seyre geçiş"));
	// ---- INTERCEPTOR ----
	Add(TEXT("glb:icept_x4"), TEXT("INTERCEPTOR X4"), TEXT("interceptor"), TEXT("icept_x4"), TEXT(""), TEXT(""), TEXT("Kuyruk üstünde dikey kalkış, yatayda ~400 km/h"));
	Add(TEXT("glb:p1sun"), TEXT("P1-SUN"), TEXT("interceptor"), TEXT("p1sun"), TEXT(""), TEXT(""), TEXT("Önleyici (~0,44 m), 4 pervane"));
	Add(TEXT("glb:sting"), TEXT("Sting"), TEXT("interceptor"), TEXT("sting"), TEXT(""), TEXT(""), TEXT("Önleyici (~0,45 m), 4 pervane"));
	// ---- helikopter ----
	Add(TEXT("glb:heli"), TEXT("Tek rotorlu heli"), TEXT("heli"), TEXT("heli"), TEXT(""), TEXT(""), TEXT("Ana rotor + kuyruk rotoru (1,75 m)"));
	Add(TEXT("smv:heli_tandem"), TEXT("Tandem kargo heli"), TEXT("heli"), TEXT("smv_heli_tandem"), TEXT(""), TEXT(""), TEXT("Chinook düzeni iki ana rotor (2,6 m, 9 kg): kuyruk rotoru yok, yük taşıma"));
	Add(TEXT("smv:coax"), TEXT("Koaksiyel mini heli"), TEXT("heli"), TEXT("smv_coax"), TEXT(""), TEXT(""), TEXT("İki karşıt dönen rotorlu küçük elektrikli helikopter (0,9 m, 2 kg)"));
	// ---- kara aracı ----
	Add(TEXT("glb:rover_4x4"), TEXT("Keşif 4×4"), TEXT("rover"), TEXT("rover_4x4"), TEXT(""), TEXT("ackermann"), TEXT("Ackermann 4×4 keşif aracı (2,2 m)"), TMap<FString, double>(), false);
	Add(TEXT("glb:rover_ugv"), TEXT("Paletli UGV"), TEXT("rover"), TEXT("rover_ugv"), TEXT(""), TEXT("skid"), TEXT("Paletli kara aracı (2,5 m), yerinde döner"), TMap<FString, double>(), false);
	Add(TEXT("glb:rover_bot"), TEXT("Teslimat robotu"), TEXT("rover"), TEXT("rover_bot"), TEXT(""), TEXT("skid"), TEXT("6 tekerli teslimat robotu (0,78 m)"), TMap<FString, double>(), false);
	// ---- gövde dosyası / Fab / yalnız-yerel / ses / fizik + istatistik (SmvVehicles tek kaynak) ----
	for (FSmvCatalogModel& M : C)
	{
		const bool bSmv = M.Key.StartsWith(TEXT("smv:")), bFab = M.Key.StartsWith(TEXT("fab:"));
		M.bLocalOnly = bSmv || bFab;
		if (!M.GlbPath.IsEmpty()) M.GlbFile = FPaths::GetCleanFilename(M.GlbPath);
		if (bSmv) M.GlbPath.Reset();   // sunucuda yok: yalnız gömülü dosya
		if (M.Key == TEXT("fab:quad")) M.FabDir = TEXT("/Game/Fab/Drone");
		if (M.Key == TEXT("fab:fpv")) M.FabDir = TEXT("/Game/Fab/DroneFpv");
		if (M.GlbFile == TEXT("caymaz.glb")) M.GlbYawDeg = 90.f;   // caymaz-sim.glb burun +Z
		M.bAddLiftRotors = M.Key == TEXT("smv:caymaz_vtol");
		M.Sound = M.Type == TEXT("plane") ? (M.Key == TEXT("glb:jet") || M.Key == TEXT("proc:interceptor") ? TEXT("jet") : TEXT("prop"))
			: M.Type == TEXT("vtol") ? TEXT("pusher") : M.Type == TEXT("rover") ? TEXT("rover")
			: M.Type == TEXT("heli") ? (M.Key == TEXT("smv:coax") ? TEXT("electric_heli") : TEXT("heli")) : TEXT("multi");
		if (const SmvCore::FVehicleSpec* V = SmvCore::FindVehicleSpec(std::string(TCHAR_TO_UTF8(*M.Key))))
		{
			SmvCore::FSitlSession S;
			S.SitlType = SmvCore::SitlTypeFromSmv(V->SmvType);
			S.Vehicle = V->Key;
			S.Airframe = V->Airframe.empty() ? std::string(TCHAR_TO_UTF8(*(M.Airframe.IsEmpty() ? FString(TEXT("classic")) : M.Airframe))) : V->Airframe;
			M.SitlBinary = UTF8_TO_TCHAR(SmvCore::SessionBinary(S).c_str());
			M.SitlModel = UTF8_TO_TCHAR(SmvCore::SessionModel(S).c_str());
			M.Physics = UTF8_TO_TCHAR(V->Physics.c_str());
			M.MassKg = (float)V->MassKg; M.SpanM = (float)V->SpanM; M.LengthM = (float)V->LengthM; M.CruiseKmh = (float)V->CruiseKmh;
			M.MaxKmh = (float)V->MaxKmh; M.EnduranceMin = (float)V->EnduranceMin; M.PayloadKg = (float)V->PayloadKg; M.Motors = V->Motors;
			M.Battery = UTF8_TO_TCHAR(V->Battery.c_str());
		}
		else
		{
			// çeşit tablosunda olmayan (INTERCEPTOR, kara aracı): tipin SITL eşlemesi
			SmvCore::FSitlSession S;
			S.SitlType = SmvCore::SitlTypeFromSmv(std::string(TCHAR_TO_UTF8(*M.Type)));
			S.Drive = M.Drive == TEXT("skid") ? "skid" : "ackermann";
			M.SitlBinary = UTF8_TO_TCHAR(SmvCore::SessionBinary(S).c_str());
			M.SitlModel = UTF8_TO_TCHAR(SmvCore::SessionModel(S).c_str());
			M.Physics = M.Type == TEXT("interceptor") ? TEXT("ArduPlane · quadplane-copter_tailsitter + INTERCEPTOR seti (HEPGO flightsim_icept)")
				: TEXT("ArduRover · ") + M.SitlModel;
			M.Motors = M.Type == TEXT("interceptor") ? 4 : 0;
		}
	}
	return C;
}

const FSmvCatalogModel* USmvDataService::FindModel(const FString& Key)
{
	for (const FSmvCatalogModel& M : Catalog()) if (M.Key == Key) return &M;
	return nullptr;
}

FString USmvDataService::DefaultModelFor(const FString& Type)
{
	// bridge.VARSAYILAN_MODEL
	if (Type == TEXT("fpv")) return TEXT("glb:iris_fpv");
	if (Type == TEXT("plane")) return TEXT("glb:rc_cessna");
	if (Type == TEXT("interceptor")) return TEXT("glb:icept_x4");
	if (Type == TEXT("heli")) return TEXT("glb:heli");
	if (Type == TEXT("vtol")) return TEXT("glb:standard_vtol");
	if (Type == TEXT("rover")) return TEXT("glb:rover_4x4");
	return TEXT("glb:iris");
}

TArray<const FSmvCatalogModel*> USmvDataService::ModelsForType(const FString& Type, bool bOnlineOnly)
{
	TArray<const FSmvCatalogModel*> Out;
	for (const FSmvCatalogModel& M : Catalog()) if (M.Type == Type && !(bOnlineOnly && M.bLocalOnly)) Out.Add(&M);
	return Out;
}

const TArray<FSmvCatalogModel>& USmvDataService::TargetModels()
{
	// Hedef / dekor GLB'leri (static/simumav/models tgt_*): YALNIZ görsel hedef nesnesi (atış, güdüm, otonom angajman davranışı yok)
	static TArray<FSmvCatalogModel> T;
	if (T.Num()) return T;
	auto Add = [](const TCHAR* Key, const TCHAR* Label, const TCHAR* File, float LenM, float CruiseKmh, float MaxKmh)
	{
		FSmvCatalogModel M;
		M.Key = Key; M.Label = Label; M.Type = TEXT("target"); M.GlbFile = File; M.bHasProp = true; M.bLocalOnly = true;
		M.LengthM = LenM; M.CruiseKmh = CruiseKmh; M.MaxKmh = MaxKmh;
		M.Desc = TEXT("Hedef / dekor modeli (yalnız görsel)");
		T.Add(M);
	};
	Add(TEXT("tgt:lancet"), TEXT("Lancet (hedef)"), TEXT("tgt_lancet.glb"), 1.65f, 80.f, 110.f);
	Add(TEXT("tgt:octopus"), TEXT("Octopus-100 (hedef)"), TEXT("tgt_octopus.glb"), 0.75f, 150.f, 250.f);
	Add(TEXT("tgt:sw300"), TEXT("Switchblade 300 (hedef)"), TEXT("tgt_sw300.glb"), 0.5f, 100.f, 160.f);
	Add(TEXT("tgt:altius"), TEXT("Altius-600 (hedef)"), TEXT("tgt_altius.glb"), 1.02f, 120.f, 200.f);
	return T;
}

bool USmvDataService::LoadBundledGlb(const FString& File, TArray<uint8>& Out)
{
	static TMap<FString, TArray<uint8>> Cache;
	if (File.IsEmpty()) return false;
	if (const TArray<uint8>* F = Cache.Find(File)) { Out = *F; return Out.Num() > 12; }
	// UFS (pak) içindeki dosya: göreli proje yolu korunur (tam yola çevrilmez)
	const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Simumav"), TEXT("ModelsSrc"), File);
	TArray<uint8> B;
	if (!FFileHelper::LoadFileToArray(B, *Path, FILEREAD_Silent) || B.Num() <= 12)
	{
		UE_LOG(LogSimumav, Log, TEXT("Gömülü GLB yok: %s"), *Path);
		Cache.Add(File, TArray<uint8>());
		return false;
	}
	Cache.Add(File, B);
	Out = MoveTemp(B);
	return true;
}

// ---------------------------------------------------------------------------

void USmvDataService::Init(USimumavClient* InClient, const FString& InProfileKey, const FString& DisplayName)
{
	Client = InClient;
	ProfileKey = InProfileKey.IsEmpty() ? FString(TEXT("local")) : InProfileKey;
	ProfileName = DisplayName.IsEmpty() ? FString(TEXT("Pilot")) : DisplayName;
}

bool USmvDataService::IsOnline() const { return Client && Client->IsLoggedIn(); }
ESmvAccountMode USmvDataService::GetMode() const { return IsOnline() ? ESmvAccountMode::Online : ESmvAccountMode::Guest; }

FString USmvDataService::ProfileDir() const { return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Simumav"), TEXT("Profiles"), ProfileKey); }
FString USmvDataService::GuestDir() const { return FPaths::Combine(ProfileDir(), TEXT("guest")); }

FString USmvDataService::NewLocalId() { return FString::Printf(TEXT("l-%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(12).ToLower()); }

template <typename T> void USmvDataService::Upsert(TArray<T>& Arr, const T& Item)
{
	for (T& X : Arr) if (X.Id == Item.Id) { X = Item; return; }
	Arr.Add(Item);
}

template <typename T> void USmvDataService::RemoveById(TArray<T>& Arr, const FString& Id)
{
	Arr.RemoveAll([&Id](const T& X) { return X.Id == Id; });
}

namespace
{
	template <typename T> TArray<T> SmvReadList(const FString& File, const TCHAR* Key)
	{
		TArray<T> Out;
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *File)) return Out;
		const TSharedPtr<FJsonObject> O = SmvJson::Parse(Text);
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (O.IsValid() && O->TryGetArrayField(Key, A) && A)
		{
			for (const TSharedPtr<FJsonValue>& V : *A)
			{
				const TSharedPtr<FJsonObject> J = V.IsValid() ? V->AsObject() : nullptr;
				if (!J.IsValid()) continue;
				T Item = T::FromJson(J);
				// yerel kimlik + varsayılan bayrağı (FromJson sunucu biçimini okur)
				Item.Id = SmvJson::Str(J, TEXT("id"), Item.Id);
				Out.Add(Item);
			}
		}
		return Out;
	}

	void SmvWriteJson(const FString& File, const TSharedRef<FJsonObject>& O)
	{
		FFileHelper::SaveStringToFile(SmvJson::ToString(O, true), *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
}

void USmvDataService::LoadLocal()
{
	const FString D = GuestDir();
	DronesCache = SmvReadList<FSmvDrone>(FPaths::Combine(D, TEXT("hangar.json")), TEXT("drones"));
	PilotsCache = SmvReadList<FSmvPilot>(FPaths::Combine(D, TEXT("crew.json")), TEXT("pilots"));
	TracksCache = SmvReadList<FSmvTrack>(FPaths::Combine(D, TEXT("tracks.json")), TEXT("tracks"));
	MissionsCache = SmvReadList<FSmvMission>(FPaths::Combine(D, TEXT("missions.json")), TEXT("missions"));
	FlightsCache.Reset();
	FString Text;
	if (FFileHelper::LoadFileToString(Text, *FPaths::Combine(D, TEXT("flights.json"))))
	{
		const TSharedPtr<FJsonObject> O = SmvJson::Parse(Text);
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (O.IsValid() && O->TryGetArrayField(TEXT("flights"), A) && A)
		{
			for (const TSharedPtr<FJsonValue>& V : *A)
			{
				const TSharedPtr<FJsonObject> J = V->AsObject();
				FSmvFlightSummary S;
				S.FlightId = SmvJson::Str(J, TEXT("id"));
				S.Result = SmvJson::Str(J, TEXT("result"));
				S.EndReason = SmvJson::Str(J, TEXT("end_reason"));
				S.DurationS = SmvJson::Num(J, TEXT("duration_s"));
				S.DistM = SmvJson::Num(J, TEXT("dist_m"));
				S.MaxSpeedMs = SmvJson::Num(J, TEXT("max_speed_seen"));
				S.AlertScore = (int32)SmvJson::Num(J, TEXT("alert_score"), -1);
				S.AlertGrade = SmvJson::Str(J, TEXT("alert_grade"));
				S.GatesPassed = (int32)SmvJson::Num(J, TEXT("gates_passed"));
				S.TargetScore = (int32)SmvJson::Num(J, TEXT("target_score"));
				FlightsCache.Add(S);
			}
		}
	}
	LoadSettingsLocal();
	if (DronesCache.Num() == 0 || PilotsCache.Num() == 0 || TracksCache.Num() == 0) SeedDefaults();
	OnChanged.Broadcast();
}

void USmvDataService::SeedDefaults()
{
	if (DronesCache.Num() == 0)
	{
		const TCHAR* Types[] = { TEXT("multirotor"), TEXT("fpv"), TEXT("plane"), TEXT("vtol") };
		const TCHAR* Names[] = { TEXT("Iris"), TEXT("FPV Yarışçı"), TEXT("Cessna"), TEXT("VTOL") };
		const TCHAR* Colors[] = { TEXT("#e53935"), TEXT("#fb8c00"), TEXT("#1e88e5"), TEXT("#43a047") };
		for (int32 i = 0; i < 4; ++i)
		{
			FSmvDrone D;
			D.Id = NewLocalId();
			D.Name = Names[i];
			D.Type = Types[i];
			D.ModelKey = DefaultModelFor(D.Type);
			D.Color = Colors[i];
			if (const FSmvCatalogModel* M = FindModel(D.ModelKey)) { D.Params = M->Defaults; if (!M->Airframe.IsEmpty()) D.Airframe = M->Airframe; }
			D.bIsDefault = (i == 0);
			DronesCache.Add(D);
		}
	}
	if (PilotsCache.Num() == 0)
	{
		FSmvPilot P;
		P.Id = NewLocalId();
		P.Name = ProfileName.Left(40);
		P.bIsDefault = true;
		PilotsCache.Add(P);
	}
	if (TracksCache.Num() == 0)
	{
		// prosedürel (çevrim dışı) + gerçek arazi (Cesium belirteci varsa) iki başlangıç parkuru
		FSmvTrack A;
		A.Id = NewLocalId();
		A.Name = TEXT("Eğitim Sahası (prosedürel)");
		A.Lat = 39.9334; A.Lon = 32.8597; A.GroundMsl = 0;
		A.WorldMode = TEXT("procedural");
		A.WindMs = 3; A.WindDir = 270;
		A.bIsDefault = true;
		TracksCache.Add(A);
		FSmvTrack B;
		B.Id = NewLocalId();
		B.Name = TEXT("Kapadokya (gerçek arazi)");
		B.Lat = 38.6431; B.Lon = 34.8289;
		B.WorldMode = TEXT("real");
		B.WindMs = 4; B.WindDir = 300; B.WindGust = 2;
		B.TimeOfDay = TEXT("dusk");
		TracksCache.Add(B);
	}
	SaveLocal();
}

void USmvDataService::SaveLocal() const
{
	if (IsOnline()) return;   // hesap kipinde sunucu esastır; misafir dosyaları dokunulmadan kalır
	const FString D = GuestDir();
	IFileManager::Get().MakeDirectory(*D, true);
	auto Write = [&D](const TCHAR* File, const TCHAR* Key, TArray<TSharedPtr<FJsonValue>> Items)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("v"), 1);
		O->SetArrayField(Key, Items);
		SmvWriteJson(FPaths::Combine(D, File), O);
	};
	TArray<TSharedPtr<FJsonValue>> A;
	for (const FSmvDrone& X : DronesCache) { TSharedRef<FJsonObject> J = X.ToJson(false); J->SetStringField(TEXT("id"), X.Id); J->SetStringField(TEXT("type"), X.Type); J->SetStringField(TEXT("drive"), X.Drive); A.Add(MakeShared<FJsonValueObject>(J)); }
	Write(TEXT("hangar.json"), TEXT("drones"), A);
	A.Reset();
	for (const FSmvPilot& X : PilotsCache) { TSharedRef<FJsonObject> J = X.ToJson(); J->SetStringField(TEXT("id"), X.Id); J->SetBoolField(TEXT("is_default"), X.bIsDefault); J->SetNumberField(TEXT("flights"), X.Flights); J->SetNumberField(TEXT("hours"), X.Hours); A.Add(MakeShared<FJsonValueObject>(J)); }
	Write(TEXT("crew.json"), TEXT("pilots"), A);
	A.Reset();
	for (const FSmvTrack& X : TracksCache) { TSharedRef<FJsonObject> J = X.ToJson(); J->SetStringField(TEXT("id"), X.Id); J->SetBoolField(TEXT("is_default"), X.bIsDefault); if (X.GroundMsl > -1000) J->SetNumberField(TEXT("ground_msl"), X.GroundMsl); A.Add(MakeShared<FJsonValueObject>(J)); }
	Write(TEXT("tracks.json"), TEXT("tracks"), A);
	A.Reset();
	for (const FSmvMission& X : MissionsCache) { TSharedRef<FJsonObject> J = X.ToJson(); J->SetStringField(TEXT("id"), X.Id); J->SetStringField(TEXT("track_id"), X.TrackId); A.Add(MakeShared<FJsonValueObject>(J)); }
	Write(TEXT("missions.json"), TEXT("missions"), A);
	A.Reset();
	for (const FSmvFlightSummary& X : FlightsCache) A.Add(MakeShared<FJsonValueObject>(X.ToJson()));
	Write(TEXT("flights.json"), TEXT("flights"), A);
}

void USmvDataService::LoadSettingsLocal()
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(ProfileDir(), TEXT("settings.json")))) return;
	const TSharedPtr<FJsonObject> O = SmvJson::Parse(Text);
	if (!O.IsValid()) return;
	FSmvSettings& S = SettingsCache;
	S.ApplyServerJson(O);
	S.GpwsMode = SmvJson::Str(O, TEXT("gpws_mode"), S.GpwsMode);
	S.GpwsSens = SmvJson::Str(O, TEXT("gpws_sens"), S.GpwsSens);
	S.GpwsLang = SmvJson::Str(O, TEXT("gpws_lang"), S.GpwsLang);
	S.BankCopter = (int32)SmvJson::Num(O, TEXT("bank_copter"), S.BankCopter);
	S.BankPlane = (int32)SmvJson::Num(O, TEXT("bank_plane"), S.BankPlane);
	S.GaugeTheme = SmvJson::Str(O, TEXT("gauge_theme"), S.GaugeTheme);
	S.Theme = SmvJson::Str(O, TEXT("theme"), S.Theme);
	S.GraphicsQuality = (int32)SmvJson::Num(O, TEXT("graphics_quality"), S.GraphicsQuality);
	S.bMpUdpOut = SmvJson::Bool(O, TEXT("mp_udp_out"), S.bMpUdpOut);
	S.MpUdpHost = SmvJson::Str(O, TEXT("mp_udp_host"), S.MpUdpHost);
	S.MpUdpPort = (int32)SmvJson::Num(O, TEXT("mp_udp_port"), S.MpUdpPort);
	S.InputDevice = SmvJson::Str(O, TEXT("input_device"), S.InputDevice);
	S.bStickMode2 = SmvJson::Bool(O, TEXT("stick_mode2"), S.bStickMode2);
	S.StickMode = FMath::Clamp((int32)SmvJson::Num(O, TEXT("stick_mode"), S.bStickMode2 ? 2 : 1), 1, 4);
	S.StickExpo = SmvJson::Num(O, TEXT("stick_expo"), S.StickExpo);
	FSmvSecrets::Load(TEXT("cesium_token"), S.CesiumToken);   // belirteç düz dosyada tutulmaz
	S.bAlertTones = SmvJson::Bool(O, TEXT("alert_tones"), S.bAlertTones);
	S.RcAxisMap.Reset(); S.RcInvert.Reset();
	const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
	if (O->TryGetArrayField(TEXT("rc_axis_map"), A) && A) for (const TSharedPtr<FJsonValue>& V : *A) S.RcAxisMap.Add((int32)V->AsNumber());
	if (O->TryGetArrayField(TEXT("rc_invert"), A) && A) for (const TSharedPtr<FJsonValue>& V : *A) S.RcInvert.Add(V->AsBool());
}

void USmvDataService::SaveSettingsLocalOnly() const
{
	const FSmvSettings& S = SettingsCache;
	TSharedRef<FJsonObject> O = S.ToServerJson();
	O->SetStringField(TEXT("gpws_mode"), S.GpwsMode);
	O->SetStringField(TEXT("gpws_sens"), S.GpwsSens);
	O->SetStringField(TEXT("gpws_lang"), S.GpwsLang);
	O->SetNumberField(TEXT("bank_copter"), S.BankCopter);
	O->SetNumberField(TEXT("bank_plane"), S.BankPlane);
	O->SetStringField(TEXT("gauge_theme"), S.GaugeTheme);
	O->SetStringField(TEXT("theme"), S.Theme);
	O->SetNumberField(TEXT("graphics_quality"), S.GraphicsQuality);
	O->SetBoolField(TEXT("mp_udp_out"), S.bMpUdpOut);
	O->SetStringField(TEXT("mp_udp_host"), S.MpUdpHost);
	O->SetNumberField(TEXT("mp_udp_port"), S.MpUdpPort);
	O->SetStringField(TEXT("input_device"), S.InputDevice);
	O->SetBoolField(TEXT("stick_mode2"), S.bStickMode2);
	O->SetNumberField(TEXT("stick_mode"), S.StickMode);
	O->SetNumberField(TEXT("stick_expo"), S.StickExpo);
	if (S.CesiumToken.IsEmpty()) FSmvSecrets::Delete(TEXT("cesium_token")); else FSmvSecrets::Save(TEXT("cesium_token"), S.CesiumToken);
	O->SetBoolField(TEXT("alert_tones"), S.bAlertTones);
	TArray<TSharedPtr<FJsonValue>> M, I;
	for (int32 V : S.RcAxisMap) M.Add(MakeShared<FJsonValueNumber>(V));
	for (bool B : S.RcInvert) I.Add(MakeShared<FJsonValueBoolean>(B));
	O->SetArrayField(TEXT("rc_axis_map"), M);
	O->SetArrayField(TEXT("rc_invert"), I);
	IFileManager::Get().MakeDirectory(*ProfileDir(), true);
	SmvWriteJson(FPaths::Combine(ProfileDir(), TEXT("settings.json")), O);
}

const FSmvDrone* USmvDataService::DefaultDrone() const
{
	for (const FSmvDrone& D : DronesCache) if (D.bIsDefault) return &D;
	return DronesCache.Num() ? &DronesCache[0] : nullptr;
}

const FSmvPilot* USmvDataService::DefaultPilot() const
{
	for (const FSmvPilot& P : PilotsCache) if (P.bIsDefault) return &P;
	return PilotsCache.Num() ? &PilotsCache[0] : nullptr;
}

const FSmvTrack* USmvDataService::DefaultTrack() const
{
	for (const FSmvTrack& T : TracksCache) if (T.bIsDefault) return &T;
	return TracksCache.Num() ? &TracksCache[0] : nullptr;
}

// ---------------------------------------------------------------------------
// çevrim içi çekme
// ---------------------------------------------------------------------------

void USmvDataService::RefreshOnline(FSmvDone Done)
{
	if (!IsOnline()) { LoadLocal(); Done.ExecuteIfBound(true, FString()); return; }
	TWeakObjectPtr<USmvDataService> Weak(this);
	struct FState { int32 Left = 5; bool bOk = true; FString Msg; };
	TSharedRef<FState> St = MakeShared<FState>();
	auto Part = [Weak, St, Done](bool bOk, const FString& M)
	{
		St->bOk &= bOk;
		if (!bOk && St->Msg.IsEmpty()) St->Msg = M;
		if (--St->Left == 0)
		{
			if (USmvDataService* Self = Weak.Get()) Self->OnChanged.Broadcast();
			Done.ExecuteIfBound(St->bOk, St->Msg);
		}
	};
	auto List = [](const TSharedPtr<FJsonObject>& O, const TCHAR* K) { const TArray<TSharedPtr<FJsonValue>>* A = nullptr; static TArray<TSharedPtr<FJsonValue>> Empty; return (O.IsValid() && O->TryGetArrayField(K, A) && A) ? *A : Empty; };
	Client->Get(TEXT("/api/hangar"), FSmvRestDone::CreateLambda([Weak, Part, List](int32 S, const TSharedPtr<FJsonObject>& O, const FString&, const FString& M)
	{
		if (USmvDataService* Self = Weak.Get()) if (S == 200) { Self->DronesCache.Reset(); for (const auto& V : List(O, TEXT("drones"))) Self->DronesCache.Add(FSmvDrone::FromJson(V->AsObject())); }
		Part(S == 200, M);
	}));
	Client->Get(TEXT("/api/crew"), FSmvRestDone::CreateLambda([Weak, Part, List](int32 S, const TSharedPtr<FJsonObject>& O, const FString&, const FString& M)
	{
		if (USmvDataService* Self = Weak.Get()) if (S == 200) { Self->PilotsCache.Reset(); for (const auto& V : List(O, TEXT("pilots"))) Self->PilotsCache.Add(FSmvPilot::FromJson(V->AsObject())); }
		Part(S == 200, M);
	}));
	Client->Get(TEXT("/api/tracks"), FSmvRestDone::CreateLambda([Weak, Part, List](int32 S, const TSharedPtr<FJsonObject>& O, const FString&, const FString& M)
	{
		if (USmvDataService* Self = Weak.Get()) if (S == 200) { Self->TracksCache.Reset(); for (const auto& V : List(O, TEXT("tracks"))) Self->TracksCache.Add(FSmvTrack::FromJson(V->AsObject())); }
		Part(S == 200, M);
	}));
	Client->Get(TEXT("/api/missions"), FSmvRestDone::CreateLambda([Weak, Part, List](int32 S, const TSharedPtr<FJsonObject>& O, const FString&, const FString& M)
	{
		if (USmvDataService* Self = Weak.Get()) if (S == 200) { Self->MissionsCache.Reset(); for (const auto& V : List(O, TEXT("missions"))) Self->MissionsCache.Add(FSmvMission::FromJson(V->AsObject())); }
		Part(S == 200, M);
	}));
	Client->Get(TEXT("/api/settings"), FSmvRestDone::CreateLambda([Weak, Part](int32 S, const TSharedPtr<FJsonObject>& O, const FString&, const FString& M)
	{
		if (USmvDataService* Self = Weak.Get()) if (S == 200) Self->SettingsCache.ApplyServerJson(O);
		Part(S == 200, M);
	}));
}

// ---------------------------------------------------------------------------
// yazma
// ---------------------------------------------------------------------------

#define SMV_ONLINE_WRITE(VERB, PATH, BODY, ONOK) \
	{ TWeakObjectPtr<USmvDataService> Weak(this); \
	  Client->Request(TEXT(VERB), PATH, BODY, FSmvRestDone::CreateLambda([Weak, Done, CapId](int32 S, const TSharedPtr<FJsonObject>& O, const FString& C, const FString& M) \
	  { USmvDataService* Self = Weak.Get(); const bool bOk = S >= 200 && S < 300; \
	    if (Self && bOk) { ONOK; Self->OnChanged.Broadcast(); } \
	    Done.ExecuteIfBound(bOk, bOk ? FString() : (M.IsEmpty() ? C : M)); })); }

void USmvDataService::SaveDrone(const FSmvDrone& DIn, FSmvDone Done)
{
	if (IsOnline())
	{
		const bool bNew = !SmvJson::IsServerId(DIn.Id);
		const FString CapId = DIn.Id;
		const FString Path = bNew ? FString(TEXT("/api/hangar")) : FString::Printf(TEXT("/api/hangar/%s"), *DIn.Id);
		const TSharedRef<FJsonObject> Body = DIn.ToJson(bNew);
		if (bNew) SMV_ONLINE_WRITE("POST", Path, Body, Upsert(Self->DronesCache, FSmvDrone::FromJson(O)))
		else SMV_ONLINE_WRITE("PUT", Path, Body, Upsert(Self->DronesCache, FSmvDrone::FromJson(O)))
		return;
	}
	FSmvDrone D = DIn;
	if (D.Id.IsEmpty()) D.Id = NewLocalId();
	if (D.bIsDefault) for (FSmvDrone& X : DronesCache) X.bIsDefault = false;
	Upsert(DronesCache, D);
	SaveLocal();
	OnChanged.Broadcast();
	Done.ExecuteIfBound(true, FString());
}

void USmvDataService::DeleteDrone(const FString& Id, FSmvDone Done)
{
	if (IsOnline()) { const FString CapId = Id; SMV_ONLINE_WRITE("DELETE", FString::Printf(TEXT("/api/hangar/%s"), *Id), nullptr, RemoveById(Self->DronesCache, CapId)) return; }
	if (DronesCache.Num() <= 1) { Done.ExecuteIfBound(false, TEXT("Hangarda en az bir dron kalmalı.")); return; }
	RemoveById(DronesCache, Id);
	SaveLocal(); OnChanged.Broadcast(); Done.ExecuteIfBound(true, FString());
}

void USmvDataService::SavePilot(const FSmvPilot& PIn, FSmvDone Done)
{
	if (IsOnline())
	{
		const bool bNew = !SmvJson::IsServerId(PIn.Id);
		const FString CapId = PIn.Id;
		const FString Path = bNew ? FString(TEXT("/api/crew")) : FString::Printf(TEXT("/api/crew/%s"), *PIn.Id);
		TSharedRef<FJsonObject> Body = PIn.ToJson();
		if (!bNew) Body->SetBoolField(TEXT("is_default"), PIn.bIsDefault);
		if (bNew) SMV_ONLINE_WRITE("POST", Path, Body, Upsert(Self->PilotsCache, FSmvPilot::FromJson(O)))
		else SMV_ONLINE_WRITE("PUT", Path, Body, Upsert(Self->PilotsCache, FSmvPilot::FromJson(O)))
		return;
	}
	FSmvPilot P = PIn;
	if (P.Id.IsEmpty()) P.Id = NewLocalId();
	if (P.bIsDefault) for (FSmvPilot& X : PilotsCache) X.bIsDefault = false;
	Upsert(PilotsCache, P);
	SaveLocal(); OnChanged.Broadcast(); Done.ExecuteIfBound(true, FString());
}

void USmvDataService::DeletePilot(const FString& Id, FSmvDone Done)
{
	if (IsOnline()) { const FString CapId = Id; SMV_ONLINE_WRITE("DELETE", FString::Printf(TEXT("/api/crew/%s"), *Id), nullptr, RemoveById(Self->PilotsCache, CapId)) return; }
	if (PilotsCache.Num() <= 1) { Done.ExecuteIfBound(false, TEXT("En az bir pilot kalmalı.")); return; }
	RemoveById(PilotsCache, Id);
	SaveLocal(); OnChanged.Broadcast(); Done.ExecuteIfBound(true, FString());
}

void USmvDataService::SaveTrack(const FSmvTrack& TIn, FSmvDone Done)
{
	if (IsOnline())
	{
		const bool bNew = !SmvJson::IsServerId(TIn.Id);
		const FString CapId = TIn.Id;
		const FString Path = bNew ? FString(TEXT("/api/tracks")) : FString::Printf(TEXT("/api/tracks/%s"), *TIn.Id);
		TSharedRef<FJsonObject> Body = TIn.ToJson();
		if (!bNew) Body->SetBoolField(TEXT("is_default"), TIn.bIsDefault);
		if (bNew) SMV_ONLINE_WRITE("POST", Path, Body, Upsert(Self->TracksCache, FSmvTrack::FromJson(O)))
		else SMV_ONLINE_WRITE("PUT", Path, Body, Upsert(Self->TracksCache, FSmvTrack::FromJson(O)))
		return;
	}
	FSmvTrack T = TIn;
	if (T.Id.IsEmpty()) T.Id = NewLocalId();
	if (T.bIsDefault) for (FSmvTrack& X : TracksCache) X.bIsDefault = false;
	Upsert(TracksCache, T);
	SaveLocal(); OnChanged.Broadcast(); Done.ExecuteIfBound(true, FString());
}

void USmvDataService::DeleteTrack(const FString& Id, FSmvDone Done)
{
	if (IsOnline()) { const FString CapId = Id; SMV_ONLINE_WRITE("DELETE", FString::Printf(TEXT("/api/tracks/%s"), *Id), nullptr, RemoveById(Self->TracksCache, CapId)) return; }
	if (TracksCache.Num() <= 1) { Done.ExecuteIfBound(false, TEXT("En az bir parkur kalmalı.")); return; }
	RemoveById(TracksCache, Id);
	MissionsCache.RemoveAll([&Id](const FSmvMission& M) { return M.TrackId == Id; });
	SaveLocal(); OnChanged.Broadcast(); Done.ExecuteIfBound(true, FString());
}

void USmvDataService::SaveMission(const FSmvMission& MIn, FSmvDone Done)
{
	if (IsOnline())
	{
		const bool bNew = !SmvJson::IsServerId(MIn.Id);
		const FString CapId = MIn.Id;
		const FString Path = bNew ? FString(TEXT("/api/missions")) : FString::Printf(TEXT("/api/missions/%s"), *MIn.Id);
		if (bNew) SMV_ONLINE_WRITE("POST", Path, MIn.ToJson(), Upsert(Self->MissionsCache, FSmvMission::FromJson(O)))
		else SMV_ONLINE_WRITE("PUT", Path, MIn.ToJson(), Upsert(Self->MissionsCache, FSmvMission::FromJson(O)))
		return;
	}
	FSmvMission M = MIn;
	if (M.Id.IsEmpty()) M.Id = NewLocalId();
	Upsert(MissionsCache, M);
	SaveLocal(); OnChanged.Broadcast(); Done.ExecuteIfBound(true, FString());
}

void USmvDataService::DeleteMission(const FString& Id, FSmvDone Done)
{
	if (IsOnline()) { const FString CapId = Id; SMV_ONLINE_WRITE("DELETE", FString::Printf(TEXT("/api/missions/%s"), *Id), nullptr, RemoveById(Self->MissionsCache, CapId)) return; }
	RemoveById(MissionsCache, Id);
	SaveLocal(); OnChanged.Broadcast(); Done.ExecuteIfBound(true, FString());
}

void USmvDataService::SaveSettings(FSmvDone Done)
{
	SaveSettingsLocalOnly();
	if (IsOnline()) { const FString CapId = FString(); SMV_ONLINE_WRITE("PUT", FString(TEXT("/api/settings")), SettingsCache.ToServerJson(), Self->SettingsCache.ApplyServerJson(O)) return; }
	OnChanged.Broadcast();
	Done.ExecuteIfBound(true, FString());
}

void USmvDataService::AddFlightRecord(const FSmvFlightSummary& S)
{
	FlightsCache.Insert(S, 0);
	if (FlightsCache.Num() > 100) FlightsCache.SetNum(100);   // sunucu kotasıyla aynı (flights 100)
	if (!IsOnline()) SaveLocal();
	OnChanged.Broadcast();
}

void USmvDataService::SetDefault(const FString& Kind, const FString& Id)
{
	if (Kind == TEXT("drone")) { for (FSmvDrone& X : DronesCache) if (X.Id == Id) { FSmvDrone C = X; C.bIsDefault = true; SaveDrone(C, FSmvDone()); } }
	else if (Kind == TEXT("pilot")) { for (FSmvPilot& X : PilotsCache) if (X.Id == Id) { FSmvPilot C = X; C.bIsDefault = true; SavePilot(C, FSmvDone()); } }
	else if (Kind == TEXT("track")) { for (FSmvTrack& X : TracksCache) if (X.Id == Id) { FSmvTrack C = X; C.bIsDefault = true; SaveTrack(C, FSmvDone()); } }
}

#undef SMV_ONLINE_WRITE

// ---------------------------------------------------------------------------
// misafir → hesap aktarımı
// ---------------------------------------------------------------------------

bool USmvDataService::HasGuestData() const
{
	return FPaths::FileExists(FPaths::Combine(GuestDir(), TEXT("hangar.json")));
}

void USmvDataService::ImportGuestToAccount(FSmvDone Done)
{
	if (!IsOnline()) { Done.ExecuteIfBound(false, TEXT("Önce giriş yapın.")); return; }
	const FString D = GuestDir();
	const TArray<FSmvDrone> GDrones = SmvReadList<FSmvDrone>(FPaths::Combine(D, TEXT("hangar.json")), TEXT("drones"));
	const TArray<FSmvPilot> GPilots = SmvReadList<FSmvPilot>(FPaths::Combine(D, TEXT("crew.json")), TEXT("pilots"));
	const TArray<FSmvTrack> GTracks = SmvReadList<FSmvTrack>(FPaths::Combine(D, TEXT("tracks.json")), TEXT("tracks"));
	const TArray<FSmvMission> GMissions = SmvReadList<FSmvMission>(FPaths::Combine(D, TEXT("missions.json")), TEXT("missions"));
	struct FImp { int32 Left = 0; int32 Ok = 0; int32 Fail = 0; FString FirstErr; TMap<FString, FString> TrackMap; TArray<FSmvMission> Missions; };
	TSharedRef<FImp> St = MakeShared<FImp>();
	St->Missions = GMissions;
	St->Left = GDrones.Num() + GPilots.Num() + GTracks.Num();
	TWeakObjectPtr<USmvDataService> Weak(this);
	auto FinishMissions = [Weak, St, Done]()
	{
		USmvDataService* Self = Weak.Get();
		if (!Self) return;
		int32 Left = 0;
		for (const FSmvMission& M : St->Missions) if (St->TrackMap.Contains(M.TrackId)) ++Left;
		TSharedRef<int32> Pending = MakeShared<int32>(Left);
		auto End = [Weak, St, Done]()
		{
			if (USmvDataService* S2 = Weak.Get()) S2->RefreshOnline(FSmvDone());
			Done.ExecuteIfBound(St->Fail == 0, FString::Printf(TEXT("%d kayıt aktarıldı, %d başarısız%s%s"), St->Ok, St->Fail, St->FirstErr.IsEmpty() ? TEXT("") : TEXT(": "), *St->FirstErr));
		};
		if (Left == 0) { End(); return; }
		for (const FSmvMission& M : St->Missions)
		{
			const FString* NewTrack = St->TrackMap.Find(M.TrackId);
			if (!NewTrack) continue;
			FSmvMission C = M;
			C.TrackId = *NewTrack;
			Self->Client->Post(TEXT("/api/missions"), C.ToJson(), FSmvRestDone::CreateLambda([St, Pending, End](int32 S, const TSharedPtr<FJsonObject>&, const FString& Cd, const FString& Ms)
			{
				if (S == 201 || S == 200) St->Ok++; else { St->Fail++; if (St->FirstErr.IsEmpty()) St->FirstErr = Ms.IsEmpty() ? Cd : Ms; }
				if (--(*Pending) == 0) End();
			}));
		}
	};
	if (St->Left == 0) { FinishMissions(); return; }
	auto Step = [St, FinishMissions](int32 S, const FString& C, const FString& M)
	{
		if (S == 201 || S == 200) St->Ok++; else { St->Fail++; if (St->FirstErr.IsEmpty()) St->FirstErr = M.IsEmpty() ? C : M; }
		if (--St->Left == 0) FinishMissions();
	};
	for (const FSmvDrone& X : GDrones)
		Client->Post(TEXT("/api/hangar"), X.ToJson(true), FSmvRestDone::CreateLambda([Step](int32 S, const TSharedPtr<FJsonObject>&, const FString& C, const FString& M) { Step(S, C, M); }));
	for (const FSmvPilot& X : GPilots)
		Client->Post(TEXT("/api/crew"), X.ToJson(), FSmvRestDone::CreateLambda([Step](int32 S, const TSharedPtr<FJsonObject>&, const FString& C, const FString& M) { Step(S, C, M); }));
	for (const FSmvTrack& X : GTracks)
	{
		const FString OldId = X.Id;
		Client->Post(TEXT("/api/tracks"), X.ToJson(), FSmvRestDone::CreateLambda([Step, St, OldId](int32 S, const TSharedPtr<FJsonObject>& O, const FString& C, const FString& M)
		{
			if (S == 201 || S == 200) St->TrackMap.Add(OldId, SmvJson::Id(O, TEXT("id")));
			Step(S, C, M);
		}));
	}
}

// ---------------------------------------------------------------------------
// model önbelleği
// ---------------------------------------------------------------------------

void USmvDataService::FetchModelGlb(const FString& ModelKey, TFunction<void(bool, const TArray<uint8>&)> Done)
{
	const FSmvCatalogModel* M = FindModel(ModelKey);
	if (M && !M->GlbFile.IsEmpty())
	{
		TArray<uint8> Local;
		if (LoadBundledGlb(M->GlbFile, Local)) { Done(true, Local); return; }
	}
	if (!M || M->GlbPath.IsEmpty() || !Client) { Done(false, TArray<uint8>()); return; }
	const FString File = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Simumav"), TEXT("ModelCache"), FPaths::GetCleanFilename(M->GlbPath));
	TArray<uint8> Bytes;
	if (FFileHelper::LoadFileToArray(Bytes, *File) && Bytes.Num() > 12) { Done(true, Bytes); return; }
	Client->GetBytes(M->GlbPath, [File, Done](bool bOk, const TArray<uint8>& B)
	{
		if (bOk && B.Num() > 12)
		{
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
			FFileHelper::SaveArrayToFile(B, *File);
		}
		Done(bOk && B.Num() > 12, B);
	});
}
