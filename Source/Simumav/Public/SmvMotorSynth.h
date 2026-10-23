// Prosedürel motor sesi (ses dosyası/MetaSound varlığı gerekmez): USynthComponent ses iş parçacığında dalga üretir.
// Model: elektrikli çok rotor — pervane geçiş frekansı (RPM/60 × kanat) + harmonikler + hava gürültüsü (gaza bağlı);
//        içten yanmalı / itici uçak — ateşleme frekansı + egzoz gürültüsü; rüzgâr sesi hava hızıyla. Doppler kameraya göre yaklaşık.
// Parametreler oyun iş parçacığından SetParams ile verilir (SynthCommand ile ses iş parçacığına geçer).
#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "SmvMotorSynth.generated.h"

UENUM(BlueprintType)
enum class ESmvEngineKind : uint8
{
	ElectricMulti, ElectricProp, Combustion, Jet, Rover
};

UCLASS(ClassGroup = Simumav, meta = (BlueprintSpawnableComponent))
class SIMUMAV_API USmvMotorSynth : public USynthComponent
{
	GENERATED_BODY()

public:
	USmvMotorSynth(const FObjectInitializer& ObjectInitializer);

	/** Throttle01 0–1, Airspeed m/s, bArmed, Doppler çarpanı (1 = yok), Gain 0–1. */
	void SetParams(float Throttle01, float AirspeedMs, bool bArmed, float Doppler, float Gain);
	void SetKind(ESmvEngineKind K, int32 Rotors);

protected:
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
	// ses iş parçacığı durumu
	float Sr = 48000.f;
	float Phase[6] = { 0, 0, 0, 0, 0, 0 };
	float CurThr = 0.f, TgtThr = 0.f, CurAir = 0.f, TgtAir = 0.f, CurGain = 0.f, TgtGain = 0.f, Dop = 1.f;
	bool bOn = false;
	ESmvEngineKind Kind = ESmvEngineKind::ElectricMulti;
	int32 NumRotors = 4;
	uint32 Noise = 22222;
	float NoiseLp = 0.f, WindLp = 0.f;
	float NextNoise();
};
