// Simumav — gerçek fizik çarpışması (Unreal fizik/çarpışma sorguları; tahmin YOK).
//   • FSmvContactSolver: oyuncu dronunun gövde şekli (kutu bileşimi; pervane diskleri dahil) her karede önceki çözülmüş pozdan SITL'in
//     verdiği yeni poza SÜPÜRÜLÜR (SweepMultiByChannel — sürekli çarpışma algılama, tünelleme yok; uzun yol alt adımlara bölünür);
//     başlangıç çakışmasında motorun MTD'si (çıkış yönü + derinlik) ile yüzeyden dışarı itilir, temas yüzeyi boyunca kaydırılır.
//     Temas normali + çarpma hızı SmvCore::ClassifyContact ile sınıflanır (sürtünme / sert / tavan / iniş / düşüş / su).
//   • SmvPhysics::Scan: ışın izleriyle engel sensörleri — aşağı menzil bulucu (gövde ekseni), yukarı, 360° yakınlık (72 ya da 8 sektör).
//   • SmvPhysics::PrepareLevelCollision: Fab seviyelerinde kapalı çarpışmaları açar (ISM/HISM/Foliage dahil), ağ istatistiği yazar.
//   • SmvPhysics::EvaluateSpot: kalkış noktası (aşağı ışın → zemin, yukarı küre süpürmesi → üstü açık, kapsül çakışması → boşluk).
// Ayarlar: Config/DefaultGame.ini [/Script/Simumav.SmvPhysics]; hata ayıklama çizimi: konsol `smv.Fizik.Ciz 1|2` ya da Debug=1|2.
// Mimari ve sınırlar: docs/fizik-carpisma.md
#pragma once

#include "CoreMinimal.h"
#include "CollisionQueryParams.h"
#include "Engine/EngineTypes.h"
#include "Engine/HitResult.h"
#include "SmvCollision.h"

class UWorld;
class AActor;
class ULevel;

struct FSmvPhysicsSettings
{
	bool bEnabled = true;               // Enabled — kapalıysa eski davranış (yalnız yer kesişimi düzeltmesi)
	float ShapeScale = 1.f;             // ShapeScale — 1 = görünen gövde (görünürlük ×2,5 dahil); 0,4 ≈ gerçek boyut
	float SkinCm = 1.5f;                // SkinCm — temas sonrası yüzeyle bırakılan pay
	bool bTraceComplex = true;          // TraceComplex — karmaşık (üçgen) çarpışmaya karşı sorgu
	ECollisionChannel Channel = ECC_Visibility;   // Channel=Visibility|WorldDynamic|Camera
	int32 Sectors = 72;                 // Sectors — 72 (5°) ya da 8 (45°)
	float ProxMaxM = 30.f;              // ProxMaxM — yakınlık menzili
	float RngMaxM = 40.f;               // RngMaxM — aşağı menzil bulucu menzili (SITL RNGFND1_MAX ile aynı)
	float SensorHz = 15.f;              // SensorHz
	bool bSitlSensors = true;           // SitlSensors — DISTANCE_SENSOR / OBSTACLE_DISTANCE SITL'e (yalnız yerel)
	bool bSitlAvoid = true;             // SitlAvoid — ArduPilot AVOID_* (yakınlıkla kaçınma)
	float PenCrashM = 1.f;              // PenCrashM — sürtünmede SITL konumu yüzeyin bu kadar içine girerse sert çarpma
	float SustainCrashS = 3.f;          // SustainCrashS — sürekli temas süresi sınırı
	float RestDisarmS = 1.5f;           // RestDisarmS — yapı üstüne iniş: SITL yüzeyin altına inmeyi sürdürürse motor durdurma
	float ProxWarnM = 5.f;              // ProxWarnM — HUD yakınlık uyarısı
	int32 Debug = 0;                    // Debug — 0 kapalı, 1 gövde şekli, 2 + sensör ışınları
	static const FSmvPhysicsSettings& Get();
	/** Etkin hata ayıklama düzeyi (konsol smv.Fizik.Ciz ≥ 0 ise o, yoksa ayar). */
	static int32 DebugLevel();
};

/** Aktör uzayında kutu (cm). */
struct FSmvBodyBox
{
	FVector Center = FVector::ZeroVector;
	FVector Half = FVector(20.0);
};

struct FSmvContactEvent
{
	SmvCore::EContact Class = SmvCore::EContact::None;
	FString Kind;                        // terrain | building | tree | water | ceiling | obstacle
	FVector Point = FVector::ZeroVector;
	FVector Normal = FVector::UpVector;  // yüzey normali (UE dünya)
	double Nz = 1;                       // normal · yukarı
	double Vn = 0, Vt = 0, Speed = 0;    // m/s
	double PenetrationM = 0, DurationS = 0;
	FString Actor, Component, Material, PhysMaterial;
	bool bEscalated = false;             // sürekli temas → sert
	bool bRestDisarm = false;            // yapı üstüne iniş → motor durdurma isteği
	double Wall = 0;
	FString Describe() const;
};

class FSmvContactSolver
{
public:
	void Configure(const TArray<FSmvBodyBox>& InBoxes, SmvCore::EVehicleClass InVehicle);
	/** Sonraki karede ışınlanır (sıfırla / yeniden başlat). */
	void Reset();
	/** İstenen (SITL) konum → fizikle çözülmüş görüntü konumu. EnuRot: yerel ENU → UE (yukarı yönü); VelMs: UE dünyasında m/s. */
	FVector Resolve(UWorld* W, const AActor* Self, const FVector& Desired, const FQuat& Rot, const FQuat& EnuRot, const FVector& VelMs, bool bArmed);
	TArray<FSmvContactEvent> DrainEvents() { TArray<FSmvContactEvent> E = MoveTemp(Events); Events.Reset(); return E; }
	TArray<TWeakObjectPtr<AActor>>& IgnoreList() { return Ignore; }
	const TArray<FSmvBodyBox>& GetBoxes() const { return Boxes; }
	bool IsTouching() const { return bInContact; }
	bool IsOnFloor() const { return bInContact && CurClass == SmvCore::EContact::Landing; }
	SmvCore::EContact CurrentClass() const { return CurClass; }
	double CurrentPenetrationM() const { return PenM; }
	/** Gövde kutuları (yeşil serbest, sarı temas, kırmızı çarpma sınıfı) ve son temas normali. */
	void DrawDebug(UWorld* W, const FVector& Pos, const FQuat& Rot) const;

private:
	TArray<FSmvBodyBox> Boxes;
	SmvCore::EVehicleClass Vehicle = SmvCore::EVehicleClass::Vertical;
	TArray<TWeakObjectPtr<AActor>> Ignore;
	TArray<FSmvContactEvent> Events;
	FVector Resolved = FVector::ZeroVector;
	bool bHas = false;
	double MinHalfCm = 20;
	double RestPen0M = 0;                // yerde dururken olağan batma (gövde altı → aktör kökü)
	// temas durumu
	bool bInContact = false;
	SmvCore::EContact CurClass = SmvCore::EContact::None;
	double ContactStart = 0, LastTouch = 0, LastEvent = 0, RestSince = -1;
	bool bRestSent = false;
	double PenM = 0;
	FHitResult LastHit;
	bool SweepBody(UWorld* W, const FVector& From, const FVector& To, const FQuat& Rot, const FCollisionQueryParams& Q, FHitResult& OutHit, FVector& OutPos) const;
	bool Depenetrate(UWorld* W, FVector& Pos, const FQuat& Rot, const FCollisionQueryParams& Q, FHitResult* OutHit) const;
	void UpdateContact(bool bTouch, const FHitResult& H, const FVector& Desired, const FVector& Pos, const FQuat& EnuRot, const FVector& VelMs, bool bArmed);
};

struct FSmvSensorScan
{
	bool bValid = false;
	float DownM = -1.f;                  // gövde ekseni boyunca aşağı (menzil dışı −1)
	float UpM = -1.f;                    // yukarı
	int32 Sectors = 0;
	TArray<float> SectorM;               // 0 = burun, saat yönünde (MAVLink BODY_FRD); −1 engel yok
	float NearestM = -1.f;
	float NearestBearingDeg = 0.f;
};

namespace SmvPhysics
{
	FCollisionQueryParams MakeQuery(FName Tag, const AActor* Self, const TArray<TWeakObjectPtr<AActor>>* Ignore);
	/** Engel sensörleri (ışın izleri). bDraw: ışınları çiz (hata ayıklama). */
	void Scan(UWorld* W, const FVector& Origin, const FQuat& BodyRot, const FQuat& EnuRot, const FCollisionQueryParams& Q, FSmvSensorScan& Out, bool bDraw);
	/** Yüzey türü (bileşen etiketi, sınıf, ağ/aktör adı): terrain | building | tree | water | obstacle */
	FString SurfaceKind(const FHitResult& H, const FVector& Up);
	FString MaterialName(const FHitResult& H);
	/** Fab/şehir seviyesi: görünür statik ağ bileşenlerinin (ISM/HISM/Foliage dahil) sorgu çarpışmasını açar; ağ istatistiği döner. */
	FString PrepareLevelCollision(ULevel* Level);
	/** Kalkış noktası (fizik): (X, Y) için aşağı ışın → zemin; normal düzlüğü; yukarı küre süpürmesi → üstü açık (tavan / dal);
	 *  araç boyutunda kapsül çakışması → boşluk; çevre düzlüğü. bRequireLandscape: zemin Landscape olmalı. */
	bool EvaluateSpot(UWorld* W, double X, double Y, double ZTop, double ZBot, float RadiusCm, float HeightCm, bool bRequireLandscape,
		const FCollisionQueryParams& Q, double& OutZ, FString* OutWhy = nullptr);
}
