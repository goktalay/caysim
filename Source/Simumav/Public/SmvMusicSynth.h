// Menü müziği (web SIMUMAV ui/music.js eşi — dosya yok, sentez): La minör Am7 → Fmaj7 → Cmaj7 → G6, 76 BPM; katmanlar: yumuşak pad,
// bas, tohumlu arpej, çan, basit yankı. Ses düzeyi hedefe yumuşakça kayar (menü 1, uç sihirbazı / salon 0,7, uçuş 0 — 1,2 sn'de söner).
#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "SmvMusicSynth.generated.h"

UCLASS(ClassGroup = Simumav, meta = (BlueprintSpawnableComponent))
class SIMUMAV_API USmvMusicSynth : public USynthComponent
{
	GENERATED_BODY()

public:
	USmvMusicSynth(const FObjectInitializer& ObjectInitializer);
	/** Hedef düzey 0–1 (ana ses × müzik kanalı × bağlam) */
	void SetLevel(float InLevel);

protected:
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
	float Sr = 48000.f;
	double T = 0.0;
	float Level = 0.f, Target = 0.f;
	float PadPh[4] = { 0, 0, 0, 0 };
	float PadPh2[4] = { 0, 0, 0, 0 };
	float BassPh = 0.f, ArpPh = 0.f, BellPh = 0.f;
	float Lp = 0.f;
	TArray<float> Echo;
	int32 EchoPos = 0;
	uint32 Seed = 7;
};
