// Uçuş içi 3B mini harita yakalaması (bkz. SmvMapCam.h).
#include "SmvMapCam.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "CesiumWorldSetup.h"

namespace
{
	/** eğik bakış (derece aşağı): üstten bakışa yakın ama arazi kabartması ve binalar 3B görünsün */
	constexpr float GSmvMapTilt = 58.f;
}

ASmvMapCam::ASmvMapCam()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.5f;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	for (TObjectPtr<USceneCaptureComponent2D>* C : { &MapCap, &ViewCap })
	{
		*C = CreateDefaultSubobject<USceneCaptureComponent2D>(C == &MapCap ? TEXT("MapCapture") : TEXT("ViewCapture"));
		(*C)->SetupAttachment(Root);
		(*C)->bCaptureEveryFrame = false;
		(*C)->bCaptureOnMovement = false;
		(*C)->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		(*C)->ShowFlags.SetMotionBlur(false);
		(*C)->ShowFlags.SetDepthOfField(false);
	}
	MapCap->FOVAngle = CamFov;
	// harita: sis ve hacimsel bulut yok (yukarıdan net görünüm); otomatik pozlama kapalı değil — gündüz / gece sahneyle aynı
	// pozlama: yakalama seyrek (1/12 sn) ve kendi göz uyumu geçmişiyle başlıyordu → harita SİYAH açılıp yavaş yavaş aydınlanıyordu
	// (kullanıcı, 2026-10-30): durum kalıcı + uyum anında
	for (USceneCaptureComponent2D* C : { MapCap.Get(), ViewCap.Get() })
	{
		C->bAlwaysPersistRenderingState = true;
		C->PostProcessSettings.bOverride_AutoExposureSpeedUp = true;   C->PostProcessSettings.AutoExposureSpeedUp = 40.f;
		C->PostProcessSettings.bOverride_AutoExposureSpeedDown = true; C->PostProcessSettings.AutoExposureSpeedDown = 40.f;
		C->PostProcessBlendWeight = 1.f;
	}
	MapCap->ShowFlags.SetFog(false);
	MapCap->ShowFlags.SetVolumetricFog(false);
	MapCap->ShowFlags.SetCloud(false);
	// harita = yüzeyin ham rengi (taban rengi; ışık / pozlama / saat / hava etkisi yok): son renk yakalamasında harita tamamen SİYAH
	// çıkıyordu (alacakaranlık + yağmurda ve uzaktan bakışta göz uyumu / bulut katmanı). Saydam katman (yağmur, bulut kartları) çizilmez.
	MapCap->CaptureSource = ESceneCaptureSource::SCS_BaseColor;
	MapCap->ShowFlags.SetTranslucency(false);
	MapCap->ShowFlags.SetParticles(false);
	MapCap->ShowFlags.SetAtmosphere(false);
}

void ASmvMapCam::EnsureTargets(bool bBig)
{
	const int32 W = bBig ? 1600 : 640, H = bBig ? 900 : 480;
	if (!MapRt || MapRt->SizeX != W || MapRt->SizeY != H)
	{
		MapRt = NewObject<UTextureRenderTarget2D>(this);
		MapRt->ClearColor = FLinearColor(0.02f, 0.03f, 0.05f, 1.f);
		MapRt->InitAutoFormat(W, H);
		MapRt->UpdateResourceImmediate(true);
		MapCap->TextureTarget = MapRt;
	}
	if (!ViewRt)
	{
		ViewRt = NewObject<UTextureRenderTarget2D>(this);
		ViewRt->ClearColor = FLinearColor::Black;
		ViewRt->InitAutoFormat(640, 360);
		ViewRt->UpdateResourceImmediate(true);
		ViewCap->TextureTarget = ViewRt;
	}
	Aspect = (float)W / (float)H;
}

void ASmvMapCam::Step(float Dt, const FVector& Target, float RangeM, bool bBig, bool bView, const FVector& ViewLoc, const FRotator& ViewRot, float ViewFov, float YawDeg, float TiltDeg)
{
	EnsureTargets(bBig);
	const double Now = FPlatformTime::Seconds();
	StepAt = Now;
	// hedef ve yakınlık yumuşak izlenir (titreşimsiz); büyük sıçramada (sıfırlama / ışınlanma) doğrudan
	const float K = 1.f - FMath::Exp(-FMath::Clamp(Dt, 0.f, 0.1f) * 6.f);
	if (!bInit || FVector::Dist(SmoothTarget, Target) > 200000.0) { SmoothTarget = Target; SmoothRange = RangeM; bInit = true; }
	SmoothTarget = FMath::Lerp(SmoothTarget, Target, (double)K);
	SmoothRange = FMath::Lerp(SmoothRange, RangeM, K);
	SmoothYaw = SmoothYaw + FMath::FindDeltaAngleDegrees(SmoothYaw, YawDeg) * K;
	SmoothTilt = FMath::Lerp(SmoothTilt, FMath::Clamp(TiltDeg, 15.f, 89.f), K);
	// varsayılan kuzey yukarı (yaw 0: kamera hedefin güneyinde, kuzeye −Y bakar); fareyle döndürülür / eğilir; uzaklık görünen yarı genişliği verir
	const float Half = FMath::Tan(FMath::DegreesToRadians(CamFov * 0.5f));
	const double D = SmoothRange * 100.0 / Half;
	CamRot = FRotator(-SmoothTilt, -90.f + SmoothYaw, 0.f);
	CamLoc = SmoothTarget - CamRot.Vector() * D;
	(void)GSmvMapTilt;
	const double Every = bBig ? 1.0 / 30.0 : 1.0 / 12.0;
	if (Now - MapAt >= Every || bBig != bBigNow)
	{
		MapAt = Now; bBigNow = bBig;
		MapCap->FOVAngle = CamFov;
		MapCap->SetWorldLocationAndRotation(CamLoc, CamRot);
		MapCap->CaptureScene();
		// Cesium: harita görüşündeki karolar da yüklensin (yalnız oyuncu kamerası değil)
		CesiumCamId = ACaySimWorldSetup::UpsertExtraCamera(this, CesiumCamId, FVector2D(MapRt->SizeX, MapRt->SizeY), CamLoc, CamRot, CamFov);
	}
	if (bView && Now - ViewAt >= 1.0 / 24.0)
	{
		ViewAt = Now;
		ViewCap->FOVAngle = FMath::Clamp(ViewFov, 30.f, 120.f);
		ViewCap->SetWorldLocationAndRotation(ViewLoc, ViewRot);
		ViewCap->CaptureScene();
	}
}

void ASmvMapCam::Deproject(const FVector2D& Uv, FVector& OutOrigin, FVector& OutDir) const
{
	const double Half = FMath::Tan(FMath::DegreesToRadians((double)CamFov * 0.5));
	const double Y = (Uv.X - 0.5) * 2.0 * Half, Z = -(Uv.Y - 0.5) * 2.0 * Half / Aspect;
	OutOrigin = CamLoc;
	OutDir = CamRot.RotateVector(FVector(1.0, Y, Z)).GetSafeNormal();
}

bool ASmvMapCam::Project(const FVector& World, FVector2D& OutUv) const
{
	const FVector L = CamRot.UnrotateVector(World - CamLoc);   // X ileri, Y sağ, Z yukarı
	if (L.X <= 1.0) return false;
	const double Half = FMath::Tan(FMath::DegreesToRadians((double)CamFov * 0.5));
	OutUv.X = 0.5 + (L.Y / L.X) / (2.0 * Half);
	OutUv.Y = 0.5 - (L.Z / L.X) / (2.0 * Half) * Aspect;
	return true;
}

void ASmvMapCam::Tick(float Dt)
{
	Super::Tick(Dt);
	if (CesiumCamId >= 0 && FPlatformTime::Seconds() - StepAt > 1.0) { ACaySimWorldSetup::RemoveExtraCamera(this, CesiumCamId); CesiumCamId = -1; }
}

void ASmvMapCam::EndPlay(const EEndPlayReason::Type Why)
{
	ACaySimWorldSetup::RemoveExtraCamera(this, CesiumCamId);
	CesiumCamId = -1;
	Super::EndPlay(Why);
}
