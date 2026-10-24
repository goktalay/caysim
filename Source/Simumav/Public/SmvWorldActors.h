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
class UPoseableMeshComponent;
class USmvMotorSynth;
class UAudioComponent;
class ICaySimGeoTransform;
class FSmvContactSolver;
class USmvGlbBody;
struct FSmvCatalogModel;

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
	virtual void Tick(float DeltaSeconds) override;
	/** Fab dron gövdesi (skeletal) kullanılıyor mu. */
	bool IsUsingFabBody() const { return FabMesh != nullptr || FabStatic != nullptr; }
	/** Oyuncu dronu: gerçek çarpışma (UE fizik sorguları; SmvPhysics). Hayalet/rakip/hedef dronlarda kapalı kalır. */
	void EnablePhysicsContact(bool bOn);
	/** Çarpışma çözücüsü (kapalıysa null) — GameMode temas olaylarını buradan alır, yok sayılacak aktörleri buraya yazar. */
	FSmvContactSolver* GetContactSolver() const { return Solver; }
	/** Görünürlük ölçeği (gövde ×2,5; kara aracı ×1). */
	float GetVisScale() const { return VisScale; }
	virtual void BeginDestroy() override;
	/** Katalog GLB gövdesi (Content/Simumav/ModelsSrc) kullanılıyor mu. */
	bool IsUsingGlbBody() const { return bGlbBody; }
	/** SERVO_OUTPUT_RAW (PWM) ve SERVOn_FUNCTION — pervane devri ve kontrol yüzeyleri (oyun modu telemetriden besler). */
	void SetServoOutputs(const TArray<int32>& Pwm, const TArray<int32>& Fn) { ServoPwm = Pwm; ServoFn = Fn; }
	/** Hangar donanımı gövdede görünür (SmvDroneDress.cpp): ilaçlama tankı + bom + memeler, paket bırakma tutucusu + yük kutusu,
	 *  paraşüt kabı; çıkartma/logo (decal; M_SmvDecal yoksa ince levha), kaplama (parlak/mat/metal/karbon). Gövde değişince yeniden çağrılır. */
	void ApplyEquipment(const FSmvEquipment& E, const FLinearColor& BodyColor);
	/** Uçuşta eklenti işlevi (yalnız görsel; fizik SITL'de): paraşüt açık, yük takılı, ilaç püskürtülüyor */
	void SetChuteDeployed(bool bOn);
	void SetPayloadAttached(bool bOn);
	void SetSprayActive(bool bOn);
	bool IsChuteDeployed() const { return bChuteOut; }

protected:
	virtual void BuildPlaceholder() override;
	virtual bool ResolveRenderLocation(const FCaySimPose& Pose, const FQuat& EnuRot, const FQuat& WorldRot, FVector& InOutLoc) override;
	/** Fab quad gövdesi (SmvFabContent::LoadDroneMesh); pervane kemikleri ARM'lıyken döner. */
	UPROPERTY() TObjectPtr<UPoseableMeshComponent> FabMesh;
	/** Fab gövdesi iskeletsizse (statik ağ) */
	UPROPERTY() TObjectPtr<UStaticMeshComponent> FabStatic;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USmvMotorSynth> Motor;
	/** Katalog GLB gövdesi (pervane/yüzey/ışık hareketi bileşenin içinde) */
	UPROPERTY() TObjectPtr<USmvGlbBody> GlbBody;
	/** Uçak/VTOL motor sesi (Fab Aircraft Engine Sounds döngüsü; gaz → perde/ses düzeyi). Yoksa yalnız prosedürel ses. */
	UPROPERTY() TObjectPtr<UAudioComponent> EngineAudio;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	/** Pervane göbekleri (dönen düğüm; kanatlar alt bileşen). */
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> Hubs;
	UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> SphereMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> ConeMesh;
	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMaterial;
	/** Donanım parçaları (ApplyEquipment) ve çıkartmalar */
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> DressParts;
	UPROPERTY() TArray<TObjectPtr<class UDecalComponent>> DressDecals;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> ChuteParts;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> PayloadBox;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> SprayParts;

private:
	bool bChuteOut = false;
	void ClearDress();
	UStaticMeshComponent* AddDress(UStaticMesh* M, const FVector& PosM, const FVector& ScaleM, const FRotator& Rot, const FLinearColor& Color, float Rough = 0.5f, float Metal = 0.1f);
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
	/** Fab gövdesini kurar (Dir: /Game/Fab/Drone | /Game/Fab/DroneFpv; ayar öneki Drone|Fpv; yoksa false → BasicShapes gövdesi). */
	bool BuildFabBody(const TCHAR* Dir, const TCHAR* IniPrefix, float DefaultSpanM);
	/** Ölçek/yön/yer ofseti (bileşen uzayı sınırları, dönüş merkezi, burun ipucu). */
	bool PlaceFabBody(USceneComponent* C, const FBoxSphereBounds& B, const FVector& Ctr, const FVector& Fwd, const TCHAR* SpanKey, const TCHAR* YawKey, float DefaultSpanM);
	TArray<FName> FabPropBones;
	/** Pervane kemiklerinin bileşen uzayındaki başlangıç (referans) dönüşleri */
	TArray<FQuat> FabPropRef;
	/** Dönüş yönü (+1/−1; çapraz çiftler aynı) */
	TArray<float> FabPropDir;
	float FabPropPhase = 0.f;
	bool bGlbBody = false;
	TArray<int32> ServoPwm;
	TArray<int32> ServoFn;
	/** Katalog modelinin GLB'sinden gövde (yoksa false → Fab / BasicShapes gövdesi). */
	bool BuildGlbModel(const FSmvCatalogModel& M);
	/** Fab motor sesi etkin mi; prosedürel sesin bu durumdaki payı (uçak 0: motor sesi tek başına; VTOL: dikey rotorlar için) */
	bool bFabEngineSound = false;
	float SynthShare = 1.f;
	/** Yumuşatılmış gaz (Fab motor sesi perdesi) */
	float EngineThr = 0.f;
	/** Fizik çarpışma çözücüsü (ham işaretçi: tam tür yalnız .cpp'de; BeginDestroy'da silinir) */
	FSmvContactSolver* Solver = nullptr;
	/** Gövde çarpışma şekli: görünen gövdenin aktör uzayı sınırlarından kutu bileşimi (pervane diskleri dahil) */
	void RebuildPhysicsShape();
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
	/** Hava: ıslaklık (koyulaşma, parlaklık, su birikintisi) ve kar örtüsü 0..1 — M_SmvTerrainWx varsa (FSmvAtmosphere çağırır). */
	void SetSurfaceWeather(float Wetness, float Snow);
	virtual void Destroyed() override;

protected:
	/** Fab manzarası (Brushify ağaç/çalı/kaya/uzak dağ, ia_scatter ot/kaya; SmvScenery) — yoksa temel şekil ağaçları */
	UPROPERTY() TObjectPtr<class ASmvScenery> Scenery;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Ground;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> TreeTrunks;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> TreeCrowns;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> Buildings;
	/** Şehir dekoru (Özel parkur › Şehir): araç gövdesi / kabini, insan gövdesi / başı — yalnız görsel */
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> CityCars;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> CityCabins;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> CityPeople;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> CityHeads;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Runway;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> WaterPlane;
	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMaterial;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> TerrainMid;

private:
	FSmvTrackWorld W;
	float PendingWet = 0.f, PendingSnow = 0.f;
	double FlatR = 300.0;
	int32 Seed = 1;
	struct FMountain { double E, N, H, R; };
	TArray<FMountain> Mountains;
	double Noise2(double X, double Y) const;
};
