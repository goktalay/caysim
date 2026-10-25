#include "SmvLoadGate.h"
#include "SimumavModule.h"
#include "Engine/World.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UObjectGlobals.h"
#include "Components/SkyLightComponent.h"
#include "ContentStreaming.h"
#include "ShaderCompiler.h"
#include "ShaderPipelineCache.h"
#include "PipelineStateCache.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "HAL/PlatformTime.h"

namespace
{
	// toplam ilerlemede aşama ağırlıkları (harita akışı en uzun süren)
	const float GWeight[(int32)ESmvLoad::Num] = { 30.f, 10.f, 14.f, 16.f, 10.f, 10.f, 10.f };
}

void FSmvLoadGate::Begin(const FString& InLabel, std::initializer_list<ESmvLoad> Used)
{
	LabelStr = InLabel;
	T0 = FPlatformTime::Seconds();
	bActive = true;
	bSummaryDone = false;
	Shown = 0.f;
	for (int32 i = 0; i < (int32)ESmvLoad::Num; ++i) { Stages[i] = FSmvLoadStage(); Stages[i].bUsed = false; Stages[i].bDone = true; Stages[i].Progress = 1.f; }
	for (ESmvLoad S : Used) { FSmvLoadStage& St = Stages[(int32)S]; St.bUsed = true; St.bDone = false; St.Progress = 0.f; }
	UE_LOG(LogSimumav, Log, TEXT("SMVLOAD [%s] başladı"), *LabelStr);
}

double FSmvLoadGate::Elapsed() const { return bActive ? FPlatformTime::Seconds() - T0 : 0.0; }

bool FSmvLoadGate::IsDone() const
{
	for (const FSmvLoadStage& S : Stages) if (!S.bDone) return false;
	return true;
}

void FSmvLoadGate::Update(ESmvLoad S, float Progress, bool bReady, const FString& Detail, double TimeoutS, int32 StableNeed)
{
	FSmvLoadStage& St = Stages[(int32)S];
	if (!bActive || !St.bUsed || St.bDone) return;
	St.Progress = FMath::Max(St.Progress, FMath::Clamp(Progress, 0.f, 1.f));
	St.Detail = Detail;
	St.StableFrames = bReady ? St.StableFrames + 1 : 0;
	const double T = Elapsed();
	if (St.StableFrames >= FMath::Max(1, StableNeed))
	{
		St.bDone = true; St.Progress = 1.f; St.DoneAt = T;
		UE_LOG(LogSimumav, Log, TEXT("SMVLOAD [%s] %s tamam: %.2f sn%s%s"), *LabelStr, StageLogName(S), T, Detail.IsEmpty() ? TEXT("") : TEXT(" — "), *Detail);
	}
	else if (T > TimeoutS)
	{
		St.bDone = true; St.bTimeout = true; St.DoneAt = T;
		UE_LOG(LogSimumav, Warning, TEXT("SMVLOAD [%s] %s ZAMAN AŞIMI (%.0f sn; ilerleme %%%.0f)%s%s"), *LabelStr, StageLogName(S), TimeoutS, St.Progress * 100.f,
			Detail.IsEmpty() ? TEXT("") : TEXT(" — "), *Detail);
		St.Progress = 1.f;
	}
}

float FSmvLoadGate::Overall() const
{
	float Sum = 0.f, W = 0.f;
	for (int32 i = 0; i < (int32)ESmvLoad::Num; ++i)
	{
		if (!Stages[i].bUsed) continue;
		Sum += GWeight[i] * Stages[i].Progress;
		W += GWeight[i];
	}
	const float P = W > 0.f ? Sum / W : 1.f;
	Shown = FMath::Max(Shown, P);
	return Shown;
}

ESmvLoad FSmvLoadGate::Current() const
{
	for (int32 i = 0; i < (int32)ESmvLoad::Num; ++i) if (Stages[i].bUsed && !Stages[i].bDone) return (ESmvLoad)i;
	return ESmvLoad::Num;
}

void FSmvLoadGate::LogSummary()
{
	if (bSummaryDone || !bActive) return;
	bSummaryDone = true;
	FString S;
	for (int32 i = 0; i < (int32)ESmvLoad::Num; ++i)
	{
		if (!Stages[i].bUsed) continue;
		S += FString::Printf(TEXT(" %s=%.2f%s"), StageLogName((ESmvLoad)i), Stages[i].DoneAt, Stages[i].bTimeout ? TEXT("(zaman aşımı)") : TEXT(""));
	}
	UE_LOG(LogSimumav, Log, TEXT("SMVLOAD [%s] TAMAM toplam %.2f sn —%s"), *LabelStr, Elapsed(), *S);
}

const TCHAR* FSmvLoadGate::StageKey(ESmvLoad S)
{
	switch (S)
	{
	case ESmvLoad::World: return TEXT("load.st.world");
	case ESmvLoad::Scenery: return TEXT("load.st.scenery");
	case ESmvLoad::Atmos: return TEXT("load.st.atmos");
	case ESmvLoad::Sitl: return TEXT("load.st.sitl");
	case ESmvLoad::Shaders: return TEXT("load.st.shaders");
	case ESmvLoad::Streaming: return TEXT("load.st.streaming");
	case ESmvLoad::Warmup: return TEXT("load.st.warmup");
	default: return TEXT("load.st.done");
	}
}

const TCHAR* FSmvLoadGate::StageLogName(ESmvLoad S)
{
	switch (S)
	{
	case ESmvLoad::World: return TEXT("harita");
	case ESmvLoad::Scenery: return TEXT("dekor");
	case ESmvLoad::Atmos: return TEXT("atmosfer");
	case ESmvLoad::Sitl: return TEXT("sitl");
	case ESmvLoad::Shaders: return TEXT("golgelendirici");
	case ESmvLoad::Streaming: return TEXT("doku");
	case ESmvLoad::Warmup: return TEXT("isinma");
	default: return TEXT("-");
	}
}

int32 FSmvLoadGate::ShaderJobs()
{
	return GShaderCompilingManager ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
}

int32 FSmvLoadGate::PsoPending()
{
	return (int32)FShaderPipelineCache::NumPrecompilesRemaining() + (int32)PipelineStateCache::NumActivePrecacheRequests();
}

int32 FSmvLoadGate::StreamingPending()
{
	return IStreamingManager::Get().GetNumWantingResources();
}

int32 FSmvLoadGate::AsyncPackages()
{
	return GetNumAsyncPackages();
}

bool FSmvLoadGate::WorldPartitionDone(UWorld* W)
{
	if (!W) return true;
	UWorldPartitionSubsystem* Wp = W->GetSubsystem<UWorldPartitionSubsystem>();
	return !Wp || Wp->IsAllStreamingCompleted();
}

int32 FSmvLoadGate::RecaptureSky(UWorld* W)
{
	int32 N = 0;
	for (TObjectIterator<USkyLightComponent> It; It; ++It)
	{
		USkyLightComponent* C = *It;
		if (!IsValid(C) || C->GetWorld() != W || !C->IsRegistered()) continue;
		C->RecaptureSky();
		++N;
	}
	return N;
}
