// Manzara (yalnız görsel): Fab paketlerinden ağaç / çalı / kaya / ot / uzak dağ / uçan araç ağları ve Hyper dokulu arazi malzemesi.
//   - Brushify Forest Pack: ağaçlar (huş, sedir, akçaağaç), çalılar, orman kayaları, uzak dağ ağları (Mountain_Generic_03, Generic_Erosion_*)
//     ve dağ malzemeleri (MI_<ağ>_<Desert|Grasslands|Rocky|Snow>)
//   - ia_scatter: kaya ve ot / eğrelti / yabani ot ağları (eklentinin dağıtım Blueprint'i editör aracıdır; dağıtımı burada kod yapar)
//   - Hyper Auto Landscape Material v4: dokuları Scripts/Python/simumav_varliklar.py → M_SmvTerrainHyper (eğim/yükseklik otomatik)
//   - Flying AI: gökte uzak trafik ağı (yalnız görsel; davranış / hedef yok)
// Paketler yoksa her şey boş döner ve eski temel şekil dünyası kalır. Klasörler SmvScenery.cpp MANZARA-KLASORLERI bloğunda —
// CI (Tools/fab/fab-hazirla.ps1) bu bloğu okuyup klasörleri pakete pişirir.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Templates/Function.h"
#include "SmvScenery.generated.h"

class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UHierarchicalInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UProceduralMeshComponent;
class ULevel;

enum class ESmvSceneryKind : uint8
{
	Tree,
	Bush,
	Rock,
	Grass,
	Mountain,
	Cliff,
	Flyer,
};

struct SIMUMAV_API FSmvScenery
{
	/** Pakette bulunan ağlar (ilk çağrıda yüklenir; yoksa boş). */
	static const TArray<UStaticMesh*>& Meshes(ESmvSceneryKind Kind);
	/** Saha çitli haritalar: saha kutusunun (≤ 400 m'lik statik ağlar) MarginCm dışında kalan arazi bileşenlerini ve dev arka plan
	 *  ağlarını gizler, çarpışmasını kapatır ("saha dışı gösterilmesin"; Yıkık İnşaat'ın uzak kum tepeleri). Gizlenen sayısı. */
	static int32 HideOutsideSite(const TArray<ULevel*>& Levels, double MarginCm);
	/** Hyper dokulu otomatik arazi malzemesi (/Game/Simumav/Materials/M_SmvTerrainHyper; yoksa nullptr). */
	static UMaterialInterface* TerrainMaterial();
	/** Şehir asfaltı / kaldırım (/Game/Simumav/Materials/M_SmvStreet; yoksa nullptr). */
	static UMaterialInterface* StreetMaterial();
	/** Brushify uzak dağ ağı için biyom malzemesi (Desert | Grasslands | Rocky | Snow; yoksa nullptr → ağın kendi malzemesi). */
	static UMaterialInterface* MountainMaterial(const UStaticMesh* Mesh, const FString& Biome);
	/** Pakete pişirilecek klasörler (CI bloğu). */
	static TArrayView<const TCHAR* const> CookDirs();
	/** [/Script/Simumav.SmvScenery] Enabled (varsayılan True) */
	static bool Enabled();
	static float GetFloat(const TCHAR* Key, float Default);
};

/** Dağıtım yoğunluğu (adet) */
struct FSmvScatterDensity
{
	int32 Trees = 0;
	int32 Bushes = 0;
	int32 Rocks = 0;
	int32 Grass = 0;
	/** Ağaçların başladığı iç yarıçap (m; ev alanı açık kalsın) */
	double TreeInnerM = 0.0;
	/** Otların dış yarıçapı (m; uzakta görünmez, yalnız yakın plan) */
	double GrassOuterM = 350.0;
	/** Kuru / çöl görünümü: ağaç yerine çalı ve kaya ağırlıklı */
	bool bDry = false;
};

UCLASS()
class SIMUMAV_API ASmvScenery : public AActor
{
	GENERATED_BODY()

public:
	ASmvScenery();
	/** Yerel halka (InnerM..OuterM, m) içinde ağaç / çalı / kaya / ot dağıt. Ground(Doğu m, Kuzey m, OutKonum cm, OutYukarı): false → nokta atlanır. */
	void Scatter(double InnerM, double OuterM, const FSmvScatterDensity& D, int32 Seed, TFunctionRef<bool(double, double, FVector&, FVector&)> Ground);
	/** Ufuk dağları: Center çevresinde InnerCm..OuterCm halkasına Count adet uzak dağ ağı (yükseklik MinHCm..MaxHCm, taban BaseZ). */
	void AddMountains(const FVector& Center, double InnerCm, double OuterCm, double BaseZ, int32 Count, const FString& Biome, int32 Seed, double MinHCm, double MaxHCm);
	/** Verilen dönüşümlerle ağ örnekleri (şehir ağaçları, direkler; Material verilirse tüm yuvalara). */
	void AddMeshInstances(UStaticMesh* Mesh, const TArray<FTransform>& WorldXf, bool bCollide, double CullM, UMaterialInterface* Material = nullptr, bool bShadow = true);
	/** Fab seviyesinin çevresine zemin (Hyper malzemeli geniş arazi diski, adanın altından), kaya/çalı ve uzak dağlar:
	 *  "havada ada" görünümü kalkar. Levels: yüklenmiş seviyeler; Ignore: ışın sorgularında yok sayılacak aktörler. */
	/** bScatter false: yalnız düz zemin + uzak dağlar (saha çitli haritalar; uzak bitki kümeleri benekli görünüyordu) */
	bool BuildSurround(const TArray<ULevel*>& Levels, const FString& Biome, int32 Seed, const TArray<AActor*>& Ignore, bool bScatter = true);
	/** Merkez (taban Z = Center.Z) çevresinde FlatM (m) yarıçapına dek düz, dışında kum tepeli / dalgalı arazi diski (OuterM m),
	 *  ufuk dağları ve isteğe bağlı kaya/çalı/ağaç dağıtımı. Prosedürel dünyada arazinin dışını kapatmak için de kullanılır. */
	void BuildGroundDisk(const FVector& Center, double FlatM, double OuterM, const FString& Biome, int32 Seed, bool bScatter);
	/** Hava: arazi diskinde ıslaklık / kar (M_SmvTerrainHyper parametreleri) */
	void SetSurfaceWeather(float Wetness, float Snow);
	int32 GetInstanceCount() const { return Placed; }

protected:
	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> Hisms;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Singles;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> Ground;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> GroundMid;

private:
	UHierarchicalInstancedStaticMeshComponent* HismFor(UStaticMesh* Mesh, bool bCollide, double CullM, UMaterialInterface* Material, bool bShadow);
	int32 Placed = 0;
};

/** Gökte uzak trafik (Flying AI ağı): büyük elips yörüngelerde yavaş geçen araçlar — yalnız görsel, çarpışma / davranış yok. */
UCLASS()
class SIMUMAV_API ASmvSkyTraffic : public AActor
{
	GENERATED_BODY()

public:
	ASmvSkyTraffic();
	virtual void Tick(float DeltaSeconds) override;
	/** Center çevresinde Count araç; yarıçap RadiusCm (± %35), irtifa AltMinCm..AltMaxCm (Center.Z'ye göre). false → ağ yok. */
	bool Build(const FVector& Center, int32 Count, double RadiusCm, double AltMinCm, double AltMaxCm, int32 Seed);

protected:
	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Craft;

private:
	struct FLeg { double Rx, Ry, Alt, Speed, Phase, Yaw0; };
	TArray<FLeg> Legs;
	FVector C0 = FVector::ZeroVector;
	TArray<FRotator> Fixes;
	double T = 0.0;
};
