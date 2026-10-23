// Steam: OnlineSubsystemSteam (DefaultEngine.ini) üzerinden kimlik + başarımlar. Steam yoksa (geliştirme/Editor) sessizce yerel kipte çalışır.
// Başarımlar misafir kipte de çalışır (Steam yereldir); yazılamazsa (Steam kapalı) yerel kuyrukta tutulur ve bir sonraki açılışta denenir.
// Başarım kimlikleri Steamworks panelindeki API adlarıyla aynı olmalı (docs/steam-yayin.md) ve DefaultEngine.ini [OnlineSubsystemSteam]
// Achievement_N_Id listesinde bulunmalı (OSS Steam yalnız yapılandırılan başarımları yazar).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SmvSteam.generated.h"

/** Başarım kimlikleri (Steamworks API adı). */
namespace SmvAch
{
	static const TCHAR* const FirstFlight = TEXT("ACH_FIRST_FLIGHT");          // ilk tamamlanan uçuş
	static const TCHAR* const FirstLanding = TEXT("ACH_SAFE_LANDING");         // kazasız iniş (≥ 60 sn)
	static const TCHAR* const RingWin = TEXT("ACH_RING_RACE_WIN");             // halka yarışı galibiyeti
	static const TCHAR* const DogfightWin = TEXT("ACH_DOGFIGHT_WIN");          // it dalaşı galibiyeti
	static const TCHAR* const DisciplineA = TEXT("ACH_DISCIPLINE_A");          // uçuş disiplini A notu
	static const TCHAR* const AllRings = TEXT("ACH_ALL_RINGS");                // parkurdaki tüm halkalar
	static const TCHAR* const Interceptor = TEXT("ACH_INTERCEPT");             // ilk hedef önleme
	static const TCHAR* const Ace = TEXT("ACH_ACE");                           // tüm hedefler (ace rozeti)
	static const TCHAR* const Ramp = TEXT("ACH_RAMP_LAUNCH");                  // rampadan fırlatma
	static const TCHAR* const Rover = TEXT("ACH_ROVER");                       // kara aracıyla sürüş
	static const TCHAR* const Hours10 = TEXT("ACH_HOURS_10");                  // toplam 10 saat
	static const TCHAR* const CloudFlight = TEXT("ACH_CLOUD_FLIGHT");          // simumav.com hesabıyla bulutta uçuş
}

UCLASS()
class SIMUMAV_API USmvSteamSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "Simumav|Steam") bool IsSteamAvailable() const { return bSteam; }
	/** Steam kullanıcı adı (yoksa Windows kullanıcı adı). */
	UFUNCTION(BlueprintPure, Category = "Simumav|Steam") FString GetPersonaName() const { return Persona; }
	/** Steam 64 bit kimliği (yerel profil anahtarı; yoksa "local"). */
	UFUNCTION(BlueprintPure, Category = "Simumav|Steam") FString GetSteamId() const { return SteamId; }
	/** Uygulama kimliği (steam_appid.txt / DefaultEngine.ini SteamDevAppId; 480 = Spacewar geliştirme yer tutucusu). */
	UFUNCTION(BlueprintPure, Category = "Simumav|Steam") int32 GetAppId() const { return AppId; }

	UFUNCTION(BlueprintCallable, Category = "Simumav|Steam") void Unlock(const FString& AchievementId);
	UFUNCTION(BlueprintPure, Category = "Simumav|Steam") bool IsUnlockedLocally(const FString& AchievementId) const { return Unlocked.Contains(AchievementId); }
	/** Steam katmanında URL aç (yoksa sistem tarayıcısı). */
	UFUNCTION(BlueprintCallable, Category = "Simumav|Steam") void OpenUrl(const FString& Url);

private:
	bool bSteam = false;
	FString Persona;
	FString SteamId = TEXT("local");
	int32 AppId = 480;
	TSet<FString> Unlocked;
	TArray<FString> PendingWrites;
	void LoadLocal();
	void SaveLocal() const;
	void Flush();
};
