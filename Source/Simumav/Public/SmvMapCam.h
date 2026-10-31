// Uçuş içi 3B mini harita (kullanıcı 2026-10-29): dronun üstünde, kuzeyi yukarı tutan eğik bakışlı ayrı bir sahne yakalaması.
// Gerçek dünya parkurunda Cesium karolarını (Google 3B / World Terrain), Fab haritalarında haritanın kendisini çizer — aynı dünyanın
// ikinci görünümü olduğu için ek veri / indirme yok. Ayrıca büyük harita kipinde oyuncunun kamera görünümünü küçük pencereye çeken ikinci
// yakalama. İşaretler (dron, rakipler, görev çizgisi) Slate'te çizilir: Project() dünya konumunu yakalama görüntüsünde 0..1 konuma çevirir.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SmvMapCam.generated.h"

class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

UCLASS()
class SIMUMAV_API ASmvMapCam : public AActor
{
	GENERATED_BODY()

public:
	ASmvMapCam();

	/** Her kare: Target dronun konumu; RangeM görünen yarı genişlik (m); bBig büyük harita (yüksek çözünürlük, sık yakalama);
	 *  bView kamera penceresi (ViewLoc / ViewRot: oyuncu kamerası). */
	void Step(float Dt, const FVector& Target, float RangeM, bool bBig, bool bView, const FVector& ViewLoc, const FRotator& ViewRot, float ViewFov, float YawDeg = 0.f, float TiltDeg = 58.f);
	/** görüntü konumundan (0..1) dünya ışını (Ctrl + tık: koordinata git) */
	void Deproject(const FVector2D& Uv, FVector& OutOrigin, FVector& OutDir) const;
	UTextureRenderTarget2D* GetMapTarget() const { return MapRt; }
	UTextureRenderTarget2D* GetViewTarget() const { return ViewRt; }
	/** Dünya konumu → harita görüntüsünde (0..1, 0..1); kamera arkasındaysa false */
	bool Project(const FVector& World, FVector2D& OutUv) const;
	/** Haritada yukarısı kuzey; ekranda 0 derece = yukarı */
	/** ekranda yukarısı: kuzeyden bu kadar saat yönünde (fareyle döndürme) */
	float NorthUpYaw() const { return SmoothYaw; }
	virtual void Tick(float Dt) override;
	virtual void EndPlay(const EEndPlayReason::Type Why) override;

protected:
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneCaptureComponent2D> MapCap;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneCaptureComponent2D> ViewCap;
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> MapRt;
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> ViewRt;

private:
	FVector CamLoc = FVector::ZeroVector;
	FRotator CamRot = FRotator::ZeroRotator;
	float CamFov = 50.f, Aspect = 1.f;
	double MapAt = 0, ViewAt = 0;
	FVector SmoothTarget = FVector::ZeroVector;
	bool bInit = false, bBigNow = false;
	float SmoothRange = 600.f, SmoothYaw = 0.f, SmoothTilt = 58.f;
	void EnsureTargets(bool bBig);
	/** Cesium karo seçimine eklenen harita kamerası (-1 yok); harita kapanınca 1 sn sonra çıkarılır */
	int32 CesiumCamId = -1;
	double StepAt = 0, HideAt = -100.0, DiagAt = -100.0;
};
