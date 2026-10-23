#include "CaySim.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogCaySim);

void FCaySimModule::StartupModule()
{
	// WebSockets modülü isteğe bağlı yüklenir; HepgoClient ilk bağlantıda da yükler.
	FModuleManager::Get().LoadModule(TEXT("WebSockets"));
	UE_LOG(LogCaySim, Log, TEXT("CaySim modülü yüklendi"));
}

void FCaySimModule::ShutdownModule()
{
	UE_LOG(LogCaySim, Log, TEXT("CaySim modülü kapatıldı"));
}

// Birincil oyun modülü artık Simumav; CaySim ortak çalışma zamanı modülüdür
IMPLEMENT_MODULE(FCaySimModule, CaySim);
