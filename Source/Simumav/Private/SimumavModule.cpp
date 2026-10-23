#include "SimumavModule.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogSimumav);

void FSimumavModule::StartupModule()
{
	FModuleManager::Get().LoadModule(TEXT("WebSockets"));
	UE_LOG(LogSimumav, Log, TEXT("Simumav modülü yüklendi"));
}

void FSimumavModule::ShutdownModule()
{
	UE_LOG(LogSimumav, Log, TEXT("Simumav modülü kapatıldı"));
}

IMPLEMENT_PRIMARY_GAME_MODULE(FSimumavModule, Simumav, "Simumav");
