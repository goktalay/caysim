// Dron aktörü — telemetri → transform (georeference), pervane/kontrol yüzeyi animasyonu, HEPGO GLB modeli
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CaySimTypes.h"
#include "TelemetryBuffer.h"
#include "DroneActor.generated.h"

class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UHepgoClient;
class ICaySimGeoTransform;

/** Yer tutucu gövde için dahili statik mesh takımı (Content/Drones altında Blueprint ile atanır). */
USTRUCT(BlueprintType)
struct CAYSIM_API FCaySimBuiltinMeshes
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM") TObjectPtr<UStaticMesh> FixedWing = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM") TObjectPtr<UStaticMesh> Quad = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM") TObjectPtr<UStaticMesh> Hexa = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM") TObjectPtr<UStaticMesh> Heli = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM") TObjectPtr<UStaticMesh> Propeller = nullptr;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCaySimModelReadyDelegate);

/**
 * Telemetriyle sürülen dron. Fizik yok: her karede FTelemetryBuffer'dan örneklenen konum/yönelim uygulanır.
 * Konum: ICaySimGeoTransform (Cesium georeference ya da düz ENU). Yönelim: NED → Unreal (CaySimGeo).
 * Görsel: HEPGO model kütüphanesinden GLB (glTFRuntime varsa çalışma zamanında), yoksa dahili yer tutucu.
 */
UCLASS(Blueprintable)
class CAYSIM_API ACaySimDroneActor : public AActor
{
	GENERATED_BODY()

public:
	ACaySimDroneActor();

	virtual void Tick(float DeltaSeconds) override;

	/** Oturum bilgisi ve istemci: tip, model, yönelim. GLB indirme burada başlar. */
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Dron")
	void Setup(const FCaySimSession& Session, UHepgoClient* Client);

	/** Dünya dönüşümünü sağlayan aktör (ACaySimWorldSetup). */
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Dron")
	void SetGeoTransformActor(AActor* InActor);

	/** Canlı telemetri (WS) — tampon üzerinden. */
	void PushTelemetry(const FCaySimTelemetry& Tele);
	/** Kayıttan oynatma: tamponu atlayıp doğrudan poz uygula. */
	void ApplyPoseDirect(const FCaySimPose& Pose);

	/** Zaman kaynağı (Unix sn, `tele.ts` ile aynı ölçek). Verilmezse HEPGO istemcisinin sunucu saati ya da yerel UTC kullanılır.
	 *  Simumav: bulut uçuşunda sunucu saati tahmini, yerel SITL'de yerel UTC. */
	void SetClockSource(TFunction<double()> InClock) { Clock = MoveTemp(InClock); }
	/** Ham GLB baytlarını yükler (glTFRuntime varsa; yoksa yer tutucu kalır). Simumav model önbelleği kullanır. */
	void LoadGlbBytes(const TArray<uint8>& Bytes) { LoadGlb(Bytes); }
	/** Modelin dahili yer tutucu gövde ile mi çizildiği. */
	bool IsUsingPlaceholder() const { return GltfActor == nullptr; }

	UFUNCTION(BlueprintPure, Category = "CAYSIM|Dron") FCaySimTelemetry GetLatestTelemetry() const { return Latest; }
	/** Render edilen (enterpolasyonlu) yönelim — HUD bunu kullanır (web `sim.getPose()` karşılığı). */
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Dron") void GetRenderedPose(float& RollRad, float& PitchRad, float& YawRad) const;
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Dron") ECaySimDroneType GetDroneType() const { return DroneType; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Dron") bool IsOnGround() const { return bOnGround; }
	/** Burun yönü (Unreal dünya) — kamera takibi için. */
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Dron") FVector GetForwardWorld() const; // gövdesi .cpp'de (USceneComponent burada eksik tip)

	/** Gecikme tamponu (sn). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|Dron", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float InterpolationDelayS = 0.08f;

	/** Eksen işaretleri (model/dünya sözleşmesi doğrulanırken açılıp kapanabilir). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|Dron|Eksen") bool bInvertRoll = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|Dron|Eksen") bool bInvertPitch = false;
	/** Düz ENU yedeğinde X=Kuzey (Cesium kullanılmıyorsa WorldSetup bunu ayarlar). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|Dron|Eksen") bool bEnuXNorth = false;

	/** Dahili yer tutucu mesh'ler (Blueprint alt sınıfında atanır). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|Dron")
	FCaySimBuiltinMeshes BuiltinMeshes;

	/** Pervane azami dönüş (devir/sn) — gaz %100'de. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|Dron") float PropMaxRps = 40.f;
	/** Kontrol yüzeyi azami sapma (derece) — RC 1/2/4 kanallarından. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|Dron") float SurfaceMaxDeg = 25.f;

	/** Seyir ışıkları ve strobe (armed iken). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|Dron") bool bNavLights = true;

	/** Model yüklendi/değişti (Blueprint görsel düzenlemeleri için). */
	UPROPERTY(BlueprintAssignable, Category = "CAYSIM|Dron") FCaySimModelReadyDelegate OnModelReady;

protected:
	virtual void BeginPlay() override;

	/** Kök: konum/yönelim buraya yazılır. */
	UPROPERTY(VisibleAnywhere, Category = "CAYSIM|Dron") TObjectPtr<USceneComponent> Root;
	/** Gövde (model yönelim/ölçek/ofset düzeltmesi burada). */
	UPROPERTY(VisibleAnywhere, Category = "CAYSIM|Dron") TObjectPtr<USceneComponent> Body;
	/** Dahili yer tutucu mesh. */
	UPROPERTY(VisibleAnywhere, Category = "CAYSIM|Dron") TObjectPtr<UStaticMeshComponent> PlaceholderMesh;
	/** Pervaneler (dahili ya da model düğümleri). */
	UPROPERTY(VisibleAnywhere, Category = "CAYSIM|Dron") TArray<TObjectPtr<USceneComponent>> Propellers;
	/** Kontrol yüzeyleri (sabit kanat; modelde adlandırılmış düğüm varsa). */
	UPROPERTY(VisibleAnywhere, Category = "CAYSIM|Dron") TObjectPtr<USceneComponent> AileronL;
	UPROPERTY(VisibleAnywhere, Category = "CAYSIM|Dron") TObjectPtr<USceneComponent> AileronR;
	UPROPERTY(VisibleAnywhere, Category = "CAYSIM|Dron") TObjectPtr<USceneComponent> Elevator;
	UPROPERTY(VisibleAnywhere, Category = "CAYSIM|Dron") TObjectPtr<USceneComponent> Rudder;

	/** glTF'den yüklenen gövdenin kökü (glTFRuntime). */
	UPROPERTY() TObjectPtr<AActor> GltfActor;

	/** Yer tutucu gövde (alt sınıflar kendi gövdesini kurabilir — Simumav prosedürel gövde). */
	virtual void BuildPlaceholder();
	void BuildPlaceholderProps(int32 Count, float ArmM);
	ECaySimDroneType DroneType = ECaySimDroneType::Multirotor;
	float GroundOffsetM = 0.f; // gövde alt noktası (model sınır kutusu)
	/** Render konumu çözümü (her poz uygulamasında). Alt sınıf gerçek çarpışma sorgularıyla InOutLoc'u düzeltip true döndürürse
	 *  taban sınıfın yer kesişimi düzeltmesi (zemin yüksekliği örneği) atlanır. EnuRot: yerel ENU → Unreal; WorldRot: gövde yönelimi. */
	virtual bool ResolveRenderLocation(const FCaySimPose& /*Pose*/, const FQuat& /*EnuRot*/, const FQuat& /*WorldRot*/, FVector& /*InOutLoc*/) { return false; }
	void SetOnGroundState(bool bOn) { bOnGround = bOn; }
	/** Son görsel düzeltme (alt sınıf: fırlatma rampası / araç tavanı / el — yalnız görüntü, fizik ve telemetri değişmez). */
	virtual void AdjustRenderPose(FVector& /*InOutLoc*/, FQuat& /*InOutRot*/) {}

private:
	FTelemetryBuffer Buffer;
	FCaySimTelemetry Latest;
	FCaySimPose Rendered;
	FCaySimModelInfo Model;
	TFunction<double()> Clock;
	double NowSeconds() const;
	TWeakObjectPtr<UHepgoClient> Hepgo;
	TWeakObjectPtr<AActor> GeoActor;
	ICaySimGeoTransform* Geo = nullptr;
	bool bOnGround = true;
	float PropPhase = 0.f;

	void ApplyPose(const FCaySimPose& Pose);
	void AnimateProps(float Dt, float ThrottlePct, bool bArmed);
	void AnimateSurfaces(const TArray<int32>& Rc);
	void ApplyOrient(const FCaySimModelOrient& Orient);
	void LoadGlb(const TArray<uint8>& Bytes);
	static FVector AxisFromString(const FString& S);
};
