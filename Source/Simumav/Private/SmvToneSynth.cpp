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

void USmvToneSynth::Sfx(int32 Kind, float Pitch)
{
	const float P = FMath::Clamp(Pitch, 0.25f, 4.f);
	SynthCommand([this, Kind, P]() { BlipKind = Kind; BlipT = 0.0; BlipPitch = P; });
}

void USmvToneSynth::SetUiGain(float InGain)
{
	const float G = FMath::Clamp(InGain, 0.f, 1.f);
	SynthCommand([this, G]() { UiGain = G; });
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
		if (A > 0.f)
		{
			Ph += UE_TWO_PI * F / Sr;
			if (Ph > UE_TWO_PI) Ph -= UE_TWO_PI;
			S = FMath::Sin(Ph) * A * Gain;
		}
		if (BlipKind)
		{
			float Bf = 0.f, Ba = 0.f, Bg = Gain;
			const float Tt = (float)BlipT;
			if (BlipKind <= 4)
			{
				const double D = BlipKind == 1 ? 0.18 : BlipKind == 4 ? 0.5 : 0.1;
				if (BlipT < D) { Bf = BlipKind == 1 ? 900.f + 1200.f * (float)(BlipT / D) : BlipKind == 2 ? 2000.f : BlipKind == 3 ? 700.f : 1100.f; Ba = 0.45f; }
				else BlipKind = 0;
			}
			else if (BlipKind >= 11)
			{
				// oyun efektleri: nota dizileri (yarıton adımlarıyla), gürültü süpürmeleri
				auto Note = [](float Semi) { return 440.f * FMath::Pow(2.f, Semi / 12.f); };
				float D = 0.3f, Amp = 0.32f;
				bool bNoise = false;
				switch (BlipKind)
				{
				case 11: D = 0.16f; Bf = 880.f * BlipPitch * (Tt < 0.05f ? 1.f : 1.5f); break;
				case 12: { static const float Seq[4] = { 3.f, 7.f, 10.f, 15.f }; D = 0.72f; Bf = Note(Seq[FMath::Min(3, (int32)(Tt / 0.18f))]); break; }
				case 13: D = 0.35f; bNoise = true; Bf = 0.f; Amp = 0.18f; break;
				case 14: D = 0.4f; Bf = 600.f - 900.f * Tt; Amp = 0.22f; break;
				case 15: D = 0.25f; Bf = FMath::Fmod(Tt, 0.08f) < 0.04f ? 1500.f : 0.f; break;
				case 16: { static const float Seq[5] = { 0.f, 4.f, 7.f, 12.f, 16.f }; D = 0.9f; Bf = Note(Seq[FMath::Min(4, (int32)(Tt / 0.15f))]); break; }
				case 17: { static const float Seq[4] = { 7.f, 3.f, 0.f, -5.f }; D = 0.8f; Bf = Note(Seq[FMath::Min(3, (int32)(Tt / 0.2f))]) * 0.5f; break; }
				case 18: D = 0.22f; Bf = Tt < 0.07f ? 1976.f : 2637.f; Amp = 0.2f; break;
				case 19: D = 0.45f; bNoise = true; Amp = 0.45f; break;
				default: { static const float Seq[6] = { 0.f, 4.f, 7.f, 12.f, 7.f, 12.f }; D = 1.0f; Bf = Note(Seq[FMath::Min(5, (int32)(Tt / 0.16f))]); break; }
				}
				if (Tt < D)
				{
					const float Env = FMath::Min(1.f, Tt / 0.005f) * (1.f - Tt / D);
					if (bNoise)
					{
						Noise = Noise * 1664525u + 1013904223u;
						const float N = ((Noise >> 9) & 0x7fff) / 16384.f - 1.f;
						Lp += (N - Lp) * (BlipKind == 19 ? 0.08f : 0.25f + 0.6f * (Tt / D));
						S += Lp * Amp * Env * Gain;
						Ba = 0.f;
					}
					else Ba = Bf > 0.f ? Amp * Env : 0.f;
				}
				else BlipKind = 0;
			}
			else
			{
				// arayüz: yumuşak zarflı kısa tonlar (web sfx.js hover/select/back/sweep)
				Bg = UiGain;
				float D = 0.05f;
				switch (BlipKind)
				{
				case 5: D = 0.035f; Bf = 2400.f; break;                                 // üzerine gel
				case 6: D = 0.085f; Bf = Tt < 0.035f ? 900.f : 1350.f; break;          // tık (iki nota)
				case 7: D = 0.09f; Bf = 1100.f - 4000.f * Tt; break;                   // geri (inen)
				case 8: D = 0.16f; Bf = 500.f + 6000.f * Tt; break;                    // aç (yükselen süpürme)
				case 9: D = 0.04f; Bf = 1700.f; break;                                  // değiştir
				default: D = 0.18f; Bf = Tt < 0.09f ? 300.f : 220.f; break;            // hata
				}
				if (Tt < D)
				{
					const float Env = FMath::Min(1.f, Tt / 0.004f) * FMath::Exp(-Tt / (D * 0.45f));
					Ba = (BlipKind == 5 ? 0.12f : BlipKind == 10 ? 0.3f : 0.22f) * Env;
				}
				else BlipKind = 0;
			}
			if (Ba > 0.f)
			{
				BlipPh += UE_TWO_PI * Bf / Sr;
				if (BlipPh > UE_TWO_PI) BlipPh -= UE_TWO_PI;
				S += FMath::Sin(BlipPh) * Ba * Bg;
			}
			BlipT += Dt;
		}
		Out[i] = S;
		T += Dt;
	}
	return NumSamples;
}
