#include "SmvSteam.h"
#include "SimumavModule.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemNames.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Interfaces/OnlineAchievementsInterface.h"
#include "Interfaces/OnlineExternalUIInterface.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

static FString SmvAchFile() { return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Simumav"), TEXT("achievements.txt")); }

void USmvSteamSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Persona = FPlatformProcess::UserName(false);
	GConfig->GetInt(TEXT("OnlineSubsystemSteam"), TEXT("SteamDevAppId"), AppId, GEngineIni);
	LoadLocal();
	IOnlineSubsystem* Oss = IOnlineSubsystem::Get(STEAM_SUBSYSTEM);
	if (!Oss)
	{
		UE_LOG(LogSimumav, Log, TEXT("Steam alt sistemi yok (Steam kapalı ya da Editor): yerel kip, başarımlar yerelde tutulur"));
		return;
	}
	bSteam = true;
	if (IOnlineIdentityPtr Id = Oss->GetIdentityInterface())
	{
		const FString Nick = Id->GetPlayerNickname(0);
		if (!Nick.IsEmpty()) Persona = Nick;
		if (const FUniqueNetIdPtr U = Id->GetUniquePlayerId(0)) SteamId = U->ToString();
	}
	if (IOnlineAchievementsPtr Ach = Oss->GetAchievementsInterface())
	{
		IOnlineIdentityPtr IdQ = Oss->GetIdentityInterface();
		const FUniqueNetIdPtr U = IdQ.IsValid() ? IdQ->GetUniquePlayerId(0) : FUniqueNetIdPtr();
		if (U.IsValid())
		{
			TWeakObjectPtr<USmvSteamSubsystem> Weak(this);
			Ach->QueryAchievements(*U, FOnQueryAchievementsCompleteDelegate::CreateLambda([Weak](const FUniqueNetId&, bool bOk)
			{
				if (USmvSteamSubsystem* Self = Weak.Get()) { if (bOk) Self->Flush(); }
			}));
		}
	}
	UE_LOG(LogSimumav, Log, TEXT("Steam hazır: %s (%s), uygulama %d"), *Persona, *SteamId, AppId);
}

void USmvSteamSubsystem::Deinitialize()
{
	SaveLocal();
	Super::Deinitialize();
}

void USmvSteamSubsystem::LoadLocal()
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *SmvAchFile())) return;
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines);
	for (const FString& L : Lines)
	{
		if (L.StartsWith(TEXT("!"))) { Unlocked.Add(L.Mid(1)); PendingWrites.AddUnique(L.Mid(1)); }
		else if (!L.IsEmpty()) Unlocked.Add(L);
	}
}

void USmvSteamSubsystem::SaveLocal() const
{
	FString Text;
	for (const FString& A : Unlocked) Text += (PendingWrites.Contains(A) ? TEXT("!") : TEXT("")) + A + TEXT("\n");
	FFileHelper::SaveStringToFile(Text, *SmvAchFile());
}

void USmvSteamSubsystem::Unlock(const FString& Id)
{
	if (Id.IsEmpty()) return;
	const bool bNew = !Unlocked.Contains(Id);
	Unlocked.Add(Id);
	if (bNew) UE_LOG(LogSimumav, Log, TEXT("Başarım açıldı: %s"), *Id);
	PendingWrites.AddUnique(Id);
	SaveLocal();
	Flush();
}

void USmvSteamSubsystem::Flush()
{
	if (!bSteam || PendingWrites.Num() == 0) return;
	IOnlineSubsystem* Oss = IOnlineSubsystem::Get(STEAM_SUBSYSTEM);
	IOnlineAchievementsPtr Ach = Oss ? Oss->GetAchievementsInterface() : IOnlineAchievementsPtr();
	IOnlineIdentityPtr Idt = Oss ? Oss->GetIdentityInterface() : IOnlineIdentityPtr();
	const FUniqueNetIdPtr U = Idt.IsValid() ? Idt->GetUniquePlayerId(0) : FUniqueNetIdPtr();
	if (!Ach.IsValid() || !U.IsValid()) return;
	FOnlineAchievementsWriteRef W = MakeShared<FOnlineAchievementsWrite, ESPMode::ThreadSafe>();
	for (const FString& A : PendingWrites) W->SetFloatStat(FName(*A), 100.f);
	const TArray<FString> Sent = PendingWrites;
	TWeakObjectPtr<USmvSteamSubsystem> Weak(this);
	Ach->WriteAchievements(*U, W, FOnAchievementsWrittenDelegate::CreateLambda([Weak, Sent](const FUniqueNetId&, bool bOk)
	{
		USmvSteamSubsystem* Self = Weak.Get();
		if (!Self) return;
		if (bOk) { for (const FString& A : Sent) Self->PendingWrites.Remove(A); Self->SaveLocal(); }
		else UE_LOG(LogSimumav, Warning, TEXT("Steam başarımları yazılamadı; sonra yeniden denenecek"));
	}));
}

void USmvSteamSubsystem::OpenUrl(const FString& Url)
{
	if (bSteam)
	{
		if (IOnlineSubsystem* Oss = IOnlineSubsystem::Get(STEAM_SUBSYSTEM))
		{
			if (IOnlineExternalUIPtr Ui = Oss->GetExternalUIInterface())
			{
				if (Ui->ShowWebURL(Url, FShowWebUrlParams())) return;
			}
		}
	}
	FPlatformProcess::LaunchURL(*Url, nullptr, nullptr);
}
