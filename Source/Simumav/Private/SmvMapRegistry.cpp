#include "SmvMapRegistry.h"

// ===========================================================================================================================
// UÇUŞ HARİTALARI KAYIT TABLOSU
// Biçim: { Key, ad dil anahtarı, açıklama dil anahtarı, önizleme (Content/ altı PNG), harita adayları (';'), tür,
//          başlangıç kaydırması doğu m, kuzey m, kalkış yönü derece, sahne dekoru, Fab paketi, çevre zemini biyomu (Cesium yoksa),
//          gerçek dünya demiri enlem, boylam, zemin MSL m, yalnız gerçek dünya }
// CI (Tools/fab/fab-hazirla.ps1) işaretli bloktaki satırların 5. alanından ilk mevcut haritayı MapsToCook'a ekler —
// satır biçimini (tek satır, TEXT("...") alanları) bozmayın. Dil anahtarları Tools/i18n/metinler.py'de.
// ===========================================================================================================================
namespace
{
	const FSmvMapDef GSmvMaps[] =
	{
		// <HARITA-TABLOSU>
		{ TEXT("orman"),   TEXT("fabmap.forest"),   TEXT("fabmap.forest.desc"),   TEXT("Simumav/MapPreviews/orman.png"),    TEXT("/Game/Brushify/Maps/Forest/Forest"), ESmvMapKind::Level, 0, 0, 0, false, TEXT("Brushify"), nullptr, 41.2000, 32.4000, 157.0, false },   // Karabük Yenice ormanları, Filyos vadisi (çevre sırtları ~+230 m — haritanın tepe halkasıyla uyumlu)
		{ TEXT("yikik"),   TEXT("fabmap.ruined"),   TEXT("fabmap.ruined.desc"),   TEXT("Simumav/MapPreviews/yikik.png"),    TEXT("/Game/Ruined_Construction/Levels/Master_Level/P_Ruined_Construction"), ESmvMapKind::Level, 0, 0, 0, false, TEXT("Ruined_Construction"), TEXT("Desert"), 39.9650, 32.5800, 795.0, false, 3.0 },   // Ankara batı yakası, Etimesgut–Sincan sanayi kuşağı
		{ TEXT("sehir"),   TEXT("fabmap.city"),     TEXT("fabmap.city.desc"),     TEXT("Simumav/MapPreviews/sehir.png"),    TEXT(""), ESmvMapKind::ProcCity, 0, 0, 0, false, TEXT("EuropeanBuildings_Asset;CitySampleBuildings"), nullptr, 41.0369, 28.9850, 80.0, false },   // İstanbul Taksim Meydanı (Google 3B: gerçek şehir — İstanbul'da tam fotogerçekçi 3B kapsama; Ankara Kızılay'da karolar düz / 2,5B görünüyordu)
		{ TEXT("rafineri"), TEXT("fabmap.refinery"), TEXT("fabmap.refinery.desc"), TEXT("Simumav/MapPreviews/rafineri.png"), TEXT("/Game/Main"), ESmvMapKind::Level, 0, 0, 0, false, TEXT("refineries"), nullptr, 40.7650, 29.7750, 39.0, true },   // Kocaeli Körfez kıyısı, İzmit rafinerisi yanı (gerçek tesis 3B karolarda)
		{ TEXT("askeri"),  TEXT("fabmap.military"), TEXT("fabmap.military.desc"), TEXT("Simumav/MapPreviews/askeri.png"),   TEXT("/Game/US_Military/Levels/Showreel_Scene;/Game/US_Military/Levels/Overview"), ESmvMapKind::Level, 0, 0, 0, true, TEXT("US_Military"), TEXT("Desert"), 37.7300, 33.5500, 995.0, false, 3.0 },   // Konya Karapınar bozkırı (düz step / yarı çöl)
		{ TEXT("pist"),    TEXT("fabmap.racing"),   TEXT("fabmap.racing.desc"),   TEXT("Simumav/MapPreviews/pist.png"),     TEXT("/Game/CityRacing/Maps/CityRacingMainMap"), ESmvMapKind::Level, 0, 0, 0, false, TEXT("CityRacing"), nullptr, 40.9517, 29.4050, 130.0, false },   // İstanbul Tuzla, İstanbul Park yarış pisti yöresi
		{ TEXT("kosk"),    TEXT("fabmap.pavilion"), TEXT("fabmap.pavilion.desc"), TEXT("Simumav/MapPreviews/kosk.png"),     TEXT("/Game/SeyeonjeongPavilion/Level/Demo"), ESmvMapKind::Level, 0, 0, 0, false, TEXT("SeyeonjeongPavilion"), nullptr, 40.7440, 30.3000, 70.0, false },   // Sakarya Sapanca Gölü kuzey kıyısı
		// </HARITA-TABLOSU>
	};

	// Kodla kurulan şehir bloğu (ESmvMapKind::ProcCity). CI bu bloklardaki /Game yollarını pakete pişirir:
	// ağ klasörleri DirectoriesToAlwaysCook'a, bina seviyeleri MapsToCook'a (yalnız diskte olanlar).
	const TCHAR* const GSmvCityMeshDirs[] =
	{
		// <SEHIR-AG-KLASORLERI>
		TEXT("/Game/EuropeanBuildings_Asset/Meshes"),
		// </SEHIR-AG-KLASORLERI>
	};
	const TCHAR* const GSmvCityTowers[] =
	{
		// <SEHIR-KULELERI>
		TEXT("/Game/CitySampleBuildings/Building/Library/Kit_Ref_Bldg/LevelInstance/NYAA_Ref_N1"),
		TEXT("/Game/CitySampleBuildings/Building/Library/Kit_Ref_Bldg/LevelInstance/NYAB_Ref_N1"),
		TEXT("/Game/CitySampleBuildings/Building/Library/Kit_Ref_Bldg/LevelInstance/NYAC_Ref_N1"),
		TEXT("/Game/CitySampleBuildings/Building/Library/Kit_Ref_Bldg/LevelInstance/NYAD_Ref_N1"),
		TEXT("/Game/CitySampleBuildings/Building/Library/Kit_Ref_Bldg/LevelInstance/NYAE_Ref_N1"),
		TEXT("/Game/CitySampleBuildings/Building/Library/Kit_Ref_Bldg/LevelInstance/NYAF_Ref_N1"),
		TEXT("/Game/CitySampleBuildings/Building/Library/Kit_Ref_Bldg/LevelInstance/NYH_Ref_N1"),
		// </SEHIR-KULELERI>
	};
}

TArrayView<const FSmvMapDef> FSmvMapRegistry::All()
{
	return TArrayView<const FSmvMapDef>(GSmvMaps, UE_ARRAY_COUNT(GSmvMaps));
}

const FSmvMapDef* FSmvMapRegistry::Find(const FString& Key)
{
	for (const FSmvMapDef& D : All()) if (Key == D.Key) return &D;
	return nullptr;
}

TArrayView<const TCHAR* const> FSmvMapRegistry::CityMeshDirs()
{
	return TArrayView<const TCHAR* const>(GSmvCityMeshDirs, UE_ARRAY_COUNT(GSmvCityMeshDirs));
}

TArrayView<const TCHAR* const> FSmvMapRegistry::CityTowerLevels()
{
	return TArrayView<const TCHAR* const>(GSmvCityTowers, UE_ARRAY_COUNT(GSmvCityTowers));
}
