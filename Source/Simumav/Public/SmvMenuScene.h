// Menü 3B sahnesi (web SIMUMAV menu/flight.js eşi): kabuk ekranlarının ARKASINDA canlı dünya — prosedürel arazi (TR TEST Kalecik),
// alçak irtifada sekizli tur atan sabit kanat dron, sinematik kamera döngüsü (takip → yan pan → yörünge → yerden izleme).
// Ayrıca hangar / sihirbaz için 3B ÖNİZLEME: seçili araç sahnenin çok üstünde, yalnız kendisini gösteren SceneCapture ile render hedefine
// çizilir (Slate fırçası), yavaşça döner. Ayarlar → Görüntü'den kapatılabilir (FSmvUiPrefs::bMenuScene / bPreview3d).
// Uçuş başlarken GameMode bu aktörü yok eder (görüş hedefi piyona döner).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimumavTypes.h"
#include "SmvMenuScene.generated.h"

class ACaySimWorldSetup;
class ASmvProcTerrain;
class ASmvDroneActor;
class ACameraActor;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UPointLightComponent;
class ASmvPilotFigure;

UCLASS()
class SIMUMAV_API ASmvMenuScene : public AActor
{
	GENERATED_BODY()

public:
	ASmvMenuScene();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Önizlenecek araç (tip, katalog modeli, gövde rengi #rrggbb) */
	void SetPreview(const FString& Type, const FString& ModelKey, const FString& ColorHex);
	/** Önizleme görünür mü (görünmüyorsa yakalama durur) */
	void SetPreviewActive(bool bOn);
	/** Pilot önizlemesi (tam boy 3B figür); bFace: yüze yakın çerçeve */
	void SetPilotPreview(const FSmvAvatar& A, bool bFace);
	/** Fareyle sürükleyerek döndürme (derece) — otomatik dönüş bir süre durur */
	void AddPreviewYaw(float Deg);
	UTextureRenderTarget2D* GetPreviewTarget() const { return PreviewRT; }
	bool IsLive() const { return bLive; }

protected:
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneCaptureComponent2D> Capture;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> KeyLight;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> RimLight;
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> PreviewRT;
	UPROPERTY() TObjectPtr<ACaySimWorldSetup> WorldSetup;
	UPROPERTY() TObjectPtr<ASmvProcTerrain> Terrain;
	UPROPERTY() TObjectPtr<ASmvDroneActor> Flyer;
	UPROPERTY() TObjectPtr<ACameraActor> Cam;
	UPROPERTY() TObjectPtr<ASmvDroneActor> Preview;
	UPROPERTY() TObjectPtr<ASmvPilotFigure> PilotPrev;

private:
	bool bLive = false;
	bool bPreviewOn = false;
	double T0 = 0;
	float PreviewYaw = 0.f;
	float PreviewDistCm = 300.f;
	double LastChannelFix = 0;
	FString PrevType, PrevModel, PrevColor;
	double Lat0 = 40.0990, Lon0 = 33.4101, GroundMsl = 100.0;
	FVector CamPos = FVector::ZeroVector;
	FRotator CamRot = FRotator::ZeroRotator;
	bool bCamInit = false;
	bool bFaceZoom = false;
	float FaceK = 0.f;
	double ManualUntil = 0;
	FString PilotKey;

	void BuildWorld();
	FVector EnuToWorld(double E, double N, double Agl) const;
	void ApplyPreviewChannels();
	void UpdateCamera(float Dt, const FVector& DronePos, const FVector& DroneFwd);
};
