#include "CesiumWorldSetup.h"
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
#include "CollisionQueryParams.h"

#if CAYSIM_WITH_CESIUM
#include "CesiumGeoreference.h"
#include "Cesium3DTileset.h"
#include "CesiumSunSky.h"
#include "CesiumIonRasterOverlay.h"
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
#if CAYSIM_WITH_CESIUM
	// CesiumSunSky: georeference'a göre gerçek güneş konumu; saat/tarih ayarlanır
	if (!SunSky.IsValid())
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
#if !CAYSIM_WITH_CESIUM
		if (!Sun) Sun = W->SpawnActor<ADirectionalLight>();
#endif
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
#if CAYSIM_WITH_CESIUM
	if (SunSky.IsValid())
	{
		SunSky->SolarTime = Hours;
		SunSky->UpdateSun();
	}
#else
	if (Sun)
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
#endif
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
		ACesium3DTileset* Tiles = W->SpawnActor<ACesium3DTileset>();
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
		Tiles->SetMaximumScreenSpaceError(12.0);
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
		ACesium3DTileset* Tiles = W->SpawnActor<ACesium3DTileset>();
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

void ACaySimWorldSetup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
#if CAYSIM_WITH_CESIUM
	if (!bReady && (ActiveSource == ECaySimWorldSource::CesiumIon || ActiveSource == ECaySimWorldSource::GooglePhotoreal))
	{
		ACesium3DTileset* T = (ActiveSource == ECaySimWorldSource::GooglePhotoreal) ? GoogleTiles.Get() : Terrain.Get();
		if (T)
		{
			LoadProgress = T->GetLoadProgress();
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
	const FVector Top = GeoToUnreal(Center.Lon, Center.Lat, CenterGroundMsl + GeoidOffsetM + 500.0);
	const FVector Bottom = GeoToUnreal(Center.Lon, Center.Lat, CenterGroundMsl + GeoidOffsetM - 500.0);
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(CaySimGround), true);
	if (W->LineTraceSingleByChannel(Hit, Top, Bottom, ECC_WorldStatic, Q))
	{
		double Lon, Lat, H;
		UnrealToGeo(Hit.ImpactPoint, Lon, Lat, H);
		MeasuredGroundOffsetM = H - CenterGroundMsl; // elipsoid yüzey yüksekliği − MSL
		bGroundOffsetMeasured = true;
		UE_LOG(LogCaySim, Log, TEXT("Yer ofseti ölçüldü: %.1f m (geoid varsayımı %.1f)"), MeasuredGroundOffsetM, GeoidOffsetM);
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
