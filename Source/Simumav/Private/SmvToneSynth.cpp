#include "SmvToneSynth.h"

USmvToneSynth::USmvToneSynth(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	NumChannels = 1;
	bAutoActivate = false;
}

bool USmvToneSynth::Init(int32& SampleRate)
{
	NumChannels = 1;
	Sr = (float)SampleRate;
	return true;
}

void USmvToneSynth::SetAlarm(int32 Kind, float InGain)
{
	const float G = FMath::Clamp(InGain, 0.f, 1.f);
	SynthCommand([this, Kind, G]() { if (Alarm != Kind) T = 0.0; Alarm = Kind; Gain = G; });
}

void USmvToneSynth::Blip(int32 Kind)
{
	SynthCommand([this, Kind]() { BlipKind = Kind; BlipT = 0.0; });
}

int32 USmvToneSynth::OnGenerateAudio(float* Out, int32 NumSamples)
{
	const double Dt = 1.0 / Sr;
	for (int32 i = 0; i < NumSamples; ++i)
	{
		float S = 0.f, F = 0.f, A = 0.f;
		if (Alarm == 1)
		{
			// WARNING: 0,15 sn ton / 0,1 sn sessiz, 1000 ↔ 1400 Hz
			const double C = FMath::Fmod(T, 0.5);
			if (C < 0.15) { F = 1000.f; A = 0.5f; } else if (C >= 0.25 && C < 0.4) { F = 1400.f; A = 0.5f; }
		}
		else if (Alarm == 2)
		{
			// CAUTION: 2 sn'de bir çift çan (sönümlü)
			const double C = FMath::Fmod(T, 2.0);
			if (C < 0.6) { F = C < 0.3 ? 880.f : 660.f; A = 0.4f * (float)FMath::Exp(-FMath::Fmod(C, 0.3) * 8.0); }
		}
		if (BlipKind)
		{
			const double D = BlipKind == 1 ? 0.18 : BlipKind == 4 ? 0.5 : 0.1;
			if (BlipT < D)
			{
				F = BlipKind == 1 ? 900.f + 1200.f * (float)(BlipT / D) : BlipKind == 2 ? 2000.f : BlipKind == 3 ? 700.f : 1100.f;
				A = 0.45f;
			}
			else BlipKind = 0;
			BlipT += Dt;
		}
		if (A > 0.f)
		{
			Ph += 2.f * PI * F / Sr;
			if (Ph > 2.f * PI) Ph -= 2.f * PI;
			S = FMath::Sin(Ph) * A;
		}
		Out[i] = S * Gain;
		T += Dt;
	}
	return NumSamples;
}
