// Uçuş kaydı ve kayıttan oynatma (web ops/replay.js eşi): uçuş sırasında 5 Hz telemetri (konum, irtifa, duruş, hızlar, gaz, batarya,
// mod, ARM) kaydedilir; uçuş bitince Saved/Simumav/Replays/<tarih>_<uçuş>.json dosyasına istek (araç/parkur/dünya) ve özetle birlikte yazılır
// (en yeni 30 kayıt tutulur). Oynatma aynı 3B uçuş sahnesini kurar; USmvReplayFlightLink telemetriyi dosyadan 0,25–8× hızla akıtır
// (fizik çalışmaz; kumanda yok). Aynı kayıttan 2B rota haritası ve yazdırılabilir rapor (HTML → tarayıcıdan PDF) üretilir.
#pragma once

#include "CoreMinimal.h"
#include "SmvFlightLink.h"
#include "Containers/Ticker.h"
#include "SmvReplay.generated.h"

struct SIMUMAV_API FSmvReplayFrame
{
	double T = 0, Lat = 0, Lon = 0, AltMsl = 0, AltRel = 0;
	float Roll = 0, Pitch = 0, Yaw = 0, Vn = 0, Ve = 0, Vd = 0, As = 0, Gs = 0, Hdg = 0, Climb = 0, Thr = 0, Batt = -1;
	bool bArmed = false;
	FString Mode;
};

struct SIMUMAV_API FSmvReplayInfo
{
	FString File, Created, Drone, DroneType, Track, Result;
	double DurationS = 0, DistM = 0, MaxAltM = 0;
	int32 Frames = 0;
};

namespace SmvReplay
{
	SIMUMAV_API FString Dir();
	/** Kaydı yaz (dönüş: dosya yolu; boş = yazılamadı) */
	SIMUMAV_API FString Save(const FSmvFlightRequest& Req, const FSmvFlightSummary& Sum, const TArray<FSmvReplayFrame>& Frames);
	SIMUMAV_API bool Load(const FString& File, FSmvFlightRequest& OutReq, TArray<FSmvReplayFrame>& OutFrames, FSmvReplayInfo* OutInfo = nullptr);
	/** Kayıt listesi (en yeni önce; yalnız başlık okunur) */
	SIMUMAV_API TArray<FSmvReplayInfo> List();
	/** Yazdırılabilir rapor (rota SVG, irtifa profili, özet) → HTML dosyası; dönüş: yol */
	SIMUMAV_API FString WriteReport(const FSmvFlightRequest& Req, const FSmvFlightSummary& Sum, const TArray<FSmvReplayFrame>& Frames);
	/** Kare dizisinin toplam yatay mesafesi (m) */
	SIMUMAV_API double PathLengthM(const TArray<FSmvReplayFrame>& Frames);
}

/** Kayıttan oynatma bağlantısı: telemetri dosyadan akar (hız 0,25–8×). */
UCLASS()
class SIMUMAV_API USmvReplayFlightLink : public USmvFlightLink
{
	GENERATED_BODY()

public:
	void Init(const TArray<FSmvReplayFrame>& InFrames, float InSpeed);
	virtual void Start(const FSmvFlightRequest& Request) override;
	virtual void Stop(const FString& Reason) override;
	virtual void SendRc(const TArray<int32>& Channels) override {}
	virtual FString Command(const FString& Name, const TSharedPtr<FJsonObject>& InParams) override;
	virtual double Now() const override;
	virtual bool IsReady() const override { return bReady; }
	virtual ESmvFlightBackend Backend() const override { return ESmvFlightBackend::LocalSitl; }
	virtual void BeginDestroy() override;
	float GetSpeed() const { return Speed; }
	double GetPlayT() const { return PlayT; }
	double GetDuration() const { return Frames.Num() ? Frames.Last().T : 0.0; }

private:
	TArray<FSmvReplayFrame> Frames;
	float Speed = 1.f;
	double PlayT = 0;
	int32 Cursor = 0;
	bool bReady = false, bEndSaid = false;
	int32 StartTicks = 0;
	double Unix0 = 0, Wall0 = 0;
	FTSTicker::FDelegateHandle TickHandle;
	bool OnTick(float Dt);
};
