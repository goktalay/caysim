// Uyarı tonları (ses varlığı gerektirmez): WARNING = kesik 1 kHz/1,4 kHz, CAUTION = çift çan, kilit = kısa bip, halka geçişi = yükselen bip.
// Sesli ifade (“Pull up” vb.) için konuşma sentezi yok — planlandı (docs/ozellik-esligi.md).
#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "SmvToneSynth.generated.h"

UCLASS(ClassGroup = Simumav, meta = (BlueprintSpawnableComponent))
class SIMUMAV_API USmvToneSynth : public USynthComponent
{
	GENERATED_BODY()

public:
	USmvToneSynth(const FObjectInitializer& ObjectInitializer);
	/** 0 sessiz, 1 warning (sürekli), 2 caution (sürekli). */
	void SetAlarm(int32 Kind, float Gain);
	/** Tek seferlik: 1 halka, 2 kilit, 3 geri sayım, 4 başla. */
	void Blip(int32 Kind);

protected:
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
	float Sr = 48000.f;
	int32 Alarm = 0;
	float Gain = 0.6f;
	double T = 0.0;
	float Ph = 0.f;
	int32 BlipKind = 0;
	double BlipT = 0.0;
};
