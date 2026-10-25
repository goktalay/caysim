#include "CesiumWorldSetup.h"
// spline başlıkları Cesium başlıklarından ÖNCE (Cesium yolu Windows min/max makrolarını tanımlayabiliyor; SplineMath.h std::numeric_limits<>::min() bozuluyordu)
#include "Components/SplineComponent.h"
#include "CaySim.h"
#include "HepgoClient.h"
#include "Engine/World.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/StaticMeshActor.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PrimitiveComponent.h"
#include "CollisionQueryParams.h"

#if CAYSIM_WITH_CESIUM
#include "CesiumGeoreference.h"
#include "Cesium3DTileset.h"
#include "CesiumSunSky.h"
#include "CesiumCreditSystem.h"
#include "CesiumIonRasterOverlay.h"
#include "CesiumPolygonRasterOverlay.h"
#include "CesiumCartographicPolygon.h"
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#endif

ACaySimWorldSetup::ACaySimWorldSetup()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void ACaySimWorldSetup::BeginPlay()
{
	Super::BeginPlay();
}

float ACaySimWorldSetup::TimeOfDayHours(const FString& Time)
{
	if (Time == TEXT("night")) return 23.f;
	if (Time == TEXT("dusk")) return 19.5f;
	return 13.f;
}

void ACaySimWorldSetup::Build(const FCaySimSession& Session, UHepgoClient* Client, ECaySimWorldSource Source)
{
	Center = Session.Center;
	World = Session.World;
	Hepgo = Client;
	bReady = false;
	bGroundOffsetMeasured = false;
	LoadProgress = 0.f;
	CenterGroundMsl = Center.bHasAlt ? Center.Alt : 0.0;
	ActiveSource = Source;

#if !CAYSIM_WITH_CESIUM
	if (Source == ECaySimWorldSource::CesiumIon || Source == ECaySimWorldSource::GooglePhotoreal)
	{
		UE_LOG(LogCaySim, Warning, TEXT("Cesium for Unreal eklentisi derlemede yok; düz zemine düşülüyor"));
		ActiveSource = ECaySimWorldSource::Flat;
	}
#endif

	SetupLighting();

	switch (ActiveSource)
	{
	case ECaySimWorldSource::GooglePhotoreal:
		// ion hesabı üzerinden (varlık 2275207) — belirteç varsa HEPGO anahtarı gerekmez
		if (!CesiumIonToken.IsEmpty()) { SetupGoogleIon(); break; }
		if (Client)
		{
			TWeakObjectPtr<ACaySimWorldSetup> WeakThis(this);
			Client->FetchGoogle3dKey([WeakThis](bool bOk, const FString& Key)
			{
				ACaySimWorldSetup* Self = WeakThis.Get();
				if (!Self) return;
				if (bOk) Self->SetupGoogle(Key);
				else
				{
					UE_LOG(LogCaySim, Warning, TEXT("Google 3D Tiles anahtarı alınamadı; Cesium ion / düz zemine düşülüyor"));
					if (!Self->CesiumIonToken.IsEmpty()) { Self->ActiveSource = ECaySimWorldSource::CesiumIon; Self->SetupCesiumIon(); }
					else { Self->ActiveSource = ECaySimWorldSource::Flat; Self->SetupFlat(); }
				}
			});
			return;
		}
		ActiveSource = ECaySimWorldSource::Flat;
		SetupFlat();
		break;
	case ECaySimWorldSource::CesiumIon:
		if (CesiumIonToken.IsEmpty())
		{
			UE_LOG(LogCaySim, Warning, TEXT("Cesium ion belirteci boş; düz zemine düşülüyor"));
			ActiveSource = ECaySimWorldSource::Flat;
			SetupFlat();
		}
		else SetupCesiumIon();
		break;
	case ECaySimWorldSource::HepgoTerrain: // ileride: /terrain + /tile karoları ile prosedürel mesh
	default:
		SetupFlat();
		break;
	}
}

void ACaySimWorldSetup::SetupLighting()
{
	UWorld* W = GetWorld();
	if (!W) return;
	const bool bRealSky = ActiveSource == ECaySimWorldSource::CesiumIon || ActiveSource == ECaySimWorldSource::GooglePhotoreal;
#if CAYSIM_WITH_CESIUM
	// CesiumSunSky (yalnız gerçek dünyada): georeference'a göre gerçek güneş konumu; saat/tarih ayarlanır
	if (bRealSky && !SunSky.IsValid())
	{
		TArray<AActor*> Found;
		UGameplayStatics::GetAllActorsOfClass(W, ACesiumSunSky::StaticClass(), Found);
		SunSky = Found.Num() ? Cast<ACesiumSunSky>(Found[0]) : W->SpawnActor<ACesiumSunSky>();
	}
#endif
	if (!Sun)
	{
		TArray<AActor*> Found;
		UGameplayStatics::GetAllActorsOfClass(W, ADirectionalLight::StaticClass(), Found);
		Sun = Found.Num() ? Cast<ADirectionalLight>(Found[0]) : nullptr;
		bool bNeedSun = true;
#if CAYSIM_WITH_CESIUM
		bNeedSun = !SunSky.IsValid();   // CesiumSunSky kendi yönlü ışığını taşır
#endif
		if (!Sun && bNeedSun) Sun = W->SpawnActor<ADirectionalLight>();
	}
	if (!Sky)
	{
		TArray<AActor*> Found;
		UGameplayStatics::GetAllActorsOfClass(W, ASkyLight::StaticClass(), Found);
		Sky = Found.Num() ? Cast<ASkyLight>(Found[0]) : W->SpawnActor<ASkyLight>();
		if (Sky && Sky->GetLightComponent())
		{
			Sky->GetLightComponent()->SetRealTimeCaptureEnabled(true);
		}
	}
	if (!Atmosphere)
	{
		TArray<AActor*> Found;
		UGameplayStatics::GetAllActorsOfClass(W, ASkyAtmosphere::StaticClass(), Found);
		Atmosphere = Found.Num() ? Cast<ASkyAtmosphere>(Found[0]) : W->SpawnActor<ASkyAtmosphere>();
	}
	if (!Fog)
	{
		TArray<AActor*> Found;
		UGameplayStatics::GetAllActorsOfClass(W, AExponentialHeightFog::StaticClass(), Found);
		Fog = Found.Num() ? Cast<AExponentialHeightFog>(Found[0]) : W->SpawnActor<AExponentialHeightFog>();
	}
	if (!Clouds)
	{
		TArray<AActor*> Found;
		UGameplayStatics::GetAllActorsOfClass(W, AVolumetricCloud::StaticClass(), Found);
		Clouds = Found.Num() ? Cast<AVolumetricCloud>(Found[0]) : W->SpawnActor<AVolumetricCloud>();
	}
	ApplyWorldSettings(World);
}

void ACaySimWorldSetup::DisableBuiltinSky()
{
	if (Sun) { Sun->Destroy(); Sun = nullptr; }
	if (Sky) { Sky->Destroy(); Sky = nullptr; }
	if (Atmosphere) { Atmosphere->Destroy(); Atmosphere = nullptr; }
	if (Fog) { Fog->Destroy(); Fog = nullptr; }
	if (Clouds) { Clouds->Destroy(); Clouds = nullptr; }
#if CAYSIM_WITH_CESIUM
	if (SunSky.IsValid()) { SunSky->Destroy(); SunSky.Reset(); }
#endif
}

void ACaySimWorldSetup::ApplyWorldSettings(const FCaySimWorld& InWorld)
{
	World = InWorld;
	const float Hours = TimeOfDayHours(World.Time);
	bool bSunSky = false;
#if CAYSIM_WITH_CESIUM
	if (SunSky.IsValid())
	{
		SunSky->SolarTime = Hours;
		SunSky->UpdateSun();
		bSunSky = true;
	}
#endif
	if (Sun && !bSunSky)
	{
		// Basit güneş: saat → yükseklik açısı
		const float Elev = FMath::Clamp(FMath::Sin((Hours - 6.f) / 12.f * UE_PI) * 70.f, -10.f, 70.f);
		Sun->SetActorRotation(FRotator(-Elev, 180.f, 0.f));
		if (UDirectionalLightComponent* L = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			L->SetIntensity(World.Time == TEXT("night") ? 0.05f : (World.Time == TEXT("dusk") ? 2.f : 8.f));
			L->SetLightColor(World.Time == TEXT("dusk") ? FLinearColor(1.f, 0.7f, 0.45f) : FLinearColor::White);
		}
	}
	if (Fog && Fog->GetComponent())
	{
		UExponentialHeightFogComponent* F = Fog->GetComponent();
		F->SetFogDensity(FMath::Lerp(0.0005f, 0.02f, FMath::Clamp(World.Fog, 0.f, 1.f)));
		F->SetFogHeightFalloff(0.2f);
		F->SetStartDistance(0.f);
	}
	// AVolumetricCloud bileşen alıcısı sürüme göre değişebilir → bileşeni sınıfıyla bul
	if (UVolumetricCloudComponent* CloudComp = Clouds ? Clouds->FindComponentByClass<UVolumetricCloudComponent>() : nullptr)
	{
		CloudComp->SetVisibility(World.Fog < 0.9f);
	}
}

void ACaySimWorldSetup::SetupFlat()
{
	UWorld* W = GetWorld();
	if (!W) return;
	if (!FlatGround && FlatGroundMesh)
	{
		AStaticMeshActor* G = W->SpawnActor<AStaticMeshActor>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (G && G->GetStaticMeshComponent())
		{
			G->SetMobility(EComponentMobility::Movable);
			G->GetStaticMeshComponent()->SetStaticMesh(FlatGroundMesh);
			G->SetActorScale3D(FVector(World.SizeM / 1.f, World.SizeM / 1.f, 1.f)); // mesh 1×1 m varsayımı
		}
		FlatGround = G;
	}
	// Düz dünyada merkez = Unreal orijini, zemin = Z 0 (MSL = CenterGroundMsl)
	Finish(true);
}

void ACaySimWorldSetup::SetupCesiumIon()
{
#if CAYSIM_WITH_CESIUM
	UWorld* W = GetWorld();
	if (!W) return;
	if (!Georeference.IsValid()) Georeference = ACesiumGeoreference::GetDefaultGeoreference(W);
	ACesiumGeoreference* Geo = Georeference.Get();
	if (!Geo)
	{
		Finish(false);
		return;
	}
	// Georeference merkezi = seçilen merkez; yükseklik elipsoid (MSL + geoid)
	Geo->SetOriginLongitudeLatitudeHeight(FVector(Center.Lon, Center.Lat, CenterGroundMsl + GeoidOffsetM));

	if (!Terrain.IsValid())
	{
		ACesium3DTileset* Tiles = SpawnTiles(Geo);
		if (!Tiles)
		{
			Finish(false);
			return;
		}
		Terrain = Tiles;
		Tiles->SetTilesetSource(ETilesetSource::FromCesiumIon);
		Tiles->SetIonAssetID(CesiumTerrainAssetId);
		Tiles->SetIonAccessToken(CesiumIonToken);
		Tiles->SetGeoreference(TSoftObjectPtr<ACesiumGeoreference>(Geo));
		Tiles->SetCreatePhysicsMeshes(true);
		Tiles->SetMaximumScreenSpaceError(TilesScreenSpaceError);
		Tiles->ShowCreditsOnScreen = true;
		// Bing uydu dokusu
		UCesiumIonRasterOverlay* Overlay = NewObject<UCesiumIonRasterOverlay>(Tiles, TEXT("Imagery"));
		Overlay->IonAssetID = CesiumImageryAssetId;
		Overlay->IonAccessToken = CesiumIonToken;
		Overlay->MaterialLayerKey = TEXT("Overlay0");
		Overlay->RegisterComponent();
		Tiles->AddInstanceComponent(Overlay);
		Overlay->Refresh();
	}
	if (GoogleTiles.IsValid()) GoogleTiles->SetActorHiddenInGame(true);
	LoadProgress = 0.f;
	SourceT0 = FPlatformTime::Seconds();
	UE_LOG(LogCaySim, Log, TEXT("Cesium World Terrain + Bing uydu (ion %lld + %lld) kuruluyor: %.5f, %.5f"), CesiumTerrainAssetId, CesiumImageryAssetId, Center.Lat, Center.Lon);
	// Hazır sayma: ilk kareler yüklendikçe Tick'te LoadProgress izlenir; 100'e yaklaşınca Finish
#else
	Finish(false);
#endif
}

void ACaySimWorldSetup::SetupGoogle(const FString& Key)
{
#if CAYSIM_WITH_CESIUM
	UWorld* W = GetWorld();
	if (!W) return;
	if (!Georeference.IsValid()) Georeference = ACesiumGeoreference::GetDefaultGeoreference(W);
	ACesiumGeoreference* Geo = Georeference.Get();
	if (!Geo)
	{
		Finish(false);
		return;
	}
	Geo->SetOriginLongitudeLatitudeHeight(FVector(Center.Lon, Center.Lat, CenterGroundMsl + GeoidOffsetM));
	if (!GoogleTiles.IsValid())
	{
		ACesium3DTileset* Tiles = SpawnTiles(Geo);
		if (!Tiles)
		{
			Finish(false);
			return;
		}
		GoogleTiles = Tiles;
		Tiles->SetTilesetSource(ETilesetSource::FromUrl);
		Tiles->SetUrl(FString::Printf(TEXT("https://tile.googleapis.com/v1/3dtiles/root.json?key=%s"), *Key));
		Tiles->SetGeoreference(TSoftObjectPtr<ACesiumGeoreference>(Geo));
		Tiles->SetCreatePhysicsMeshes(true);
		Tiles->SetMaximumScreenSpaceError(8.0);
		Tiles->ShowCreditsOnScreen = true; // Google atıf zorunluluğu
	}
	if (Terrain.IsValid()) Terrain->SetActorHiddenInGame(true);
	LoadProgress = 0.f;
#else
	Finish(false);
#endif
}

#if CAYSIM_WITH_CESIUM
// Paketli oyunda ACesiumCreditSystem::GetDefaultCreditSystem tileset OnConstruction sırasında çöküyordu (erişim ihlali, BP yükleyici):
// atıf sistemi önceden kurulur (Cesium BP'si — ekran atıfları — yoksa C++ taban sınıfı) ve tileset ertelenmiş doğuşla ona bağlanır.
ACesium3DTileset* ACaySimWorldSetup::SpawnTiles(ACesiumGeoreference* Geo)
{
	UWorld* W = GetWorld();
	if (!W) return nullptr;
	if (!CreditSys.IsValid())
	{
		TArray<AActor*> Found;
		UGameplayStatics::GetAllActorsOfClass(W, ACesiumCreditSystem::StaticClass(), Found);
		if (Found.Num()) CreditSys = Cast<ACesiumCreditSystem>(Found[0]);
		else
		{
			UClass* Bp = LoadClass<ACesiumCreditSystem>(nullptr, TEXT("/CesiumForUnreal/CesiumCreditSystemBP.CesiumCreditSystemBP_C"), nullptr, LOAD_NoWarn | LOAD_Quiet);
			FActorSpawnParameters Sp;
			Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			CreditSys = W->SpawnActor<ACesiumCreditSystem>(Bp ? Bp : ACesiumCreditSystem::StaticClass(), FTransform::Identity, Sp);
			UE_LOG(LogCaySim, Log, TEXT("Cesium atıf sistemi: %s"), Bp ? TEXT("CesiumCreditSystemBP (ekran atıfları)") : TEXT("C++ taban (ekran atıfı yok)"));
		}
	}
	ACesium3DTileset* T = W->SpawnActorDeferred<ACesium3DTileset>(ACesium3DTileset::StaticClass(), FTransform::Identity, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!T) return nullptr;
	if (CreditSys.IsValid()) T->SetCreditSystem(TSoftObjectPtr<ACesiumCreditSystem>(CreditSys.Get()));
	if (Geo) T->SetGeoreference(TSoftObjectPtr<ACesiumGeoreference>(Geo));
	T->FinishSpawning(FTransform::Identity);
	return T;
}
#endif

void ACaySimWorldSetup::SetupGoogleIon()
{
#if CAYSIM_WITH_CESIUM
	UWorld* W = GetWorld();
	if (!W) return;
	if (!Georeference.IsValid()) Georeference = ACesiumGeoreference::GetDefaultGeoreference(W);
	ACesiumGeoreference* Geo = Georeference.Get();
	if (!Geo) { Finish(false); return; }
	Geo->SetOriginLongitudeLatitudeHeight(FVector(Center.Lon, Center.Lat, CenterGroundMsl + GeoidOffsetM));
	if (!GoogleTiles.IsValid())
	{
		ACesium3DTileset* Tiles = SpawnTiles(Geo);
		if (!Tiles) { Finish(false); return; }
		GoogleTiles = Tiles;
		Tiles->SetTilesetSource(ETilesetSource::FromCesiumIon);
		Tiles->SetIonAssetID(GoogleIonAssetId);
		Tiles->SetIonAccessToken(CesiumIonToken);
		Tiles->SetGeoreference(TSoftObjectPtr<ACesiumGeoreference>(Geo));
		Tiles->SetCreatePhysicsMeshes(true);
		Tiles->SetMaximumScreenSpaceError(TilesScreenSpaceError);
		Tiles->ShowCreditsOnScreen = true;   // Google atıf zorunluluğu (logo + veri sağlayıcıları)
	}
	if (Terrain.IsValid()) Terrain->SetActorHiddenInGame(true);
	LoadProgress = 0.f;
	SourceT0 = FPlatformTime::Seconds();
	UE_LOG(LogCaySim, Log, TEXT("Google Photorealistic 3D Tiles (ion %lld) kuruluyor: %.5f, %.5f, merkez %.1f m MSL"), GoogleIonAssetId, Center.Lat, Center.Lon, CenterGroundMsl);
#else
	Finish(false);
#endif
}

bool ACaySimWorldSetup::CesiumCompiled()
{
	return CAYSIM_WITH_CESIUM != 0;
}

FString ACaySimWorldSetup::SourceName() const
{
	switch (ActiveSource)
	{
	case ECaySimWorldSource::GooglePhotoreal: return TEXT("google3d");
	case ECaySimWorldSource::CesiumIon: return TEXT("ion_terrain");
	default: return TEXT("flat");
	}
}

void ACaySimWorldSetup::SetFixedGroundOffset(double OffsetM)
{
	bAutoGroundOffset = false;
	MeasuredGroundOffsetM = OffsetM;
	bGroundOffsetMeasured = true;
}

void ACaySimWorldSetup::ShiftVertical(double DzM)
{
#if CAYSIM_WITH_CESIUM
	ACesiumGeoreference* Geo = Georeference.Get();
	if (!Geo || FMath::Abs(DzM) < 1e-3) return;
	const FVector O = Geo->GetOriginLongitudeLatitudeHeight();
	Geo->SetOriginLongitudeLatitudeHeight(FVector(O.X, O.Y, O.Z + DzM));
	MeasuredGroundOffsetM = (bGroundOffsetMeasured ? MeasuredGroundOffsetM : GeoidOffsetM) + DzM;
	bGroundOffsetMeasured = true;
	UE_LOG(LogCaySim, Log, TEXT("Georeference düşey kaydırma %.2f m → merkez elipsoid yüksekliği %.2f m, yer ofseti %.2f m"), DzM, O.Z + DzM, MeasuredGroundOffsetM);
#endif
}

#if CAYSIM_WITH_CESIUM
ACesium3DTileset* ACaySimWorldSetup::ActiveTiles() const
{
	return ActiveSource == ECaySimWorldSource::GooglePhotoreal ? GoogleTiles.Get() : ActiveSource == ECaySimWorldSource::CesiumIon ? Terrain.Get() : nullptr;
}
#endif

AActor* ACaySimWorldSetup::GetTilesActor() const
{
#if CAYSIM_WITH_CESIUM
	return ActiveTiles();
#else
	return nullptr;
#endif
}

bool ACaySimWorldSetup::TraceTiles(const FVector& Top, const FVector& Bottom, FHitResult& Out) const
{
#if CAYSIM_WITH_CESIUM
	UWorld* W = GetWorld();
	const ACesium3DTileset* T = ActiveTiles();
	if (!W || !T) return false;
	TArray<FHitResult> Hits;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(CaySimTilesTrace), true);
	W->LineTraceMultiByObjectType(Hits, Top, Bottom, FCollisionObjectQueryParams(ECC_WorldStatic), Q);
	for (const FHitResult& H : Hits)
	{
		if (H.GetActor() == T) { Out = H; return true; }
	}
#endif
	return false;
}

bool ACaySimWorldSetup::ClipPolygon(const TArray<FVector>& UnrealPoints)
{
#if CAYSIM_WITH_CESIUM
	UWorld* W = GetWorld();
	ACesium3DTileset* T = ActiveTiles();
	if (!W || !T || UnrealPoints.Num() < 3) return false;
	if (ClipActor.IsValid()) { ClipActor->Destroy(); ClipActor.Reset(); }
	ACesiumCartographicPolygon* Poly = W->SpawnActor<ACesiumCartographicPolygon>();
	if (!Poly || !Poly->Polygon) return false;
	ClipActor = Poly;
	USplineComponent* Sp = Poly->Polygon;
	Sp->ClearSplinePoints(false);
	for (const FVector& P : UnrealPoints) Sp->AddSplinePoint(P, ESplineCoordinateSpace::World, false);
	for (int32 i = 0; i < Sp->GetNumberOfSplinePoints(); ++i) Sp->SetSplinePointType(i, ESplinePointType::Linear, false);
	Sp->SetClosedLoop(true, false);
	Sp->UpdateSpline();
	UCesiumPolygonRasterOverlay* Ov = NewObject<UCesiumPolygonRasterOverlay>(T, TEXT("SmvClip"));
	Ov->MaterialLayerKey = TEXT("Clipping");
	Ov->Polygons.Add(TSoftObjectPtr<ACesiumCartographicPolygon>(Poly));
	Ov->InvertSelection = false;     // çokgenin İÇİ kırpılır
	Ov->ExcludeSelectedTiles = true; // tamamen içte kalan karolar hiç yüklenmez
	Ov->RegisterComponent();
	T->AddInstanceComponent(Ov);
	Ov->Refresh();
	UE_LOG(LogCaySim, Log, TEXT("3D Tiles kırpma çokgeni: %d nokta (%s)"), UnrealPoints.Num(), *SourceName());
	return true;
#else
	return false;
#endif
}

void ACaySimWorldSetup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
#if CAYSIM_WITH_CESIUM
	// tanı: 3D Tiles durumu (her 6 sn): yükleme yüzdesi, sahnedeki karo bileşeni sayısı, görünürlük, merkez ışını
	if (ACesium3DTileset* Td = ActiveTiles())
	{
		const double Now = FPlatformTime::Seconds();
		if (Now - LastDiagAt > 6.0)
		{
			LastDiagAt = Now;
			TInlineComponentArray<UPrimitiveComponent*> Prims(Td);
			int32 Vis = 0, Coll = 0;
			for (UPrimitiveComponent* P : Prims) { if (P && P->IsVisible()) ++Vis; if (P && P->IsCollisionEnabled()) ++Coll; }
			FHitResult H;
			const bool bHit = TraceTiles(FVector(0, 0, 200000.0), FVector(0, 0, -200000.0), H);
			const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
			const FVector Cam = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
			UE_LOG(LogCaySim, Log, TEXT("3D Tiles tanı (%s): yükleme %%%.0f, bileşen %d (görünür %d, çarpışmalı %d), gizli %d, merkez ışını %s, kamera (%.0f, %.0f, %.0f)"),
				*SourceName(), Td->GetLoadProgress(), Prims.Num(), Vis, Coll, Td->IsHidden() ? 1 : 0, bHit ? *FString::Printf(TEXT("Z %.0f cm"), H.ImpactPoint.Z) : TEXT("yok"), Cam.X, Cam.Y, Cam.Z);
		}
	}
	if (!bReady && (ActiveSource == ECaySimWorldSource::CesiumIon || ActiveSource == ECaySimWorldSource::GooglePhotoreal))
	{
		ACesium3DTileset* T = (ActiveSource == ECaySimWorldSource::GooglePhotoreal) ? GoogleTiles.Get() : Terrain.Get();
		if (T)
		{
			LoadProgress = T->GetLoadProgress();
			// Google 3B hiç karo getirmiyor (kota / ağ / lisans) → World Terrain + Bing
			if (ActiveSource == ECaySimWorldSource::GooglePhotoreal && LoadProgress <= 0.01f && SourceT0 > 0 && FPlatformTime::Seconds() - SourceT0 > GoogleFallbackS && !CesiumIonToken.IsEmpty())
			{
				UE_LOG(LogCaySim, Warning, TEXT("Google 3D Tiles %.0f sn'de yüklenmedi — Cesium World Terrain + Bing uyduya düşülüyor"), GoogleFallbackS);
				T->SetActorHiddenInGame(true);
				T->Destroy();
				GoogleTiles.Reset();
				bFellBack = true;
				ActiveSource = ECaySimWorldSource::CesiumIon;
				SetupCesiumIon();
				return;
			}
			if (LoadProgress >= 99.f)
			{
				if (bAutoGroundOffset && !bGroundOffsetMeasured) MeasureGroundOffset();
				Finish(true);
			}
		}
	}
#endif
}

void ACaySimWorldSetup::MeasureGroundOffset()
{
	// Merkezin üstünden aşağı ışın: 3D Tiles yüzeyi (fizik mesh) nerede? Ev irtifası (MSL) ile farkı = yer ofseti.
	UWorld* W = GetWorld();
	if (!W) return;
	// merkezdeki 3D Tiles yüzeyi Unreal Z = 0'a (ev irtifası) gelsin: georeference düşeyde kaydırılır, yer ofseti birlikte değişir —
	// kurulumda Geo(ev MSL) ile yerleştirilmiş aktörler (pilot, rüzgâr tulumu) yerinde kalır
	// fotogerçekçi karolarda merkez ağaç / çatı olabilir: ±15 m'lik 3×3 örnekten en alçak yüzey (zemin)
	double Best = TNumericLimits<double>::Max();
	for (int32 i = -1; i <= 1; ++i) for (int32 j = -1; j <= 1; ++j)
	{
		FHitResult H;
		if (TraceTiles(FVector(i * 1500.0, j * 1500.0, 150000.0), FVector(i * 1500.0, j * 1500.0, -150000.0), H)) Best = FMath::Min(Best, (double)H.ImpactPoint.Z);
	}
	if (Best < TNumericLimits<double>::Max())
	{
		const double Dz = Best / 100.0;
		ShiftVertical(Dz);
		UE_LOG(LogCaySim, Log, TEXT("Yer ofseti ölçüldü: merkez yüzeyi %.1f m kaydırıldı (geoid varsayımı %.1f)"), Dz, GeoidOffsetM);
	}
	else
	{
		bGroundOffsetMeasured = true;
		UE_LOG(LogCaySim, Warning, TEXT("Yer ofseti ölçülemedi: merkezde 3D Tiles yüzeyi yok (geoid varsayımı %.1f m)"), GeoidOffsetM);
	}
}

void ACaySimWorldSetup::Finish(bool bOk)
{
	bReady = bOk;
	LoadProgress = 100.f;
	OnWorldReady.Broadcast(bOk);
}

// ---- ICaySimGeoTransform ------------------------------------------------

FVector ACaySimWorldSetup::GeoToUnreal(double Lon, double Lat, double HeightM) const
{
#if CAYSIM_WITH_CESIUM
	if (const ACesiumGeoreference* Geo = Georeference.Get()) return Geo->TransformLongitudeLatitudeHeightPositionToUnreal(FVector(Lon, Lat, HeightM));
#endif
	const FVector Enu = CaySimGeo::GeoToEnuMeters(Lon, Lat, HeightM, Center.Lon, Center.Lat, CenterGroundMsl);
	return FVector(Enu.X, -Enu.Y, Enu.Z) * 100.0; // X Doğu, Y Güney, Z Yukarı (Cesium sözleşmesiyle aynı)
}

void ACaySimWorldSetup::UnrealToGeo(const FVector& Unreal, double& Lon, double& Lat, double& HeightM) const
{
#if CAYSIM_WITH_CESIUM
	if (const ACesiumGeoreference* Geo = Georeference.Get())
	{
		const FVector LLH = Geo->TransformUnrealPositionToLongitudeLatitudeHeight(Unreal);
		Lon = LLH.X; Lat = LLH.Y; HeightM = LLH.Z;
		return;
	}
#endif
	const FVector Enu(Unreal.X / 100.0, -Unreal.Y / 100.0, Unreal.Z / 100.0);
	CaySimGeo::EnuMetersToGeo(Enu, Center.Lon, Center.Lat, CenterGroundMsl, Lon, Lat, HeightM);
}

FQuat ACaySimWorldSetup::EnuToUnrealRotation(const FVector& UnrealLocation) const
{
#if CAYSIM_WITH_CESIUM
	if (const ACesiumGeoreference* Geo = Georeference.Get())
	{
		// Doğu-Güney-Yukarı → Unreal; kürenin eğriliği nedeniyle konuma bağlı
		const FMatrix M = Geo->ComputeEastSouthUpToUnrealTransformation(UnrealLocation);
		return M.ToQuat();
	}
#endif
	return FQuat::Identity;
}

double ACaySimWorldSetup::MslToWorldHeight(double Lat, double Lon, double MslM) const
{
#if CAYSIM_WITH_CESIUM
	if (Georeference.IsValid()) return MslM + (bGroundOffsetMeasured ? MeasuredGroundOffsetM : GeoidOffsetM);
#endif
	return MslM;
}

bool ACaySimWorldSetup::SampleGroundHeight(double Lat, double Lon, double& OutHeightM) const
{
	UWorld* W = GetWorld();
	if (!W) return false;
	if (ActiveSource == ECaySimWorldSource::Flat)
	{
		OutHeightM = CenterGroundMsl;
		return true;
	}
	const double Base = MslToWorldHeight(Lat, Lon, CenterGroundMsl);
	const FVector Top = GeoToUnreal(Lon, Lat, Base + 3000.0);
	const FVector Bottom = GeoToUnreal(Lon, Lat, Base - 1000.0);
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(CaySimGroundSample), true);
	if (W->LineTraceSingleByChannel(Hit, Top, Bottom, ECC_WorldStatic, Q))
	{
		double L1, L2;
		UnrealToGeo(Hit.ImpactPoint, L1, L2, OutHeightM);
		return true;
	}
	return false;
}
