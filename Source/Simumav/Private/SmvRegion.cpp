// Gerçek dünya bölge haritası: çevrimdışı paket (açık veri) + Google 3B kapsama denetimi (bkz. SmvRegion.h).
#include "SmvRegion.h"
#include "SimumavModule.h"
#include "SimumavTypes.h"
#include "SmvI18n.h"
#include "SmvRegionCore.h"
#include "Async/Async.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HttpModule.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include <cmath>

namespace
{
	const TCHAR* GUserAgent = TEXT("Simumav/1.0 (+https://simumav.com; bolge haritasi)");
	// Overpass: ana sunucu + yansı (yoğunlukta 429 / 504 → sıradaki)
	const TCHAR* GOverpass[] = { TEXT("https://overpass-api.de/api/interpreter"), TEXT("https://overpass.kumi.systems/api/interpreter") };
	constexpr int32 GDemZ = 14, GFarZ = 10;
	constexpr double GFarRadiusM = 30000.0;
	constexpr int64 GGoogleIonAsset = 2275207;

	uint64 TileKey(int32 X, int32 Y) { return ((uint64)(uint32)X << 32) | (uint32)Y; }

	void HttpCall(const FString& Url, const FString& Verb, const FString& Body, const TMap<FString, FString>& Hdr, float TimeoutS,
		TFunction<void(int32, const TArray<uint8>&)> Done)
	{
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> R = FHttpModule::Get().CreateRequest();
		R->SetURL(Url);
		R->SetVerb(Verb);
		R->SetHeader(TEXT("User-Agent"), GUserAgent);
		for (const auto& Kv : Hdr) R->SetHeader(Kv.Key, Kv.Value);
		if (!Body.IsEmpty()) R->SetContentAsString(Body);
		R->SetTimeout(TimeoutS);
		R->OnProcessRequestComplete().BindLambda([Done](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
		{
			static const TArray<uint8> Empty;
			if (!bOk || !Resp.IsValid()) { Done(0, Empty); return; }
			Done(Resp->GetResponseCode(), Resp->GetContent());
		});
		R->ProcessRequest();
	}

	void After(float Seconds, TFunction<void()> Fn)
	{
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Fn](float) { Fn(); return false; }), Seconds);
	}

	FString Utf8(const TArray<uint8>& B)
	{
		FUTF8ToTCHAR C((const ANSICHAR*)B.GetData(), B.Num());
		return FString(C.Length(), C.Get());
	}

	// ---- URL çözümleme (3D Tiles alt içerik uri'leri; Google: üst sorgu parametreleri — key, session — çocuklara taşınır) ----
	void SplitUrl(const FString& U, FString& Origin, FString& Path, FString& Query)
	{
		FString Rest = U;
		Query.Reset();
		int32 Q;
		if (Rest.FindChar(TEXT('?'), Q)) { Query = Rest.Mid(Q + 1); Rest = Rest.Left(Q); }
		const int32 S = Rest.Find(TEXT("://"));
		const int32 P = S >= 0 ? Rest.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, S + 3) : -1;
		Origin = P >= 0 ? Rest.Left(P) : Rest;
		Path = P >= 0 ? Rest.Mid(P) : TEXT("/");
	}

	FString ResolveUrl(const FString& Base, const FString& Uri)
	{
		FString BO, BP, BQ, UO, UP, UQ;
		SplitUrl(Base, BO, BP, BQ);
		FString Out;
		if (Uri.StartsWith(TEXT("http://")) || Uri.StartsWith(TEXT("https://"))) Out = Uri;
		else if (Uri.StartsWith(TEXT("/"))) Out = BO + Uri;
		else
		{
			FString Dir = BP;
			int32 L;
			if (Dir.FindLastChar(TEXT('/'), L)) Dir = Dir.Left(L + 1);
			FString Rel = Uri;
			while (Rel.StartsWith(TEXT("./"))) Rel = Rel.Mid(2);
			Out = BO + Dir + Rel;
		}
		// üst sorgu parametreleri (çocukta yoksa)
		SplitUrl(Out, UO, UP, UQ);
		TArray<FString> Have, Add;
		UQ.ParseIntoArray(Have, TEXT("&"), true);
		BQ.ParseIntoArray(Add, TEXT("&"), true);
		TSet<FString> Keys;
		for (const FString& H : Have) Keys.Add(H.Left(H.Find(TEXT("="))));
		for (const FString& A : Add) { const FString K = A.Left(A.Find(TEXT("="))); if (!Keys.Contains(K)) { Have.Add(A); Keys.Add(K); } }
		return UO + UP + (Have.Num() ? TEXT("?") + FString::Join(Have, TEXT("&")) : FString());
	}

	// ---- OSM öğeleri (işçi iş parçacığında ayrıştırılır, oyun iş parçacığında tekilleştirilip yazılır) ----
	struct FOsmItem
	{
		int64 Id = 0;
		uint8 Kind = 0;      // 1 bina, 2 yol, 3 alan, 4 ağaç
		uint8 Sub = 0;       // yol: 0 taşıt, 1 yaya, 2 toprak · alan: 1 su, 2 orman, 3 park/çim, 4 çalı, 5 tarla, 6 kum, 7 mezarlık
		float H = 0, MinH = 0, W = 0;
		TArray<FVector2f> P;
	};

	uint8 AreaKind(const TSharedPtr<FJsonObject>& Tg)
	{
		const FString Nat = SmvJson::Str(Tg, TEXT("natural")), Lu = SmvJson::Str(Tg, TEXT("landuse")), Le = SmvJson::Str(Tg, TEXT("leisure")), Ww = SmvJson::Str(Tg, TEXT("waterway"));
		if (Nat == TEXT("water") || Lu == TEXT("basin") || Lu == TEXT("reservoir") || Ww == TEXT("riverbank")) return 1;
		if (Nat == TEXT("wood") || Lu == TEXT("forest")) return 2;
		if (Lu == TEXT("grass") || Lu == TEXT("meadow") || Lu == TEXT("recreation_ground") || Lu == TEXT("village_green") || Le == TEXT("park") || Le == TEXT("garden")
			|| Le == TEXT("pitch") || Le == TEXT("golf_course") || Nat == TEXT("grassland") || Nat == TEXT("heath")) return 3;
		if (Nat == TEXT("scrub")) return 4;
		if (Lu == TEXT("farmland") || Lu == TEXT("orchard") || Lu == TEXT("vineyard") || Lu == TEXT("allotments")) return 5;
		if (Nat == TEXT("beach") || Nat == TEXT("sand")) return 6;
		if (Lu == TEXT("cemetery")) return 7;
		return 0;
	}

	void ParseOverpass(const FString& Text, double Lat0, double Lon0, TArray<FOsmItem>& Out, bool& bOk)
	{
		bOk = false;
		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> Rd = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Rd, Root) || !Root.IsValid()) return;
		const TArray<TSharedPtr<FJsonValue>>* El = nullptr;
		if (!Root->TryGetArrayField(TEXT("elements"), El)) return;
		bOk = true;
		for (const TSharedPtr<FJsonValue>& V : *El)
		{
			const TSharedPtr<FJsonObject> E = V->AsObject();
			if (!E.IsValid()) continue;
			const FString Type = SmvJson::Str(E, TEXT("type"));
			const TSharedPtr<FJsonObject> Tg = SmvJson::Obj(E, TEXT("tags"));
			if (!Tg.IsValid()) continue;
			FOsmItem It;
			It.Id = (int64)SmvJson::Num(E, TEXT("id")) * 4 + (Type == TEXT("node") ? 1 : 0);
			if (Type == TEXT("node"))
			{
				if (SmvJson::Str(Tg, TEXT("natural")) != TEXT("tree")) continue;
				double Ee, Nn;
				SmvRegion::LatLonToEnu(Lat0, Lon0, SmvJson::Num(E, TEXT("lat")), SmvJson::Num(E, TEXT("lon")), Ee, Nn);
				It.Kind = 4;
				It.P.Add(FVector2f((float)Ee, (float)Nn));
				Out.Add(MoveTemp(It));
				continue;
			}
			if (Type != TEXT("way")) continue;
			const TArray<TSharedPtr<FJsonValue>>* G = nullptr;
			if (!E->TryGetArrayField(TEXT("geometry"), G) || G->Num() < 2) continue;
			for (const TSharedPtr<FJsonValue>& Pv : *G)
			{
				const TSharedPtr<FJsonObject> Po = Pv->AsObject();
				if (!Po.IsValid()) continue;
				double Ee, Nn;
				SmvRegion::LatLonToEnu(Lat0, Lon0, SmvJson::Num(Po, TEXT("lat")), SmvJson::Num(Po, TEXT("lon")), Ee, Nn);
				It.P.Add(FVector2f((float)Ee, (float)Nn));
			}
			const FString Bld = SmvJson::Str(Tg, TEXT("building"));
			const FString Hw = SmvJson::Str(Tg, TEXT("highway"));
			const double Layer = SmvRegion::ParseNumber(TCHAR_TO_UTF8(*SmvJson::Str(Tg, TEXT("layer"))));
			if (!Bld.IsEmpty() && Bld != TEXT("no"))
			{
				if (SmvJson::Str(Tg, TEXT("location")) == TEXT("underground") || It.P.Num() < 4) continue;
				std::vector<SmvRegion::FXY> Ring;
				for (const FVector2f& Q : It.P) Ring.push_back({ Q.X, Q.Y });
				if (!SmvRegion::CleanRing(Ring, 0.25)) continue;
				const double Area = std::fabs(SmvRegion::SignedArea(Ring));
				if (Area < 6.0) continue;
				It.Kind = 1;
				It.H = (float)SmvRegion::BuildingHeight(TCHAR_TO_UTF8(*Bld), TCHAR_TO_UTF8(*SmvJson::Str(Tg, TEXT("height"))),
					TCHAR_TO_UTF8(*SmvJson::Str(Tg, TEXT("building:levels"))), TCHAR_TO_UTF8(*SmvJson::Str(Tg, TEXT("roof:levels"))), (uint64)It.Id, Area);
				const double Mh = SmvRegion::ParseNumber(TCHAR_TO_UTF8(*SmvJson::Str(Tg, TEXT("min_height"))));
				It.MinH = Mh > 0 && Mh < It.H - 1.0 ? (float)Mh : 0.f;
				It.P.Reset();
				for (const SmvRegion::FXY& Q : Ring) It.P.Add(FVector2f((float)Q.X, (float)Q.Y));
				Out.Add(MoveTemp(It));
				continue;
			}
			if (!Hw.IsEmpty())
			{
				if (SmvJson::Str(Tg, TEXT("tunnel")) == TEXT("yes") || SmvJson::Str(Tg, TEXT("area")) == TEXT("yes") || Layer < -0.5) continue;
				const double W = SmvRegion::RoadWidth(TCHAR_TO_UTF8(*Hw), TCHAR_TO_UTF8(*SmvJson::Str(Tg, TEXT("lanes"))));
				if (W <= 0) continue;
				It.Kind = 2;
				It.W = (float)W;
				It.Sub = (Hw == TEXT("footway") || Hw == TEXT("path") || Hw == TEXT("cycleway") || Hw == TEXT("steps") || Hw == TEXT("pedestrian")) ? 1
					: (Hw == TEXT("track") || SmvJson::Str(Tg, TEXT("surface")) == TEXT("unpaved") || SmvJson::Str(Tg, TEXT("surface")) == TEXT("dirt")
						|| SmvJson::Str(Tg, TEXT("surface")) == TEXT("gravel") || SmvJson::Str(Tg, TEXT("surface")) == TEXT("ground")) ? 2 : 0;
				Out.Add(MoveTemp(It));
				continue;
			}
			const uint8 Ak = AreaKind(Tg);
			if (Ak && It.P.Num() >= 4)
			{
				std::vector<SmvRegion::FXY> Ring;
				for (const FVector2f& Q : It.P) Ring.push_back({ Q.X, Q.Y });
				if (!SmvRegion::CleanRing(Ring, 0.5)) continue;
				It.Kind = 3; It.Sub = Ak;
				It.P.Reset();
				for (const SmvRegion::FXY& Q : Ring) It.P.Add(FVector2f((float)Q.X, (float)Q.Y));
				Out.Add(MoveTemp(It));
			}
		}
	}

	void AppendPts(FString& S, const TArray<FVector2f>& P)
	{
		for (const FVector2f& Q : P) S += FString::Printf(TEXT(",%.1f,%.1f"), Q.X, Q.Y);
	}
}

// ===========================================================================
// FSmvRegionInfo
// ===========================================================================

bool FSmvRegionInfo::Covers(double InLat, double InLon, double InRadiusM) const
{
	double E, N;
	SmvRegion::LatLonToEnu(Lat, Lon, InLat, InLon, E, N);
	return FMath::Sqrt(E * E + N * N) + InRadiusM <= RadiusM - FSmvRegions::MarginM * 0.5 + 1.0;
}

bool FSmvRegionInfo::GoogleOk() const
{
	return bGoogleChecked && GoogleAny >= 0.9f && (FDateTime::UtcNow() - GoogleAt).GetTotalDays() < 30.0;
}

FString FSmvRegionInfo::Dir() const { return FSmvRegions::RootDir() / Id; }

TSharedRef<FJsonObject> FSmvRegionInfo::ToJson() const
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetNumberField(TEXT("v"), 1);
	O->SetStringField(TEXT("id"), Id);
	O->SetNumberField(TEXT("lat"), Lat); O->SetNumberField(TEXT("lon"), Lon);
	O->SetNumberField(TEXT("radius_m"), RadiusM); O->SetNumberField(TEXT("flight_radius_m"), FlightRadiusM);
	O->SetBoolField(TEXT("pack"), bPack);
	O->SetStringField(TEXT("pack_at"), PackAt.ToIso8601());
	O->SetNumberField(TEXT("bytes"), (double)Bytes);
	O->SetNumberField(TEXT("dem_tiles"), DemTiles); O->SetNumberField(TEXT("osm_tiles"), OsmTiles); O->SetNumberField(TEXT("osm_failed"), OsmFailed);
	O->SetNumberField(TEXT("buildings"), Buildings); O->SetNumberField(TEXT("roads"), Roads); O->SetNumberField(TEXT("areas"), Areas); O->SetNumberField(TEXT("trees"), Trees);
	O->SetNumberField(TEXT("center_msl"), CenterMsl); O->SetNumberField(TEXT("min_msl"), MinMsl); O->SetNumberField(TEXT("max_msl"), MaxMsl);
	O->SetBoolField(TEXT("google_checked"), bGoogleChecked);
	O->SetStringField(TEXT("google_at"), GoogleAt.ToIso8601());
	O->SetNumberField(TEXT("google_full"), GoogleFull); O->SetNumberField(TEXT("google_any"), GoogleAny); O->SetNumberField(TEXT("google_median_m"), GoogleMedianM);
	O->SetNumberField(TEXT("google_samples"), GoogleSamples); O->SetNumberField(TEXT("google_requests"), GoogleRequests);
	O->SetStringField(TEXT("google_note"), GoogleNote);
	O->SetStringField(TEXT("attribution"), TEXT("Arazi: AWS Terrain Tiles (Mapzen; SRTM, GMTED2010, ETOPO1, EU-DEM, NED …). Haritalar: © OpenStreetMap katkıcıları (ODbL)."));
	return O;
}

FSmvRegionInfo FSmvRegionInfo::FromJson(const TSharedPtr<FJsonObject>& O)
{
	FSmvRegionInfo R;
	if (!O.IsValid()) return R;
	R.Id = SmvJson::Str(O, TEXT("id"));
	R.Lat = SmvJson::Num(O, TEXT("lat")); R.Lon = SmvJson::Num(O, TEXT("lon"));
	R.RadiusM = SmvJson::Num(O, TEXT("radius_m")); R.FlightRadiusM = SmvJson::Num(O, TEXT("flight_radius_m"));
	R.bPack = SmvJson::Bool(O, TEXT("pack"));
	FDateTime::ParseIso8601(*SmvJson::Str(O, TEXT("pack_at")), R.PackAt);
	R.Bytes = (int64)SmvJson::Num(O, TEXT("bytes"));
	R.DemTiles = (int32)SmvJson::Num(O, TEXT("dem_tiles")); R.OsmTiles = (int32)SmvJson::Num(O, TEXT("osm_tiles")); R.OsmFailed = (int32)SmvJson::Num(O, TEXT("osm_failed"));
	R.Buildings = (int32)SmvJson::Num(O, TEXT("buildings")); R.Roads = (int32)SmvJson::Num(O, TEXT("roads"));
	R.Areas = (int32)SmvJson::Num(O, TEXT("areas")); R.Trees = (int32)SmvJson::Num(O, TEXT("trees"));
	R.CenterMsl = SmvJson::Num(O, TEXT("center_msl")); R.MinMsl = SmvJson::Num(O, TEXT("min_msl")); R.MaxMsl = SmvJson::Num(O, TEXT("max_msl"));
	R.bGoogleChecked = SmvJson::Bool(O, TEXT("google_checked"));
	FDateTime::ParseIso8601(*SmvJson::Str(O, TEXT("google_at")), R.GoogleAt);
	R.GoogleFull = (float)SmvJson::Num(O, TEXT("google_full")); R.GoogleAny = (float)SmvJson::Num(O, TEXT("google_any"));
	R.GoogleMedianM = (float)SmvJson::Num(O, TEXT("google_median_m"));
	R.GoogleSamples = (int32)SmvJson::Num(O, TEXT("google_samples")); R.GoogleRequests = (int32)SmvJson::Num(O, TEXT("google_requests"));
	R.GoogleNote = SmvJson::Str(O, TEXT("google_note"));
	return R;
}

// ===========================================================================
// İndirme işi
// ===========================================================================

class FSmvRegionJob : public TSharedFromThis<FSmvRegionJob>
{
public:
	FSmvRegionInfo Info;
	FString Token;
	bool bGoogle = true;
	bool bCancel = false;

	void Begin()
	{
		const FString D = Info.Dir();
		IFileManager::Get().MakeDirectory(*(D / TEXT("dem14")), true);
		IFileManager::Get().MakeDirectory(*(D / TEXT("dem10")), true);
		IFileManager::Get().MakeDirectory(*(D / TEXT("osm")), true);
		// arazi karoları: uçuş alanı z14 (paket yarıçapı), ufuk z10 (30 km)
		auto AddRange = [this](int32 Z, double R, bool bCircle)
		{
			int32 X0, Y0, X1, Y1;
			SmvRegion::TileRange(Info.Lat, Info.Lon, R, Z, X0, Y0, X1, Y1);
			for (int32 Y = Y0; Y <= Y1; ++Y)
				for (int32 X = X0; X <= X1; ++X)
				{
					if (bCircle && TileDistM(Z, X, Y) > R) continue;
					DemQ.Add({ Z, X, Y });
				}
		};
		AddRange(GDemZ, Info.RadiusM, true);
		const int32 N14 = DemQ.Num();
		AddRange(GFarZ, GFarRadiusM, false);
		DemTotal = DemQ.Num();
		int32 X0, Y0, X1, Y1;
		SmvRegion::TileRange(Info.Lat, Info.Lon, Info.RadiusM, GDemZ, X0, Y0, X1, Y1);
		for (int32 Y = Y0; Y <= Y1; ++Y)
			for (int32 X = X0; X <= X1; ++X)
				if (TileDistM(GDemZ, X, Y) <= Info.RadiusM) OsmQ.Add({ X, Y, 0 });
		OsmTotal = OsmQ.Num();
		SMV_KLOG(TEXT("Bölge %s: %.5f, %.5f, yarıçap %.0f m — arazi %d + %d karo, OSM %d karo, Google denetimi %s"), *Info.Id, Info.Lat, Info.Lon, Info.RadiusM,
			N14, DemTotal - N14, OsmTotal, bGoogle && !Token.IsEmpty() ? TEXT("evet") : TEXT("hayır"));
		Phase = 0;
		SetStatus(TEXT("region.st_dem"));
		PumpDem();
	}

private:
	struct FDemT { int32 Z, X, Y; };
	struct FOsmT { int32 X, Y, Tries; };
	TArray<FDemT> DemQ;
	TArray<FOsmT> OsmQ;
	int32 DemTotal = 0, DemDone = 0, DemFail = 0, DemInFlight = 0;
	int32 OsmTotal = 0, OsmDone = 0, OsmFail = 0, OsmInFlight = 0;
	int32 Phase = 0;
	TSet<int64> Seen;
	int32 NB = 0, NR = 0, NA = 0, NT = 0;

	double TileDistM(int32 Z, int32 X, int32 Y) const
	{
		double S, W, N, E;
		SmvRegion::TileBounds(Z, X, Y, S, W, N, E);
		const double La = FMath::Clamp(Info.Lat, S, N), Lo = FMath::Clamp(Info.Lon, W, E);
		double Ee, Nn;
		SmvRegion::LatLonToEnu(Info.Lat, Info.Lon, La, Lo, Ee, Nn);
		return FMath::Sqrt(Ee * Ee + Nn * Nn);
	}

	void SetStatus(const TCHAR* Key, const FString& Extra = FString())
	{
		FString S = FSmvI18n::T(Key).ToString();
		if (!Extra.IsEmpty()) S += TEXT(" ") + Extra;
		FSmvRegions::Get().Status = S;
	}

	void UpdateProgress()
	{
		const float Pd = DemTotal ? (float)DemDone / DemTotal : 1.f;
		const float Pg = Phase > 1 ? 1.f : Phase == 1 ? FMath::Clamp((float)GReq / FMath::Max(1, GReq + GQueue.Num() + GInFlight), 0.f, 0.95f) : 0.f;
		const float Po = OsmTotal ? (float)OsmDone / OsmTotal : 1.f;
		FSmvRegions::Get().Progress = 0.12f * Pd + 0.10f * Pg + 0.78f * Po;
	}

	bool Cancelled()
	{
		if (!bCancel) return false;
		return true;
	}

	// ---------------- arazi ----------------
	void PumpDem()
	{
		if (Cancelled()) return;
		while (DemInFlight < 6 && DemQ.Num())
		{
			const FDemT T = DemQ.Pop(EAllowShrinking::No);
			const FString File = Info.Dir() / FString::Printf(TEXT("dem%d/%d_%d.png"), T.Z, T.X, T.Y);
			if (IFileManager::Get().FileSize(*File) > 64) { ++DemDone; continue; }   // önceki yarım işten (devam)
			++DemInFlight;
			TWeakPtr<FSmvRegionJob> Wk = AsShared();
			const FString Url = FString::Printf(TEXT("https://s3.amazonaws.com/elevation-tiles-prod/terrarium/%d/%d/%d.png"), T.Z, T.X, T.Y);
			HttpCall(Url, TEXT("GET"), FString(), {}, 30.f, [Wk, File, T](int32 Code, const TArray<uint8>& Data)
			{
				TSharedPtr<FSmvRegionJob> J = Wk.Pin();
				if (!J.IsValid()) return;
				--J->DemInFlight;
				if (Code == 200 && Data.Num() > 64) FFileHelper::SaveArrayToFile(Data, *File);
				else { ++J->DemFail; UE_LOG(LogSimumav, Warning, TEXT("Bölge arazi karosu %d/%d/%d: HTTP %d"), T.Z, T.X, T.Y, Code); }
				++J->DemDone;
				J->UpdateProgress();
				J->PumpDem();
			});
		}
		UpdateProgress();
		if (DemInFlight == 0 && DemQ.Num() == 0 && Phase == 0)
		{
			Phase = 1;
			if (!Dem.Load(Info)) { FSmvRegions::Get().Finish(ESmvRegionState::Failed, Info, FSmvI18n::T(TEXT("region.err_dem")).ToString()); return; }
			Info.CenterMsl = Dem.HeightMsl(Info.Lat, Info.Lon);
			// yükseklik aralığı (kaba ızgara)
			Info.MinMsl = 1e9; Info.MaxMsl = -1e9;
			for (int32 i = -10; i <= 10; ++i)
				for (int32 j = -10; j <= 10; ++j)
				{
					const double H = Dem.HeightEnu(i * Info.RadiusM / 10.0, j * Info.RadiusM / 10.0);
					Info.MinMsl = FMath::Min(Info.MinMsl, H); Info.MaxMsl = FMath::Max(Info.MaxMsl, H);
				}
			Info.DemTiles = DemTotal - DemFail;
			SMV_KLOG(TEXT("Bölge %s: arazi %d karo (%d hata), merkez %.1f m MSL, aralık %.0f..%.0f m"), *Info.Id, DemTotal, DemFail, Info.CenterMsl, Info.MinMsl, Info.MaxMsl);
			if (bGoogle && !Token.IsEmpty()) StartGoogle();
			else { Phase = 2; SetStatus(TEXT("region.st_osm")); PumpOsm(); }
		}
	}

	// ---------------- Google 3B kapsama denetimi (yalnız JSON düğümleri; glb içerik indirilmez) ----------------
	FSmvRegionDem Dem;
	TArray<double> SA, SB;   // örnek düşey doğru parçaları (ECEF, 3'er)
	TArray<double> Best;     // örnek başına en ince içerikli karo boyu (m)
	struct FGq { FString Url; TArray<int32> S; };
	TArray<FGq> GQueue;
	int32 GInFlight = 0, GReq = 0, GErr = 0;
	double GT0 = 0;
	TMap<FString, FString> GHdr;
	bool bGDone = false, bGLimit = false;
	double GTargetM = 25.0, GFullM = 60.0;
	int32 GMaxReq = 900;

	void StartGoogle()
	{
		SetStatus(TEXT("region.st_google"));
		if (GConfig)
		{
			GConfig->GetDouble(TEXT("/Script/Simumav.SmvRegion"), TEXT("GoogleTargetM"), GTargetM, GGameIni);
			GConfig->GetDouble(TEXT("/Script/Simumav.SmvRegion"), TEXT("GoogleFullM"), GFullM, GGameIni);
			GConfig->GetInt(TEXT("/Script/Simumav.SmvRegion"), TEXT("GoogleMaxRequests"), GMaxReq, GGameIni);
		}
		const std::vector<SmvRegion::FXY> S = SmvRegion::CircleSamples(Info.FlightRadiusM, FMath::Clamp((int32)(Info.FlightRadiusM / 600.0) + 2, 3, 7));
		for (const SmvRegion::FXY& Q : S)
		{
			double La, Lo;
			SmvRegion::EnuToLatLon(Info.Lat, Info.Lon, Q.X, Q.Y, La, Lo);
			const double H = Dem.HeightMsl(La, Lo);
			double A[3], B[3];
			SmvRegion::GeodeticToEcef(Lo, La, H - 150.0, A);
			SmvRegion::GeodeticToEcef(Lo, La, H + 250.0, B);
			SA.Append(A, 3); SB.Append(B, 3);
			Best.Add(1e12);
		}
		GT0 = FPlatformTime::Seconds();
		TWeakPtr<FSmvRegionJob> Wk = AsShared();
		const FString Ep = FString::Printf(TEXT("https://api.cesium.com/v1/assets/%lld/endpoint?access_token=%s"), GGoogleIonAsset, *Token);
		HttpCall(Ep, TEXT("GET"), FString(), {}, 20.f, [Wk](int32 Code, const TArray<uint8>& Data)
		{
			TSharedPtr<FSmvRegionJob> J = Wk.Pin();
			if (!J.IsValid() || J->Cancelled()) return;
			TSharedPtr<FJsonObject> O = SmvJson::Parse(Utf8(Data));
			FString Url = SmvJson::Str(SmvJson::Obj(O, TEXT("options")), TEXT("url"));
			if (Url.IsEmpty()) Url = SmvJson::Str(O, TEXT("url"));
			const FString At = SmvJson::Str(O, TEXT("accessToken"));
			if (!At.IsEmpty()) J->GHdr.Add(TEXT("Authorization"), TEXT("Bearer ") + At);
			if (Code != 200 || Url.IsEmpty())
			{
				J->EndGoogle(FString::Printf(TEXT("ion uç noktası HTTP %d"), Code));
				return;
			}
			TArray<int32> All;
			for (int32 i = 0; i < J->Best.Num(); ++i) All.Add(i);
			J->GQueue.Add({ Url, All });
			J->PumpGoogle();
		});
	}

	void PumpGoogle()
	{
		if (Cancelled() || bGDone) return;
		if (FPlatformTime::Seconds() - GT0 > 150.0) bGLimit = true;
		while (!bGLimit && GInFlight < 6 && GQueue.Num())
		{
			if (GReq >= GMaxReq) { bGLimit = true; break; }
			FGq Q = GQueue.Pop(EAllowShrinking::No);
			// bu arada yeterince ince karo bulunmuş örnekler düşer
			Q.S.RemoveAll([this](int32 s) { return Best[s] <= GTargetM; });
			if (Q.S.Num() == 0) continue;
			++GInFlight; ++GReq;
			TWeakPtr<FSmvRegionJob> Wk = AsShared();
			const FString Url = Q.Url;
			const TArray<int32> Smp = Q.S;
			HttpCall(Url, TEXT("GET"), FString(), GHdr, 25.f, [Wk, Url, Smp](int32 Code, const TArray<uint8>& Data)
			{
				TSharedPtr<FSmvRegionJob> J = Wk.Pin();
				if (!J.IsValid() || J->Cancelled()) return;
				--J->GInFlight;
				TSharedPtr<FJsonObject> O = Code == 200 ? SmvJson::Parse(Utf8(Data)) : nullptr;
				const TSharedPtr<FJsonObject> Root = SmvJson::Obj(O, TEXT("root"));
				if (Root.IsValid()) J->VisitTile(Root, Url, Smp, 0);
				else { ++J->GErr; if (J->GErr <= 4) UE_LOG(LogSimumav, Warning, TEXT("Google 3B denetimi: düğüm HTTP %d"), Code); }
				J->UpdateProgress();
				J->PumpGoogle();
			});
		}
		if (GInFlight == 0 && (GQueue.Num() == 0 || bGLimit)) EndGoogle(bGLimit ? TEXT("istek / süre sınırı") : FString());
	}

	/** Sınır hacmi örneklerin düşey doğrularına değiyor mu; Size: karo boyu (m) */
	void HitSamples(const TSharedPtr<FJsonObject>& Bv, const TArray<int32>& In, TArray<int32>& Out, double& Size) const
	{
		Out.Reset();
		Size = 1e9;
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (Bv.IsValid() && Bv->TryGetArrayField(TEXT("box"), A) && A->Num() == 12)
		{
			double B[12];
			for (int32 i = 0; i < 12; ++i) B[i] = (*A)[i]->AsNumber();
			Size = SmvRegion::BoxSize(B);
			for (int32 s : In) if (SmvRegion::SegmentHitsBox(B, &SA[s * 3], &SB[s * 3])) Out.Add(s);
			return;
		}
		if (Bv.IsValid() && Bv->TryGetArrayField(TEXT("region"), A) && A->Num() == 6)
		{
			double R[6];
			for (int32 i = 0; i < 6; ++i) R[i] = (*A)[i]->AsNumber();
			Size = FMath::Max((R[3] - R[1]) * 6378137.0, (R[2] - R[0]) * 6378137.0 * FMath::Cos((R[1] + R[3]) * 0.5));
			for (int32 s : In)
			{
				double Lo, La;
				// örneğin enlem/boylamı: ECEF'ten geri (küre yaklaşımı yeterli)
				const double* P = &SA[s * 3];
				Lo = FMath::Atan2(P[1], P[0]); La = FMath::Atan2(P[2], FMath::Sqrt(P[0] * P[0] + P[1] * P[1]));
				if (SmvRegion::PointInRegion(R, Lo, La, -1000.0, 9000.0)) Out.Add(s);
			}
			return;
		}
		if (Bv.IsValid() && Bv->TryGetArrayField(TEXT("sphere"), A) && A->Num() == 4)
		{
			double Sp[4];
			for (int32 i = 0; i < 4; ++i) Sp[i] = (*A)[i]->AsNumber();
			Size = Sp[3] * 2.0;
			for (int32 s : In) if (SmvRegion::SegmentHitsSphere(Sp, &SA[s * 3], &SB[s * 3])) Out.Add(s);
			return;
		}
		Out = In;   // hacimsiz düğüm: üstünkü geçerli
	}

	void VisitTile(const TSharedPtr<FJsonObject>& T, const FString& Base, const TArray<int32>& In, int32 Depth)
	{
		if (Depth > 40) return;
		TArray<int32> Hit;
		double Size = 1e9;
		HitSamples(SmvJson::Obj(T, TEXT("boundingVolume")), In, Hit, Size);
		if (Hit.Num() == 0) return;
		const TSharedPtr<FJsonObject> C = SmvJson::Obj(T, TEXT("content"));
		FString Uri = SmvJson::Str(C, TEXT("uri"));
		if (Uri.IsEmpty()) Uri = SmvJson::Str(C, TEXT("url"));
		if (!Uri.IsEmpty())
		{
			FString Path = Uri;
			int32 Q;
			if (Path.FindChar(TEXT('?'), Q)) Path = Path.Left(Q);
			if (Path.EndsWith(TEXT(".json")))
			{
				GQueue.Add({ ResolveUrl(Base, Uri), Hit });
				return;
			}
			for (int32 s : Hit) Best[s] = FMath::Min(Best[s], Size);
		}
		TArray<int32> Active;
		for (int32 s : Hit) if (Best[s] > GTargetM) Active.Add(s);
		if (Active.Num() == 0) return;
		const TArray<TSharedPtr<FJsonValue>>* Ch = nullptr;
		if (T->TryGetArrayField(TEXT("children"), Ch))
			for (const TSharedPtr<FJsonValue>& V : *Ch) VisitTile(V->AsObject(), Base, Active, Depth + 1);
	}

	void EndGoogle(const FString& Note)
	{
		if (bGDone) return;
		bGDone = true;
		int32 NFull = 0, NAny = 0;
		int32 Bk[7] = { 0 };
		TArray<double> Got;
		for (double B : Best)
		{
			if (B < 1e11) { ++NAny; Got.Add(B); }
			if (B <= GFullM) ++NFull;
			Bk[B <= 25 ? 0 : B <= 50 ? 1 : B <= 100 ? 2 : B <= 200 ? 3 : B <= 400 ? 4 : B < 1e11 ? 5 : 6]++;
		}
		Got.Sort();
		const int32 N = FMath::Max(1, Best.Num());
		Info.bGoogleChecked = GReq > 0 && Best.Num() > 0 && GErr < GReq;
		Info.GoogleAt = FDateTime::UtcNow();
		Info.GoogleFull = (float)NFull / N;
		Info.GoogleAny = (float)NAny / N;
		Info.GoogleMedianM = Got.Num() ? (float)Got[Got.Num() / 2] : 0.f;
		Info.GoogleSamples = Best.Num();
		Info.GoogleRequests = GReq;
		Info.GoogleNote = Note;
		SMV_KLOG(TEXT("Bölge %s Google 3B denetimi: %d örnek, tam 3B (≤ %.0f m) %%%.0f, kapsama %%%.0f, ortanca karo %.0f m, %d istek (%d hata), %.1f sn %s · dağılım ≤25:%d ≤50:%d ≤100:%d ≤200:%d ≤400:%d büyük:%d yok:%d"),
			*Info.Id, Best.Num(), GFullM, Info.GoogleFull * 100.f, Info.GoogleAny * 100.f, Info.GoogleMedianM, GReq, GErr, FPlatformTime::Seconds() - GT0, *Note,
			Bk[0], Bk[1], Bk[2], Bk[3], Bk[4], Bk[5], Bk[6]);
		Phase = 2;
		SetStatus(TEXT("region.st_osm"));
		UpdateProgress();
		PumpOsm();
	}

	// ---------------- OpenStreetMap (Overpass; karo başına bbox) ----------------
	void PumpOsm()
	{
		if (Cancelled()) return;
		while (OsmInFlight < 2 && OsmQ.Num())
		{
			const FOsmT T = OsmQ[0];
			OsmQ.RemoveAt(0);
			const FString File = Info.Dir() / FString::Printf(TEXT("osm/%d_%d.json"), T.X, T.Y);
			if (T.Tries == 0 && IFileManager::Get().FileSize(*File) > 8)
			{
				// önceki yarım işten: kimlikler ve sayılar okunur (tekilleştirme sürer)
				FString Txt;
				FFileHelper::LoadFileToString(Txt, *File);
				TSharedPtr<FJsonObject> O = SmvJson::Parse(Txt);
				const TArray<TSharedPtr<FJsonValue>>* Ids = nullptr;
				if (O.IsValid() && O->TryGetArrayField(TEXT("ids"), Ids))
				{
					for (const TSharedPtr<FJsonValue>& V : *Ids) Seen.Add((int64)V->AsNumber());
					auto Cnt = [&O](const TCHAR* K) { const TArray<TSharedPtr<FJsonValue>>* A = nullptr; return O->TryGetArrayField(K, A) ? A->Num() : 0; };
					NB += Cnt(TEXT("b")); NR += Cnt(TEXT("r")); NA += Cnt(TEXT("a")); NT += Cnt(TEXT("t")) / 2;
					++OsmDone;
					continue;
				}
			}
			++OsmInFlight;
			double S, W, N, E;
			SmvRegion::TileBounds(GDemZ, T.X, T.Y, S, W, N, E);
			const FString Q = FString::Printf(TEXT("[out:json][timeout:150][bbox:%.6f,%.6f,%.6f,%.6f];(way[\"building\"];way[\"highway\"];")
				TEXT("way[\"natural\"~\"^(wood|water|scrub|grassland|heath|beach|sand)$\"];way[\"landuse\"~\"^(forest|grass|meadow|recreation_ground|village_green|cemetery|farmland|orchard|vineyard|allotments|basin|reservoir)$\"];")
				TEXT("way[\"leisure\"~\"^(park|garden|pitch|golf_course)$\"];way[\"waterway\"=\"riverbank\"];node[\"natural\"=\"tree\"];);out geom qt;"), S, W, N, E);
			const FString Body = TEXT("data=") + FGenericPlatformHttp::UrlEncode(Q);
			const FString Url = GOverpass[T.Tries % UE_ARRAY_COUNT(GOverpass)];
			TWeakPtr<FSmvRegionJob> Wk = AsShared();
			const double Lat0 = Info.Lat, Lon0 = Info.Lon;
			HttpCall(Url, TEXT("POST"), Body, { { TEXT("Content-Type"), TEXT("application/x-www-form-urlencoded") } }, 200.f,
				[Wk, T, File, Lat0, Lon0](int32 Code, const TArray<uint8>& Data)
			{
				TSharedPtr<FSmvRegionJob> J = Wk.Pin();
				if (!J.IsValid() || J->Cancelled()) return;
				if (Code != 200 || Data.Num() < 10)
				{
					--J->OsmInFlight;
					UE_LOG(LogSimumav, Warning, TEXT("Bölge OSM karosu %d/%d: HTTP %d (deneme %d)"), T.X, T.Y, Code, T.Tries + 1);
					if (T.Tries < 4)
					{
						// yoğunluk: bekle + yansıyla yeniden dene
						TWeakPtr<FSmvRegionJob> Wk2 = J;
						After(4.f + 6.f * T.Tries, [Wk2, T]() { if (TSharedPtr<FSmvRegionJob> J2 = Wk2.Pin()) { J2->OsmQ.Add({ T.X, T.Y, T.Tries + 1 }); J2->PumpOsm(); } });
					}
					else { ++J->OsmFail; ++J->OsmDone; J->UpdateProgress(); J->PumpOsm(); }
					return;
				}
				// ayrıştırma işçi iş parçacığında (Taksim'de karo başına ~4 MB JSON)
				TSharedPtr<TArray<uint8>, ESPMode::ThreadSafe> Raw = MakeShared<TArray<uint8>, ESPMode::ThreadSafe>(Data);
				Async(EAsyncExecution::ThreadPool, [Wk, T, File, Lat0, Lon0, Raw]()
				{
					TSharedPtr<TArray<FOsmItem>, ESPMode::ThreadSafe> Items = MakeShared<TArray<FOsmItem>, ESPMode::ThreadSafe>();
					bool bOk = false;
					ParseOverpass(Utf8(*Raw), Lat0, Lon0, *Items, bOk);
					AsyncTask(ENamedThreads::GameThread, [Wk, T, File, Items, bOk]()
					{
						TSharedPtr<FSmvRegionJob> J = Wk.Pin();
						if (!J.IsValid() || J->Cancelled()) return;
						--J->OsmInFlight;
						if (!bOk)
						{
							UE_LOG(LogSimumav, Warning, TEXT("Bölge OSM karosu %d/%d: yanıt çözülemedi (deneme %d)"), T.X, T.Y, T.Tries + 1);
							if (T.Tries < 4) J->OsmQ.Add({ T.X, T.Y, T.Tries + 1 });
							else { ++J->OsmFail; ++J->OsmDone; }
							J->PumpOsm();
							return;
						}
						J->WriteOsm(File, *Items);
						++J->OsmDone;
						J->UpdateProgress();
						J->SetStatus(TEXT("region.st_osm"), FString::Printf(TEXT("%d / %d · %d"), J->OsmDone, J->OsmTotal, J->NB));
						J->PumpOsm();
					});
				});
			});
		}
		UpdateProgress();
		if (OsmInFlight == 0 && OsmQ.Num() == 0 && Phase == 2) Complete();
	}

	void WriteOsm(const FString& File, const TArray<FOsmItem>& Items)
	{
		FString B, R, A, T, Ids;
		for (const FOsmItem& It : Items)
		{
			if (Seen.Contains(It.Id)) continue;   // komşu karonun sorgusunda da gelen yol / bina
			Seen.Add(It.Id);
			Ids += FString::Printf(TEXT("%s%lld"), Ids.IsEmpty() ? TEXT("") : TEXT(","), It.Id);
			if (It.Kind == 1) { B += FString::Printf(TEXT("%s[%.1f,%.1f"), B.IsEmpty() ? TEXT("") : TEXT(","), It.H, It.MinH); AppendPts(B, It.P); B += TEXT("]"); ++NB; }
			else if (It.Kind == 2) { R += FString::Printf(TEXT("%s[%.1f,%d"), R.IsEmpty() ? TEXT("") : TEXT(","), It.W, It.Sub); AppendPts(R, It.P); R += TEXT("]"); ++NR; }
			else if (It.Kind == 3) { A += FString::Printf(TEXT("%s[%d"), A.IsEmpty() ? TEXT("") : TEXT(","), It.Sub); AppendPts(A, It.P); A += TEXT("]"); ++NA; }
			else if (It.Kind == 4 && It.P.Num()) { T += FString::Printf(TEXT("%s%.1f,%.1f"), T.IsEmpty() ? TEXT("") : TEXT(","), It.P[0].X, It.P[0].Y); ++NT; }
		}
		const FString S = FString::Printf(TEXT("{\"v\":1,\"ids\":[%s],\"b\":[%s],\"r\":[%s],\"a\":[%s],\"t\":[%s]}"), *Ids, *B, *R, *A, *T);
		FFileHelper::SaveStringToFile(S, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	void Complete()
	{
		Phase = 3;
		Info.OsmTiles = OsmTotal - OsmFail;
		Info.OsmFailed = OsmFail;
		Info.Buildings = NB; Info.Roads = NR; Info.Areas = NA; Info.Trees = NT;
		// boyut
		int64 Sz = 0;
		IFileManager::Get().IterateDirectoryRecursively(*Info.Dir(), [&Sz](const TCHAR* P, bool bDir) { if (!bDir) Sz += IFileManager::Get().FileSize(P); return true; });
		Info.Bytes = Sz;
		// OSM karolarının çoğu inmediyse paket eksik sayılır (arazi tek başına uçulabilir ama bina yok → yeniden dene)
		Info.bPack = Info.DemTiles > 0 && OsmFail <= FMath::Max(0, OsmTotal / 10);
		Info.PackAt = FDateTime::UtcNow();
		SMV_KLOG(TEXT("Bölge %s tamam: %s — %d bina, %d yol, %d alan, %d ağaç; OSM %d/%d karo (%d hata), %.1f MB"), *Info.Id, Info.bPack ? TEXT("PAKET HAZIR") : TEXT("EKSİK"),
			NB, NR, NA, NT, OsmTotal - OsmFail, OsmTotal, OsmFail, Sz / 1048576.0);
		FSmvRegions::Get().Finish(Info.bPack ? ESmvRegionState::Done : ESmvRegionState::Failed, Info, Info.bPack ? FString() : FSmvI18n::T(TEXT("region.err_osm")).ToString());
	}
};

// ===========================================================================
// FSmvRegions
// ===========================================================================

FSmvRegions& FSmvRegions::Get()
{
	static FSmvRegions R;
	return R;
}

FString FSmvRegions::RootDir() { return FPaths::ProjectSavedDir() / TEXT("Simumav") / TEXT("Regions"); }

FString FSmvRegions::IdFor(double Lat, double Lon, double RadiusM)
{
	return FString::Printf(TEXT("%.4f_%.4f_%d"), Lat, Lon, FMath::RoundToInt(RadiusM));
}

void FSmvRegions::EnuToLatLon(const FSmvRegionInfo& R, double E, double N, double& Lat, double& Lon) { SmvRegion::EnuToLatLon(R.Lat, R.Lon, E, N, Lat, Lon); }

const TArray<FSmvRegionInfo>& FSmvRegions::List()
{
	if (bListed) return Items;
	bListed = true;
	Items.Reset();
	TArray<FString> Dirs;
	IFileManager::Get().FindFiles(Dirs, *(RootDir() / TEXT("*")), false, true);
	for (const FString& D : Dirs)
	{
		FString Txt;
		if (!FFileHelper::LoadFileToString(Txt, *(RootDir() / D / TEXT("region.json")))) continue;
		FSmvRegionInfo R = FSmvRegionInfo::FromJson(SmvJson::Parse(Txt));
		if (R.Id == D) Items.Add(R);
	}
	return Items;
}

const FSmvRegionInfo* FSmvRegions::FindPack(double Lat, double Lon, double RadiusM)
{
	const FSmvRegionInfo* Best = nullptr;
	for (const FSmvRegionInfo& R : List())
		if (R.bPack && R.Covers(Lat, Lon, RadiusM) && (!Best || R.PackAt > Best->PackAt)) Best = &R;
	return Best;
}

const FSmvRegionInfo* FSmvRegions::FindAny(double Lat, double Lon, double RadiusM)
{
	if (const FSmvRegionInfo* P = FindPack(Lat, Lon, RadiusM)) return P;
	for (const FSmvRegionInfo& R : List()) if (R.Covers(Lat, Lon, RadiusM)) return &R;
	return nullptr;
}

bool FSmvRegions::Delete(const FString& Id)
{
	if (Id.IsEmpty() || Id.Contains(TEXT("..")) || Id.Contains(TEXT("/")) || Id.Contains(TEXT("\\"))) return false;
	if (Job.IsValid() && Job->Info.Id == Id) Cancel();
	const bool bOk = IFileManager::Get().DeleteDirectory(*(RootDir() / Id), false, true);
	bListed = false;
	return bOk;
}

void FSmvRegions::Start(double Lat, double Lon, double FlightRadiusM, const FString& IonToken, bool bGoogleCheck)
{
	Cancel();
	TSharedPtr<FSmvRegionJob> J = MakeShared<FSmvRegionJob>();
	J->Info.Lat = Lat; J->Info.Lon = Lon;
	J->Info.FlightRadiusM = FMath::Clamp(FlightRadiusM, 100.0, 10000.0);
	J->Info.RadiusM = J->Info.FlightRadiusM + MarginM;
	J->Info.Id = IdFor(Lat, Lon, J->Info.FlightRadiusM);
	J->Token = IonToken;
	J->bGoogle = bGoogleCheck;
	Job = J;
	State = ESmvRegionState::Running;
	Progress = 0.f;
	Status = FSmvI18n::T(TEXT("region.st_dem")).ToString();
	J->Begin();
}

void FSmvRegions::Cancel()
{
	if (Job.IsValid())
	{
		Job->bCancel = true;
		if (State == ESmvRegionState::Running) { State = ESmvRegionState::Cancelled; Status = FSmvI18n::T(TEXT("region.st_cancel")).ToString(); }
	}
	Job.Reset();
}

bool FSmvRegions::IsRunningFor(double Lat, double Lon, double RadiusM) const
{
	return State == ESmvRegionState::Running && Job.IsValid() && Job->Info.Id == IdFor(Lat, Lon, FMath::Clamp(RadiusM, 100.0, 10000.0));
}

void FSmvRegions::SaveInfo(const FSmvRegionInfo& Info)
{
	FFileHelper::SaveStringToFile(SmvJson::ToString(Info.ToJson(), true), *(Info.Dir() / TEXT("region.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	bListed = false;
}

void FSmvRegions::Finish(ESmvRegionState S, const FSmvRegionInfo& Info, const FString& Msg)
{
	// yarım paket de kaydedilir (bPack false): Google denetimi ve devam için
	SaveInfo(Info);
	Last = Info;
	State = S;
	Progress = S == ESmvRegionState::Done ? 1.f : Progress;
	Status = S == ESmvRegionState::Done ? FSmvI18n::T(TEXT("region.st_done")).ToString() : Msg;
	Job.Reset();
}

// ===========================================================================
// FSmvRegionDem
// ===========================================================================

bool FSmvRegionDem::Load(const FSmvRegionInfo& InR)
{
	R = InR;
	Tiles14.Reset(); Tiles10.Reset();
	IImageWrapperModule& Iw = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	for (int32 Z : { GDemZ, GFarZ })
	{
		TArray<FString> Files;
		const FString Dir = R.Dir() / FString::Printf(TEXT("dem%d"), Z);
		IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.png")), true, false);
		for (const FString& F : Files)
		{
			FString Xs, Ys;
			if (!FPaths::GetBaseFilename(F).Split(TEXT("_"), &Xs, &Ys)) continue;
			TArray<uint8> Bytes;
			if (!FFileHelper::LoadFileToArray(Bytes, *(Dir / F))) continue;
			TSharedPtr<IImageWrapper> W = Iw.CreateImageWrapper(EImageFormat::PNG);
			TArray64<uint8> Raw;
			if (!W.IsValid() || !W->SetCompressed(Bytes.GetData(), Bytes.Num()) || W->GetWidth() != 256 || W->GetHeight() != 256 || !W->GetRaw(ERGBFormat::RGBA, 8, Raw)) continue;
			FTile T;
			T.H.SetNumUninitialized(256 * 256);
			for (int32 i = 0; i < 256 * 256; ++i) T.H[i] = (float)SmvRegion::TerrariumHeight(Raw[i * 4], Raw[i * 4 + 1], Raw[i * 4 + 2]);
			(Z == GDemZ ? Tiles14 : Tiles10).Add(TileKey(FCString::Atoi(*Xs), FCString::Atoi(*Ys)), MoveTemp(T));
		}
	}
	UE_LOG(LogSimumav, Log, TEXT("Bölge %s arazi: z14 %d karo, z10 %d karo"), *R.Id, Tiles14.Num(), Tiles10.Num());
	return Tiles14.Num() > 0;
}

double FSmvRegionDem::Sample(const TMap<uint64, FTile>& T, int32 Z, double Lat, double Lon, bool& bOk) const
{
	double Px, Py;
	SmvRegion::LonLatToPixel(Lon, Lat, Z, Px, Py);
	const double Fx = Px - 0.5, Fy = Py - 0.5;   // piksel merkezleri
	const int64 X0 = (int64)FMath::FloorToDouble(Fx), Y0 = (int64)FMath::FloorToDouble(Fy);
	const double Tx = Fx - X0, Ty = Fy - Y0;
	double V[4];
	for (int32 k = 0; k < 4; ++k)
	{
		const int64 Gx = X0 + (k & 1), Gy = Y0 + (k >> 1);
		const FTile* Tl = T.Find(TileKey((int32)(Gx >> 8), (int32)(Gy >> 8)));
		if (!Tl) { bOk = false; return 0.0; }
		V[k] = Tl->H[(int32)(Gy & 255) * 256 + (int32)(Gx & 255)];
	}
	bOk = true;
	return FMath::Lerp(FMath::Lerp(V[0], V[1], Tx), FMath::Lerp(V[2], V[3], Tx), Ty);
}

double FSmvRegionDem::HeightMsl(double Lat, double Lon) const
{
	bool bOk = false;
	double H = Sample(Tiles14, GDemZ, Lat, Lon, bOk);
	if (!bOk) H = Sample(Tiles10, GFarZ, Lat, Lon, bOk);
	if (!bOk) return 0.0;
	return H <= 0.0 ? -3.0 : H;   // deniz: taban görünmez, deniz düzlemi (0 m) üstte
}

double FSmvRegionDem::HeightEnu(double E, double N) const
{
	double La, Lo;
	SmvRegion::EnuToLatLon(R.Lat, R.Lon, E, N, La, Lo);
	return HeightMsl(La, Lo);
}

double FSmvRegionDem::RawEnu(double E, double N) const
{
	double La, Lo;
	SmvRegion::EnuToLatLon(R.Lat, R.Lon, E, N, La, Lo);
	bool bOk = false;
	double H = Sample(Tiles14, GDemZ, La, Lo, bOk);
	if (!bOk) H = Sample(Tiles10, GFarZ, La, Lo, bOk);
	return bOk ? H : 0.0;
}
