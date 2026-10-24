// Fab içerikleri (kullanıcının satın aldığı lisanslı paketler) — depoda YOK; derleme anında runner diskinden projeye alınır
// (Tools/fab/fab-hazirla.ps1, docs/fab-icerik.md). Bu sınıf yalnız çalışma anında ne bulunduğunu söyler; hiçbiri yoksa
// oyun eskisi gibi BasicShapes gövdeleri ve prosedürel dünya ile çalışır.
//   - Dron gövdesi: /Game/Fab/Drone (quad) ve /Game/Fab/DroneFpv (FPV/önleyici) altındaki ilk ağ (Scripts/Python/fab_import.py)
//   - Sahne dekoru: /Game/Fab/Props/<ürün> statik ağları (yalnız görsel; davranış yok)
//   - Gök: /Game/UltraDynamicSky (Ultra Dynamic Sky paketi; düz dünyada yerleşik gök yerine)
//   - Uçak/VTOL motor sesi: /Game/Fab/Sounds/SC_Engine_<Prop|Pusher|Jet> (Aircraft Engine Sounds'tan CI kopyası)
//   - Haritalar: KAYIT TABLOSU Private/SmvMapRegistry.cpp (ad, açıklama, önizleme, harita yolu, başlangıç); ek olarak
//     DefaultGame.ini [/Script/Simumav.SmvFabContent] +Maps=<anahtar>|/Game/<paket>/.../<Harita> satırları.
//     Parkur dünya kipi "fab:<anahtar>" — sihirbazda sanal parkur olarak listelenir, sunucuya kaydedilmez.
#pragma once

#include "CoreMinimal.h"
#include "SimumavTypes.h"
#include "SmvMapRegistry.h"

class USkeletalMesh;
class UStaticMesh;
class UWorld;
class AActor;
class USoundBase;

struct SIMUMAV_API FSmvFabMap
{
	FString Key;
	/** Uzun paket adı (/Game/.../Harita); kodla kurulan şehirde boş */
	FString Level;
	/** Görünen ad ve açıklama (kayıt tablosundaki dil anahtarları; yoksa anahtar) — Maps()/AllMaps() her çağrıda geçerli dile göre tazeler */
	FText Name;
	FText Description;
	/** Önizleme görseli: Content/ altına göre yol (ör. Simumav/MapPreviews/orman.png) ve okunacak dosya yolu
	 *  (FPaths::ProjectContentDir() ile; pakette UFS/pak içinde — FFileHelper::LoadFileToArray ile okunur; yoksa boş) */
	FString Preview;
	FString PreviewFile;
	ESmvMapKind Kind = ESmvMapKind::Level;
	/** Başlangıç noktasına ek kaydırma (m, doğu/kuzey) ve kalkış yönü (derece) */
	double StartE = 0, StartN = 0, StartYaw = 0;
	/** Statik sahne dekoru (yalnız görsel) kalkış alanının yanına konur */
	bool bDecor = false;
	/** Gereken Fab paketi (bilgi) */
	FString Package;
	/** Çevre zemini biyomu (kayıt tablosu Surround; boş → yok) */
	FString Surround;
	/** Pakette var mı (yoksa menüde gösterilip seçilemez) */
	bool bAvailable = false;
};

struct SIMUMAV_API FSmvFabContent
{
	/** Pakette gerçekten bulunan uçuş haritaları (kayıt tablosu + DefaultGame.ini +Maps; ilk çağrıda çözülür). */
	static const TArray<FSmvFabMap>& Maps();
	/** Kayıt tablosunun tamamı (pakette olmayanlar bAvailable = false) + ini ek haritaları — menü bunu gösterebilir. */
	static const TArray<FSmvFabMap>& AllMaps();
	/** "fab:<anahtar>" dünya kipine karşılık gelen harita (yoksa nullptr). */
	static const FSmvFabMap* FindMap(const FString& WorldMode);
	static bool IsFabWorld(const FString& WorldMode) { return WorldMode.StartsWith(TEXT("fab:")); }
	/** Harita için sanal parkur (kimlik "fab:<anahtar>"; merkez = SITL ev konumu = harita merkezi). */
	static FSmvTrack MakeTrack(const FSmvFabMap& Map);
	/** Fab dron gövdesi (yoksa nullptr; sonuç önbelleğe alınır). */
	static USkeletalMesh* LoadDroneMesh();
	/** Klasördeki ilk SkeletalMesh, yoksa ilk StaticMesh (/Game/Fab/Drone: quad, /Game/Fab/DroneFpv: FPV önleyici). */
	static UObject* LoadMeshIn(const FString& Dir);
	/** Sahne dekoru statik ağları (/Game/Fab/Props/<ürün>; ör. tank, hava savunma, anti-dron sistemi — yalnız görsel). */
	static const TArray<UStaticMesh*>& Props();
	/** Ultra Dynamic Sky + Ultra Dynamic Weather (Fab) kurar; parkur saat/hava/sis ayarlarını UDS değişkenlerine bağlar.
	 *  Paket yoksa ya da UseUltraDynamicSky=False ise false (yerleşik gök kalır). */
	static bool SpawnSky(UWorld* World, const FSmvTrack& Track, TArray<AActor*>& OutActors);
	/** Uçak/VTOL motor sesi (Fab Aircraft Engine Sounds; Kind: Prop | Pusher | Jet). Yoksa nullptr (prosedürel ses kalır). */
	static USoundBase* LoadEngineSound(const TCHAR* Kind);
	/** Kodla kurulan şehir: bina ağları (European Buildings) ve pakette bulunan City Sample bina seviyeleri. */
	static const TArray<UStaticMesh*>& CityMeshes();
	static TArray<FString> CityTowerLevels();
	/** Ayar (DefaultGame.ini [/Script/Simumav.SmvFabContent]); yoksa Default. */
	static float GetFloat(const TCHAR* Key, float Default);
	/** Fab Blueprint sınıflarının API'sini (işlevler + parametreleri, değişkenler + türleri) günlüğe yazar — editörsüz entegrasyon için
	 *  (duman testi günlüğünde "FABAPI <etiket>" satırları). MaxLines ile sınırlı; aynı sınıf bir kez yazılır. */
	static void DumpClassApi(const UClass* Class, const FString& Tag, int32 MaxLines = 400);
};
