// Yer kontrol sahası 3B (web flightsim_gcs.js siteLayout eşi): kalkış yönüne göre [ileri, sağ] m — istasyon (−40, 55), kule (−40, 85).
// İstasyon: shelter konteyner + anten direği + jeneratör; kule: kafes kule (4 ayak + çaprazlar), 18 m'de EO taretli platform, 21,5 m'de
// 4 sn'de bir tur dönen radar anteni. Temel şekillerden kodla kurulur (içerik gerekmez). EO yakalaması (SceneCapture) taret konumundan yapılır.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimumavTypes.h"
#include "SmvSiteActor.generated.h"

class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

UCLASS()
class SIMUMAV_API ASmvSiteActor : public AActor
{
	GENERATED_BODY()

public:
	ASmvSiteActor();
	virtual void Tick(float Dt) override;
	/** ToWorld: pist ENU (m) → dünya (cm); Heading: pist / kalkış yönü (°) */
	void Build(const FSmvSite& Site, double HeadingDeg, TFunction<FVector(double E, double N, double Up)> ToWorld);
	/** Kule tabanı ENU (m) — radar / EO göreli konumları için */
	FVector2D TowerEnu() const { return TowerXY; }
	FVector EoWorld() const;
	double SweepDeg() const;
	/** EO görüntüsü: hedef dünya noktasına çevir, görüş açısı (°) */
	void AimEo(const FVector& TargetWorld, float FovDeg, bool bCapture);
	UTextureRenderTarget2D* GetEoTarget() const { return EoRT; }
	void SetEoPost(const FPostProcessSettings& P);

protected:
	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<USceneComponent> RadarHead;
	UPROPERTY() TObjectPtr<USceneComponent> EoHead;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneCaptureComponent2D> EoCap;
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> EoRT;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> CylMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> SphMesh;
	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMat;

private:
	FSmvSite Cfg;
	FVector2D TowerXY = FVector2D::ZeroVector;
	double T0 = 0;
	FVector EoAt = FVector::ZeroVector;
	UStaticMeshComponent* Part(USceneComponent* Parent, int32 Shape, const FVector& PosCm, const FVector& SizeCm, const FLinearColor& Color, const FRotator& Rot = FRotator::ZeroRotator);
};
