// SIMUMAV — pilot ilerlemesi (yol haritası "Mürettebat"): deneyim puanı (XP), seviye 1…100, madalyalar, uçuş sonrası manevra çözümlemesi.
// Seviye eğrisi ve madalya eşikleri sunucuyla BİREBİR (app/simumav/xp.py: gereken(L) = round(100 × (L−1)^1.8)); tür puanı bileşenleri
// app/simumav/modes.py ucus_bilesenleri / puanla ile aynı kurallar. Manevra puanı yalnız istemcinin yerel XP'sine eklenir (misafir kip);
// hesap kipinde XP'yi sunucu hesaplar (istemci yalnız gösterir). Yalnız C++17, Unreal'sız (tests/cpp/test_ilerleme.py).
#pragma once

#include <string>
#include <vector>

namespace SmvCore
{
	constexpr int XP_MAX_LEVEL = 100;
	constexpr int XP_MIN_AIR_S = 10;

	/** Seviyeye ulaşmak için gereken TOPLAM XP (seviye 1 = 0; seviye 100 ≈ 390 bin) — xp.gereken */
	long long XpForLevel(int Level);
	/** Toplam XP → seviye (1…100) — xp.seviye */
	int LevelForXp(long long Xp);
	/** Seviye içi ilerleme 0..1 (100. seviyede 1) */
	double LevelProgress(long long Xp);

	struct FMedal { const char* Id; int Level; };
	/** Madalya eşikleri (xp.MEDALS) */
	const std::vector<FMedal>& Medals();

	/** Uçuş özeti (istemci yerel karşılığı; sunucu flights.summary alanlarıyla aynı anlam) */
	struct FFlightFacts
	{
		std::string Mode = "free";      // free | rings | cargo_drop | spraying | dogfight | facility_defense | ship_defense
		std::string Result = "ok";      // ok | crash | abort
		double AirS = 0, DistM = 0, DurationS = 0;
		int AlertScore = -1;            // uyarı disiplini puanı (0..100; −1 yok)
		bool bLandedSoft = false;       // kazasız iniş (ARM kapalı, irtifa ≤ 2 m)
		bool bInCircle = false;
		int RingsPassed = 0;
		int RaceRank = 0;               // 0 yok; 1. 2. 3. …
		bool bRaceFinished = false, bWon = false;
		bool bBotsOnly = true;          // rakiplerin hepsi yapay zekâ
		int MatchPlayers = 1;
		// tür puanları (yerel oyun mantığı SmvGameRules)
		bool bReleased = false; double DropBestM = -1;   // yük bırakma: en iyi isabet (m)
		double SprayCoverage = -1;                         // 0..1
		double DefenseScore = -1; bool bDefenseOk = false; // savunma puanı (ham) + başarı
		int TargetScore = 0;
	};

	/** XP bileşeni (anahtar → puan) */
	struct FXpPart { std::string Key; int Xp = 0; };

	/** Tür puanı bileşenleri (modes.puanla + mac_puani) */
	std::vector<FXpPart> ModeXp(const FFlightFacts& F);

	/** Manevra çözümlemesi için 5 Hz iz karesi (FSmvReplayFrame alt kümesi) */
	struct FTrackSample
	{
		double T = 0, AltRel = 0;
		double Roll = 0, Pitch = 0, Yaw = 0;   // derece
		double Climb = 0, Gs = 0;              // m/s
		bool bArmed = false;
	};

	/** Tanınan manevra: anahtar (dil dosyasında maneuver.<anahtar>), sayı, toplam XP */
	struct FManeuver { std::string Key; int Count = 0; int Xp = 0; };

	/** İzden manevralar: keskin dönüş, takla (roll), luping, hızlı tırmanış, dalış, alçak geçiş, yüksek hız, sabit askı, yumuşak iniş.
	 *  Her manevra türünün XP'si sınırlıdır (çiftlik önlemi). Hava süresi XP_MIN_AIR_S altındaysa boş. */
	std::vector<FManeuver> AnalyzeManeuvers(const std::vector<FTrackSample>& S);

	/** Uçuşun toplam XP'si (tür + manevra); OutParts ayrıntı (manevralar "m:<anahtar>" önekiyle) */
	int FlightXp(const FFlightFacts& F, const std::vector<FTrackSample>& Track, std::vector<FXpPart>& OutParts, std::vector<FManeuver>& OutManeuvers);
}
