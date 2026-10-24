// Uçuş haritaları KAYIT TABLOSU (Fab içerikleri; tablo: Private/SmvMapRegistry.cpp).
// Menü/parkur sihirbazı haritaları buradan listeler (FSmvFabContent::AllMaps / Maps); CI (Tools/fab/fab-hazirla.ps1) aynı
// tablodan hangi haritaların pakete pişirileceğini okur — yeni harita eklemek için yalnız tabloya bir satır eklemek yeter.
// Lisanslı harita dosyaları depoda değildir; pakette bulunmayan satırlar menüde "yok" olarak işaretlenir (bAvailable = false).
#pragma once

#include "CoreMinimal.h"

/** Harita türü: Fab paketindeki hazır seviye ya da kodla yerleştirilen sahne. */
enum class ESmvMapKind : uint8
{
	/** Fab paketinin hazır haritası (Levels adaylarından ilki pakette olan) seviye örneği olarak yüklenir. */
	Level,
	/** Kodla kurulan şehir bloğu (European Buildings ağları + City Sample bina seviyeleri; SmvMapRegistry.cpp şehir tabloları). */
	ProcCity,
};

/** Kayıt tablosu satırı (derleme anında sabit). */
struct SIMUMAV_API FSmvMapDef
{
	/** Kimlik; parkur dünya kipi "fab:<Key>" */
	const TCHAR* Key;
	/** Görünen ad / açıklama dil anahtarları (Content/Simumav/Lang/<dil>.json; Tools/i18n/metinler.py) */
	const TCHAR* NameKey;
	const TCHAR* DescKey;
	/** Önizleme görseli: Content/ altına göre PNG yolu (pakette UFS; tam yol FSmvFabMap::PreviewFile) */
	const TCHAR* Preview;
	/** Harita paketi adayları, ';' ile — ilk bulunan yüklenir (ProcCity'de boş) */
	const TCHAR* Levels;
	ESmvMapKind Kind;
	/** Başlangıç (dronun kalkış noktası): harita kendi PlayerStart'ı / düz açıklık / sınır kutusu merkezine göre bulunur,
	 *  bu değerler o noktaya EK kaydırmadır (m, doğu/kuzey) ve kalkış yönüdür (derece, kuzeyden saat yönü). */
	double StartE;
	double StartN;
	double StartYaw;
	/** Statik sahne dekoru (tank, hava savunma, anti-dron sistemi — yalnız görsel) kalkış alanının yanına konsun mu */
	bool bDecor;
	/** Gereken Fab paketi (Content altı klasör; bilgi/rehber için) */
	const TCHAR* Package;
	/** Çevre zemini biyomu (Desert | Rocky | Grasslands | Snow): harita "havada ada" ise çevresine Hyper malzemeli arazi diski, kaya/çalı ve
	 *  uzak dağlar kurulur (ASmvScenery::BuildSurround). nullptr → yok. */
	const TCHAR* Surround = nullptr;
};

struct SIMUMAV_API FSmvMapRegistry
{
	/** Tablonun tamamı (pakette olsun olmasın). */
	static TArrayView<const FSmvMapDef> All();
	static const FSmvMapDef* Find(const FString& Key);
	/** Kodla kurulan şehir: bina ağlarının klasörleri ve City Sample bina seviyeleri (pakette olanlar kullanılır). */
	static TArrayView<const TCHAR* const> CityMeshDirs();
	static TArrayView<const TCHAR* const> CityTowerLevels();
};
