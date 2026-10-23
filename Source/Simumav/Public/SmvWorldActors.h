// Kodla kurulan dünya aktörleri (içerik varlığı gerektirmez; motorun /Engine/BasicShapes ağları + çalışma anı malzeme örnekleri):
//   ASmvDroneActor   — CAYSIM dron aktörü + prosedürel gövde (GLB yoksa) + prosedürel motor sesi
//   ASmvGateActor    — halka/kapı (prosedürel torus), sıradaki halka vurgulu
//   ASmvProcTerrain  — prosedürel dünya (Cesium belirteci yoksa / parkur `procedural`): ısı haritası arazi, dağlar, ağaç ve bina örnekleri,
//                      pist, su yüzeyi. Ev çevresi (300 m) SITL'in düz zemini ile aynı yükseklikte düzdür.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DroneActor.h"
#include "SimumavTypes.h"
#include "SmvWorldActors.generated.h"

class UProceduralMeshComponent;
class UInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class USmvMotorSynth;
class ICaySimGeoTransform;

UCLASS()
class SIMUMAV_API ASmvDroneActor : public ACaySimDroneActor
{
	GENERATED_BODY()

public:
	ASmvDroneActor();
	/** SIMUMAV tipine göre gövde + renk + motor sesi türü. */
	// Not: parametre adı üye SmvType ile aynı olmamalı (C4458)
	void ConfigureVisual(const FString& InType, const FString& ModelKey, const FLinearColor& Color);
	USmvMotorSynth* GetMotor() const { return Motor; }
	/** Görünür yarıçap (m; görünürlük ölçeği dahil) — takip kamerası mesafesi buna göre. */
	float GetVisualRadiusM() const { return VisRadiusM; }
	/** Burun ucu (m, gövde ekseninde; ölçek dahil) — FPV kamerası. */
	float GetVisualNoseM() const { return VisNoseM; }
	/** Kamera konumuna göre ses (Doppler + uzaklık) — her kare. */
	void UpdateAudio(const FVector& Listener, float Volume);

protected:
	virtual void BuildPlaceholder() override;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USmvMotorSynth> Motor;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	/** Pervane göbekleri (dönen düğüm; kanatlar alt bileşen). */
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> Hubs;
	UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> SphereMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> ConeMesh;
	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMaterial;

private:
	FString SmvType = TEXT("multirotor");
	FString SmvModelKey;
	FLinearColor BodyColor = FLinearColor(0.8f, 0.1f, 0.1f);
	FVector LastPos = FVector::ZeroVector;
	/** Görünürlük ölçeği (web MODEL_SCALE 2,5; kara aracı 1). */
	float VisScale = 2.5f;
	float VisRadiusM = 1.f;
	float VisNoseM = 0.5f;
	UStaticMeshComponent* AddPart(UStaticMesh* M, const FVector& PosM, const FVector& ScaleM, const FRotator& Rot, const FLinearColor& Color, USceneComponent* Parent = nullptr);
	/** Dönen pervane: göbek + iki kanatlı pala (bForward: disk ileri bakar, X ekseni çevresinde döner). */
	void AddRotor(const FVector& PosM, float DiaM, bool bForward, const FLinearColor& Color);
	void ClearParts();
};

UCLASS()
class SIMUMAV_API ASmvGateActor : public AActor
{
	GENERATED_BODY()

public:
	ASmvGateActor();
	/** Yarıçap (m), kalınlık (m), renk. */
	void Build(float RadiusM, float TubeM, const FLinearColor& Color);
	void SetState(int32 State);   // 0 bekliyor, 1 sıradaki, 2 geçildi

protected:
	UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Mesh;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Mat;
	FLinearColor Base = FLinearColor(1.f, 0.5f, 0.f);
};

UCLASS()
class SIMUMAV_API ASmvProcTerrain : public AActor
{
	GENERATED_BODY()

public:
	ASmvProcTerrain();
	/** Parkur dünyası + merkez; Geo: dünya kurulumunun dönüşümü (aynı eksen sözleşmesi). */
	void Build(const FSmvTrack& Track, double GroundMsl, AActor* GeoActor);
	/** Yerel ENU (m) noktasında arazi yüksekliği (merkez zeminine göre). */
	double HeightAt(double E, double N) const;

protected:
	UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Ground;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> TreeTrunks;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> TreeCrowns;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> Buildings;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Runway;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> WaterPlane;
	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMaterial;

private:
	FSmvTrackWorld W;
	double FlatR = 300.0;
	int32 Seed = 1;
	struct FMountain { double E, N, H, R; };
	TArray<FMountain> Mountains;
	double Noise2(double X, double Y) const;
};
