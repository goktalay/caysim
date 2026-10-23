// Fab içerikleri (kullanıcının satın aldığı lisanslı paketler) — depoda YOK; derleme anında runner diskinden projeye alınır
// (Tools/fab/fab-hazirla.ps1, docs/fab-icerik.md). Bu sınıf yalnız çalışma anında ne bulunduğunu söyler; hiçbiri yoksa
// oyun eskisi gibi BasicShapes gövdeleri ve prosedürel dünya ile çalışır.
//   - Dron gövdesi: /Game/Fab/Drone (quad) ve /Game/Fab/DroneFpv (FPV/önleyici) altındaki ilk ağ (Scripts/Python/fab_import.py)
//   - Sahne dekoru: /Game/Fab/Props/<ürün> statik ağları (yalnız görsel; davranış yok)
//   - Gök: /Game/UltraDynamicSky (Ultra Dynamic Sky paketi; düz dünyada yerleşik gök yerine)
//   - Haritalar: DefaultGame.ini [/Script/Simumav.SmvFabContent] +Maps=<anahtar>|/Game/<paket>/.../<Harita>  (CI ekler)
//     Parkur dünya kipi "fab:<anahtar>" — sihirbazda sanal parkur olarak listelenir, sunucuya kaydedilmez.
#pragma once

#include "CoreMinimal.h"
#include "SimumavTypes.h"

class USkeletalMesh;
class UStaticMesh;
class UWorld;
class AActor;

struct SIMUMAV_API FSmvFabMap
{
	FString Key;
	/** Uzun paket adı (/Game/.../Harita) */
	FString Level;
	/** Görünen ad (dil dosyası "fabmap.<anahtar>"; yoksa anahtar) — Maps() her çağrıda geçerli dile göre tazeler */
	FText Name;
};

struct SIMUMAV_API FSmvFabContent
{
	/** Pakette gerçekten bulunan Fab haritaları (ilk çağrıda yapılandırmadan okunur). */
	static const TArray<FSmvFabMap>& Maps();
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
	/** Ayar (DefaultGame.ini [/Script/Simumav.SmvFabContent]); yoksa Default. */
	static float GetFloat(const TCHAR* Key, float Default);
};
