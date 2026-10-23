// HAZIR FPV PARKURLARI (web §47 / sunucu app/flightsim_fpv.py eşi) — 8 parkur: hangar, orman, yarı yıkık şehir, stadyum, rafineri, kanyon,
// tepedeki ahşap ev iskeleti, yarım beton bina. Tanımlar sunucunun ürettiği JSON'dan (Content/Simumav/Fpv/courses.json; Tools/fpv/parkur_aktar.py)
// okunur — kapılar, engeller, dekor, başlangıç, tur, zorluk, ortam birebir aynı. Yerel zaman denemesi: parkur izleyici (kapı geçişi doğru yönde
// + açıklık içinde, en çok 2 kapı ileri bakışla kaçırma cezası, engel çarpması kuralı penalty | reset | continue, tur ve sektör süreleri),
// kişisel rekor hayaleti (10 Hz örnek; Saved/Simumav/Fpv/<parkur>.json). Koordinat: parkur merkezine göre ENU m (x doğu, y kuzey, z zeminden),
// yaw pusula ° (0 kuzey, saat yönü); ileri = (sin, cos), sağ = (cos, −sin). Hedefe dalış / çarpma senaryosu yoktur (yalnız spor / beceri).
#pragma once

#include "CoreMinimal.h"

struct SIMUMAV_API FSmvFpvGate
{
	FString Kind;            // start | gate | circle | window | dive | ladder | tunnel | flag | slalom
	double X = 0, Y = 0, Z = 0, Yaw = 0, W = 3, H = 3, R = -1, A = -1;
	bool bFrame = true, bSectorEnd = false;
	TArray<FVector> Via, ViaLap;
	FString Side, Label;
	bool bPole = false;
	double PoleX = 0, PoleY = 0, PoleR = 0.09;
};

struct SIMUMAV_API FSmvFpvHole { double U = 0, V = 0, W = 0, H = 0; };

struct SIMUMAV_API FSmvFpvObst
{
	FString T;               // box | cyl | sph | pipe | torus
	FString Mat, Part;
	double X = 0, Y = 0, Z = 0, W = 0, D = 0, H = 0, Yaw = 0, R = 0, RR = 0, Tube = 0;
	FVector A = FVector::ZeroVector, B = FVector::ZeroVector;
	TArray<FSmvFpvHole> Holes;
	FString Axis = TEXT("y");
	bool bVis = true, bFlat = false;
	int32 Gate = -1;
};

struct SIMUMAV_API FSmvFpvCourse
{
	FString Id, Venue;
	int32 Difficulty = 1, Laps = 1, GateCount = 0, Sectors = 1;
	double ParLapS = 30, LengthM = 0;
	TArray<FString> Kinds;
	TArray<FSmvFpvGate> Gates;
	TArray<FSmvFpvObst> Obstacles, Decor;
	double StartX = 0, StartY = 0, StartYaw = 0;
	FString EnvTime = TEXT("day"), EnvSky, EnvGround;
	double EnvFog = 0.1, Ceiling = 0;
	FVector2D Size = FVector2D(100, 100);
	bool bCanyon = false, bHill = false;
	FText Name() const;
	FText VenueName() const;
};

/** Kontrol noktası: [S/F] + (kapı 1…m−1 + S/F) × tur */
struct SIMUMAV_API FSmvFpvCheckpoint
{
	int32 I = 0, Gate = 0, Lap = 0, Sector = 0;
	bool bSectorEnd = false;
};

struct SIMUMAV_API FSmvFpvCfg
{
	FString CourseId;
	int32 Laps = 2;
	FString CrashRule = TEXT("penalty");   // penalty | reset | continue
	double CrashPenS = 2.0, MissPenS = 3.0;
	int32 TimeLimitMin = 10;
	bool bGhost = true;
	bool IsActive() const { return !CourseId.IsEmpty(); }
};

struct SIMUMAV_API FSmvFpvEvent
{
	FString Kind;            // ring | miss | sector | lap | hit | reset | wrong_way | finish
	int32 I = 0, Gate = 0, Lap = 0, Sector = 0, Obstacle = -1;
	double T = 0, S = 0, PenS = 0, TotalS = 0;
	bool bBest = false;
	FString Mat, Rule;
};

struct SIMUMAV_API FSmvFpvGhostSample { float T = 0, X = 0, Y = 0, Z = 0, Yaw = 0; };

/** Parkur izleyici (sunucu ParkurIzleyici eşi). Zaman yarış başlangıcına (t0) göre sn. */
class SIMUMAV_API FSmvFpvTracker
{
public:
	FSmvFpvTracker(const FSmvFpvCourse& InCourse, const FSmvFpvCfg& InCfg, double InT0);
	/** p: parkur koordinatı (m, z zeminden), yaw derece */
	TArray<FSmvFpvEvent> Update(const FVector& P, double T, float YawDeg);
	bool Finished() const { return Seq >= Points.Num(); }
	int32 Lap() const;
	int32 NextIndex() const { return Seq; }
	int32 NextGate() const { return Points.IsValidIndex(Seq) ? Points[Seq].Gate : 0; }
	int32 Total() const { return Points.Num(); }
	double DistNext() const { return Dist; }
	double Penalty() const { return Pen; }
	int32 Misses() const { return Missed.Num(); }
	int32 Hits() const { return HitCount; }
	double BestLap() const;
	double TotalS() const { return FinishT >= 0 ? FinishT - T0 + Pen : -1; }
	double LapStart() const { return LapT0; }
	const TArray<double>& Laps() const { return LapTimes; }
	const TArray<TArray<double>>& SectorTimes() const { return Sectors; }
	const TMap<int32, double>& BestSectors() const { return BestSector; }
	const TArray<double>& Splits() const { return SplitT; }
	const TArray<FSmvFpvGhostSample>& Samples() const { return Ghost; }
	const FSmvFpvCourse& Course() const { return C; }
	const FSmvFpvCfg& Cfg() const { return K; }
	/** Kapı durumu (0 bekliyor, 1 sıradaki, 2 sonraki 1–2, 3 geçildi) — görsel */
	int32 GateState(int32 GateIdx) const;

private:
	FSmvFpvCourse C;
	FSmvFpvCfg K;
	TArray<FSmvFpvCheckpoint> Points;
	int32 Seq = 0;
	bool bHasLast = false;
	FVector Last = FVector::ZeroVector;
	double T0 = 0, FinishT = -1, Pen = 0, Dist = 0;
	TArray<int32> Missed;
	int32 HitCount = 0;
	bool bContact = false;
	double ContactT = -1e9, LapT0 = -1, SectorT0 = -1, SampleT = -1e9;
	TArray<double> LapTimes, SplitT;
	TArray<TArray<double>> Sectors;
	TMap<int32, double> BestSector;
	TArray<FSmvFpvGhostSample> Ghost;
	void Advance(double T, bool bPassed, TArray<FSmvFpvEvent>& Ev);
};

/** Kişisel rekor (yerel) */
struct SIMUMAV_API FSmvFpvRecord
{
	double TotalS = -1, BestLapS = -1;
	int32 Laps = 0, Runs = 0;
	FString Date;
	TArray<FSmvFpvGhostSample> Ghost;
};

namespace SmvFpv
{
	SIMUMAV_API const TArray<FSmvFpvCourse>& Courses();
	SIMUMAV_API const FSmvFpvCourse* Find(const FString& Id);
	SIMUMAV_API TArray<FSmvFpvCheckpoint> Checkpoints(const FSmvFpvCourse& C, int32 Laps);
	/** Dikey kapıda (normal, sağ, yukarı); dalışta normal (0,0,−1) */
	SIMUMAV_API void GateAxes(const FSmvFpvGate& G, FVector& N, FVector& U, FVector& V);
	/** p0 → p1 kapı düzlemi kesişimi: Dir +1 ileri, −1 ters, 0 yok; bInside açıklık içinde */
	SIMUMAV_API int32 GateCross(const FVector& P0, const FVector& P1, const FSmvFpvGate& G, bool& bInside);
	/** Nokta engelin içinde mi (Inf: dron yarıçapı kadar şişirme) */
	SIMUMAV_API bool InObstacle(const FVector& P, const FSmvFpvObst& E, double Inf);
	/** Parça boyunca ilk temas eden engelin sırası ya da −1 */
	SIMUMAV_API int32 SegmentHit(const TArray<FSmvFpvObst>& Obs, const FVector& P0, const FVector& P1, double Inf);
	SIMUMAV_API FVector Forward(double YawDeg);
	SIMUMAV_API FVector Right(double YawDeg);
	/** Puan (TOP 10 FPV bileşeni gösterimi): 250 × par / tur ortalaması, en çok 375 */
	SIMUMAV_API int32 Score(const FSmvFpvCourse& C, double TotalS, int32 Laps);
	SIMUMAV_API FSmvFpvRecord LoadRecord(const FString& CourseId, int32 Laps);
	SIMUMAV_API void SaveRecord(const FString& CourseId, int32 Laps, const FSmvFpvRecord& R);
	/** Malzeme rengi (görsel) */
	SIMUMAV_API FLinearColor MatColor(const FString& Mat);
}
