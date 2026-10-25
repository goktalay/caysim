#include "SimumavModule.h"
#include "Modules/ModuleManager.h"

#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "Misc/ScopeLock.h"

DEFINE_LOG_CATEGORY(LogSimumav);

void SmvDiagWrite(const FString& Line)
{
	static FCriticalSection Lock;
	FScopeLock L(&Lock);
	static FString File;
	if (File.IsEmpty())
	{
		File = FPaths::ProjectLogDir() / TEXT("Simumav-tani.log");
		IFileManager::Get().MakeDirectory(*FPaths::ProjectLogDir(), true);
		FFileHelper::SaveStringToFile(TEXT("Simumav tanı günlüğü (Shipping)\n"), *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
	const FString Out = FString::Printf(TEXT("[%s] %s\n"), *FDateTime::Now().ToString(TEXT("%Y.%m.%d-%H.%M.%S")), *Line);
	FFileHelper::SaveStringToFile(Out, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
}

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
