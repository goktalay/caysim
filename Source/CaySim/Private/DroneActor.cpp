#include "DroneActor.h"
#include "CaySim.h"
#include "CaySimGeo.h"
#include "HepgoClient.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

#if CAYSIM_WITH_GLTFRUNTIME
#include "glTFRuntimeFunctionLibrary.h"
#include "glTFRuntimeAssetActor.h"
#endif

ACaySimDroneActor::ACaySimDroneActor()
	: Buffer(0.08, 256)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Body = CreateDefaultSubobject<USceneComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	PlaceholderMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Placeholder"));
	PlaceholderMesh->SetupAttachment(Body);
	PlaceholderMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderMesh->SetCastShadow(true);
}

void ACaySimDroneActor::BeginPlay()
{
	Super::BeginPlay();
	Buffer.SetDelay(InterpolationDelayS);
	if (!GltfActor) BuildPlaceholder();
}

void ACaySimDroneActor::SetGeoTransformActor(AActor* InActor)
{
	GeoActor = InActor;
	Geo = Cast<ICaySimGeoTransform>(InActor);
	if (!Geo && InActor)
	{
		UE_LOG(LogCaySim, Warning, TEXT("DroneActor: verilen aktör ICaySimGeoTransform uygulamıyor"));
	}
}

void ACaySimDroneActor::Setup(const FCaySimSession& Session, UHepgoClient* Client)
{
	DroneType = Session.Type;
	Model = Session.Model;
	Hepgo = Client;
	Buffer.Reset();
	BuildPlaceholder();

	// HEPGO'daki hazır GLB varsa indir (Bearer) ve çalışma zamanında yükle
	if (Model.bReady && !Model.GlbUrl.IsEmpty() && Client)
	{
#if CAYSIM_WITH_GLTFRUNTIME
		TWeakObjectPtr<ACaySimDroneActor> WeakThis(this);
		Client->FetchBytes(Model.GlbUrl, FHepgoBytesNative::CreateLambda([WeakThis](bool bOk, const TArray<uint8>& Bytes)
		{
			if (ACaySimDroneActor* Self = WeakThis.Get())
			{
				if (bOk && Bytes.Num() > 12) Self->LoadGlb(Bytes);
				else UE_LOG(LogCaySim, Warning, TEXT("GLB indirilemedi: %s"), *Self->Model.GlbUrl);
			}
		}));
#else
		UE_LOG(LogCaySim, Log, TEXT("glTFRuntime eklentisi yok: '%s' için dahili yer tutucu gövde kullanılıyor"), *Model.Name);
#endif
	}
	ApplyOrient(Model.Orient);
	OnModelReady.Broadcast();
}

FVector ACaySimDroneActor::AxisFromString(const FString& S)
{
	const FString K = S.ToLower();
	if (K == TEXT("+x")) return FVector(1, 0, 0);
	if (K == TEXT("-x")) return FVector(-1, 0, 0);
	if (K == TEXT("+y")) return FVector(0, 1, 0);
	if (K == TEXT("-y")) return FVector(0, -1, 0);
	if (K == TEXT("-z")) return FVector(0, 0, -1);
	return FVector(0, 0, 1);
}

void ACaySimDroneActor::ApplyOrient(const FCaySimModelOrient& Orient)
{
	// GLB (sağ el, Y yukarı) dosya eksenleri: forward f, up u → gövde çerçevesi (x ileri, z yukarı, y sağ).
	// Web: R = [f×u, f, u]ᵀ (three.js, z ileri). Unreal'de gövde: X ileri, Y sağ, Z yukarı (sol el).
	// glTFRuntime varlığı yüklerken Y-up → Z-up dönüşümünü kendisi yapar; burada yalnız burun ekseni düzeltmesi + ölçek + ofset.
	if (!GltfActor) return;
	const FVector F = AxisFromString(Orient.Forward);
	const FVector U = AxisFromString(Orient.Up);
	// glTF (x sağ, y yukarı, z ileriye doğru -z) → glTFRuntime varsayılan dönüşümü: UE X = -glTF Z, UE Y = glTF X, UE Z = glTF Y
	auto ToUe = [](const FVector& G) { return FVector(-G.Z, G.X, G.Y); };
	const FVector Fu = ToUe(F), Uu = ToUe(U);
	const FVector Ru = FVector::CrossProduct(Uu, Fu); // sol el: sağ = up × forward
	const FMatrix M(Fu, Ru, Uu, FVector::ZeroVector); // satırlar: istenen X, Y, Z eksenlerinin dosya eksenleri
	const FQuat Q = M.ToQuat().Inverse();
	GltfActor->SetActorRelativeRotation(Q.Rotator());
	GltfActor->SetActorRelativeScale3D(FVector(Orient.Scale));
	GltfActor->SetActorRelativeLocation(FVector(Orient.Offset.X, Orient.Offset.Y, Orient.Offset.Z) * 100.0);
}

void ACaySimDroneActor::LoadGlb(const TArray<uint8>& Bytes)
{
#if CAYSIM_WITH_GLTFRUNTIME
	FglTFRuntimeConfig Cfg;
	Cfg.TransformBaseType = EglTFRuntimeTransformBaseType::YForward; // glTF -Z ileri → UE +X ileri ayarı
	UglTFRuntimeAsset* Asset = UglTFRuntimeFunctionLibrary::glTFLoadAssetFromData(Bytes, Cfg);
	if (!Asset)
	{
		UE_LOG(LogCaySim, Warning, TEXT("GLB çözümlenemedi (%d bayt)"), Bytes.Num());
		return;
	}
	AglTFRuntimeAssetActor* A = GetWorld()->SpawnActorDeferred<AglTFRuntimeAssetActor>(AglTFRuntimeAssetActor::StaticClass(), FTransform::Identity, this);
	if (!A) return;
	A->Asset = Asset;
	A->FinishSpawning(FTransform::Identity);
	A->AttachToComponent(Body, FAttachmentTransformRules::KeepRelativeTransform);
	GltfActor = A;
	PlaceholderMesh->SetVisibility(false, true);
	// Pervane düğümleri: adı prop|propeller|rotor|pervane içeren bileşenler
	Propellers.Reset();
	TArray<USceneComponent*> Comps;
	A->GetComponents<USceneComponent>(Comps);
	for (USceneComponent* C : Comps)
	{
		const FString N = C->GetName().ToLower();
		if (N.Contains(TEXT("prop")) || N.Contains(TEXT("rotor")) || N.Contains(TEXT("pervane"))) Propellers.Add(C);
		else if (N.Contains(TEXT("aileron_l")) || N.Contains(TEXT("kanatcik_sol"))) AileronL = C;
		else if (N.Contains(TEXT("aileron_r")) || N.Contains(TEXT("kanatcik_sag"))) AileronR = C;
		else if (N.Contains(TEXT("elevator")) || N.Contains(TEXT("irtifa"))) Elevator = C;
		else if (N.Contains(TEXT("rudder")) || N.Contains(TEXT("dumen"))) Rudder = C;
	}
	ApplyOrient(Model.Orient);
	// Yer ofseti: sınır kutusu altı
	FVector Origin, Extent;
	A->GetActorBounds(false, Origin, Extent);
	GroundOffsetM = (float)((Origin.Z - Extent.Z) - GetActorLocation().Z) / 100.f;
	OnModelReady.Broadcast();
	UE_LOG(LogCaySim, Log, TEXT("GLB yüklendi: %s (%d bileşen, %d pervane)"), *Model.Name, Comps.Num(), Propellers.Num());
#endif
}

void ACaySimDroneActor::BuildPlaceholder()
{
	UStaticMesh* Mesh = nullptr;
	int32 PropCount = 0;
	float ArmM = 0.3f;
	switch (DroneType)
	{
	case ECaySimDroneType::FixedWing: Mesh = BuiltinMeshes.FixedWing; PropCount = 1; ArmM = 0.f; break;
	case ECaySimDroneType::Vtol:      Mesh = BuiltinMeshes.FixedWing; PropCount = 5; ArmM = 0.5f; break;
	case ECaySimDroneType::Hexarotor: Mesh = BuiltinMeshes.Hexa;      PropCount = 6; ArmM = 0.45f; break;
	case ECaySimDroneType::Helicopter:Mesh = BuiltinMeshes.Heli;      PropCount = 2; ArmM = 0.f; break;
	default:                          Mesh = BuiltinMeshes.Quad;      PropCount = 4; ArmM = 0.3f; break;
	}
	if (GltfActor)
	{
		GltfActor->Destroy();
		GltfActor = nullptr;
	}
	PlaceholderMesh->SetStaticMesh(Mesh);
	PlaceholderMesh->SetVisibility(Mesh != nullptr, true);
	if (Mesh)
	{
		const FBoxSphereBounds B = Mesh->GetBounds();
		GroundOffsetM = (float)(B.Origin.Z - B.BoxExtent.Z) / 100.f;
	}
	BuildPlaceholderProps(PropCount, ArmM);
}

void ACaySimDroneActor::BuildPlaceholderProps(int32 Count, float ArmM)
{
	for (TObjectPtr<USceneComponent>& P : Propellers)
	{
		if (P && P->GetOwner() == this) P->DestroyComponent();
	}
	Propellers.Reset();
	if (!BuiltinMeshes.Propeller || Count <= 0) return;
	for (int32 i = 0; i < Count; ++i)
	{
		UStaticMeshComponent* P = NewObject<UStaticMeshComponent>(this, *FString::Printf(TEXT("Prop%d"), i));
		P->SetStaticMesh(BuiltinMeshes.Propeller);
		P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		P->SetupAttachment(Body);
		P->RegisterComponent();
		FVector Pos = FVector::ZeroVector;
		FRotator Rot = FRotator::ZeroRotator;
		if (DroneType == ECaySimDroneType::FixedWing || (DroneType == ECaySimDroneType::Vtol && i == 0))
		{
			Pos = FVector(0.9f, 0.f, 0.f) * 100.f; // burun pervanesi (çekici)
			Rot = FRotator(90.f, 0.f, 0.f);          // disk ileri baksın
		}
		else if (DroneType == ECaySimDroneType::Helicopter)
		{
			Pos = (i == 0) ? FVector(0.f, 0.f, 0.35f) * 100.f : FVector(-1.1f, 0.05f, 0.25f) * 100.f;
			Rot = (i == 0) ? FRotator::ZeroRotator : FRotator(0.f, 0.f, 90.f);
		}
		else
		{
			const int32 N = (DroneType == ECaySimDroneType::Vtol) ? Count - 1 : Count;
			const int32 k = (DroneType == ECaySimDroneType::Vtol) ? i - 1 : i;
			const float Ang = (2.f * PI * k) / FMath::Max(1, N) + PI / N; // kollar arasına
			Pos = FVector(FMath::Cos(Ang) * ArmM, FMath::Sin(Ang) * ArmM, 0.05f) * 100.f;
		}
		P->SetRelativeLocationAndRotation(Pos, Rot);
		Propellers.Add(P);
	}
}

void ACaySimDroneActor::PushTelemetry(const FCaySimTelemetry& Tele)
{
	Latest = Tele;
	const double LocalNow = Hepgo.IsValid() ? Hepgo->NowServer() : FPlatformTime::Seconds();
	Buffer.Push(Tele, LocalNow);
}

void ACaySimDroneActor::ApplyPoseDirect(const FCaySimPose& Pose)
{
	Latest = Pose.Latest;
	ApplyPose(Pose);
}

void ACaySimDroneActor::GetRenderedPose(float& RollRad, float& PitchRad, float& YawRad) const
{
	RollRad = Rendered.Roll; PitchRad = Rendered.Pitch; YawRad = Rendered.Yaw;
}

void ACaySimDroneActor::ApplyPose(const FCaySimPose& Pose)
{
	if (!Pose.bValid) return;
	Rendered = Pose;

	FVector Loc;
	FQuat EnuRot = FQuat::Identity;
	if (Geo)
	{
		const double H = Geo->MslToWorldHeight(Pose.Lat, Pose.Lon, Pose.AltMsl);
		Loc = Geo->GeoToUnreal(Pose.Lon, Pose.Lat, H);
		EnuRot = Geo->EnuToUnrealRotation(Loc);
	}
	else
	{
		// Georeference yok: ev konumunu merkez al, düz ENU (X doğu, Y kuzey, Z yukarı → UE'ye Y ters)
		const double Lat0 = Latest.bHasHome ? Latest.HomeLat : Pose.Lat;
		const double Lon0 = Latest.bHasHome ? Latest.HomeLon : Pose.Lon;
		const double H0 = Latest.bHasHome ? Latest.HomeAlt : 0.0;
		const FVector Enu = CaySimGeo::GeoToEnuMeters(Pose.Lon, Pose.Lat, Pose.AltMsl, Lon0, Lat0, H0);
		Loc = bEnuXNorth ? FVector(Enu.Y, Enu.X, Enu.Z) * 100.0 : FVector(Enu.X, -Enu.Y, Enu.Z) * 100.0;
	}

	// Yer ile kesişmeyi engelle (gövde altı)
	double Ground;
	if (Geo && Geo->SampleGroundHeight(Pose.Lat, Pose.Lon, Ground))
	{
		const double Hw = Geo->MslToWorldHeight(Pose.Lat, Pose.Lon, Pose.AltMsl);
		bOnGround = (Hw - Ground) < 0.5 + FMath::Abs(GroundOffsetM);
		if (Hw + GroundOffsetM < Ground)
		{
			const FVector Lifted = Geo->GeoToUnreal(Pose.Lon, Pose.Lat, Ground - GroundOffsetM);
			Loc = Lifted;
		}
	}
	else
	{
		bOnGround = Pose.AltRel < 0.5 + FMath::Abs(GroundOffsetM);
	}

	const float Roll = bInvertRoll ? -Pose.Roll : Pose.Roll;
	const float Pitch = bInvertPitch ? -Pose.Pitch : Pose.Pitch;
	const FRotator Local = CaySimGeo::NedToUnrealRotator(Roll, Pitch, Pose.Yaw, bEnuXNorth);
	// Cesium: yerel ENU çerçevesi dünya eğriliğiyle döner → önce ENU→Unreal, sonra gövde yönelimi
	const FQuat WorldRot = EnuRot * Local.Quaternion();
	Root->SetWorldLocationAndRotation(Loc, WorldRot);
}

void ACaySimDroneActor::AnimateProps(float Dt, float ThrottlePct, bool bArmed)
{
	const float Target = bArmed ? FMath::Max(0.15f, ThrottlePct / 100.f) : 0.f;
	PropPhase += Dt * PropMaxRps * Target * 360.f;
	if (PropPhase > 360.f) PropPhase -= 360.f;
	for (int32 i = 0; i < Propellers.Num(); ++i)
	{
		USceneComponent* P = Propellers[i];
		if (!P) continue;
		const float Dir = (i % 2 == 0) ? 1.f : -1.f; // karşıt dönüşlü çiftler
		FRotator R = P->GetRelativeRotation();
		// Disk ekseni: multirotorda Z (yaw), çekici pervanede X (roll)
		const bool bForwardAxis = (DroneType == ECaySimDroneType::FixedWing) || (DroneType == ECaySimDroneType::Vtol && i == 0);
		if (bForwardAxis) R.Roll = PropPhase * Dir;
		else R.Yaw = PropPhase * Dir;
		P->SetRelativeRotation(R);
	}
}

void ACaySimDroneActor::AnimateSurfaces(const TArray<int32>& Rc)
{
	if (Rc.Num() < 4) return;
	auto Norm = [](int32 V) { return V > 0 ? FMath::Clamp((V - 1500) / 500.f, -1.f, 1.f) : 0.f; };
	const float Ail = Norm(Rc[0]), Ele = Norm(Rc[1]), Rud = Norm(Rc[3]);
	if (AileronL) AileronL->SetRelativeRotation(FRotator(-Ail * SurfaceMaxDeg, 0, 0));
	if (AileronR) AileronR->SetRelativeRotation(FRotator(Ail * SurfaceMaxDeg, 0, 0));
	if (Elevator) Elevator->SetRelativeRotation(FRotator(-Ele * SurfaceMaxDeg, 0, 0));
	if (Rudder) Rudder->SetRelativeRotation(FRotator(0, Rud * SurfaceMaxDeg, 0));
}

void ACaySimDroneActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Buffer.IsEmpty())
	{
		const double Now = Hepgo.IsValid() ? Hepgo->NowServer() : FPlatformTime::Seconds();
		ApplyPose(Buffer.Sample(Now));
	}
	AnimateProps(DeltaSeconds, Rendered.Throttle, Latest.bArmed);
	if (DroneType == ECaySimDroneType::FixedWing || DroneType == ECaySimDroneType::Vtol) AnimateSurfaces(Latest.Rc);
}
