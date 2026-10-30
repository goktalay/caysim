// Arayüz tercihleri — yalnız bu bilgisayarda (GameUserSettings.ini [/Script/Simumav.SmvUiPrefs]; profil JSON'una girmez):
// ses mikseri kanalları (motor / uyarı / arayüz / müzik — ana ses FSmvSettings::Volume), tanıtım bayrakları, kokpit göstergesi,
// menü 3B sahnesi, tuş atamaları (klavye + gamepad; Enhanced Input eylem adlarıyla — ASmvPilotPawn::RemapKeys uygular).
// WASD / ok tuşları çubuk eksenleridir (kumanda modu Mode 1–4 belirler) — atanamaz.
#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

/** Atanabilir eylem: ad (Enhanced Input), varsayılan klavye ve gamepad tuşu, dil anahtarı. */
struct SIMUMAV_API FSmvBindDef
{
	FName Action;
	FKey Kb;
	FKey Pad;
	FString LabelKey;
};

struct SIMUMAV_API FSmvUiPrefs
{
	float VolMotor = 1.f;
	float VolAlerts = 1.f;
	float VolUi = 0.7f;
	float VolMusic = 0.5f;
	bool bMenuMusic = true;
	bool bTutorialDone = false;
	bool bFlightTipsDone = false;
	/** Sabit kanat / VTOL için klasik kokpit gösterge paneli (altılı) */
	bool bCockpit = false;
	/** GPWS sesli uyarı (konuşma; tam kipte) */
	bool bGpwsSpeech = true;
	/** Uçuş video kaydı (MJPEG AVI; uçuş başında başlar) */
	bool bRecordVideo = false;
	/** Gerçek dünya (Cesium): google3d (Google Photorealistic 3D Tiles; varsayılan — 25 sn'de çizilecek karo yoksa arazi + uyduya düşer) |
	 *  terrain (World Terrain + Bing uydu) | offline (yalnız çevrimdışı bölge paketi — SmvRegion; Fab haritalarında gerçek çevre yok) | off.
	 *  Gerçek dünya parkurunda Google yalnız bölge denetimi geçtiyse akıtılır, değilse çevrimdışı paket. */
	FString RealWorld = TEXT("google3d");
	/** Hız efekti (ekran kenarı hız çizgileri): 0 kapalı | 1 hafif (varsayılan) | 2 belirgin */
	int32 SpeedFx = 1;
	/** Uyarı kademesi: off | critical (yalnız eyleme dönük kritik uyarılar; varsayılan) | all */
	FString AlertLevel = TEXT("critical");
	/** Termal paleti: white (beyaz-sıcak) | black (siyah-sıcak) | iron (ironbow) */
	FString ThermalPalette = TEXT("white");
	/** PiP penceresi: genişlik (px, 16:9) ve sağ/alt kenara uzaklık */
	float PipW = 384.f, PipR = 16.f, PipB = 80.f;
	/** Uçuş mini haritası: açık mı, sol / üst kenara uzaklık, genişlik (4:3), görünen yarı genişlik (m; tekerlekle yakınlaştır)
	 *  ve büyük harita kipinde kamera penceresi (genişlik 16:9, sağ / alt kenar) */
	bool bMap = false;
	float MapL = 16.f, MapT = 150.f, MapW = 320.f, MapRangeM = 600.f, MapYaw = 0.f, MapTilt = 58.f;
	float ViewW = 384.f, ViewR = 360.f, ViewB = 96.f;
	/** Dron kartı (web harekât paneli): sağ/üst kenara uzaklık, küçültülmüş mü, katlı bölümler (virgüllü) */
	float PanelR = 12.f, PanelT = 56.f;
	bool bPanelMin = false;
	FString PanelFold = TEXT("ops");
	/** Güç kadranları tasarımı (web gtheme): cockpit | classic | glass | minimal | military | none */
	FString DialTheme = TEXT("cockpit");
	/** Ana menüde canlı 3B sahne (uçan dron); kapalıysa yalnız animasyonlu arka plan */
	bool bMenuScene = true;
	/** Hangar 3B önizleme */
	bool bPreview3d = true;
	/** Hassasiyet (web §27 ⚙): hazır ayar precise | normal | agile | custom; ölü bölge, expo ve oran (roll+pitch / yaw).
	 *  Boş = eski tek expo (FSmvSettings::StickExpo). */
	FString SensPreset;
	/** Eksen başına (0 roll, 1 pitch, 2 gaz, 3 yaw — web rcfeel): ölü bölge 0–0.2, expo 0–0.9, oran 0.3–1 */
	float Dead[4] = { 0.03f, 0.03f, 0.03f, 0.04f };
	float Ex[4] = { 0.2f, 0.2f, 0.1f, 0.2f };
	float Rate[4] = { 1.f, 1.f, 1.f, 0.9f };
	/** Roll ve pitch birlikte ayarlanır */
	bool bLinkRP = true;
	/** Hazır ayarı değerlere uygula */
	void ApplySensPreset(const FString& P);
	/** Eksen şekillendirme (Axis: 0 roll, 1 pitch, 3 yaw) — FallbackExpo: hazır ayar yoksa */
	float Shape(float V, int32 Axis, float FallbackExpo) const;
	/** Bağlıysa pitch = roll */
	void SyncLink();
	/** Son uçuşta ölçülen tüketim (★ Önerilen): dron kimliği → km/L, W (yoksa −1) */
	static void SetMeasured(const FString& DroneId, double KmPerL, double W);
	static bool GetMeasured(const FString& DroneId, double& KmPerL, double& W);
	/** Bayram / mevsim teması: auto | off */
	FString Season = TEXT("auto");
	/** Sanal dünya / parkur uçuşları için kayıtlı konumlar: "ad|enlem|boylam" (uçuş akışı → Konum) */
	TArray<FString> SavedLocs;
	/** Bayram ülkesi (2 harf; boş = otomatik) */
	FString HolidayCountry;
	/** Tuş atamaları: "kb.<Eylem>" / "pad.<Eylem>" → FKey adı */
	TMap<FString, FString> Binds;

	static FSmvUiPrefs& Get();
	void Save();

	/** Atanabilir eylemler (varsayılanlar ASmvPilotPawn ile aynı). */
	static const TArray<FSmvBindDef>& Bindable();
	/** Eylemin geçerli tuşu (bPad: gamepad yuvası). Atama yoksa varsayılan. */
	FKey KeyFor(FName Action, bool bPad) const;
	/** Varsayılan tuşa karşılık gelen atanmış tuş (pawn MapKey: varsayılan → kullanıcı). Atanabilir değilse aynısı. */
	FKey Resolve(FName Action, const FKey& Default) const;
	void SetKey(FName Action, bool bPad, const FKey& Key);
	void ResetKeys() { Binds.Reset(); }
	/** Aynı yuvada bu tuşu kullanan başka eylem (çakışma uyarısı); yoksa NAME_None */
	FName ConflictOf(FName Action, bool bPad, const FKey& Key) const;

private:
	bool bLoaded = false;
	void Load();
};
