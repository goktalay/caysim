// Çevrimdışı bölge paketinden gerçek dünya (SmvRegion): arazi (Terrarium yükseklikleri, parçalı ProceduralMesh + çarpışma),
// ufuk halkası (z10, 30 km), OSM binaları (taban izi yükseltilir: duvar + çatı, kat/pencere malzemesi M_SmvFacade), yollar
// (arazi üstüne serilen şeritler, M_SmvStreet), su (göl / nehir yüzeyi + kıyıda deniz düzlemi), orman / park ağaçları (Brushify).
// Düz dünya kaynağıyla kurulur: Unreal orijini = parkur ev noktası, Z 0 = GroundMsl (ASmvProcTerrain ile aynı dönüşüm).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SmvRegion.h"
#include "SmvRegionWorld.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class ASmvScenery;
struct FSmvTrack;

UCLASS()
class SIMUMAV_API ASmvRegionWorld : public AActor
{
	GENERATED_BODY()

public:
	ASmvRegionWorld();
	/** Paketi yükleyip sahneyi kurar; false → paket okunamadı (arazi yok). GeoActor: dünya kurulumu (ICaySimGeoTransform). */
	bool Build(const FSmvRegionInfo& Region, const FSmvTrack& Track, double GroundMsl, AActor* GeoActor);
	/** Yerel ENU (m) → arazi MSL (m) */
	double HeightMsl(double E, double N) const { return Dem.HeightEnu(E, N); }
	/** Hava: ıslaklık / kar (arazi malzemesi) */
	void SetSurfaceWeather(float Wetness, float Snow);
	virtual void Destroyed() override;
	int32 NumBuildings() const { return NBuild; }

protected:
	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> Parts;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> TerrainMids;
	UPROPERTY() TObjectPtr<ASmvScenery> Scenery;

private:
	FSmvRegionDem Dem;
	double Ground = 0;
	FVector Origin = FVector::ZeroVector;
	TFunction<FVector(double, double, double)> ToUe;
	int32 NBuild = 0, NRoad = 0, NTree = 0, NWater = 0;
	UProceduralMeshComponent* NewPart(const TCHAR* Name, bool bCollide);
	void BuildTerrain(double HalfM, double StepM);
	void BuildHorizon(double InnerM, double OuterM);
	void BuildOsm(const FSmvTrack& Track);
	UMaterialInterface* TerrainMaterial();
};
