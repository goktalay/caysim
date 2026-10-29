// Uçuş içi 3B mini harita yakalaması (bkz. SmvMapCam.h).
#include "SmvMapCam.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"

namespace
{
	/** eğik bakış (derece aşağı): üstten bakışa yakın ama arazi kabartması ve binalar 3B görünsün */
	constexpr float GSmvMapTilt = 58.f;
}

ASmvMapCam::ASmvMapCam()
{
	PrimaryActorTick.bCanEverTick = false;
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
	MapCap->ShowFlags.SetFog(false);
	MapCap->ShowFlags.SetVolumetricFog(false);
	MapCap->ShowFlags.SetCloud(false);
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

void ASmvMapCam::Step(float Dt, const FVector& Target, float RangeM, bool bBig, bool bView, const FVector& ViewLoc, const FRotator& ViewRot, float ViewFov)
{
	EnsureTargets(bBig);
	const double Now = FPlatformTime::Seconds();
	// hedef ve yakınlık yumuşak izlenir (titreşimsiz); büyük sıçramada (sıfırlama / ışınlanma) doğrudan
	const float K = 1.f - FMath::Exp(-FMath::Clamp(Dt, 0.f, 0.1f) * 6.f);
	if (!bInit || FVector::Dist(SmoothTarget, Target) > 200000.0) { SmoothTarget = Target; SmoothRange = RangeM; bInit = true; }
	SmoothTarget = FMath::Lerp(SmoothTarget, Target, (double)K);
	SmoothRange = FMath::Lerp(SmoothRange, RangeM, K);
	// kuzey yukarı: kamera hedefin güneyinde (+Y), kuzeye (−Y) bakar; uzaklık görünen yarı genişliği verir
	const float Half = FMath::Tan(FMath::DegreesToRadians(CamFov * 0.5f));
	const double D = SmoothRange * 100.0 / Half;
	const double Tr = FMath::DegreesToRadians((double)GSmvMapTilt);
	CamLoc = SmoothTarget + FVector(0.0, D * FMath::Cos(Tr), D * FMath::Sin(Tr));
	CamRot = FRotator(-GSmvMapTilt, -90.f, 0.f);
	const double Every = bBig ? 1.0 / 30.0 : 1.0 / 12.0;
	if (Now - MapAt >= Every || bBig != bBigNow)
	{
		MapAt = Now; bBigNow = bBig;
		MapCap->FOVAngle = CamFov;
		MapCap->SetWorldLocationAndRotation(CamLoc, CamRot);
		MapCap->CaptureScene();
	}
	if (bView && Now - ViewAt >= 1.0 / 24.0)
	{
		ViewAt = Now;
		ViewCap->FOVAngle = FMath::Clamp(ViewFov, 30.f, 120.f);
		ViewCap->SetWorldLocationAndRotation(ViewLoc, ViewRot);
		ViewCap->CaptureScene();
	}
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
