#include "SmvMusicSynth.h"

namespace
{
	// akorlar (MIDI): Am7, Fmaj7, Cmaj7, G6
	const int32 GChords[4][4] = { { 57, 60, 64, 67 }, { 53, 57, 60, 64 }, { 48, 52, 55, 59 }, { 55, 59, 62, 64 } };
	const int32 GBass[4] = { 45, 41, 36, 43 };
	float Midi(float N) { return 440.f * FMath::Pow(2.f, (N - 69.f) / 12.f); }
	float Tri(float Ph) { const float X = Ph / UE_TWO_PI; return 4.f * FMath::Abs(X - FMath::FloorToFloat(X + 0.5f)) - 1.f; }
}

USmvMusicSynth::USmvMusicSynth(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	NumChannels = 1;
	bAutoActivate = false;
}

bool USmvMusicSynth::Init(int32& SampleRate)
{
	NumChannels = 1;
	Sr = (float)SampleRate;
	Echo.Init(0.f, FMath::Max(1, (int32)(Sr * 0.395f)));   // 3/8 vuruşa yakın yankı
	return true;
}

void USmvMusicSynth::SetLevel(float InLevel)
{
	const float L = FMath::Clamp(InLevel, 0.f, 1.f);
	SynthCommand([this, L]() { Target = L; });
}

int32 USmvMusicSynth::OnGenerateAudio(float* Out, int32 NumSamples)
{
	const double Dt = 1.0 / Sr;
	const double Beat = 60.0 / 76.0;
	const double Bar = Beat * 4.0;
	const float Slew = (float)Dt / 1.2f;
	for (int32 i = 0; i < NumSamples; ++i)
	{
		Level += FMath::Clamp(Target - Level, -Slew, Slew);
		if (Level <= 0.0001f && Target <= 0.f) { Out[i] = 0.f; T += Dt; continue; }
		const int32 BarI = (int32)(T / Bar);
		const int32 Ci = BarI % 4;
		const double InBar = T - BarI * Bar;
		// pad: dört ses, hafif detune, akor geçişinde yumuşak zarf
		const float PadEnv = FMath::Min(1.f, (float)InBar / 0.8f) * FMath::Min(1.f, (float)(Bar - InBar) / 0.4f + 0.35f);
		float Pad = 0.f;
		for (int32 k = 0; k < 4; ++k)
		{
			const float F = Midi((float)GChords[Ci][k]);
			PadPh[k] = FMath::Fmod(PadPh[k] + UE_TWO_PI * F / Sr, UE_TWO_PI);
			PadPh2[k] = FMath::Fmod(PadPh2[k] + UE_TWO_PI * F * 1.004f / Sr, UE_TWO_PI);
			Pad += (Tri(PadPh[k]) + Tri(PadPh2[k])) * 0.5f;
		}
		Pad *= 0.045f * PadEnv;
		// bas: vuruş başında sönümlü
		const double InBeat = FMath::Fmod(T, Beat * 2.0);
		BassPh = FMath::Fmod(BassPh + UE_TWO_PI * Midi((float)GBass[Ci]) / Sr, UE_TWO_PI);
		const float Bass = FMath::Sin(BassPh) * 0.11f * FMath::Exp(-(float)InBeat * 1.4f);
		// arpej: sekizlikler, tohumlu nota seçimi
		const double Eighth = Beat * 0.5;
		const int32 Step = (int32)(T / Eighth);
		const double InStep = T - Step * Eighth;
		uint32 H = (uint32)Step * 2654435761u ^ Seed;
		H ^= H >> 13;
		const int32 Note = GChords[Ci][H % 4] + 12 * (int32)((H >> 4) % 2 + 1);
		ArpPh = FMath::Fmod(ArpPh + UE_TWO_PI * Midi((float)Note) / Sr, UE_TWO_PI);
		const float Arp = FMath::Sin(ArpPh) * 0.035f * FMath::Exp(-(float)InStep * 7.f) * ((H >> 8) % 5 != 0 ? 1.f : 0.f);
		// çan: her ölçünün başında yüksek sinüs
		BellPh = FMath::Fmod(BellPh + UE_TWO_PI * Midi((float)GChords[Ci][2] + 24.f) / Sr, UE_TWO_PI);
		const float Bell = FMath::Sin(BellPh) * 0.025f * FMath::Exp(-(float)InBar * 2.2f);
		float S = Pad + Bass + Arp + Bell;
		// yumuşatma + yankı
		Lp += (S - Lp) * 0.35f;
		S = Lp;
		if (Echo.Num())
		{
			const float E = Echo[EchoPos];
			Echo[EchoPos] = S + E * 0.38f;
			EchoPos = (EchoPos + 1) % Echo.Num();
			S += E * 0.3f;
		}
		Out[i] = S * Level;
		T += Dt;
	}
	return NumSamples;
}
