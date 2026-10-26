// Gerçek dünya bölge haritası (Parkur › Gerçek Dünya): koordinat + azami uçuş mesafesi (daire yarıçapı) için
//   1) ÇEVRİMDIŞI BÖLGE PAKETİ — çevrimdışı kullanıma izin veren açık verilerle indirilir ve diskte tutulur:
//        arazi: AWS Terrain Tiles (Terrarium PNG; z14 ≈ 9 m/px uçuş alanı, z10 ufuk halkası) — SRTM / Copernicus / EU-DEM … (atıf)
//        binalar, yollar, su, orman, park: OpenStreetMap (Overpass API; © OpenStreetMap katkıcıları, ODbL)
//      Saved/Simumav/Regions/<kimlik>/ : region.json, dem14/<x>_<y>.png, dem10/<x>_<y>.png, osm/<x>_<y>.json (yerel ENU metre)
//   2) GOOGLE FOTOGERÇEKÇİ 3B KAPSAMA DENETİMİ — Google karoları İNDİRİLMEZ / SAKLANMAZ (Map Tiles API politikası: önceden getirme,
//      önbellekleme ve çevrimdışı kullanım yasak). Yalnız karo ağacının JSON düğümleri okunur: daire içindeki örnek noktaların
//      üstündeki en ince karo boyu → "tam 3B" / "2,5B" / "yok" oranları. Sonuç region.json'da (30 gün geçerli).
// Giriş kapısı: gerçek dünya parkuruna yalnız paketi inmiş bölgede girilir (USmvFlow::ValidateDraft). Google denetimi geçtiyse ve
// Ayarlar › Görüntü › Gerçek dünya = Google ise uçuşta Google 3B akışı, yoksa çevrimdışı paket (ASmvRegionWorld) kullanılır.
#pragma once

#include "CoreMinimal.h"

struct SIMUMAV_API FSmvRegionInfo
{
	FString Id;
	double Lat = 0, Lon = 0;
	/** Paketin kapsadığı yarıçap (m; parkur yarıçapı + kenar payı) */
	double RadiusM = 0;
	/** Parkurun istediği yarıçap (m) */
	double FlightRadiusM = 0;
	// ---- çevrimdışı paket ----
	bool bPack = false;
	FDateTime PackAt;
	int64 Bytes = 0;
	int32 DemTiles = 0, Buildings = 0, Roads = 0, Areas = 0, Trees = 0, OsmTiles = 0, OsmFailed = 0;
	double CenterMsl = 0, MinMsl = 0, MaxMsl = 0;
	// ---- Google 3B kapsama denetimi ----
	bool bGoogleChecked = false;
	FDateTime GoogleAt;
	/** Örneklerin tam 3B (ince karo) oranı ve herhangi bir Google karosu olan oranı (0..1) */
	float GoogleFull = 0.f, GoogleAny = 0.f;
	/** Örneklerin ortanca en ince karo boyu (m) */
	float GoogleMedianM = 0.f;
	int32 GoogleSamples = 0, GoogleRequests = 0;
	FString GoogleNote;

	/** Paket (Lat, Lon, R) dairesini örtüyor mu */
	bool Covers(double InLat, double InLon, double InRadiusM) const;
	/** Google denetimi geçerli mi (≤ 30 gün, kapsama ≥ %90) */
	bool GoogleOk() const;
	FString Dir() const;
	TSharedRef<class FJsonObject> ToJson() const;
	static FSmvRegionInfo FromJson(const TSharedPtr<class FJsonObject>& O);
};

enum class ESmvRegionState : uint8 { Idle, Running, Done, Failed, Cancelled };

class SIMUMAV_API FSmvRegions
{
public:
	static FSmvRegions& Get();
	/** Kenar payı (m): arazi ağının parkur dairesinin dışında kalan kısmı */
	static constexpr double MarginM = 600.0;
	static FString RootDir();
	/** Google denetimi için Cesium ion belirteci: Ayarlar, yoksa pakete gömülü */
	static FString EffectiveToken(const FString& SettingsToken);
	static FString IdFor(double Lat, double Lon, double RadiusM);

	/** Diskteki bölgeler (ilk çağrıda okunur) */
	const TArray<FSmvRegionInfo>& List();
	/** Daireyi örten paketli bölge (en yenisi) */
	const FSmvRegionInfo* FindPack(double Lat, double Lon, double RadiusM);
	/** Daire için kayıt (paket olmasa da Google denetimi olabilir) */
	const FSmvRegionInfo* FindAny(double Lat, double Lon, double RadiusM);
	bool Delete(const FString& Id);

	/** İndirme + denetim başlat (çalışan iş varsa iptal edilir). IonToken boşsa Google denetimi atlanır. */
	void Start(double Lat, double Lon, double FlightRadiusM, const FString& IonToken, bool bGoogleCheck = true);
	void Cancel();
	ESmvRegionState GetState() const { return State; }
	bool IsRunningFor(double Lat, double Lon, double RadiusM) const;
	/** 0..1 */
	float GetProgress() const { return Progress; }
	/** Kullanıcıya gösterilecek aşama metni */
	FString GetStatus() const { return Status; }
	/** Son biten işin sonucu */
	const FSmvRegionInfo& LastResult() const { return Last; }

	/** ENU (m) → enlem/boylam (paket merkezi) */
	static void EnuToLatLon(const FSmvRegionInfo& R, double E, double N, double& Lat, double& Lon);

private:
	friend class FSmvRegionJob;
	void Finish(ESmvRegionState S, const FSmvRegionInfo& Info, const FString& Msg);
	void SaveInfo(const FSmvRegionInfo& Info);
	TArray<FSmvRegionInfo> Items;
	bool bListed = false;
	TSharedPtr<class FSmvRegionJob> Job;
	ESmvRegionState State = ESmvRegionState::Idle;
	float Progress = 0.f;
	FString Status;
	FSmvRegionInfo Last;
};

/** Paket yükseklik örnekleyicisi (Terrarium PNG'ler çözülmüş, bellek içi). ASmvRegionWorld ve kalkış zemini kullanır. */
class SIMUMAV_API FSmvRegionDem
{
public:
	bool Load(const FSmvRegionInfo& R);
	/** MSL (m); paket dışında ufuk (z10) verisi, o da yoksa 0. Deniz tabanı (batimetri) 0'a kırpılır (deniz düzlemi ayrı). */
	double HeightMsl(double Lat, double Lon) const;
	double HeightEnu(double E, double N) const;
	/** Ham değer (batimetri dahil) — deniz maskesi için */
	double RawEnu(double E, double N) const;
	bool IsValid() const { return Tiles14.Num() > 0; }
	const FSmvRegionInfo& Info() const { return R; }

private:
	struct FTile { TArray<float> H; };
	double Sample(const TMap<uint64, FTile>& T, int32 Z, double Lat, double Lon, bool& bOk) const;
	TMap<uint64, FTile> Tiles14, Tiles10;
	FSmvRegionInfo R;
};
