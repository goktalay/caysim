// Simumav birincil oyun modülü
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSimumav, Log, All);

class FSimumavModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
