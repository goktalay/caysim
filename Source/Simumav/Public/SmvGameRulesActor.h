// Uçuş içi oyun türleri (yol haritası "Uçuş › Oyun türü"): yük bırakma, tarla ilaçlama, kritik tesis savunma, gemi savunma.
// Kurallar saf çekirdekte (Private/Core/SmvGameRules); bu aktör sahneyi kurar (hedef bölgeleri, tarla sınırı + kapsama haritası, korunacak
// tesis / gemi + deniz, gelen hedef dronlar), telemetriyi çekirdeğe verir, oyuncu eylemlerini (Z yük bırak · M ilaçlama · J paraşüt · kilit tuşu)
// SITL komutlarına (gripper / sprayer / parachute) ve görsellere bağlar, kendi HUD katmanını (puan, kapsama, sağlık, kilit çerçevesi) çizer.
// ANGAJMAN KURALI (§40): savunma YALNIZ oyuncu kontrollü — kilit yalnız HUD çerçevesidir, puan pilotun aracını tehdide yaklaştırmasıyla sayılır;
// otonom güdüm, otomatik nişan ya da silah/mühimmat davranışı YOKTUR.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimumavTypes.h"
#include "SmvGameRulesActor.generated.h"

class ASmvDroneActor;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;
class USmvFlow;
class UTexture2D;
struct FCaySimTelemetry;
class SWidget;

/** Multispektral kamera renk tablosu (bitki örtüsü kırmızı-eflatun, diğer yüzeyler camgöbeği-gri; NDVI benzeri sahte renk) */
SIMUMAV_API UTexture2D* SmvMultispectralLut();

UCLASS()
class SIMUMAV_API ASmvGameRulesActor : public AActor
{
	GENERATED_BODY()

public:
	ASmvGameRulesActor();
	/** Oyun türünü kurar. ToWorld: ev noktasına göre ENU (m) + zemin üstü (m) → dünya (cm). */
	void Setup(const FSmvFlightRequest& Req, USmvFlow* InFlow, ASmvDroneActor* InDrone, TFunction<FVector(double, double, double)> InToWorld);
	/** Telemetri (ev noktasına göre ENU hesaplanır) */
	void OnTele(const FCaySimTelemetry& T);
	/** Oyuncu eylemi: drop | spray | chute | lock (HUD düğmeleri ve tuşlar) */
	void Action(const FString& Name);
	/** Uçuş özetine tür sonucu (puan, rozet) */
	void Augment(FSmvFlightSummary& S) const;
	virtual void Tick(float Dt) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** Olay şeridi (GameMode PushEvent) ve SITL komutu (GameMode Command) */
	TFunction<void(const FString&, int32)> OnEvent;
	TFunction<void(const FString&, TSharedPtr<FJsonObject>)> OnCommand;

	// ---- HUD için okunur durum ----
	FString Mode;
	int32 Score() const;
	FText Line1() const;
	FText Line2() const;
	float Bar01() const;
	FLinearColor BarColor() const;
	bool bSpraying = false;

protected:
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Cells;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> PayloadMeshes;
	UPROPERTY() TArray<TObjectPtr<ASmvDroneActor>> ThreatActors;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> FacilityParts;
	UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> CylMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> ConeMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> SphereMesh;
	UPROPERTY() TObjectPtr<UMaterialInterface> GlowMat;
	UPROPERTY() TObjectPtr<UMaterialInterface> FxMat;
	UPROPERTY() TObjectPtr<ASmvDroneActor> Drone;
	TWeakObjectPtr<USmvFlow> Flow;

private:
	struct FImpl;
	TSharedPtr<FImpl> Impl;
	TFunction<FVector(double, double, double)> ToWorld;
	FSmvFlightRequest Req;
	double E = 0, N = 0, U = 0, Ve = 0, Vn = 0, Vu = 0, YawDeg = 0, Gs = 0, TeleT = 0;
	bool bArmed = false, bHasTele = false, bChute = false, bDone = false;
	double SimT = 0, StartAt = 0, LastSpawnLog = 0;
	TSharedPtr<SWidget> HudHost;
	FVector Ground(double InE, double InN, double Lift = 0.0) const;
	UStaticMeshComponent* AddPart(UStaticMesh* M, const FVector& At, const FVector& Scale, const FRotator& Rot, const FLinearColor& C, bool bGlow, float Glow = 2.f);
	void BuildDrop();
	void BuildSpray();
	void BuildDefense(bool bShip);
	void TickGame(float Dt);
	void Emit(const FString& Key, int32 Sev, const TMap<FString, FString>& Args = TMap<FString, FString>());
	void PushResult();
	void BuildHud();
	FVector ThreatWorld(int32 Id) const;
	friend class SSmvGameHud;
};
