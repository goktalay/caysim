#include "SmvMapRegistry.h"

// ===========================================================================================================================
// UÇUŞ HARİTALARI KAYIT TABLOSU
// Biçim: { Key, ad dil anahtarı, açıklama dil anahtarı, önizleme (Content/ altı PNG), harita adayları (';'), tür,
//          başlangıç kaydırması doğu m, kuzey m, kalkış yönü derece, sahne dekoru, Fab paketi[, çevre zemini biyomu] }
// CI (Tools/fab/fab-hazirla.ps1) işaretli bloktaki satırların 5. alanından ilk mevcut haritayı MapsToCook'a ekler —
// satır biçimini (tek satır, TEXT("...") alanları) bozmayın. Dil anahtarları Tools/i18n/metinler.py'de.
// ===========================================================================================================================
namespace
{
	const FSmvMapDef GSmvMaps[] =
	{
		// <HARITA-TABLOSU>
		{ TEXT("orman"),   TEXT("fabmap.forest"),   TEXT("fabmap.forest.desc"),   TEXT("Simumav/MapPreviews/orman.png"),    TEXT("/Game/Brushify/Maps/Forest/Forest"), ESmvMapKind::Level, 0, 0, 0, false, TEXT("Brushify") },
		{ TEXT("yikik"),   TEXT("fabmap.ruined"),   TEXT("fabmap.ruined.desc"),   TEXT("Simumav/MapPreviews/yikik.png"),    TEXT("/Game/Ruined_Construction/Levels/Master_Level/P_Ruined_Construction"), ESmvMapKind::Level, 0, 0, 0, false, TEXT("Ruined_Construction"), TEXT("Desert") },
		{ TEXT("sehir"),   TEXT("fabmap.city"),     TEXT("fabmap.city.desc"),     TEXT("Simumav/MapPreviews/sehir.png"),    TEXT(""), ESmvMapKind::ProcCity, 0, 0, 0, false, TEXT("EuropeanBuildings_Asset;CitySampleBuildings") },
		{ TEXT("rafineri"), TEXT("fabmap.refinery"), TEXT("fabmap.refinery.desc"), TEXT("Simumav/MapPreviews/rafineri.png"), TEXT("/Game/Main"), ESmvMapKind::Level, 0, 0, 0, false, TEXT("refineries") },
		{ TEXT("askeri"),  TEXT("fabmap.military"), TEXT("fabmap.military.desc"), TEXT("Simumav/MapPreviews/askeri.png"),   TEXT("/Game/US_Military/Levels/Showreel_Scene;/Game/US_Military/Levels/Overview"), ESmvMapKind::Level, 0, 0, 0, true, TEXT("US_Military"), TEXT("Desert") },
		{ TEXT("pist"),    TEXT("fabmap.racing"),   TEXT("fabmap.racing.desc"),   TEXT("Simumav/MapPreviews/pist.png"),     TEXT("/Game/CityRacing/Maps/CityRacingMainMap"), ESmvMapKind::Level, 0, 0, 0, false, TEXT("CityRacing") },
		{ TEXT("kosk"),    TEXT("fabmap.pavilion"), TEXT("fabmap.pavilion.desc"), TEXT("Simumav/MapPreviews/kosk.png"),     TEXT("/Game/SeyeonjeongPavilion/Level/Demo"), ESmvMapKind::Level, 0, 0, 0, false, TEXT("SeyeonjeongPavilion") },
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
