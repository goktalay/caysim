// Fab içerikleri (kullanıcının satın aldığı lisanslı paketler) — depoda YOK; derleme anında runner diskinden projeye alınır
// (Tools/fab/fab-hazirla.ps1, docs/fab-icerik.md). Bu sınıf yalnız çalışma anında ne bulunduğunu söyler; hiçbiri yoksa
// oyun eskisi gibi BasicShapes gövdeleri ve prosedürel dünya ile çalışır.
//   - Dron gövdesi: /Game/Fab/Drone altındaki ilk SkeletalMesh (Scripts/Python/fab_import.py içe aktarır)
//   - Haritalar: DefaultGame.ini [/Script/Simumav.SmvFabContent] +Maps=<anahtar>|/Game/<paket>/.../<Harita>  (CI ekler)
//     Parkur dünya kipi "fab:<anahtar>" — sihirbazda sanal parkur olarak listelenir, sunucuya kaydedilmez.
#pragma once

#include "CoreMinimal.h"
#include "SimumavTypes.h"

class USkeletalMesh;

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
	/** Ayar (DefaultGame.ini [/Script/Simumav.SmvFabContent]); yoksa Default. */
	static float GetFloat(const TCHAR* Key, float Default);
};
