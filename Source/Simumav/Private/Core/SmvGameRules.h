// SIMUMAV — oyun türleri (yol haritası "Uçuş › Oyun türü") yerel oyun mantığı: yük bırakma (hedef bölgeleri + balistik yük + isabet puanı),
// tarla ilaçlama (kapsama ızgarası + puan), kritik tesis / gemi savunma (dalgalar halinde gelen hedef dronlar, korunacak tesis/gemi sağlığı).
// Tür kimlikleri ve AI/puan kuralları sunucu app/simumav/modes.py ile aynı. ANGAJMAN KURALI (§40, bağlayıcı): savunmada YALNIZ oyuncu
// kontrolü — "kilit" yalnız HUD göstergesidir (oyuncunun burnunun önündeki en yakın tehdit), puan pilotun aracını tehdide yaklaştırmasıyla
// (temas/yakın geçiş) sayılır. Otonom güdüm, otomatik nişan, hedefe kendiliğinden yönelme ya da silah/mühimmat davranışı YOKTUR.
// Koordinatlar: ev noktasına göre ENU metre (E doğu, N kuzey, U zemin üstü). Yalnız C++17, Unreal'sız (tests/cpp/test_oyun.py).
#pragma once

#include <string>
#include <vector>

namespace SmvCore
{
	enum class EGameKind { Free, Rings, CargoDrop, Spraying, Dogfight, FacilityDefense, ShipDefense };

	/** Sunucu tür kimliği (modes.MODES anahtarı): free | rings | cargo_drop | spraying | dogfight | facility_defense | ship_defense */
	const char* GameKindId(EGameKind K);
	EGameKind GameKindFromId(const std::string& Id);
	/** Karşılaşma motoru (halka yarışı / it dalaşı) mı */
	bool GameKindIsMatch(EGameKind K);
	/** Tek oyunculuda otomatik eklenen yapay zekâ pilot sayısı (modes ai.single; savunmada tehdit sayısı ayrı) */
	int GameKindSingleAi(EGameKind K);
	/** Savunma dalga planı: dalga i (0'dan) tehdit sayısı = min(8, 2 + i) — modes.ai_onerisi */
	int DefenseWaveSize(int Wave);
	int DefenseThreatTotal(int Waves);

	struct FGameEvent
	{
		std::string Kind;
		int I = 0;
		double Value = 0, E = 0, N = 0;
	};

	// ------------------------------------------------------------------ yük bırakma
	struct FDropCfg
	{
		int Payloads = 6;
		int Zones = 4;
		double AreaMinR = 90, AreaMaxR = 260;
		double ZoneR = 15;            // görsel hedef halkası (isabet eşikleri ayrıca 5 / 15 / 30 m)
		double DragK = 0.12;          // doğrusal sürükleme (1/sn)
		unsigned Seed = 1;
	};
	struct FDropZone { double E = 0, N = 0, R = 15; int Hits = 0; double BestM = -1; };
	struct FPayload
	{
		double E = 0, N = 0, U = 0, Ve = 0, Vn = 0, Vu = 0, T0 = 0;
		bool bLanded = false;
		int Zone = -1, Points = 0;
		double DistM = -1;
	};
	class FDropGame
	{
	public:
		void Setup(const FDropCfg& C);
		/** Yükü bırak (araç konumu + hızı). Hak kalmadıysa false. */
		bool Release(double T, double E, double N, double U, double Ve, double Vn, double Vu);
		/** Düşen yükleri ilerletir; yere değen yük puanlanır ("land" olayı: I yük, Value mesafe, E/N iniş noktası). GroundU: zemin (ev zeminine göre m) */
		void Step(double Dt, std::vector<FGameEvent>& Ev, double GroundU = 0.0);
		const std::vector<FDropZone>& Zones() const { return Z; }
		const std::vector<FPayload>& Payloads() const { return P; }
		int Left() const { return Cfg.Payloads - (int)P.size(); }
		int Score() const;
		/** En iyi isabet (m; −1 yok) */
		double BestM() const;
		bool Released() const { return !P.empty(); }
		bool Done() const;
		/** Tek isabet puanı (modes.puanla accuracy eşikleri) */
		static int PointsFor(double DistM);
	private:
		FDropCfg Cfg;
		std::vector<FDropZone> Z;
		std::vector<FPayload> P;
	};

	// ------------------------------------------------------------------ tarla ilaçlama
	struct FSprayCfg
	{
		double CenterE = 0, CenterN = 140, W = 70, L = 120, HeadingDeg = 0;   // tarla dikdörtgeni (L boyu heading yönünde)
		double Cell = 4;
		double SwathW = 6;            // püskürtme genişliği (m; aparat seçeneği width_m)
		double MinAgl = 1.0, MaxAgl = 8.0, MaxGs = 12.0;
		double TankL = 10, RateLps = 0.12;
	};
	class FSprayGame
	{
	public:
		void Setup(const FSprayCfg& C);
		/** Bir adım: püskürtme açıksa ve irtifa/hız aralıktaysa altındaki hücreler işaretlenir; olaylar: too_high, too_fast, tank_empty, done */
		void Step(double T, double Dt, double E, double N, double Agl, double Gs, bool bSpraying, std::vector<FGameEvent>& Ev);
		int Cols() const { return Nx; }
		int Rows() const { return Ny; }
		bool Covered(int Cx, int Cy) const { return Grid[(size_t)Cy * Nx + Cx] != 0; }
		/** Hücre merkezi (ENU) */
		void CellCenter(int Cx, int Cy, double& E, double& N) const;
		double Coverage() const;
		double TankLeft() const { return Tank; }
		int OversprayCells() const { return Over; }
		int Score() const;
		bool Done() const { return bDone; }
		/** Son adımda yeni işaretlenen hücreler (görsel güncelleme için; sonraki Step'te temizlenir) */
		const std::vector<int>& NewCells() const { return Fresh; }
		const FSprayCfg& Config() const { return Cfg; }
	private:
		FSprayCfg Cfg;
		int Nx = 0, Ny = 0, Over = 0, Marked = 0;
		std::vector<unsigned char> Grid;
		std::vector<int> Fresh;
		double Tank = 0, LastWarnT = -100, OverAcc = 0;
		bool bDone = false, bEmptySaid = false;
		bool ToLocal(double E, double N, double& U, double& V) const;
	};

	// ------------------------------------------------------------------ savunma (tesis / gemi)
	struct FDefCfg
	{
		int Waves = 3;
		double SpawnR = 1300, Speed = 17, AltM = 45;
		double ContactR = 8;          // pilotun aracı tehdide bu kadar yaklaşınca etkisiz (temas)
		double HitR = 35;             // tehdit tesise bu kadar yaklaşınca isabet
		double FacE = 0, FacN = 160, FacU = 0;
		int Hp = 100, DamagePerHit = 20;
		double WaveGap = 14, FirstAt = 6;
		bool bShip = false;
		double ShipSpeed = 2.5, ShipHeadingDeg = 90;
		double LockR = 380, LockConeDeg = 22;
		unsigned Seed = 1;
	};
	struct FThreat
	{
		int Id = 0, Wave = 0;
		double E = 0, N = 0, U = 0, Ve = 0, Vn = 0, Vu = 0, Phase = 0;
		bool bAlive = false, bNeutralized = false, bImpact = false;
	};
	class FDefenseGame
	{
	public:
		void Setup(const FDefCfg& C);
		/** Adım: tehditleri ilerletir, temas/isabet olayları ("spawn", "wave", "contact", "impact", "lock", "unlock", "done"). PlayerYawDeg: pusula (kuzeyden saat yönü). */
		void Step(double T, double Dt, double PE, double PN, double PU, double PlayerYawDeg, std::vector<FGameEvent>& Ev);
		const std::vector<FThreat>& Threats() const { return Th; }
		int Hp() const { return HpNow; }
		int Neutralized() const { return Neut; }
		int Impacts() const { return Imp; }
		int Wave() const { return WaveNow; }
		int Waves() const { return Cfg.Waves; }
		/** HUD kilidi (yalnız gösterge): burnun önündeki aday tehdit (0 yok) ve aday süresi; oyuncu kilit tuşuyla onaylar (PlayerLock) */
		int LockId() const { return Lock; }
		/** Oyuncu kilit tuşu: adayı "kilitli" işaretler (yalnız HUD + temas puanı +50); aday değişince düşer */
		void PlayerLock(bool bOn) { bPlayerLock = bOn && Lock != 0; }
		bool IsPlayerLocked() const { return bPlayerLock && Lock != 0; }
		double LockHeldS() const { return LockS; }
		double FacilityE() const { return FacE; }
		double FacilityN() const { return FacN; }
		int Score() const;
		bool Done() const { return bDone; }
		bool Success() const { return bDone && HpNow > 0; }
		int Total() const { return DefenseThreatTotal(Cfg.Waves); }
	private:
		FDefCfg Cfg;
		std::vector<FThreat> Th;
		double FacE = 0, FacN = 0, T0 = -1, LastT = 0;
		int HpNow = 100, Neut = 0, Imp = 0, LockedContacts = 0, WaveNow = 0, Spawned = 0, NextId = 1, Lock = 0;
		double LockS = 0;
		bool bDone = false, bPlayerLock = false;
		unsigned Rng = 1;
		double Rand01();
		void SpawnWave(int W, std::vector<FGameEvent>& Ev);
	};
}
