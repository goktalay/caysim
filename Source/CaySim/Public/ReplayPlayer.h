// Kayıttan oynatma — `/sessions/{id}/replay` gzip JSON kareleri; oynat/duraklat/hız/zaman; enterpolasyon
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CaySimTypes.h"
#include "TelemetryBuffer.h"
#include "ReplayPlayer.generated.h"

class UHepgoClient;

/** Kayıt dosyası başlığı (`{v, hz, fields, frames, modes, events, duration_s, home, takeoff_t, start_utc, vehicle, type, n, source}`). */
USTRUCT(BlueprintType)
struct CAYSIM_API FCaySimReplayHeader
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") int32 Version = 0;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") float Hz = 25.f;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") int32 Count = 0;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") float DurationS = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") float TakeoffT = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") double StartUtc = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString Vehicle;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString TypeKey;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") double HomeLat = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") double HomeLon = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") double HomeAlt = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") TArray<FCaySimEvent> Events;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FReplayLoadedDelegate, bool, bOk, const FCaySimReplayHeader&, Header);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FReplayTimeDelegate, float, TimeS);

/**
 * Kayıt oynatıcı. Kareleri bir kez indirir (gzip → JSON), sabit Hz'li dizide tutar; `Tick` ile oynatma saatini ilerletir,
 * `Sample` iki komşu kareden enterpolasyon (açılar en kısa yol / slerp; 5 sn'den uzun boşlukta tutma) yapar.
 * Başlangıç: ilk ARM − 5 sn, otomatik oynatır (web sürümüyle aynı).
 */
UCLASS(BlueprintType)
class CAYSIM_API UCaySimReplayPlayer : public UObject
{
	GENERATED_BODY()

public:
	/** Kareleri indirir; HAZIR değilse (409) `Retry` ile yeniden denenir. */
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Kayıt")
	void Load(UHepgoClient* Client, const FString& SessionId);

	/** Ham gzip/JSON gövdesinden yükle (test / yerel dosya). */
	bool LoadFromBytes(const TArray<uint8>& Bytes);

	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Kayıt") void Play();
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Kayıt") void Pause();
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Kayıt") void TogglePlay();
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Kayıt") void Restart();
	/** 0.5 / 1 / 2 / 4 / 8 */
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Kayıt") void SetSpeed(float Mult);
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Kayıt") void Seek(float TimeS);
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Kayıt") void Skip(float DeltaS);

	/** Oyun döngüsünden çağrılır; oynuyorsa saati ilerletir ve OnTime yayınlar. */
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Kayıt") void Tick(float DeltaSeconds);

	/** Geçerli zamandaki enterpolasyonlu poz. */
	FCaySimPose Sample() const { return SampleAt(TimeS); }
	FCaySimPose SampleAt(float T) const;

	UFUNCTION(BlueprintPure, Category = "CAYSIM|Kayıt") bool IsLoaded() const { return Frames.Num() > 0; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Kayıt") bool IsPlaying() const { return bPlaying; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Kayıt") float GetTime() const { return TimeS; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Kayıt") float GetDuration() const { return Header.DurationS; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Kayıt") float GetSpeed() const { return Speed; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Kayıt") FCaySimReplayHeader GetHeader() const { return Header; }
	/** Kayıt saati (UTC Unix sn) — HUD "REC ▸ PLAY" zamanı. */
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Kayıt") double GetWallClock() const { return Header.StartUtc + TimeS; }
	/** [T0, T1] aralığındaki olaylar (olay şeridi). */
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Kayıt") TArray<FCaySimEvent> EventsBetween(float T0, float T1) const;
	/** Geçerli karedeki ayrık telemetri (mod/arm/rc/batarya). */
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Kayıt") FCaySimTelemetry CurrentTelemetry() const { return SampleAt(TimeS).Latest; }

	UPROPERTY(BlueprintAssignable, Category = "CAYSIM|Kayıt") FReplayLoadedDelegate OnLoaded;
	UPROPERTY(BlueprintAssignable, Category = "CAYSIM|Kayıt") FReplayTimeDelegate OnTime;

	/** Yükleme ilerlemesi (0–1) ve durum metni. */
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM|Kayıt") float LoadProgress = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM|Kayıt") FString Status;

private:
	struct FFrame
	{
		float T = 0.f;
		FCaySimTelemetry Tele;
		FQuat QNed = FQuat::Identity;
	};
	TArray<FFrame> Frames;
	FCaySimReplayHeader Header;
	float TimeS = 0.f;
	float Speed = 1.f;
	bool bPlaying = false;
	TWeakObjectPtr<UHepgoClient> Hepgo;
	FString SessionId;
	int32 RetryCount = 0;

	void OnBytes(bool bOk, const TArray<uint8>& Bytes);
	bool ParseJson(const FString& Text);
	int32 FindFrame(float T) const;
	static bool GunzipIfNeeded(const TArray<uint8>& In, TArray<uint8>& Out);
};
