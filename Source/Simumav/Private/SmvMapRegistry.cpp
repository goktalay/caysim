#include "SmvMapRegistry.h"
#include "HAL/IConsoleManager.h"

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
		{ TEXT("rafineri"), TEXT("fabmap.refinery"), TEXT("fabmap.refinery.desc"), TEXT("Simumav/MapPreviews/rafineri.png"), TEXT("/Game/PostRefinery/Maps/Showcase;/Game/PostRefinery/Maps/PackedActorsShowcase"), ESmvMapKind::Level, 0, 0, 0, false, TEXT("PostRefinery"), TEXT("Desert"), 40.7650, 29.7750, 39.0, false, 3.0 },   // Kocaeli Körfez kıyısı, İzmit rafinerisi yanı. KK Design Post Apocalyptic Refinery (hazır harita; KitBash3D Refineries'in haritası boştu — kullanıcı 2026-10-28 satın aldı), saha çitli
		{ TEXT("askeri"),  TEXT("fabmap.military"), TEXT("fabmap.military.desc"), TEXT("Simumav/MapPreviews/askeri.png"),   TEXT("/Game/US_Military/Levels/Showreel_Scene;/Game/US_Military/Levels/Overview"), ESmvMapKind::Level, 0, 0, 0, true, TEXT("US_Military"), TEXT("Desert"), 37.7300, 33.5500, 995.0, false, 3.0 },   // Konya Karapınar bozkırı (düz step / yarı çöl)
		{ TEXT("pist"),    TEXT("fabmap.racing"),   TEXT("fabmap.racing.desc"),   TEXT("Simumav/MapPreviews/pist.png"),     TEXT("/Game/CityRacing/Maps/CityRacingMainMap"), ESmvMapKind::Level, 0, 0, 0, false, TEXT("CityRacing"), nullptr, 40.9517, 29.4050, 130.0, false },   // İstanbul Tuzla, İstanbul Park yarış pisti yöresi
		{ TEXT("kosk"),    TEXT("fabmap.pavilion"), TEXT("fabmap.pavilion.desc"), TEXT("Simumav/MapPreviews/kosk.png"),     TEXT("/Game/SeyeonjeongPavilion/Level/Demo"), ESmvMapKind::Level, 0, 0, 0, false, TEXT("SeyeonjeongPavilion"), nullptr, 40.7440, 30.3000, 70.0, false },   // Sakarya Sapanca Gölü kuzey kıyısı
		{ TEXT("karakol"), TEXT("fabmap.outpost"),  TEXT("fabmap.outpost.desc"),  TEXT("Simumav/MapPreviews/karakol.png"),  TEXT("/Game/OutpostandBunker/Maps/Map_Bunker_01;/Game/OutpostandBunker/Maps/Map_Shrubland_01"), ESmvMapKind::Level, 0, 0, 0, true, TEXT("OutpostandBunker"), nullptr, 39.5800, 32.1500, 880.0, false, 20.0 },   // Ankara Polatlı bozkırı; FreshCan Military Outpost & Bunker (4 × 4 km çalılık arazi), saha çitli
		{ TEXT("modernsehir"), TEXT("fabmap.moderncity"), TEXT("fabmap.moderncity.desc"), TEXT("Simumav/MapPreviews/modernsehir.png"), TEXT("/Game/City/Scene/Scene;/Game/City/Scene/Overview"), ESmvMapKind::Level, 0, 0, 0, false, TEXT("City"), nullptr, 41.1100, 29.0200, 120.0, false, 10.0, nullptr, true },   // İstanbul Maslak; Modern 3D City Environment (10 blok + yol ağı + araçlar), saha çitli
		{ TEXT("adalar"),  TEXT("fabmap.islands"),  TEXT("fabmap.islands.desc"),  TEXT("Simumav/MapPreviews/adalar.png"),   TEXT("/Game/IslandsInTheOcean/Maps/Islands_in_the_ocean"), ESmvMapKind::Level, 0, 0, 0, false, TEXT("IslandsInTheOcean"), nullptr, 39.8300, 26.0500, 10.0, false, 50.0, nullptr, true },   // Çanakkale Bozcaada açıkları; F.S.S. Islands in the Ocean (16 × 16 km açık dünya: şehir, havalimanı, köprüler), saha çiti 50 m
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

void FSmvMapRegistry::ApplyGpuProfile(const FSmvMapDef* Def)
{
	IConsoleVariable* Hw = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Lumen.HardwareRayTracing"));
	if (!Hw) return;
	// ilk çağrıdaki değer taban (güvenli grafik kipi zaten 0 yapmışsa öyle kalır)
	static int32 Base = -1;
	if (Base < 0) Base = Hw->GetInt();
	const int32 Want = (Def && Def->bLumenSoft) ? 0 : Base;
	if (Hw->GetInt() != Want)
	{
		Hw->Set(Want, ECVF_SetByCode);
		UE_LOG(LogTemp, Log, TEXT("Harita GPU profili (%s): Lumen %s izleme"), Def ? Def->Key : TEXT("-"), Want ? TEXT("donanımsal") : TEXT("yazılım"));
	}
}
