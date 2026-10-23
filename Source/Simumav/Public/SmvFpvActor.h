// FPV parkuru sahne aktörü: parkur engellerini / dekorunu (örneklenmiş temel şekiller: kutu — pencere/kapı boşluklu —, silindir, küre, boru,
// simit), neon kapıları (sıradaki yeşil ve nabızlı, sonraki 1–2 pembe, geçilenler sönük, diğerleri turuncu), bayrak bezlerini ve başlangıç
// pedini kurar; parkur izleyicisini (FSmvFpvTracker) taşır; kişisel rekor hayaletini (yarı saydam renkli dron) yarış saatine göre uçurur.
// Engeller fizikte yoktur (ArduPilot SITL düz zemin): çarpma kuraldır (ceza / başlangıca dön / yalnız sayım) — web §47 ile aynı.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SmvFpv.h"
#include "SmvFpvActor.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class ASmvGateActor;
class ASmvDroneActor;

UCLASS()
class SIMUMAV_API ASmvFpvCourseActor : public AActor
{
	GENERATED_BODY()

public:
	ASmvFpvCourseActor();
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** ToWorld: pist ENU (m; doğu, kuzey, yukarı — SITL ev konumu = başlangıç pedi) → dünya (cm) */
	void Init(const FSmvFpvCourse& Course, const FSmvFpvCfg& Cfg, TFunction<FVector(double E, double N, double Up)> ToWorld, const FString& VehicleType);
	/** Yarış başladı (giriş sekansı bitti): izleyici sıfırlanır */
	void StartRace(double T0);
	bool IsRacing() const { return Tracker.IsValid(); }
	/** Dron konumu: pist ENU (m) + yön (°) + yarış saati → olaylar */
	TArray<FSmvFpvEvent> Feed(double E, double N, double Up, float YawDeg, double T);
	void UpdateGhost(double T);
	const FSmvFpvTracker* GetTracker() const { return Tracker.Get(); }
	const FSmvFpvCourse& GetCourse() const { return Course; }
	const FSmvFpvCfg& GetCfg() const { return Cfg; }
	/** Rekor (yarış başında yüklenir) */
	const FSmvFpvRecord& GetRecord() const { return Record; }
	/** Hayalete göre fark (sn; + geride) — sıradaki kontrol noktasının rekordaki geçişine göre; yoksa NaN */
	double GhostDelta() const;
	/** Bitince rekoru kaydeder; yeni rekor mu */
	bool SaveIfRecord();
	/** Sıradaki kapının pist ENU merkezi */
	FVector NextGateEnu() const;

protected:
	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Ism;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> GateParts;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> GateMats;
	UPROPERTY() TArray<TObjectPtr<ASmvGateActor>> Rings;
	UPROPERTY() TObjectPtr<ASmvDroneActor> Ghost;
	UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> CylMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> SphMesh;
	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMat;

private:
	FSmvFpvCourse Course;
	FSmvFpvCfg Cfg;
	FSmvFpvRecord Record;
	TUniquePtr<FSmvFpvTracker> Tracker;
	TFunction<FVector(double, double, double)> W;
	FVector Origin = FVector::ZeroVector, Ew = FVector(100, 0, 0), Nw = FVector(0, -100, 0), Uw = FVector(0, 0, 100);
	TMap<FString, UInstancedStaticMeshComponent*> IsmBy;
	/** kapı → [parça başı, parça sayısı] (GateParts / GateMats içinde); halka kapısı → Rings sırası */
	TArray<int32> GateMatIdx;
	TArray<int32> GateRingIdx;
	int32 LastStateHash = -1;
	int32 GhostIdx = 0;
	double RaceT0 = 0;

	FVector P(double X, double Y, double Z) const;   // parkur → dünya
	FVector Dir(const FVector& Enu) const;           // ENU yön → dünya yönü (birim)
	UInstancedStaticMeshComponent* IsmFor(int32 Shape, const FString& Mat);
	void AddBox(const FVector& CenterEnu, const FVector& R, const FVector& F, double W, double D, double H, const FString& Mat);
	void AddPrim(const FSmvFpvObst& E);
	UStaticMeshComponent* AddGateBar(const FVector& CenterEnu, const FVector& R, const FVector& F, double W, double D, double H, UMaterialInstanceDynamic* M);
	void BuildGate(int32 Index, const FSmvFpvGate& G);
	void RefreshGates();
};
