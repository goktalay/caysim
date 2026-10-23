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

/** Rüzgâr tulumu (web flightsim3d sockState): 6 m direk + 5 halkalı turuncu-beyaz tulum; şişkinlik 7,7 m/s'de tam, düşük rüzgârda sarkar, dalgalanır. */
UCLASS()
class SIMUMAV_API ASmvWindSock : public AActor
{
	GENERATED_BODY()

public:
	ASmvWindSock();
	virtual void BeginPlay() override;
	virtual void Tick(float Dt) override;
	/** Rüzgâr: hız (m/s), geldiği yön (° kuzeyden) */
	void SetWind(float Ms, float FromDeg) { WindMs = Ms; WindFrom = FromDeg; }

protected:
	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<USceneComponent> Pivot;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Rings;

private:
	float WindMs = 3.f, WindFrom = 270.f, Yaw = 0.f, Droop = 60.f, T = 0.f;
};

/** Bayram süsleri 3B (web flightsim_holiday3d eşi): çarpışmasız, ≤ 4 m (direkler 8 m), evden 45–95 m halkada; pist koridoru ve saha tamponlu.
 *  Tema: kurban (koyunlar), ramazan/hicri (fener dizileri), noel (çam + hediye), paskalya (yumurtalar), cadılar (balkabakları + korkuluk),
 *  yılbaşı/ay yeni yılı/diwali (ışık dizileri), diğerleri (dalgalanan bayraklar, tema renkleri). */
UCLASS()
class SIMUMAV_API ASmvHolidayDecor : public AActor
{
	GENERATED_BODY()

public:
	ASmvHolidayDecor();
	virtual void Tick(float Dt) override;
	void Build(const FString& Theme, double HeadingDeg, TFunction<FVector(double E, double N, double Up)> ToWorld);

protected:
	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Cloths;
	UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> CylMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> SphMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> ConeMesh;
	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMat;

private:
	float T = 0.f;
	UStaticMeshComponent* Part(int32 Shape, const FVector& PosCm, const FVector& SizeCm, const FLinearColor& Color, const FRotator& Rot = FRotator::ZeroRotator);
};
