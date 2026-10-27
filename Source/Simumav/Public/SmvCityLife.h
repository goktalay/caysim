// Şehir hayatı (yalnız görsel): Fab "City Traffic Pro" araç modelleriyle sokak trafiği ve yaya kalabalığı.
//   - Trafik: kodla kurulan şehir ızgarasında (SpawnFabCity) şeritlerde ilerleyen araçlar — öndeki araçla güvenli aralık, kavşaklarda
//     iki evreli ışık (X / Y caddeleri sırayla, arada tümü kırmızı), şehir kenarında karşı uca sarma; tekerlek kemikleri döner.
//     Araç fiziği / ChaosVehicles ve paketin trafik Blueprint'leri kullanılmaz: yalnız ağ + malzeme klasörleri pişirilir.
//   - Özel parkur › Şehir (prosedürel dünya): park etmiş araçlar ve yaya grupları.
//   - Kalabalık: OverCrowd VAT kalabalığı (/Game/Fab/Crowd: OverCrowd Pure WPO VAT ağları + M_SmvCrowdVat; örnekli statik ağ,
//     yürüme / bekleme ve giysi rengi örnek başına veride) varsa onunla; yoksa paketteki
//     insan iskeletli ağları (UE5 Manny / Quinn, yürüme / bekleme animasyonu) — kaldırımlarda yürüyen, meydanda duran gruplar.
// Paketler yoksa hiçbir şey kurulmaz (çağıran temel şekil dekoruna düşer). Hepsi yalnız görseldir: çarpışma, davranış, hedef yok.
// Pişirilen klasörler SmvCityLife.cpp PISIR-KLASORLERI bloğunda (CI okur; dron gövdesi klasörleri de orada).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SmvCityLife.generated.h"

class UStaticMesh;
class USkeletalMesh;
class UAnimSequence;
class UPoseableMeshComponent;
class USkeletalMeshComponent;
class UMeshComponent;
class UInstancedStaticMeshComponent;

struct SIMUMAV_API FSmvCityLife
{
	/** Araç modelleri (City Traffic Pro iskeletli araç ağları; 2,8–8 m). Yoksa boş. */
	static const TArray<USkeletalMesh*>& CarModels();
	/** Yaya modelleri (iskeletli insan ağları) ve animasyonlar (yürüme / bekleme). */
	static const TArray<USkeletalMesh*>& PeopleModels();
	static UAnimSequence* WalkAnim(const USkeletalMesh* For);
	static UAnimSequence* IdleAnim(const USkeletalMesh* For);
	/** OverCrowd VAT kalabalık ağları (/Game/Fab/Crowd; CI'da OverCrowd ile pişirilir). Yoksa boş. */
	static const TArray<UStaticMesh*>& VatCrowdMeshes();
	static bool HasCrowd() { return VatCrowdMeshes().Num() > 0 || PeopleModels().Num() > 0 || HasCsCrowd(); }
	/** City Sample Crowds (Epic) modüler MetaHuman yayaları pakette mi */
	static bool HasCsCrowd();
	/** Pişirilecek klasörler (CI bloğu) */
	static TArrayView<const TCHAR* const> CookDirs();
	/** [/Script/Simumav.SmvCityLife] ayarları */
	static float GetFloat(const TCHAR* Key, float Default);
};

UCLASS()
class SIMUMAV_API ASmvCityLife : public AActor
{
	GENERATED_BODY()

public:
	ASmvCityLife();
	virtual void Tick(float DeltaSeconds) override;

	/** Izgara şehir trafiği: X ve Y yönlü caddeler Coords konumlarında (cm, aktöre göre), cadde genişliği RoadCm, şehir yarı boyu HalfCm.
	 *  Cars: toplam araç. false → model yok. */
	bool BuildGridTraffic(const TArray<double>& Coords, double RoadCm, double HalfCm, int32 Cars, int32 Seed);
	/** Park etmiş araç (statik poz; dünya konumu zemin, Yaw derece). */
	bool AddParkedCar(const FVector& World, float Yaw, int32 Pick);
	/** Kalabalık: Walk = yürüme yolları (dünya cm, kapalı döngüler), Spots = duran grup merkezleri; Count kişi. Kurulan kişi sayısı. */
	int32 BuildCrowd(const TArray<TArray<FVector>>& Walk, const TArray<FVector>& Spots, int32 Count, int32 Seed);
	int32 NumCars() const { return CarComps.Num(); }
	int32 NumPeople() const { return PeopleComps.Num() + VatPeople; }

protected:
	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TArray<TObjectPtr<UMeshComponent>> CarComps;
	UPROPERTY() TArray<TObjectPtr<USkeletalMeshComponent>> PeopleComps;
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> CrowdHisms;
	/** City Sample yaya parçaları (giysi / ayakkabı / yüz / saç; gövdenin pozunu izler) */
	UPROPERTY() TArray<TObjectPtr<USkeletalMeshComponent>> CsParts;
	UPROPERTY() TArray<TObjectPtr<class UStaticMeshComponent>> CsStatic;   // OverCrowd VAT kalabalığı: ağ başına bir ISM

private:
	struct FLane
	{
		int32 Axis = 0;          // 0: araç Y boyunca (cadde X = Fixed), 1: araç X boyunca (cadde Y = Fixed)
		double Fixed = 0;        // şerit merkez çizgisi (cm)
		int32 Dir = 1;           // +1 / -1
		double Len = 0;          // şerit boyu (cm)
		TArray<double> Stops;    // dur çizgileri (s, cm)
		TArray<int32> Cars;      // araçlar (artan s; döngüsel sıra korunur)
	};
	struct FCar
	{
		int32 Lane = -1;
		double S = 0, V = 0, VMax = 1100;
		double LenCm = 450;
		double WheelPhase = 0, WheelR = 33;
		TArray<FName> Wheels;
		TArray<FQuat> WheelRef;
		FVector LocalOff = FVector::ZeroVector;   // ağ merkezini şerit noktasına getiren ofset (ölçekli, aktör yönünde)
		float YawFix = 0.f;
	};
	struct FWalker
	{
		int32 Comp = -1, Path = 0;
		int32 Ism = -1, Inst = -1;   // VAT kalabalığı: ISM dizini ve örnek (Comp yerine)
		double S = 0, V = 140;
		int32 Dir = 1;
		double Side = 0;   // yol merkezinden yanal kayma (cm)
	};
	TArray<FLane> Lanes;
	TArray<FCar> CarState;
	TArray<TArray<FVector>> WalkPaths;
	TArray<double> WalkLen;
	TArray<FWalker> Walkers;
	double Half = 0, Road = 0, T = 0;
	int32 VatPeople = 0;
	bool bCsDiag = false;
	UMeshComponent* MakeCar(USkeletalMesh* Model, FCar& C);
	void PlaceCar(int32 i, const FVector& At, float Yaw);
	FVector LanePoint(const FLane& L, double S, float& OutYaw) const;
	bool Green(int32 Axis) const;
	FVector PathPoint(int32 Path, double S, FVector& OutDir) const;
	/** OverCrowd VAT kalabalığı (ISM + M_SmvCrowdVat; iskelet / animasyon güncellemesi yok). false → VAT ağı yok, iskeletli yedek kurulur. */
	bool BuildVatCrowd(const TArray<FVector>& Spots, int32 Count, uint32& Rs);
	/** Yakın plan City Sample MetaHuman yayaları (kalkış noktası çevresi); kurulan kişi sayısı */
	int32 BuildCsNear(const TArray<FVector>& Spots, uint32& Rs);
	TArray<TArray<FTransform>> VatWalkXf;   // ISM başına yürüyenlerin örnek dönüşümleri (0..n-1, her karede toplu güncellenir)
};
