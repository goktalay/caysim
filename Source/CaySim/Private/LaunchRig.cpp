#include "LaunchRig.h"
#include "CaySim.h"
#include "CaySimGeo.h"
#include "DroneActor.h"
#include "CesiumWorldSetup.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

ACaySimLaunchRig::ACaySimLaunchRig()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	RampBase = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RampBase"));
	RampBase->SetupAttachment(Root);
	RampArm = CreateDefaultSubobject<USceneComponent>(TEXT("RampArm"));
	RampArm->SetupAttachment(RampBase);
	RampArm->SetRelativeLocation(FVector(0, 0, 80.f)); // menteşe yüksekliği 0,8 m
	RampRail = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RampRail"));
	RampRail->SetupAttachment(RampArm);
	RampCarriage = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RampCarriage"));
	RampCarriage->SetupAttachment(RampRail);

	Vehicle = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Vehicle"));
	Vehicle->SetupAttachment(Root);
	static const TCHAR* WheelNames[4] = { TEXT("WheelFL"), TEXT("WheelFR"), TEXT("WheelRL"), TEXT("WheelRR") };
	static const FVector WheelPos[4] = { FVector(150, -90, 35), FVector(150, 90, 35), FVector(-150, -90, 35), FVector(-150, 90, 35) };
	for (int32 i = 0; i < 4; ++i)
	{
		UStaticMeshComponent* Wh = CreateDefaultSubobject<UStaticMeshComponent>(WheelNames[i]);
		Wh->SetupAttachment(Vehicle);
		Wh->SetRelativeLocation(WheelPos[i]);
		Wheels.Add(Wh);
	}

	Runway = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Runway"));
	Runway->SetupAttachment(Root);

	for (UStaticMeshComponent* C : { RampBase.Get(), RampRail.Get(), RampCarriage.Get(), Vehicle.Get(), Runway.Get() })
	{
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	for (TObjectPtr<UStaticMeshComponent>& W : Wheels) W->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ACaySimLaunchRig::Setup(const FCaySimLaunchConfig& InConfig, ACaySimWorldSetup* InWorldSetup, ACaySimDroneActor* Drone, double Lat, double Lon, double GroundMsl)
{
	Config = InConfig;
	WorldSetup = InWorldSetup;
	DroneActor = Drone;
	Lat0 = Lat; Lon0 = Lon; Ground0 = GroundMsl;
	Phase = ECaySimLaunchPhase::Idle;
	PhaseT = 0.f;
	VehicleSpeedNow = 0.f;
	VehicleDistM = 0.f;
	bArmedSeen = false;
	LastReleaseSpeedKmh = 0.f;
	TargetTakeoffAltM = -1.f;

	RampBase->SetStaticMesh(RampBaseMesh);
	RampRail->SetStaticMesh(RampRailMesh);
	RampCarriage->SetStaticMesh(RampCarriageMesh);
	Vehicle->SetStaticMesh(VehicleMesh);
	for (TObjectPtr<UStaticMeshComponent>& W : Wheels) W->SetStaticMesh(VehicleWheelMesh);
	Runway->SetStaticMesh(RunwayMesh);

	ShowFor(Config.Method);
	PlaceAtGround();
	SetRampAngle(Config.RampAngleDeg);
	SetRampAzimuth(Config.RampAzimuthDeg);
	RampCarriage->SetRelativeLocation(FVector::ZeroVector);
	if (Config.Method == ECaySimLaunchMethod::Runway)
	{
		// Pist: merkezden heading yönünde 600 m, 20 m geniş (mesh 1×1 m varsayımı)
		Runway->SetRelativeScale3D(FVector(600.f, 20.f, 1.f));
		Runway->SetRelativeLocation(FVector(250.f * 100.f, 0, 2.f));
	}
}

void ACaySimLaunchRig::PlaceAtGround()
{
	ACaySimWorldSetup* W = WorldSetup.Get();
	FVector Loc = FVector::ZeroVector;
	FQuat Enu = FQuat::Identity;
	if (W)
	{
		const double H = W->MslToWorldHeight(Lat0, Lon0, Ground0);
		Loc = W->GeoToUnreal(Lon0, Lat0, H);
		Enu = W->EnuToUnrealRotation(Loc);
	}
	// Yön: heading (kuzeyden) → Unreal yaw (Doğu = 0)
	const FRotator Yaw = CaySimGeo::NedToUnrealRotator(0.f, 0.f, FMath::DegreesToRadians(Config.HeadingDeg), false);
	SetActorLocationAndRotation(Loc, Enu * Yaw.Quaternion());
}

void ACaySimLaunchRig::ShowFor(ECaySimLaunchMethod M)
{
	const bool bRamp = (M == ECaySimLaunchMethod::Ramp);
	const bool bVeh = (M == ECaySimLaunchMethod::Vehicle);
	const bool bRw = (M == ECaySimLaunchMethod::Runway);
	RampBase->SetVisibility(bRamp, true);
	Vehicle->SetVisibility(bVeh, true);
	Runway->SetVisibility(bRw, false);
}

void ACaySimLaunchRig::SetRampAngle(float Deg)
{
	Config.RampAngleDeg = FMath::Clamp(Deg, 5.f, 45.f);
	// Kol menteşesi: pozitif pitch burnu yukarı kaldırır (ray ileri-yukarı)
	RampArm->SetRelativeRotation(FRotator(Config.RampAngleDeg, 0.f, 0.f));
}

void ACaySimLaunchRig::SetRampAzimuth(float Deg)
{
	Config.RampAzimuthDeg = FMath::Clamp(Deg, -45.f, 45.f);
	RampBase->SetRelativeRotation(FRotator(0.f, Config.RampAzimuthDeg, 0.f));
}

void ACaySimLaunchRig::SetVehicleSpeed(float Ms)
{
	Config.VehicleSpeedMs = FMath::Clamp(Ms, 3.f, 30.f);
}

void ACaySimLaunchRig::SetPhase(ECaySimLaunchPhase P)
{
	if (Phase == P) return;
	Phase = P;
	PhaseT = 0.f;
	OnPhaseChanged.Broadcast(P);
	UE_LOG(LogCaySim, Log, TEXT("Kalkış evresi: %d"), (int32)P);
}

void ACaySimLaunchRig::OnLaunchEvent(const FCaySimLaunchEvent& E)
{
	if (E.Method != Config.Method)
	{
		Config.Method = E.Method;
		ShowFor(E.Method);
	}
	if (E.AngleDeg > 0.f) SetRampAngle(E.AngleDeg);
	if (E.ReleaseKmh > 0.f) SetVehicleSpeed(E.ReleaseKmh / 3.6f); // araç: bırakma hızı (`launch` evresi)
	// `speed_kmh` (released; üç yöntemde de o anki SITL yer hızı) ve `alt` (armed; hedef kalkış irtifası) yalnız bilgi/HUD içindir
	if (E.SpeedKmh > 0.f) LastReleaseSpeedKmh = E.SpeedKmh;
	if (E.AltM >= 0.f) TargetTakeoffAltM = E.AltM;
	// HEPGO §13 evreleri: armed → launch (itiş/araç hareketi) → released (dron ayrıldı)
	if (E.Phase == TEXT("armed")) SetPhase(ECaySimLaunchPhase::Armed);
	else if (E.Phase == TEXT("launch")) SetPhase(ECaySimLaunchPhase::Rolling);
	else if (E.Phase == TEXT("released")) SetPhase(ECaySimLaunchPhase::Released);
}

void ACaySimLaunchRig::OnTelemetry(const FCaySimTelemetry& T)
{
	// Olay gelmezse evreleri telemetriden türet
	switch (Phase)
	{
	case ECaySimLaunchPhase::Idle:
		if (T.bArmed) { bArmedSeen = true; SetPhase(ECaySimLaunchPhase::Armed); }
		break;
	case ECaySimLaunchPhase::Armed:
		if (!T.bArmed) SetPhase(ECaySimLaunchPhase::Idle);
		else if (T.Groundspeed > 0.5f || T.Throttle > 30.f) SetPhase(ECaySimLaunchPhase::Rolling);
		break;
	case ECaySimLaunchPhase::Rolling:
		if (T.Groundspeed >= ReleaseSpeedMs || T.AltRel > 1.0) SetPhase(ECaySimLaunchPhase::Released);
		break;
	case ECaySimLaunchPhase::Released:
		if (T.AltRel > AirborneAltM) SetPhase(ECaySimLaunchPhase::Airborne);
		break;
	case ECaySimLaunchPhase::Airborne:
		if (PhaseT > 5.f) SetPhase(ECaySimLaunchPhase::Done);
		break;
	default:
		if (!T.bArmed && T.AltRel < 0.5 && Phase == ECaySimLaunchPhase::Done) SetPhase(ECaySimLaunchPhase::Idle); // yeniden kalkış
		break;
	}
}

void ACaySimLaunchRig::UpdateRamp(float Dt)
{
	// Taşıyıcı: Rolling/Released sırasında ray boyunca strok; sonra geri sarma
	float Pos = 0.f;
	if (Phase == ECaySimLaunchPhase::Rolling || Phase == ECaySimLaunchPhase::Released)
	{
		Pos = FMath::Clamp(PhaseT / RampStrokeS, 0.f, 1.f);
		Pos = Pos * Pos; // ivmeli
	}
	else if (Phase == ECaySimLaunchPhase::Airborne || Phase == ECaySimLaunchPhase::Done)
	{
		Pos = FMath::Clamp(1.f - PhaseT / 3.f, 0.f, 1.f); // yavaş geri
	}
	RampCarriage->SetRelativeLocation(FVector(Pos * RampLengthM * 100.f, 0.f, 0.f));
}

void ACaySimLaunchRig::UpdateVehicle(float Dt)
{
	float Target = 0.f;
	if (Phase == ECaySimLaunchPhase::Rolling || Phase == ECaySimLaunchPhase::Released) Target = Config.VehicleSpeedMs;
	else if (Phase == ECaySimLaunchPhase::Airborne) Target = FMath::Max(0.f, Config.VehicleSpeedMs * (1.f - PhaseT / VehicleBrakeS));
	VehicleSpeedNow = FMath::FInterpTo(VehicleSpeedNow, Target, Dt, 2.f);
	VehicleDistM += VehicleSpeedNow * Dt;
	if (Phase == ECaySimLaunchPhase::Idle) { VehicleDistM = 0.f; VehicleSpeedNow = 0.f; }
	Vehicle->SetRelativeLocation(FVector(VehicleDistM * 100.f, 0.f, 0.f));
	const float WheelR = 0.35f;
	WheelSpin += (VehicleSpeedNow / WheelR) * Dt * (180.f / UE_PI);
	for (TObjectPtr<UStaticMeshComponent>& W : Wheels)
	{
		if (W) W->SetRelativeRotation(FRotator(-WheelSpin, 0.f, 0.f));
	}
}

void ACaySimLaunchRig::AnchorToDrone()
{
	// Düzeneğin bağlama noktası (rampa taşıyıcısı / araç kasası) dronun konumuna oturtulur; yön korunur.
	ACaySimDroneActor* D = DroneActor.Get();
	if (!D) return;
	// TObjectPtr → ham işaretçi açıkça (.Get()); C tarzı dönüşüm TObjectPtr üzerinde belirsiz olabilir
	USceneComponent* Attach = (Config.Method == ECaySimLaunchMethod::Ramp) ? static_cast<USceneComponent*>(RampCarriage.Get()) : static_cast<USceneComponent*>(Vehicle.Get());
	if (!Attach) return;
	const FVector BedOffset = (Config.Method == ECaySimLaunchMethod::Vehicle) ? FVector(0, 0, 150.f) : FVector(0, 0, 20.f); // kasa/kızak üstü
	const FVector AttachWorld = Attach->GetComponentTransform().TransformPosition(BedOffset);
	const FVector Delta = D->GetActorLocation() - AttachWorld;
	// Yalnız yatay kayma (SITL yer kayması); dikey yer hizası PlaceAtGround'dan kalır
	SetActorLocation(GetActorLocation() + FVector(Delta.X, Delta.Y, 0.f));
}

void ACaySimLaunchRig::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	PhaseT += DeltaSeconds;
	if (bAnchorToDrone && (Phase == ECaySimLaunchPhase::Idle || Phase == ECaySimLaunchPhase::Armed || Phase == ECaySimLaunchPhase::Rolling)
		&& (Config.Method == ECaySimLaunchMethod::Ramp || Config.Method == ECaySimLaunchMethod::Vehicle))
	{
		AnchorToDrone();
	}
	switch (Config.Method)
	{
	case ECaySimLaunchMethod::Ramp:    UpdateRamp(DeltaSeconds); break;
	case ECaySimLaunchMethod::Vehicle: UpdateVehicle(DeltaSeconds); break;
	default: break;
	}
}
