// Kalkış donanımı — pist / pnömatik rampa / pickup araç; görsel + animasyon, tetik telemetri ve `launch` olaylarından
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CaySimTypes.h"
#include "LaunchRig.generated.h"

class UStaticMeshComponent;
class USceneComponent;
class ACaySimDroneActor;
class ACaySimWorldSetup;

/** Rampa / araç parametreleri (sihirbaz "Kalkış" adımı; sunucuya `launch` ile de bildirilir). */
USTRUCT(BlueprintType)
struct CAYSIM_API FCaySimLaunchConfig
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") ECaySimLaunchMethod Method = ECaySimLaunchMethod::Runway;
	/** Rampa yükseliş açısı (derece, 5–45). */
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") float RampAngleDeg = 15.f;
	/** Rampa yan açısı (derece; 2 eksenli rampa). */
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") float RampAzimuthDeg = 0.f;
	/** Araç hızı (m/s, 5–25). */
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") float VehicleSpeedMs = 12.f;
	/** Pist başı yönü (derece); pist varsa world.runway_heading. */
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") float HeadingDeg = 0.f;
};

/** Donanımın animasyon evresi. */
UENUM(BlueprintType)
enum class ECaySimLaunchPhase : uint8
{
	Idle       UMETA(DisplayName = "Beklemede"),
	Armed      UMETA(DisplayName = "Hazır (dron yerleştirildi)"),
	Rolling    UMETA(DisplayName = "Hareket (araç/rampa itişi)"),
	Released   UMETA(DisplayName = "Bırakıldı"),
	Airborne   UMETA(DisplayName = "Havada"),
	Done       UMETA(DisplayName = "Tamam")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCaySimLaunchPhaseDelegate, ECaySimLaunchPhase, NewPhase);

/**
 * Kalkış donanımı aktörü. Dronun fiziği sunucuda; bu aktör yalnız görsel donanımı kurar ve dronla eşzamanlı
 * hareket eder: rampa pistonu dronu 'release' anına dek taşır, araç hızlanır ve dron ayrıldıktan sonra yavaşlar.
 * Tetikler: `launch` olayı (phase armed/release/airborne) ya da yedek olarak telemetri (armed → yer hızı eşiği → alt_rel > 3 m).
 */
UCLASS(Blueprintable)
class CAYSIM_API ACaySimLaunchRig : public AActor
{
	GENERATED_BODY()

public:
	ACaySimLaunchRig();
	virtual void Tick(float DeltaSeconds) override;

	/** Merkez konumuna yerleştirir ve donanımı seçilen yönteme göre kurar. */
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Kalkış")
	void Setup(const FCaySimLaunchConfig& Config, ACaySimWorldSetup* World, ACaySimDroneActor* Drone, double Lat, double Lon, double GroundMsl);

	/** Sunucu `launch` olayı. */
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Kalkış")
	void OnLaunchEvent(const FCaySimLaunchEvent& Event);

	/** Telemetri yedeği (olay yoksa evreleri tahmin eder). */
	void OnTelemetry(const FCaySimTelemetry& Tele);

	UFUNCTION(BlueprintPure, Category = "CAYSIM|Kalkış") ECaySimLaunchPhase GetPhase() const { return Phase; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Kalkış") FCaySimLaunchConfig GetConfig() const { return Config; }

	/** Rampa açısı değiştir (sihirbaz canlı önizleme). */
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Kalkış") void SetRampAngle(float Deg);
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Kalkış") void SetRampAzimuth(float Deg);
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Kalkış") void SetVehicleSpeed(float Ms);

	UPROPERTY(BlueprintAssignable, Category = "CAYSIM|Kalkış") FCaySimLaunchPhaseDelegate OnPhaseChanged;

	// Mesh atamaları (Blueprint alt sınıfı; Content/Launch)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|Kalkış|Mesh") TObjectPtr<UStaticMesh> RampBaseMesh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|Kalkış|Mesh") TObjectPtr<UStaticMesh> RampRailMesh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|Kalkış|Mesh") TObjectPtr<UStaticMesh> RampCarriageMesh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|Kalkış|Mesh") TObjectPtr<UStaticMesh> VehicleMesh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|Kalkış|Mesh") TObjectPtr<UStaticMesh> VehicleWheelMesh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|Kalkış|Mesh") TObjectPtr<UStaticMesh> RunwayMesh;

	/** Rampa ray uzunluğu (m) ve taşıyıcı strok süresi (sn). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|Kalkış") float RampLengthM = 4.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|Kalkış") float RampStrokeS = 0.35f;
	/** Araç bırakma sonrası fren süresi (sn). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|Kalkış") float VehicleBrakeS = 6.f;
	/** Telemetri yedeği eşikleri. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|Kalkış") float ReleaseSpeedMs = 4.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|Kalkış") float AirborneAltM = 3.f;

protected:
	UPROPERTY(VisibleAnywhere, Category = "CAYSIM|Kalkış") TObjectPtr<USceneComponent> Root;
	/** Rampa: taban (yaw) → kol (pitch) → ray → taşıyıcı (ray boyunca kayar). */
	UPROPERTY(VisibleAnywhere, Category = "CAYSIM|Kalkış") TObjectPtr<UStaticMeshComponent> RampBase;
	UPROPERTY(VisibleAnywhere, Category = "CAYSIM|Kalkış") TObjectPtr<USceneComponent> RampArm;
	UPROPERTY(VisibleAnywhere, Category = "CAYSIM|Kalkış") TObjectPtr<UStaticMeshComponent> RampRail;
	UPROPERTY(VisibleAnywhere, Category = "CAYSIM|Kalkış") TObjectPtr<UStaticMeshComponent> RampCarriage;
	/** Araç: gövde + 4 tekerlek. */
	UPROPERTY(VisibleAnywhere, Category = "CAYSIM|Kalkış") TObjectPtr<UStaticMeshComponent> Vehicle;
	UPROPERTY(VisibleAnywhere, Category = "CAYSIM|Kalkış") TArray<TObjectPtr<UStaticMeshComponent>> Wheels;
	/** Pist şeridi. */
	UPROPERTY(VisibleAnywhere, Category = "CAYSIM|Kalkış") TObjectPtr<UStaticMeshComponent> Runway;

private:
	FCaySimLaunchConfig Config;
	ECaySimLaunchPhase Phase = ECaySimLaunchPhase::Idle;
	TWeakObjectPtr<ACaySimWorldSetup> WorldSetup;
	TWeakObjectPtr<ACaySimDroneActor> DroneActor;
	double Lat0 = 0, Lon0 = 0, Ground0 = 0;
	float PhaseT = 0.f;
	float VehicleSpeedNow = 0.f;
	float VehicleDistM = 0.f;
	float WheelSpin = 0.f;
	bool bArmedSeen = false;

	void SetPhase(ECaySimLaunchPhase P);
	void ShowFor(ECaySimLaunchMethod M);
	void UpdateRamp(float Dt);
	void UpdateVehicle(float Dt);
	void PlaceAtGround();
};
