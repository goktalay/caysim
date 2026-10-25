// Simumav birincil oyun modülü
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSimumav, Log, All);

/** Shipping derlemesinde UE_LOG derlenmez: tanı için önemli satırlar (harita / yükleme kapısı / duman testi) ayrıca
 *  Saved/Logs/Simumav-tani.log dosyasına yazılır (yalnız Shipping; diğerlerinde no-op). */
SIMUMAV_API void SmvDiagWrite(const FString& Line);
#if UE_BUILD_SHIPPING
#define SMV_KLOG(Fmt, ...) SmvDiagWrite(FString::Printf(Fmt, ##__VA_ARGS__))
#else
#define SMV_KLOG(Fmt, ...) UE_LOG(LogSimumav, Display, Fmt, ##__VA_ARGS__)
#endif

class FSimumavModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
