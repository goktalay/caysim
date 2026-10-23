#include "SmvWorldActors.h"
#include "SimumavModule.h"
#include "SmvMotorSynth.h"
#include "CaySimGeo.h"
#include "SmvRace.h"
#include "SmvPyRandom.h"
#include "SmvFabContent.h"
#include "SmvGlbBody.h"
#include "SmvDataService.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"
#include "Misc/ConfigCacheIni.h"
#include "ProceduralMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"
#include "SmvPhysics.h"

namespace
{
	const TCHAR* GSmvCube = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* GSmvCyl = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const TCHAR* GSmvSphere = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	const TCHAR* GSmvCone = TEXT("/Engine/BasicShapes/Cone.Cone");
	const TCHAR* GSmvMat = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	/** Editor Python betiğinin ürettiği malzemeler (varsa) — yoksa BasicShapeMaterial */
	const TCHAR* GSmvTerrainMat = TEXT("/Game/Simumav/Materials/M_SmvTerrain.M_SmvTerrain");
	/** Hava duyarlı arazi (Wetness / Snow parametreleri; editor Python üretir) — yoksa M_SmvTerrain */
	const TCHAR* GSmvTerrainWxMat = TEXT("/Game/Simumav/Materials/M_SmvTerrainWx.M_SmvTerrainWx");
	const TCHAR* GSmvGlowMat = TEXT("/Game/Simumav/Materials/M_SmvGlow.M_SmvGlow");
}

// ---------------------------------------------------------------------------
// ASmvDroneActor
// ---------------------------------------------------------------------------

ASmvDroneActor::ASmvDroneActor()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(GSmvCube);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(GSmvCyl);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sph(GSmvSphere);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(GSmvCone);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(GSmvMat);
	CubeMesh = Cube.Object; CylinderMesh = Cyl.Object; SphereMesh = Sph.Object; ConeMesh = Cone.Object; BaseMaterial = Mat.Object;
	Motor = CreateDefaultSubobject<USmvMotorSynth>(TEXT("Motor"));
	Motor->SetupAttachment(Body);
}

UStaticMeshComponent* ASmvDroneActor::AddPart(UStaticMesh* M, const FVector& PosM, const FVector& ScaleM, const FRotator& Rot, const FLinearColor& Color, USceneComponent* Parent)
{
	if (!M) return nullptr;
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetStaticMesh(M);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetupAttachment(Parent ? Parent : Body.Get());
	C->RegisterComponent();
	// BasicShapes 100 cm birim ağlar → ölçek (m)
	C->SetRelativeLocationAndRotation(PosM * 100.f, Rot);
	C->SetRelativeScale3D(ScaleM);
	if (BaseMaterial)
	{
		UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(BaseMaterial, C);
		D->SetVectorParameterValue(TEXT("Color"), Color);
		C->SetMaterial(0, D);
	}
	Parts.Add(C);
	return C;
}

void ASmvDroneActor::AddRotor(const FVector& PosM, float DiaM, bool bForward, const FLinearColor& Color)
{
	USceneComponent* Hub = NewObject<USceneComponent>(this);
	Hub->SetupAttachment(Body);
	Hub->RegisterComponent();
	Hub->SetRelativeLocation(PosM * 100.f);
	Hubs.Add(Hub);
	// iki kanatlı pala: tek ince kutu (uçtan uca çap); taban sınıfı göbeği döndürür (kopter: yaw, çekici pervane: roll)
	if (bForward)
	{
		AddPart(CubeMesh, FVector::ZeroVector, FVector(0.006f, DiaM, DiaM * 0.09f), FRotator::ZeroRotator, Color, Hub);
		AddPart(ConeMesh, FVector(0.02f, 0, 0), FVector(0.04f, 0.04f, 0.04f), FRotator(-90, 0, 0), FLinearColor(0.75f, 0.75f, 0.78f), Hub);   // sivri göbek
	}
	else
	{
		AddPart(CubeMesh, FVector::ZeroVector, FVector(DiaM, DiaM * 0.09f, 0.006f), FRotator(0, 0, 4), Color, Hub);
		AddPart(CylinderMesh, FVector(0, 0, 0.004f), FVector(0.025f, 0.025f, 0.012f), FRotator::ZeroRotator, FLinearColor(0.75f, 0.75f, 0.78f), Hub);
	}
	Propellers.Add(Hub);
}

void ASmvDroneActor::ClearParts()
{
	for (TObjectPtr<UStaticMeshComponent>& P : Parts) if (IsValid(P)) P->DestroyComponent();
	for (TObjectPtr<USceneComponent>& H : Hubs)
	{
		Propellers.Remove(H);
		if (IsValid(H)) H->DestroyComponent();
	}
	Parts.Reset();
	Hubs.Reset();
	if (IsValid(FabMesh)) FabMesh->DestroyComponent();
	FabMesh = nullptr;
	if (IsValid(FabStatic)) FabStatic->DestroyComponent();
	FabStatic = nullptr;
	FabPropBones.Reset();
	FabPropRef.Reset();
	FabPropDir.Reset();
	if (GlbBody) GlbBody->Clear();
	bGlbBody = false;
}

bool ASmvDroneActor::BuildGlbModel(const FSmvCatalogModel& M)
{
	TArray<uint8> Bytes;
	if (M.GlbFile.IsEmpty() || !USmvDataService::LoadBundledGlb(M.GlbFile, Bytes)) return false;
	if (!GlbBody)
	{
		GlbBody = NewObject<USmvGlbBody>(this, TEXT("GlbBody"));
		GlbBody->SetupAttachment(Body);
		GlbBody->RegisterComponent();
	}
	FSmvGlbBuildOptions O;
	O.YawDeg = M.GlbYawDeg;
	O.Type = SmvType;
	O.Airframe = M.Airframe;
	O.bAddLiftRotors = M.bAddLiftRotors;
	if (!GlbBody->Build(Bytes, O, M.Key)) return false;
	bGlbBody = true;
	const FBox B = GlbBody->GetLocalBounds();
	// görünürlük ölçeği (web MODEL_SCALE 2,5): küçük dronlar büyütülür, ≥ 1,6 m açıklıklı uçaklar gerçek boyutta; kara aracı gerçek boyut
	const float SpanM = (float)FMath::Max(B.GetSize().X, B.GetSize().Y) / 100.f;
	VisScale = SmvType == TEXT("rover") ? 1.f : FMath::Clamp(1.6f / FMath::Max(0.1f, SpanM), 1.f, 2.5f);
	Body->SetRelativeScale3D(FVector(VisScale));
	GroundOffsetM = (float)B.Min.Z / 100.f * VisScale;
	VisRadiusM = FMath::Max(0.3f, (float)FVector2D(FMath::Max(FMath::Abs(B.Min.X), FMath::Abs(B.Max.X)), FMath::Max(FMath::Abs(B.Min.Y), FMath::Abs(B.Max.Y))).Size() / 100.f * VisScale);
	VisNoseM = (float)B.Max.X / 100.f * VisScale;
	return true;
}

bool ASmvDroneActor::BuildFabBody(const TCHAR* Dir, const TCHAR* IniPrefix, float DefaultSpanM)
{
	UObject* MeshObj = FSmvFabContent::LoadMeshIn(Dir);
	if (!MeshObj) return false;
	USkeletalMesh* SK = Cast<USkeletalMesh>(MeshObj);
	UStaticMesh* SM = Cast<UStaticMesh>(MeshObj);
	if (!SK && !SM) return false;
	const FString SpanKey = FString(IniPrefix) + TEXT("SpanM"), YawKey = FString(IniPrefix) + TEXT("YawDeg");
	if (SM)
	{
		// statik gövde (iskeletsiz model): pervaneler dönmez
		UStaticMeshComponent* SC = NewObject<UStaticMeshComponent>(this);
		SC->SetStaticMesh(SM);
		SC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SC->SetupAttachment(Body);
		SC->RegisterComponent();
		FabStatic = SC;
		return PlaceFabBody(SC, SM->GetBounds(), SM->GetBounds().Origin, FVector::ZeroVector, *SpanKey, *YawKey, DefaultSpanM);
	}
	UPoseableMeshComponent* C = NewObject<UPoseableMeshComponent>(this);
	C->SetSkinnedAssetAndUpdate(SK);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(true);
	C->SetupAttachment(Body);
	C->RegisterComponent();
	FabMesh = C;

	// kemikler: bileşen uzayı referans pozu (yerel pozların zinciri)
	// Quadcopter_Drone (Fab): drone_rig, root, body, arm_F_L, propeller_F_L, blade1_F_L, blade2_F_L, … camera_roll, camera_tilt
	const FReferenceSkeleton& Ref = SK->GetRefSkeleton();
	const TArray<FTransform>& Local = Ref.GetRefBonePose();
	const int32 NB = Ref.GetNum();
	TArray<FTransform> Cs; Cs.SetNum(NB);
	TArray<bool> IsProp; IsProp.Init(false, NB);
	int32 Gimbal = INDEX_NONE;
	FString Names;
	FVector PropSum = FVector::ZeroVector, FrontSum = FVector::ZeroVector, BackSum = FVector::ZeroVector;
	int32 NFront = 0, NBack = 0;
	for (int32 i = 0; i < NB; ++i)
	{
		const int32 P = Ref.GetParentIndex(i);
		Cs[i] = P >= 0 ? Local[i] * Cs[P] : Local[i];
		const FString N = Ref.GetBoneName(i).ToString();
		const FString L = N.ToLower();
		Names += (i ? TEXT(", ") : TEXT("")) + N;
		IsProp[i] = L.Contains(TEXT("prop")) || L.Contains(TEXT("rotor")) || L.Contains(TEXT("blade")) || L.Contains(TEXT("pervane")) || L.Contains(TEXT("fan"));
		// yalnız en üstteki pervane kemiği döner (kanat kemikleri onunla birlikte gelir)
		if (IsProp[i] && !(P >= 0 && IsProp[P]))
		{
			FabPropBones.Add(Ref.GetBoneName(i));
			FabPropRef.Add(Cs[i].GetRotation());
			const FVector Pl = Cs[i].GetLocation();
			PropSum += Pl;
			// ön/arka adlandırması (…_F_L / …_B_R) varsa burun yönü buradan
			if (L.Contains(TEXT("_f_")) || L.Contains(TEXT("front"))) { FrontSum += Pl; ++NFront; }
			else if (L.Contains(TEXT("_b_")) || L.Contains(TEXT("back")) || L.Contains(TEXT("rear"))) { BackSum += Pl; ++NBack; }
			// karşıt dönüş: çapraz çiftler aynı yöne (ön-sol & arka-sağ +, ön-sağ & arka-sol −)
			const bool bF = L.Contains(TEXT("_f_")) || L.Contains(TEXT("front"));
			const bool bLeft = L.EndsWith(TEXT("_l")) || L.Contains(TEXT("left"));
			const bool bNamed = (bF || L.Contains(TEXT("_b_")) || L.Contains(TEXT("back")) || L.Contains(TEXT("rear")));
			FabPropDir.Add(bNamed ? ((bF == bLeft) ? 1.f : -1.f) : ((FabPropBones.Num() % 2) ? 1.f : -1.f));
		}
		if (Gimbal == INDEX_NONE && (L.Contains(TEXT("gimbal")) || L.Contains(TEXT("camera")) || L.StartsWith(TEXT("cam")))) Gimbal = i;
	}
	UE_LOG(LogSimumav, Log, TEXT("Fab dron kemikleri (%d): %s — dönen pervane kemiği %d"), NB, *Names, FabPropBones.Num());

	const FBoxSphereBounds B = SK->GetBounds();
	// dönüş/merkez: pervanelerin ortası (yoksa sınır kutusu merkezi)
	const FVector Ctr = FabPropBones.Num() >= 3 ? PropSum / FabPropBones.Num() : B.Origin;
	// yön ipucu: ön/arka pervane adları, o da yoksa gimbal/kamera kemiği burun (+X) kabul edilir
	FVector Fwd = FVector::ZeroVector;
	if (NFront > 0 && NBack > 0) Fwd = FrontSum / NFront - BackSum / NBack;
	else if (Gimbal != INDEX_NONE) Fwd = Cs[Gimbal].GetLocation() - Ctr;
	return PlaceFabBody(C, B, Ctr, Fwd, *SpanKey, *YawKey, DefaultSpanM);
}

bool ASmvDroneActor::PlaceFabBody(USceneComponent* C, const FBoxSphereBounds& B, const FVector& Ctr, const FVector& Fwd, const TCHAR* SpanKey, const TCHAR* YawKey, float DefaultSpanM)
{
	const FVector Ext = B.BoxExtent;
	// ölçek: ayar varsa yatay en büyük boyut <önek>SpanM; yoksa model zaten gerçekçi boyuttaysa (20–150 cm) 1, değilse varsayılana
	// (Fab Quadcopter_Drone 51,8 × 45,6 cm). Görünürlük büyütmesi ×2,5 gövdeye ayrıca uygulanır (web MODEL_SCALE).
	const float SpanCm = 2.f * FMath::Max(1.f, (float)FMath::Max(Ext.X, Ext.Y));
	float SpanM = 0.f;
	float S = 1.f;
	if (GConfig && GConfig->GetFloat(TEXT("/Script/Simumav.SmvFabContent"), SpanKey, SpanM, GGameIni) && SpanM > 0.05f) S = SpanM * 100.f / SpanCm;
	else if (SpanCm < 20.f || SpanCm > 150.f) S = DefaultSpanM * 100.f / SpanCm;
	float Yaw = 0.f;
	if (!GConfig || !GConfig->GetFloat(TEXT("/Script/Simumav.SmvFabContent"), YawKey, Yaw, GGameIni))
	{
		if (FVector2D(Fwd.X, Fwd.Y).Size() > SpanCm * 0.05f) Yaw = -(float)FMath::RadiansToDegrees(FMath::Atan2(Fwd.Y, Fwd.X));
	}
	const FRotator Rot(0.f, Yaw, 0.f);
	FVector Loc = -Rot.RotateVector(FVector(Ctr.X, Ctr.Y, 0.0) * S);
	Loc.Z = 0.0;
	C->SetRelativeLocationAndRotation(Loc, Rot);
	C->SetRelativeScale3D(FVector(S));
	VisScale = 2.5f;
	GroundOffsetM = (float)(B.Origin.Z - Ext.Z) * S / 100.f * VisScale;
	VisRadiusM = FMath::Max(0.3f, (float)FVector2D(Ext.X, Ext.Y).Size() * S / 100.f * VisScale);
	VisNoseM = (float)FMath::Max(Ext.X, Ext.Y) * S / 100.f * VisScale;
	UE_LOG(LogSimumav, Log, TEXT("Fab dron gövdesi: sınır %.1f×%.1f×%.1f cm, ölçek %.3f (×%.1f görünürlük), yön %.0f°"), Ext.X * 2, Ext.Y * 2, Ext.Z * 2, S, VisScale, Yaw);
	return true;
}

void ASmvDroneActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bGlbBody && GlbBody)
	{
		const FCaySimTelemetry& Tg = GetLatestTelemetry();
		FSmvGlbInputs In;
		In.bArmed = Tg.bArmed;
		In.ThrottlePct = Tg.Throttle;
		In.GroundspeedMs = Tg.Groundspeed;
		In.Servo = &ServoPwm;
		In.ServoFn = &ServoFn;
		GlbBody->Animate(DeltaSeconds, In);
		return;
	}
	if (!FabMesh || FabPropBones.Num() == 0) return;
	const FCaySimTelemetry& T = GetLatestTelemetry();
	const float Target = T.bArmed ? FMath::Max(0.15f, T.Throttle / 100.f) : 0.f;
	if (Target <= 0.f) return;
	FabPropPhase = FMath::Fmod(FabPropPhase + DeltaSeconds * PropMaxRps * Target * 360.f, 360.f);
	for (int32 i = 0; i < FabPropBones.Num(); ++i)
	{
		const float Dir = FabPropDir.IsValidIndex(i) ? FabPropDir[i] : 1.f;
		const FQuat Q = FQuat(FVector::UpVector, FMath::DegreesToRadians(FabPropPhase * Dir)) * FabPropRef[i];
		FabMesh->SetBoneRotationByName(FabPropBones[i], Q.Rotator(), EBoneSpaces::ComponentSpace);
	}
}

void ASmvDroneActor::ConfigureVisual(const FString& InType, const FString& ModelKey, const FLinearColor& Color)
{
	SmvType = InType;
	SmvModelKey = ModelKey;
	BodyColor = Color;
	if (Motor)
	{
		ESmvEngineKind K = ESmvEngineKind::ElectricMulti;
		int32 Rotors = 4;
		if (InType == TEXT("plane")) K = ESmvEngineKind::ElectricProp;
		else if (InType == TEXT("interceptor")) K = ESmvEngineKind::ElectricProp;
		else if (InType == TEXT("rover")) K = ESmvEngineKind::Rover;
		else if (InType == TEXT("heli")) { K = ESmvEngineKind::Combustion; Rotors = 2; }
		// katalog: motor sayısı (hexa/octa/Y6 → daha çok rotor sesi) ve elektrikli heli (koaksiyel) ayrımı
		if (const FSmvCatalogModel* Cm = USmvDataService::FindModel(ModelKey))
		{
			if (K == ESmvEngineKind::ElectricMulti && Cm->Motors > 0) Rotors = Cm->Motors;
			if (Cm->Sound == TEXT("electric_heli")) { K = ESmvEngineKind::ElectricMulti; Rotors = 2; }
		}
		Motor->SetKind(K, Rotors);
		Motor->Start();
	}
	// Uçak/VTOL: Fab Aircraft Engine Sounds motor döngüsü (pakette varsa); dronlar prosedürel seste kalır
	const TCHAR* SoundKind = nullptr;
	if (InType == TEXT("plane")) SoundKind = ModelKey.Contains(TEXT("jet")) ? TEXT("Jet") : TEXT("Prop");
	else if (InType == TEXT("vtol")) SoundKind = TEXT("Pusher");
	if (const FSmvCatalogModel* Cm = USmvDataService::FindModel(ModelKey))
	{
		if (Cm->Sound == TEXT("jet")) SoundKind = TEXT("Jet");
		else if (Cm->Sound == TEXT("prop")) SoundKind = TEXT("Prop");
		else if (Cm->Sound == TEXT("pusher")) SoundKind = TEXT("Pusher");
	}
	USoundBase* EngineSnd = SoundKind ? FSmvFabContent::LoadEngineSound(SoundKind) : nullptr;
	bFabEngineSound = EngineSnd != nullptr;
	SynthShare = !bFabEngineSound ? 1.f : (InType == TEXT("vtol") ? 0.6f : 0.f);
	EngineThr = 0.f;
	if (bFabEngineSound)
	{
		if (!EngineAudio)
		{
			EngineAudio = NewObject<UAudioComponent>(this, TEXT("EngineAudio"));
			EngineAudio->bAutoActivate = false;
			EngineAudio->bAllowSpatialization = true;
			// uzaklık zayıflaması UpdateAudio'da elle (prosedürel sesle aynı eğri)
			EngineAudio->bOverrideAttenuation = true;
			EngineAudio->AttenuationOverrides.bAttenuate = false;
			EngineAudio->SetupAttachment(Body);
			EngineAudio->RegisterComponent();
		}
		EngineAudio->Stop();
		EngineAudio->SetSound(EngineSnd);
		EngineAudio->SetVolumeMultiplier(0.f);
	}
	else if (EngineAudio)
	{
		EngineAudio->Stop();
	}
	BuildPlaceholder();
	RebuildPhysicsShape();
}

// ---------------------------------------------------------------------------
// gerçek çarpışma (SmvPhysics): görünen gövdenin şekli her karede SITL pozuna süpürülür
// ---------------------------------------------------------------------------

void ASmvDroneActor::EnablePhysicsContact(bool bOn)
{
	if (bOn && !FSmvPhysicsSettings::Get().bEnabled) bOn = false;
	if (!bOn) { delete Solver; Solver = nullptr; return; }
	if (!Solver) Solver = new FSmvContactSolver();
	RebuildPhysicsShape();
}

void ASmvDroneActor::BeginDestroy()
{
	delete Solver;
	Solver = nullptr;
	Super::BeginDestroy();
}

void ASmvDroneActor::RebuildPhysicsShape()
{
	if (!Solver) return;
	// aktör uzayında görünür bileşenlerin sınırları (pervaneler, kızaklar, kanatlar dahil)
	FBox B(ForceInit);
	const FTransform ActorTm = GetActorTransform();
	TInlineComponentArray<UPrimitiveComponent*> Prims(this);
	for (UPrimitiveComponent* P : Prims)
	{
		if (!P || !P->IsVisible()) continue;
		const FBoxSphereBounds Lb = P->CalcBounds(P->GetComponentTransform().GetRelativeTransform(ActorTm));
		if (Lb.BoxExtent.GetMax() > 0.5) B += Lb.GetBox();
	}
	const double R = FMath::Max(30.0, (double)VisRadiusM * 100.0);
	if (!B.IsValid || B.GetExtent().GetMax() < 5.0 || B.GetExtent().GetMax() > 5000.0)
		B = FBox(FVector(-R, -R, FMath::Min(-5.0, (double)GroundOffsetM * 100.0)), FVector(R, R, FMath::Max(15.0, R * 0.25)));
	// ölçek (ShapeScale): kutu merkez çevresinde küçülür, alt yüzey (iniş takımı) yerinde kalır
	const double Sc = FSmvPhysicsSettings::Get().ShapeScale;
	const FVector Ext = B.GetExtent() * Sc;
	const FVector Ctr(B.GetCenter().X, B.GetCenter().Y, B.Min.Z + Ext.Z);
	TArray<FSmvBodyBox> Boxes;
	SmvCore::EVehicleClass Veh = SmvCore::EVehicleClass::Vertical;
	if (SmvType == TEXT("plane") || SmvType == TEXT("vtol") || SmvType == TEXT("interceptor"))
	{
		Veh = SmvCore::EVehicleClass::Plane;
		// gövde (burundan kuyruğa, ince) + kanat (açıklık boyunca, yassı) — sınır kutusundan oranla
		FSmvBodyBox Fus; Fus.Center = Ctr; Fus.Half = FVector(Ext.X, FMath::Max(8.0, Ext.Y * 0.12), Ext.Z);
		FSmvBodyBox Wing; Wing.Center = Ctr + FVector(0, 0, Ext.Z * 0.2); Wing.Half = FVector(FMath::Max(10.0, Ext.X * 0.28), Ext.Y, FMath::Max(4.0, Ext.Z * 0.3));
		Boxes.Add(Fus);
		Boxes.Add(Wing);
	}
	else if (SmvType == TEXT("rover"))
	{
		Veh = SmvCore::EVehicleClass::Rover;
		// tekerlekler zemine değer → kutu tabanı %15 yukarıda (zemin teması çarpışma değil; duvar/su temasları sayılır)
		FSmvBodyBox RoverBox; RoverBox.Center = Ctr + FVector(0, 0, Ext.Z * 0.15); RoverBox.Half = FVector(Ext.X, Ext.Y, Ext.Z * 0.85);
		Boxes.Add(RoverBox);
	}
	else
	{
		// çok rotor / heli / FPV: pervane disklerini kapsayan yassı kutu (kollar + diskler) + gövde/kızak çekirdeği
		FSmvBodyBox Disc; Disc.Center = Ctr + FVector(0, 0, Ext.Z * 0.35); Disc.Half = FVector(Ext.X, Ext.Y, FMath::Max(4.0, Ext.Z * 0.65));
		FSmvBodyBox Core; Core.Center = Ctr; Core.Half = FVector(FMath::Max(6.0, Ext.X * 0.45), FMath::Max(6.0, Ext.Y * 0.45), Ext.Z);
		Boxes.Add(Disc);
		Boxes.Add(Core);
	}
	Solver->Configure(Boxes, Veh);
	UE_LOG(LogSimumav, Log, TEXT("Fizik gövdesi (%s): sınır %.0f×%.0f×%.0f cm, ölçek %.2f, %d kutu"), *SmvType, Ext.X * 2, Ext.Y * 2, Ext.Z * 2, Sc, Boxes.Num());
}

bool ASmvDroneActor::ResolveRenderLocation(const FCaySimPose& Pose, const FQuat& EnuRot, const FQuat& WorldRot, FVector& InOutLoc)
{
	if (!Solver || !GetWorld()) return false;
	const FCaySimTelemetry T = GetLatestTelemetry();
	const FVector VelMs = EnuRot.RotateVector(CaySimGeo::NedVelocityToUnreal(FVector(T.Vn, T.Ve, T.Vd), bEnuXNorth)) / 100.0;
	InOutLoc = Solver->Resolve(GetWorld(), this, InOutLoc, WorldRot, EnuRot, VelMs, T.bArmed);
	SetOnGroundState(Solver->IsOnFloor() || Pose.AltRel < 0.3);
	return true;
}

void ASmvDroneActor::BuildPlaceholder()
{
	ClearParts();   // taban sınıfı Propellers'ı yok etmeden önce kendi göbek/parçalarımız
	Super::BuildPlaceholder();   // dahili (BP) mesh yoksa gövde gizli kalır
	if (!IsUsingPlaceholder()) { Body->SetRelativeScale3D(FVector::OneVector); return; }
	if (PlaceholderMesh && PlaceholderMesh->GetStaticMesh()) return;   // Blueprint gövdesi atanmış
	// Pervaneler görünür hızda dönsün (40 dev/sn 60 fps'te durağan/geri dönüyor görünür — stroboskop)
	PropMaxRps = 7.f;
	const FLinearColor Dark(0.04f, 0.04f, 0.05f), Grey(0.35f, 0.36f, 0.38f), Glass(0.08f, 0.16f, 0.3f), Prop(0.07f, 0.07f, 0.08f);
	const FLinearColor NavL(1.f, 0.05f, 0.05f), NavR(0.05f, 1.f, 0.15f), White(0.95f, 0.95f, 0.95f);
	Propellers.Reset();
	float Low = -0.1f;   // gövdenin en alt noktası (m, ölçeksiz)
	float Radius = 0.4f, Nose = 0.2f;
	VisScale = 2.5f;
	// katalog modeli: Fab gövdesi seçeneği (fab:*) ya da pakete gömülü GLB gövdesi (Content/Simumav/ModelsSrc)
	if (const FSmvCatalogModel* Cm = USmvDataService::FindModel(SmvModelKey))
	{
		if (!Cm->FabDir.IsEmpty())
		{
			const bool bFpvDir = Cm->FabDir.EndsWith(TEXT("Fpv"));
			if (BuildFabBody(*Cm->FabDir, bFpvDir ? TEXT("Fpv") : TEXT("Drone"), bFpvDir ? 0.35f : 0.6f)) { Body->SetRelativeScale3D(FVector(VisScale)); return; }
		}
		else if (BuildGlbModel(*Cm))
		{
			return;
		}
	}
	// FPV yarışçı ve önleyici: Fab FPV_Interceptor_Drone gövdesi varsa onu kullan (yoksa aşağıdaki basit gövdeler)
	if ((SmvType == TEXT("fpv") || SmvType == TEXT("interceptor")) && BuildFabBody(TEXT("/Game/Fab/DroneFpv"), TEXT("Fpv"), 0.35f))
	{
		Body->SetRelativeScale3D(FVector(VisScale));
		return;
	}
	if (SmvType == TEXT("plane") || SmvType == TEXT("vtol") || SmvType == TEXT("interceptor"))
	{
		AddPart(CylinderMesh, FVector(0, 0, 0), FVector(0.12f, 0.12f, 1.0f), FRotator(90, 0, 0), BodyColor);   // gövde (X boyunca)
		AddPart(SphereMesh, FVector(0.5f, 0, 0), FVector(0.12f, 0.12f, 0.12f), FRotator::ZeroRotator, BodyColor);   // burun
		AddPart(CubeMesh, FVector(0.05f, 0, 0.03f), FVector(0.25f, 1.6f, 0.02f), FRotator::ZeroRotator, BodyColor);   // kanat
		AddPart(CubeMesh, FVector(0.05f, -0.8f, 0.03f), FVector(0.22f, 0.02f, 0.025f), FRotator::ZeroRotator, NavL);   // sol kanat ucu (kırmızı)
		AddPart(CubeMesh, FVector(0.05f, 0.8f, 0.03f), FVector(0.22f, 0.02f, 0.025f), FRotator::ZeroRotator, NavR);    // sağ kanat ucu (yeşil)
		AddPart(CubeMesh, FVector(-0.45f, 0, 0.03f), FVector(0.12f, 0.5f, 0.015f), FRotator::ZeroRotator, BodyColor);   // yatay kuyruk
		AddPart(CubeMesh, FVector(-0.45f, 0, 0.1f), FVector(0.12f, 0.015f, 0.15f), FRotator::ZeroRotator, BodyColor);   // dikey kuyruk
		AddPart(SphereMesh, FVector(0.2f, 0, 0.06f), FVector(0.12f, 0.08f, 0.06f), FRotator::ZeroRotator, Glass);
		AddRotor(FVector(0.58f, 0, 0), 0.28f, true, Prop);   // çekici pervane (Propellers[0])
		if (SmvType == TEXT("vtol"))
		{
			AddPart(CubeMesh, FVector(0.05f, 0.4f, 0.0f), FVector(0.8f, 0.03f, 0.03f), FRotator::ZeroRotator, Grey);
			AddPart(CubeMesh, FVector(0.05f, -0.4f, 0.0f), FVector(0.8f, 0.03f, 0.03f), FRotator::ZeroRotator, Grey);
			for (const FVector& P : { FVector(0.4f, 0.4f, 0.05f), FVector(-0.3f, 0.4f, 0.05f), FVector(0.4f, -0.4f, 0.05f), FVector(-0.3f, -0.4f, 0.05f) }) AddRotor(P, 0.3f, false, Prop);
		}
		// iniş takımı (üç teker)
		AddPart(CylinderMesh, FVector(0.3f, 0, -0.1f), FVector(0.05f, 0.05f, 0.02f), FRotator(0, 0, 90), Dark);
		AddPart(CylinderMesh, FVector(-0.05f, 0.15f, -0.1f), FVector(0.06f, 0.06f, 0.025f), FRotator(0, 0, 90), Dark);
		AddPart(CylinderMesh, FVector(-0.05f, -0.15f, -0.1f), FVector(0.06f, 0.06f, 0.025f), FRotator(0, 0, 90), Dark);
		AddPart(CubeMesh, FVector(0.3f, 0, -0.07f), FVector(0.015f, 0.015f, 0.06f), FRotator::ZeroRotator, Grey);
		AddPart(CubeMesh, FVector(-0.05f, 0.08f, -0.07f), FVector(0.015f, 0.15f, 0.015f), FRotator(0, 0, -25), Grey);
		AddPart(CubeMesh, FVector(-0.05f, -0.08f, -0.07f), FVector(0.015f, 0.15f, 0.015f), FRotator(0, 0, 25), Grey);
		Low = -0.13f; Radius = 0.8f; Nose = 0.65f;
	}
	else if (SmvType == TEXT("rover"))
	{
		VisScale = 1.f;   // kara aracı gerçek boyutta (web ROVER_SCALE)
		AddPart(CubeMesh, FVector(0, 0, 0.25f), FVector(1.8f, 0.9f, 0.4f), FRotator::ZeroRotator, BodyColor);
		AddPart(CubeMesh, FVector(0.3f, 0, 0.55f), FVector(0.6f, 0.7f, 0.25f), FRotator::ZeroRotator, Glass);
		for (const FVector& P : { FVector(0.6f, 0.5f, 0.15f), FVector(-0.6f, 0.5f, 0.15f), FVector(0.6f, -0.5f, 0.15f), FVector(-0.6f, -0.5f, 0.15f) })
			AddPart(CylinderMesh, P, FVector(0.3f, 0.3f, 0.15f), FRotator(0, 0, 90), Dark);
		Low = 0.f; Radius = 1.f; Nose = 0.9f;
	}
	else if (SmvType == TEXT("heli"))
	{
		AddPart(SphereMesh, FVector(0, 0, 0), FVector(0.6f, 0.3f, 0.3f), FRotator::ZeroRotator, BodyColor);
		AddPart(SphereMesh, FVector(0.15f, 0, 0.03f), FVector(0.3f, 0.24f, 0.2f), FRotator::ZeroRotator, Glass);
		AddPart(CylinderMesh, FVector(-0.6f, 0, 0.05f), FVector(0.05f, 0.05f, 0.8f), FRotator(90, 0, 0), BodyColor);
		AddPart(CubeMesh, FVector(-0.98f, 0, 0.13f), FVector(0.1f, 0.012f, 0.16f), FRotator::ZeroRotator, BodyColor);   // dikey kuyruk
		AddPart(CubeMesh, FVector(-0.98f, 0.04f, 0.15f), FVector(0.02f, 0.006f, 0.26f), FRotator::ZeroRotator, Prop);   // kuyruk rotoru (sabit)
		AddPart(CubeMesh, FVector(0, 0.15f, -0.2f), FVector(0.6f, 0.02f, 0.02f), FRotator::ZeroRotator, Dark);
		AddPart(CubeMesh, FVector(0, -0.15f, -0.2f), FVector(0.6f, 0.02f, 0.02f), FRotator::ZeroRotator, Dark);
		AddPart(CylinderMesh, FVector(0, 0, 0.17f), FVector(0.05f, 0.05f, 0.1f), FRotator::ZeroRotator, Grey);   // rotor mili
		AddRotor(FVector(0, 0, 0.23f), 1.7f, false, Prop);
		Low = -0.21f; Radius = 0.9f; Nose = 0.6f;
	}
	else
	{
		// çok rotor: X-quad (FPV daha küçük) ya da hexa (altı kol; model anahtarında hexa/typhoon)
		const bool bFpv = SmvType == TEXT("fpv");
		const FString MK = SmvModelKey.ToLower();
		const bool bHexa = SmvType == TEXT("hexa") || MK.Contains(TEXT("hexa")) || MK.Contains(TEXT("typhoon")) || MK.Contains(TEXT("h480"));
		// quad (FPV yarışçı hariç): Fab gövdesi varsa onu kullan; hexa/uçak/heli/kara aracı basit gövdede kalır
		if (!bFpv && !bHexa && BuildFabBody(TEXT("/Game/Fab/Drone"), TEXT("Drone"), 0.6f))
		{
			Body->SetRelativeScale3D(FVector(VisScale));
			return;
		}
		const int32 N = bHexa ? 6 : 4;
		const float Arm = bFpv ? 0.12f : bHexa ? 0.32f : 0.25f;
		const float PropD = bFpv ? 0.13f : bHexa ? 0.26f : 0.25f;
		const float Bw = bFpv ? 0.12f : 0.2f, Bd = bFpv ? 0.08f : 0.14f;
		AddPart(CubeMesh, FVector(0, 0, 0), FVector(Bw, Bd, 0.05f), FRotator::ZeroRotator, BodyColor);   // gövde
		AddPart(SphereMesh, FVector(0, 0, 0.025f), FVector(Bw * 0.85f, Bd * 0.85f, 0.05f), FRotator::ZeroRotator, BodyColor);   // üst kapak
		AddPart(CubeMesh, FVector(Bw * 0.45f, 0, 0.0f), FVector(0.02f, Bd * 0.7f, 0.03f), FRotator::ZeroRotator, FLinearColor(1.f, 0.55f, 0.05f));   // burun şeridi (turuncu)
		AddPart(SphereMesh, FVector(Bw * 0.5f, 0, -0.035f), FVector(0.045f, 0.045f, 0.045f), FRotator::ZeroRotator, Glass);   // kamera / gimbal
		for (int32 i = 0; i < N; ++i)
		{
			const float Deg = bHexa ? 30.f + 60.f * i : 45.f + 90.f * i;
			const float A = FMath::DegreesToRadians(Deg);
			const FVector Tip(FMath::Cos(A) * Arm, FMath::Sin(A) * Arm, 0.01f);
			const bool bFront = Tip.X > 0.01f;
			const FLinearColor Led = bFront ? (Tip.Y < 0 ? NavL : NavR) : White;
			AddPart(CubeMesh, Tip * 0.5f, FVector(Arm, 0.022f, 0.016f), FRotator(0, Deg, 0), Grey);   // kol
			AddPart(CylinderMesh, Tip + FVector(0, 0, 0.012f), FVector(0.04f, 0.04f, 0.035f), FRotator::ZeroRotator, Dark);   // motor
			AddPart(SphereMesh, Tip + FVector(0, 0, -0.012f), FVector(0.022f, 0.022f, 0.022f), FRotator::ZeroRotator, Led);   // seyir ışığı (sol kırmızı, sağ yeşil, arka beyaz)
			AddRotor(Tip + FVector(0, 0, 0.034f), PropD, false, Prop);
		}
		// iniş takımı: iki kızak + dört bacak
		const float Sk = bFpv ? 0.05f : 0.08f;
		for (const float Y : { Sk, -Sk })
		{
			AddPart(CubeMesh, FVector(0, Y, -0.088f), FVector(Bw * 1.2f, 0.014f, 0.012f), FRotator::ZeroRotator, Dark);
			AddPart(CubeMesh, FVector(Bw * 0.3f, Y, -0.058f), FVector(0.012f, 0.012f, 0.055f), FRotator::ZeroRotator, Dark);
			AddPart(CubeMesh, FVector(-Bw * 0.3f, Y, -0.058f), FVector(0.012f, 0.012f, 0.055f), FRotator::ZeroRotator, Dark);
		}
		Low = -0.094f; Radius = Arm + PropD * 0.5f; Nose = Bw * 0.5f + 0.05f;
	}
	Body->SetRelativeScale3D(FVector(VisScale));
	GroundOffsetM = Low * VisScale;
	VisRadiusM = FMath::Max(0.3f, Radius * VisScale);
	VisNoseM = Nose * VisScale;
}

void ASmvDroneActor::UpdateAudio(const FVector& Listener, float Volume)
{
	if (!Motor) return;
	const FCaySimTelemetry& T = GetLatestTelemetry();
	const FVector Pos = GetActorLocation();
	const float Dt = FMath::Max(GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f, 1e-3f);
	const FVector Vel = (Pos - LastPos) / Dt / 100.f;   // m/s
	LastPos = Pos;
	const FVector ToL = (Listener - Pos).GetSafeNormal();
	const float Vr = (float)FVector::DotProduct(Vel, ToL);   // dinleyiciye yaklaşma hızı
	const float Doppler = 343.f / FMath::Max(100.f, 343.f - Vr);
	const float DistM = (float)(FVector::Dist(Listener, Pos) / 100.0);
	const float Atten = FMath::Clamp(30.f / FMath::Max(DistM, 1.f), 0.02f, 1.f);
	Motor->SetParams(T.Throttle / 100.f, T.Airspeed > 0 ? T.Airspeed : T.Groundspeed, T.bArmed, Doppler, Volume * Atten * SynthShare);
	if (bFabEngineSound && EngineAudio)
	{
		// gaz → devir: perde 0,6–1,5 (Doppler ile), ses düzeyi rölantide %30, tam gazda %100
		const float Thr = T.bArmed ? FMath::Clamp(T.Throttle / 100.f, 0.f, 1.f) : 0.f;
		EngineThr = FMath::FInterpTo(EngineThr, Thr, Dt, 2.5f);
		const float Vol = T.bArmed ? (0.3f + 0.7f * EngineThr) * Volume * Atten : 0.f;
		if (Vol > 0.001f)
		{
			if (!EngineAudio->IsPlaying()) EngineAudio->Play();   // döngü değilse de yeniden başlar
			EngineAudio->SetPitchMultiplier(FMath::Clamp(FMath::Lerp(0.6f, 1.5f, EngineThr) * Doppler, 0.4f, 2.f));
			EngineAudio->SetVolumeMultiplier(Vol);
		}
		else if (EngineAudio->IsPlaying())
		{
			EngineAudio->Stop();
		}
	}
}

// ---------------------------------------------------------------------------
// ASmvGateActor — prosedürel torus
// ---------------------------------------------------------------------------

ASmvGateActor::ASmvGateActor()
{
	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Torus"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->bUseAsyncCooking = true;
}

void ASmvGateActor::Build(float RadiusM, float TubeM, const FLinearColor& Color)
{
	Base = Color;
	const int32 Seg = 48, Side = 12;
	TArray<FVector> V; TArray<int32> Tri; TArray<FVector> N; TArray<FVector2D> UV; TArray<FLinearColor> C; TArray<FProcMeshTangent> T;
	const float R = RadiusM * 100.f, r = TubeM * 100.f;
	for (int32 i = 0; i <= Seg; ++i)
	{
		const float U = 2.f * UE_PI * i / Seg;
		// halka düzlemi YZ (normal +X = geçiş yönü; aktör yaw'ı halka yönüne çevrilir)
		const FVector Ctr(0.f, R * FMath::Cos(U), R * FMath::Sin(U));
		for (int32 j = 0; j <= Side; ++j)
		{
			const float W = 2.f * UE_PI * j / Side;
			const FVector Radial = FVector(0.f, FMath::Cos(U), FMath::Sin(U));
			const FVector Nrm = Radial * FMath::Cos(W) + FVector(1, 0, 0) * FMath::Sin(W);
			V.Add(Ctr + Nrm * r);
			N.Add(Nrm);
			UV.Add(FVector2D((float)i / Seg, (float)j / Side));
			C.Add(Color);
			T.Add(FProcMeshTangent(FVector(0.f, -FMath::Sin(U), FMath::Cos(U)), false));
		}
	}
	for (int32 i = 0; i < Seg; ++i)
		for (int32 j = 0; j < Side; ++j)
		{
			const int32 A = i * (Side + 1) + j, B = (i + 1) * (Side + 1) + j;
			Tri.Append({ A, B, A + 1, B, B + 1, A + 1 });
		}
	Mesh->CreateMeshSection_LinearColor(0, V, Tri, N, UV, C, T, false);
	UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, GSmvGlowMat);
	if (!M) M = LoadObject<UMaterialInterface>(nullptr, GSmvMat);
	if (M)
	{
		Mat = UMaterialInstanceDynamic::Create(M, this);
		Mat->SetVectorParameterValue(TEXT("Color"), Color);
		Mesh->SetMaterial(0, Mat);
	}
}

void ASmvGateActor::SetState(int32 State)
{
	if (!Mat) return;
	const FLinearColor C = State == 1 ? FLinearColor(0.1f, 1.f, 0.2f) : State == 2 ? FLinearColor(0.3f, 0.3f, 0.3f) : Base;
	Mat->SetVectorParameterValue(TEXT("Color"), C);
	Mat->SetScalarParameterValue(TEXT("Glow"), State == 1 ? 8.f : 1.f);
}

// ---------------------------------------------------------------------------
// ASmvProcTerrain
// ---------------------------------------------------------------------------

ASmvProcTerrain::ASmvProcTerrain()
{
	Ground = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Ground"));
	SetRootComponent(Ground);
	Ground->bUseAsyncCooking = true;
	TreeTrunks = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("TreeTrunks"));
	TreeTrunks->SetupAttachment(Ground);
	TreeCrowns = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("TreeCrowns"));
	TreeCrowns->SetupAttachment(Ground);
	Buildings = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Buildings"));
	Buildings->SetupAttachment(Ground);
	Runway = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Runway"));
	Runway->SetupAttachment(Ground);
	WaterPlane = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Water"));
	WaterPlane->SetupAttachment(Ground);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(GSmvCube);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(GSmvCyl);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(GSmvCone);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(GSmvMat);
	TreeTrunks->SetStaticMesh(Cyl.Object);
	TreeCrowns->SetStaticMesh(Cone.Object);
	Buildings->SetStaticMesh(Cube.Object);
	Runway->SetStaticMesh(Cube.Object);
	WaterPlane->SetStaticMesh(Cube.Object);
	BaseMaterial = Mat.Object;
	TreeTrunks->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TreeCrowns->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Buildings->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Buildings->ComponentTags.Add(TEXT("building"));
	WaterPlane->ComponentTags.Add(TEXT("water"));
	WaterPlane->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
}

double ASmvProcTerrain::Noise2(double X, double Y) const
{
	// değer gürültüsü (tohumlu, 3 oktav)
	auto Hash = [this](int32 Ix, int32 Iy) { uint32 H = (uint32)(Ix * 374761393 + Iy * 668265263) ^ (uint32)(Seed * 2246822519u); H = (H ^ (H >> 13)) * 1274126177u; return ((H ^ (H >> 16)) & 0xffff) / 65535.0; };
	double Sum = 0, Amp = 1, Freq = 1, Norm = 0;
	for (int32 O = 0; O < 3; ++O)
	{
		const double Fx = X * Freq, Fy = Y * Freq;
		const int32 Ix = FMath::FloorToInt(Fx), Iy = FMath::FloorToInt(Fy);
		const double Tx = Fx - Ix, Ty = Fy - Iy;
		const double Sx = Tx * Tx * (3 - 2 * Tx), Sy = Ty * Ty * (3 - 2 * Ty);
		const double A = Hash(Ix, Iy), B = Hash(Ix + 1, Iy), C = Hash(Ix, Iy + 1), D = Hash(Ix + 1, Iy + 1);
		Sum += Amp * FMath::Lerp(FMath::Lerp(A, B, Sx), FMath::Lerp(C, D, Sx), Sy);
		Norm += Amp; Amp *= 0.5; Freq *= 2;
	}
	return Sum / Norm;
}

void ASmvProcTerrain::SetSurfaceWeather(float Wetness, float Snow)
{
	PendingWet = FMath::Clamp(Wetness, 0.f, 1.f);
	PendingSnow = FMath::Clamp(Snow, 0.f, 1.f);
	if (!TerrainMid) return;
	TerrainMid->SetScalarParameterValue(TEXT("Wetness"), PendingWet);
	TerrainMid->SetScalarParameterValue(TEXT("Snow"), PendingSnow);
}

double ASmvProcTerrain::HeightAt(double E, double N) const
{
	const double Dist = FMath::Sqrt(E * E + N * N);
	double H = (Noise2(E / 600.0, N / 600.0) - 0.5) * 30.0;   // hafif dalgalı ova
	for (const FMountain& M : Mountains)
	{
		const double D = FMath::Sqrt((E - M.E) * (E - M.E) + (N - M.N) * (N - M.N));
		if (D < M.R) H += M.H * FMath::Square(FMath::Cos(D / M.R * UE_DOUBLE_PI * 0.5));
	}
	// ev çevresi düz (SITL'in düz zemini) — 300 m'den 600 m'ye yumuşak geçiş
	const double Blend = FMath::Clamp((Dist - FlatR) / FlatR, 0.0, 1.0);
	return H * Blend * Blend;
}

void ASmvProcTerrain::Build(const FSmvTrack& Track, double GroundMsl, AActor* GeoActor)
{
	ICaySimGeoTransform* Geo = Cast<ICaySimGeoTransform>(GeoActor);
	if (!Geo) { UE_LOG(LogSimumav, Warning, TEXT("Prosedürel dünya: dönüşüm aktörü yok")); return; }
	W = Track.World;
	Seed = W.Seed ? W.Seed : 1;
	const double Half = FMath::Clamp(W.SizeM, 1000.0, 12000.0) / 2.0;
	SmvCore::FPyRandom Rng((uint64_t)Seed);
	Mountains.Reset();
	for (int32 i = 0; i < FMath::Clamp(W.MountainCount, 0, 12); ++i)
	{
		const double A = Rng.Uniform(0.0, 2.0 * UE_DOUBLE_PI), R = Rng.Uniform(0.45, 0.95) * Half;
		Mountains.Add({ R * FMath::Sin(A), R * FMath::Cos(A), W.MountainHeight * Rng.Uniform(0.5, 1.0), Rng.Uniform(0.15, 0.35) * Half });
	}
	auto ToUe = [Geo, &Track, GroundMsl](double E, double N, double H)
	{
		double Lat, Lon;
		SmvCore::LatLon(Track.Lat, Track.Lon, E, N, Lat, Lon);
		return Geo->GeoToUnreal(Lon, Lat, GroundMsl + H);
	};
	const FVector Origin = ToUe(0, 0, 0);
	SetActorLocation(Origin);
	// arazi ızgarası
	const int32 Grid = 160;
	const double Step = 2 * Half / Grid;
	TArray<FVector> V; TArray<int32> Tri; TArray<FVector> Nrm; TArray<FVector2D> UV; TArray<FLinearColor> Col; TArray<FProcMeshTangent> Tan;
	V.Reserve((Grid + 1) * (Grid + 1));
	for (int32 y = 0; y <= Grid; ++y)
		for (int32 x = 0; x <= Grid; ++x)
		{
			const double E = -Half + x * Step, N = -Half + y * Step;
			const double H = HeightAt(E, N);
			V.Add(ToUe(E, N, H) - Origin);
			UV.Add(FVector2D(x / 8.f, y / 8.f));
			const float Tint = (float)FMath::Clamp(H / FMath::Max(1.0, W.MountainHeight), 0.0, 1.0);
			Col.Add(FMath::Lerp(FLinearColor(0.18f, 0.32f, 0.12f), FLinearColor(0.45f, 0.42f, 0.38f), Tint));
		}
	for (int32 y = 0; y < Grid; ++y)
		for (int32 x = 0; x < Grid; ++x)
		{
			const int32 A = y * (Grid + 1) + x, B = A + 1, C = A + Grid + 1, D = C + 1;
			Tri.Append({ A, C, B, B, C, D });
		}
	// normaller (yüz normallerinin ortalaması)
	Nrm.Init(FVector::ZeroVector, V.Num());
	for (int32 i = 0; i < Tri.Num(); i += 3)
	{
		const FVector Fn = FVector::CrossProduct(V[Tri[i + 2]] - V[Tri[i]], V[Tri[i + 1]] - V[Tri[i]]).GetSafeNormal();
		Nrm[Tri[i]] += Fn; Nrm[Tri[i + 1]] += Fn; Nrm[Tri[i + 2]] += Fn;
	}
	for (FVector& X : Nrm) { X = X.GetSafeNormal(); if (X.Z < 0) X = -X; }
	Ground->CreateMeshSection_LinearColor(0, V, Tri, Nrm, UV, Col, Tan, true);
	UMaterialInterface* TerrainWx = LoadObject<UMaterialInterface>(nullptr, GSmvTerrainWxMat, nullptr, LOAD_NoWarn | LOAD_Quiet);
	UMaterialInterface* TerrainMat = TerrainWx ? nullptr : LoadObject<UMaterialInterface>(nullptr, GSmvTerrainMat);
	if (TerrainWx)
	{
		TerrainMid = UMaterialInstanceDynamic::Create(TerrainWx, this);
		Ground->SetMaterial(0, TerrainMid);
		SetSurfaceWeather(PendingWet, PendingSnow);
	}
	else if (TerrainMat) Ground->SetMaterial(0, TerrainMat);
	else if (BaseMaterial)
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.2f, 0.33f, 0.14f));
		Ground->SetMaterial(0, M);
	}
	auto Tinted = [this](UMeshComponent* C, const FLinearColor& Color)
	{
		if (!BaseMaterial) return;
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		M->SetVectorParameterValue(TEXT("Color"), Color);
		C->SetMaterial(0, M);
	};
	Tinted(TreeTrunks, FLinearColor(0.25f, 0.15f, 0.07f));
	Tinted(TreeCrowns, FLinearColor(0.05f, 0.22f, 0.06f));
	Tinted(Buildings, FLinearColor(0.6f, 0.58f, 0.55f));
	Tinted(Runway, FLinearColor(0.08f, 0.08f, 0.09f));
	Tinted(WaterPlane, FLinearColor(0.05f, 0.18f, 0.3f));

	// ağaçlar ve binalar (ev çevresinin dışında)
	TreeTrunks->ClearInstances(); TreeCrowns->ClearInstances(); Buildings->ClearInstances();
	const int32 Trees = FMath::Clamp((int32)(W.TreeDensity * 1500.0), 0, 4000);
	for (int32 i = 0; i < Trees; ++i)
	{
		const double E = Rng.Uniform(-Half, Half), N = Rng.Uniform(-Half, Half);
		if (FMath::Sqrt(E * E + N * N) < FlatR * 1.2) continue;
		const double H = HeightAt(E, N);
		const float Tall = (float)Rng.Uniform(6.0, 16.0);
		const FVector P = ToUe(E, N, H) - Origin;
		TreeTrunks->AddInstance(FTransform(FRotator::ZeroRotator, P + FVector(0, 0, Tall * 20.f), FVector(0.3f, 0.3f, Tall * 0.4f)));
		TreeCrowns->AddInstance(FTransform(FRotator::ZeroRotator, P + FVector(0, 0, Tall * 75.f), FVector(Tall * 0.35f, Tall * 0.35f, Tall * 0.7f)));
	}
	for (int32 i = 0; i < FMath::Clamp(W.BuildingCount, 0, 400); ++i)
	{
		const double A = Rng.Uniform(0.0, 2.0 * UE_DOUBLE_PI), R = Rng.Uniform(FlatR * 1.3, Half * 0.6);
		const double E = R * FMath::Sin(A), N = R * FMath::Cos(A);
		const FVector P = ToUe(E, N, HeightAt(E, N)) - Origin;
		const FVector S((float)Rng.Uniform(0.08, 0.3), (float)Rng.Uniform(0.08, 0.3), (float)Rng.Uniform(0.06, 0.4));
		Buildings->AddInstance(FTransform(FRotator(0, (float)Rng.Uniform(0, 90), 0), P + FVector(0, 0, S.Z * 5000.f), S * 100.f));
	}
	// pist (ev noktasından pist yönünde, 600 × 30 m)
	Runway->SetVisibility(W.bRunway);
	if (W.bRunway)
	{
		const double H = FMath::DegreesToRadians(W.RunwayHeading);
		const FVector Fwd = ToUe(FMath::Sin(H) * 100.0, FMath::Cos(H) * 100.0, 0) - Origin;
		// ince levha: üst yüzü zeminden 0,5 cm yukarıda (eskiden 20 cm kalınlıktaydı → yerdeki dron levhanın İÇİNDE kalıp görünmüyordu)
		Runway->SetRelativeLocation(Fwd * 2.5f + FVector(0, 0, -1.5f));
		Runway->SetRelativeRotation(FRotator(0, Fwd.Rotation().Yaw, 0));
		Runway->SetRelativeScale3D(FVector(600.f, 30.f, 0.04f));   // 600 × 30 m × 4 cm (BasicShapes küpü 1 m)
	}
	// su: alçak kesimleri kaplayan düzlem (water oranıyla)
	WaterPlane->SetVisibility(W.Water > 0.01);
	if (W.Water > 0.01)
	{
		const FVector Wc = ToUe(Half * 0.55, -Half * 0.5, -2.0) - Origin;
		WaterPlane->SetRelativeLocation(Wc);
		const float S = (float)(Half * W.Water * 2.0);
		WaterPlane->SetRelativeScale3D(FVector(S, S, 0.01f));
	}
}
