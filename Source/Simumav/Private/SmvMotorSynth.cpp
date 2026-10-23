#include "SmvMotorSynth.h"

USmvMotorSynth::USmvMotorSynth(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	NumChannels = 1;
	bAutoActivate = false;
}

bool USmvMotorSynth::Init(int32& SampleRate)
{
	NumChannels = 1;
	Sr = (float)SampleRate;
	return true;
}

void USmvMotorSynth::SetParams(float Throttle01, float AirspeedMs, bool bArmed, float Doppler, float Gain)
{
	const float T = FMath::Clamp(Throttle01, 0.f, 1.f);
	const float A = FMath::Max(0.f, AirspeedMs);
	const float D = FMath::Clamp(Doppler, 0.5f, 2.f);
	const float G = FMath::Clamp(Gain, 0.f, 1.f);
	SynthCommand([this, T, A, bArmed, D, G]()
	{
		TgtThr = T; TgtAir = A; bOn = bArmed; Dop = D; TgtGain = G;
	});
}

void USmvMotorSynth::SetKind(ESmvEngineKind K, int32 Rotors)
{
	const int32 R = FMath::Clamp(Rotors, 1, 8);
	SynthCommand([this, K, R]() { Kind = K; NumRotors = R; });
}

float USmvMotorSynth::NextNoise()
{
	// xorshift32 → [-1, 1]
	Noise ^= Noise << 13; Noise ^= Noise >> 17; Noise ^= Noise << 5;
	return (float)(Noise & 0xffff) / 32767.5f - 1.f;
}

int32 USmvMotorSynth::OnGenerateAudio(float* Out, int32 NumSamples)
{
	const float TwoPi = 2.f * PI;
	for (int32 i = 0; i < NumSamples; ++i)
	{
		// parametre yumuşatma (≈ 50 ms)
		const float K = 1.f / (0.05f * Sr);
		CurThr += ((bOn ? TgtThr : 0.f) - CurThr) * K * 4.f;
		CurAir += (TgtAir - CurAir) * K;
		CurGain += (TgtGain - CurGain) * K;

		float Base = 0.f, Amp = 0.f, NoiseAmt = 0.f;
		switch (Kind)
		{
		case ESmvEngineKind::ElectricMulti:
			// 2 kanatlı pervane: RPM ≈ 3000 + 9000·gaz → BPF = RPM/60·2
			Base = (3000.f + 9000.f * CurThr) / 60.f * 2.f;
			Amp = bOn ? 0.15f + 0.6f * CurThr : 0.f;
			NoiseAmt = 0.25f + 0.5f * CurThr;
			break;
		case ESmvEngineKind::ElectricProp:
			Base = (2500.f + 7000.f * CurThr) / 60.f * 2.f;
			Amp = bOn ? 0.1f + 0.5f * CurThr : 0.f;
			NoiseAmt = 0.2f + 0.4f * CurThr;
			break;
		case ESmvEngineKind::Combustion:
			// iki zamanlı tek silindir: ateşleme frekansı = RPM/60
			Base = (2200.f + 7500.f * CurThr) / 60.f;
			Amp = bOn ? 0.25f + 0.55f * CurThr : 0.f;
			NoiseAmt = 0.35f;
			break;
		case ESmvEngineKind::Jet:
			Base = 180.f + 900.f * CurThr;
			Amp = bOn ? 0.1f + 0.3f * CurThr : 0.f;
			NoiseAmt = 0.9f;
			break;
		case ESmvEngineKind::Rover:
			Base = 60.f + 240.f * CurThr;
			Amp = bOn ? 0.2f + 0.4f * CurThr : 0.f;
			NoiseAmt = 0.1f;
			break;
		}
		Base *= Dop;
		float S = 0.f;
		// harmonikler (rotorlar arası hafif ayrışma → vuruş/çırpıntı)
		const int32 Voices = Kind == ESmvEngineKind::ElectricMulti ? FMath::Min(NumRotors, 6) : 3;
		for (int32 v = 0; v < Voices; ++v)
		{
			const float Detune = Kind == ESmvEngineKind::ElectricMulti ? 1.f + 0.011f * (v - Voices * 0.5f) : (float)(v + 1);
			Phase[v] += TwoPi * Base * Detune / Sr;
			if (Phase[v] > TwoPi) Phase[v] -= TwoPi;
			const float Saw = (Phase[v] / PI) - 1.f;   // testere: zengin harmonik
			S += (Kind == ESmvEngineKind::Combustion ? (Saw > 0.6f ? 1.f : -0.2f) : Saw) / (v + 1);
		}
		S *= Amp / FMath::Max(1, Voices) * 1.6f;
		// pervane/egzoz gürültüsü (alçak geçiren)
		NoiseLp += (NextNoise() - NoiseLp) * 0.15f;
		S += NoiseLp * NoiseAmt * Amp;
		// rüzgâr (hava hızıyla)
		WindLp += (NextNoise() - WindLp) * 0.02f;
		S += WindLp * FMath::Clamp(CurAir / 40.f, 0.f, 1.f) * 0.5f;
		Out[i] = FMath::Clamp(S * CurGain, -1.f, 1.f);
	}
	return NumSamples;
}
