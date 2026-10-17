// Telemetri tamponu — gecikme tamponlu enterpolasyon (varsayılan 80 ms), açılar slerp
#pragma once

#include "CoreMinimal.h"
#include "CaySimTypes.h"

/** Enterpolasyonlu örnek: konum (WGS84), yönelim (NED Euler → quaternion), hızlar. */
struct CAYSIM_API FCaySimPose
{
	double T = 0.0;          // sunucu zamanı (Unix sn)
	double Lat = 0.0;
	double Lon = 0.0;
	double AltMsl = 0.0;
	double AltRel = 0.0;
	/** NED gövde yönelimi (roll/pitch/yaw) → quaternion; eksen dönüşümü DroneActor'da */
	FQuat QNed = FQuat::Identity;
	float Roll = 0.f, Pitch = 0.f, Yaw = 0.f;     // rad (quaternion'dan türetilir)
	FVector VelNed = FVector::ZeroVector;         // m/s (vn, ve, vd)
	float Throttle = 0.f;                        // %
	float Airspeed = 0.f, Groundspeed = 0.f;
	bool bValid = false;
	/** Son gerçek karenin kopyası (ayrık alanlar: mod, armed, batarya, rc…) */
	FCaySimTelemetry Latest;
};

/**
 * Sabit gecikmeli oynatma tamponu. Kareler sunucu zamanı `ts` ile sıralanır; `Sample(Now)` çağrısı
 * `Now - DelayS` anındaki durumu iki komşu kareden enterpolasyonla verir (konum doğrusal; yönelim slerp).
 * Kare gelmezse (ağ duraklaması) son kareyi hız vektörüyle en çok `MaxExtrapolateS` ileri taşır, sonra tutar.
 * Tek iş parçacığı (oyun) için tasarlandı; WS geri çağrımları oyun iş parçacığındadır.
 */
class CAYSIM_API FTelemetryBuffer
{
public:
	explicit FTelemetryBuffer(double InDelayS = 0.080, int32 InCapacity = 256);

	/** Yeni kare (WS `tele`). Zamanı geriye giden kareler atılır; `ts`=0 ise yerel zaman kullanılır. */
	void Push(const FCaySimTelemetry& Tele, double LocalNow);
	/** `Now` sunucu zamanı (UHepgoClient::NowServer). */
	FCaySimPose Sample(double Now) const;
	/** Tamponu boşaltır (oturum değişimi / replay sıfırlama). */
	void Reset();

	void SetDelay(double S) { DelayS = FMath::Clamp(S, 0.0, 1.0); }
	double GetDelay() const { return DelayS; }
	int32 Num() const { return Frames.Num(); }
	bool IsEmpty() const { return Frames.Num() == 0; }
	/** Son karenin sunucu zamanı. */
	double LatestTime() const { return Frames.Num() ? Frames.Last().T : 0.0; }
	/** Ölçülen kare aralığı (ms, üstel ortalama) — HUD'da "50 Hz" göstergesi. */
	float MeanIntervalMs() const { return MeanDtMs; }

	/** NED Euler (rad) → quaternion (Z-down NED çerçevesinde, ZYX sırası). */
	static FQuat NedEulerToQuat(float Roll, float Pitch, float Yaw);
	/** Quaternion → NED Euler (rad). */
	static void QuatToNedEuler(const FQuat& Q, float& Roll, float& Pitch, float& Yaw);

private:
	struct FFrame
	{
		double T = 0.0;
		FCaySimPose Pose;
	};
	TArray<FFrame> Frames;   // artan T
	int32 Capacity;
	double DelayS;
	double MaxExtrapolateS = 0.25;
	float MeanDtMs = 20.f;
	int64 LastTbm = 0;

	static FCaySimPose Lerp(const FCaySimPose& A, const FCaySimPose& B, double Alpha);
	static FCaySimPose FromTele(const FCaySimTelemetry& T, double Time);
};
