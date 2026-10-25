#include "CaySim.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#if CAYSIM_WITH_CESIUM
THIRD_PARTY_INCLUDES_START
#include <Cesium3DTilesContent/registerAllTileContentTypes.h>
THIRD_PARTY_INCLUDES_END
#endif

DEFINE_LOG_CATEGORY(LogCaySim);

void FCaySimModule::StartupModule()
{
	// WebSockets modülü isteğe bağlı yüklenir; HepgoClient ilk bağlantıda da yükler.
	FModuleManager::Get().LoadModule(TEXT("WebSockets"));
#if CAYSIM_WITH_CESIUM
	// Paketli oyunda Cesium for Unreal eklentisi BAĞLANMIYOR (günlükte "Mounting … CesiumForUnreal" yok): kod tek parça ikilide olduğu için
	// sınıflar çalışıyor ama CesiumRuntime modülünün StartupModule'ü hiç çağrılmıyor → glTF / b3dm / i3dm / pnts dönüştürücüleri kayıtsız
	// kalıyordu. Google fotogerçekçi 3B ve OSM Buildings karoları "glTF" sihirli sayısıyla tanınamayıp JSON sanılıyor ("Error when parsing JSON
	// content … byte offset 0"), hiçbir karo çizilmiyordu (arazi quantized-mesh yükleyicisi dönüştürücü kullanmadığı için çalışıyordu).
	// Dönüştürücüler burada kaydedilir (tekrar kayıt zararsız: aynı anahtarın üzerine yazar). Modül yüklenmez: StartupModule eklenti
	// bağlı değilken IPluginManager::FindPlugin(...)->GetBaseDir() ile çöker.
	Cesium3DTilesContent::registerAllTileContentTypes();
	const TSharedPtr<IPlugin> Cp = IPluginManager::Get().FindPlugin(TEXT("CesiumForUnreal"));
	UE_LOG(LogCaySim, Log, TEXT("Cesium 3D Tiles içerik dönüştürücüleri kaydedildi (eklenti %s, CesiumRuntime modülü %s)"),
		Cp.IsValid() ? (Cp->IsEnabled() ? TEXT("bağlı") : TEXT("kapalı")) : TEXT("BULUNAMADI"),
		FModuleManager::Get().IsModuleLoaded(TEXT("CesiumRuntime")) ? TEXT("yüklü") : TEXT("yüklenmemiş"));
#endif
	UE_LOG(LogCaySim, Log, TEXT("CaySim modülü yüklendi"));
}

void FCaySimModule::ShutdownModule()
{
	UE_LOG(LogCaySim, Log, TEXT("CaySim modülü kapatıldı"));
}

// Birincil oyun modülü artık Simumav; CaySim ortak çalışma zamanı modülüdür
IMPLEMENT_MODULE(FCaySimModule, CaySim);
