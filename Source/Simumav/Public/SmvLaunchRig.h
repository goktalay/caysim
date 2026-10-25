// Sabit kanat kalkış / fırlatma düzeneği (web flightsim_launch.js + hand_launch.js eşi; yalnız görsel):
//   ramp    — pnömatik rampa römorku: şasi + 4 teker + ayaklar, azimut tablası, eğik 6 m ray, kızak, sarı pnömatik tüp, kırmızı tampon;
//             uçak kızakta ray açısıyla durur, SITL ileri ilerledikçe ray boyunca yükselir; raydan çıkınca 0,6 sn'de SITL pozuna yumuşak geçiş.
//   vehicle — sivil araç (City Traffic Pro aracı; yoksa basit mavi pikap) + tavan beşiği: araç SITL uçağının yer izini taşır,
//             bırakmada 4 m/s² ile frenler; uçak tavanda.
//   hand    — elden atış: uçak pilotun omzunda (+1,6 m, 0,55 m önde); 3 sn geri sayım, atış, 1,2 sn'de SITL pozuna geçiş.
//   wheels  — tekerlekli kalkış: 400 × 18 m asfalt şerit (orta çizgili) kalkış yönünde.
// Fizik / SITL değişmez (SmvLocalFlightLink kalkış dizisi; SmvSitlConfig::LaunchParams).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SmvLaunchRig.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class USkeletalMeshComponent;
class ASmvDroneActor;

UCLASS()
class SIMUMAV_API ASmvLaunchRig : public AActor
{
	GENERATED_BODY()
public:
	ASmvLaunchRig();
	virtual void Tick(float Dt) override;
	/** Kur: yöntem, ev noktası (UE), kalkış yönü (UE yaw), rampa açısı (°), dron */
	void Build(const FString& Method, const FVector& Home, float YawDeg, float RampDeg, ASmvDroneActor* InDrone);
	/** Aşama: idle | armed | launch | released */
	void SetPhase(const FString& Phase);
	const FString& GetMethod() const { return Method; }
	/** Elden atışta pilotun durması gereken nokta (UE) ve yönü */
	FVector PilotSpot() const;
private:
	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<USceneComponent> RailPivot;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Sled;
	UPROPERTY() TObjectPtr<USceneComponent> Car;
	UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> CylMesh;
	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMat;
	TWeakObjectPtr<ASmvDroneActor> Drone;
	FString Method, Phase = TEXT("idle");
	FVector Home = FVector::ZeroVector, Fwd = FVector::ForwardVector;
	float Yaw = 0.f, RampDeg = 15.f;
	double PhaseAt = 0, ReleaseAt = -1;
	double RoofZ = 180.0;          // araç tavanı (cm, zeminden)
	double CarS = 0, CarV = 0, LastD = 0;   // araç yol / hız (cm, cm/s)
	FVector BlendFrom = FVector::ZeroVector;
	float BlendPitchFrom = 0.f;
	static constexpr double RailLenM = 6.0, RailBaseM = 1.05;
	UStaticMeshComponent* Part(UStaticMesh* M, USceneComponent* Parent, const FVector& PosM, const FVector& ScaleM, const FRotator& Rot, const FLinearColor& Color);
	void BuildRamp();
	void BuildVehicle();
	void BuildRunway();
};
