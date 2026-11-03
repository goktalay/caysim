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
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PrimitiveComponent.h"
#include "CollisionQueryParams.h"
#include "UObject/UObjectIterator.h"
#include "RHI.h"

#if CAYSIM_WITH_CESIUM
#include "CesiumGeoreference.h"
#include "CesiumCameraManager.h"
#include "CesiumCamera.h"
#include "Cesium3DTileset.h"
#include "CesiumSunSky.h"
#include "CesiumCreditSystem.h"
#include "CesiumSampleHeightResult.h"
#include "CesiumIonRasterOverlay.h"
#include "CesiumPolygonRasterOverlay.h"
#include "CesiumCartographicPolygon.h"
THIRD_PARTY_INCLUDES_START
#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>
THIRD_PARTY_INCLUDES_END
#include <mutex>
#include <atomic>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#endif

#if CAYSIM_WITH_CESIUM
namespace
{
	// cesium-native iletileri (karo yükleme hataları, glTF okuma uyarıları) paketli oyunda günlükte hiç görünmüyordu: ek spdlog alıcısı
	// LogCaySim'e "cesium-native" önekiyle yazar (uyarı ve üstü; -CesiumStats ile bilgi düzeyi de)
	class FCaySimSpdSink final : public spdlog::sinks::base_sink<std::mutex>
	{
	protected:
		virtual void sink_it_(const spdlog::details::log_msg& Msg) override
		{
			const std::string S(Msg.payload.data(), Msg.payload.size());
			static std::atomic<int32> N{0};
			if (N.fetch_add(1) > 4000) return;   // sel önleme
			UE_LOG(LogCaySim, Log, TEXT("cesium-native[%d]: %s"), (int32)Msg.level, UTF8_TO_TCHAR(S.c_str()));
		}
		virtual void flush_() override {}
	};

	void InstallCesiumLogSink(bool bVerbose)
	{
		static bool bDone = false;
		if (bDone) return;
		bDone = true;
		std::shared_ptr<spdlog::logger> L = spdlog::default_logger();
		if (!L) return;
		auto Sink = std::make_shared<FCaySimSpdSink>();
		Sink->set_level(bVerbose ? spdlog::level::info : spdlog::level::warn);
		L->sinks().push_back(Sink);
		if (bVerbose) L->set_level(spdlog::level::info);
		UE_LOG(LogCaySim, Log, TEXT("cesium-native günlük alıcısı eklendi (%d alıcı, düzey %d)"), (int32)L->sinks().size(), (int32)L->level());
	}
}
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

// Uçuş bitince kurduğu 3D Tiles / kırpma / güneş-gök aktörleri de kalkar (eskiden sonraki uçuşlarda karo kümeleri birikiyordu)
void ACaySimWorldSetup::EndPlay(const EEndPlayReason::Type Reason)
{
#if CAYSIM_WITH_CESIUM
	for (TWeakObjectPtr<ACesium3DTileset>* P : { &Terrain, &GoogleTiles, &CollisionTiles, &Buildings })
	{
		if (P->IsValid()) (*P)->Destroy();
		P->Reset();
	}
	if (ClipActor.IsValid()) ClipActor->Destroy();
	ClipActor.Reset();
	if (SunSky.IsValid()) SunSky->Destroy();
	SunSky.Reset();
#endif
	Super::EndPlay(Reason);
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
	// tüm gerçek dünya Cesium World Terrain + uydu ile (kullanıcı 2026-11-02): Google Photorealistic 3D Tiles günlük "root" kotası
	// (Map Tiles API) her dünya kurulumunda harcanıyor ve doluyordu (HTTP 429) → Google isteği arazi + uyduya çevrilir
	if (Source == ECaySimWorldSource::GooglePhotoreal) Source = ECaySimWorldSource::CesiumIon;
	ActiveSource = Source;
	bLogTileStats = FParse::Param(FCommandLine::Get(), TEXT("CesiumStats"));
#if CAYSIM_WITH_CESIUM
	InstallCesiumLogSink(bLogTileStats);
#endif
#if CAYSIM_WITH_CESIUM
	// Google 3B karoları işçi kuyruğunda takılıyordu (500+ "Loading-Worker", hiç "Loading-Main", 0 glTF bileşeni; 1588 glb HTTP 200 ile
	// indirilmişken): Cesium for Unreal glTF dokularını D3D12'de işçi iş parçacığında RHIAsyncCreateTexture2D + CompletionEvent->Wait() ile
	// oluşturur (CesiumTextureResource.cpp createAsyncTextureAndWait) — UE 5.8 D3D12'de bu bekleme dönmüyor, karo hiç ana iş parçacığına
	// geçmiyor. Arazi + Bing'de karo başına doku yok (bindirme ayrı, az sayıda) — o yüzden yalnız Google'da görülüyordu. Eşzamanlı doku
	// oluşturma kapatılınca Cesium dokuyu oyun / çizim iş parçacığında kurar (D3D dışındaki bütün RHI'larda zaten böyle). -CesiumAsyncTex: eski yol.
	// NOT: kapatınca işçi kuyruğu boşalıyor ama D3D12 RHIThread'de FD3D12Texture::UpdateTexture erişim ihlali (smoke 40240005950) — yalnız deney: -CesiumSyncTex
	if (GRHISupportsAsyncTextureCreation && FParse::Param(FCommandLine::Get(), TEXT("CesiumSyncTex")))
	{
		GRHISupportsAsyncTextureCreation = false;
		UE_LOG(LogCaySim, Log, TEXT("3D Tiles: eşzamansız RHI doku oluşturma kapatıldı (Cesium glTF dokuları çizim iş parçacığında)"));
	}
#endif

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
		Tiles->ForbidHoles = true;   // ayrıntılı karo gelene dek kaba karo kalır (görev haritasında mavi delikler)
		Tiles->SetCreatePhysicsMeshes(true);
		Tiles->SetMaximumScreenSpaceError(TilesScreenSpaceError);
		Tiles->ShowCreditsOnScreen = true;
		Tiles->LogSelectionStats = bLogTileStats;
		Tiles->MaximumSimultaneousTileLoads = 12;
		// Bing uydu dokusu
		UCesiumIonRasterOverlay* Overlay = NewObject<UCesiumIonRasterOverlay>(Tiles, TEXT("Imagery"));
		Overlay->IonAssetID = CesiumImageryAssetId;
		Overlay->IonAccessToken = CesiumIonToken;
		Overlay->MaterialLayerKey = TEXT("Overlay0");
		// uydu dokusu web Cesium kadar net: ekran-uzayı hatası 1 (varsayılan 2), doku 4096
		Overlay->SetMaximumScreenSpaceError(1.0);
		Overlay->SetMaximumTextureSize(4096);
		Overlay->SetMaximumSimultaneousTileLoads(24);
		Overlay->RegisterComponent();
		Tiles->AddInstanceComponent(Overlay);
		Overlay->Refresh();
	}
	// 3B binalar (Cesium OSM Buildings): web Cesium'daki haritada tanımlı binalar; çarpışmalı (drona engel)
	if (!Buildings.IsValid() && CesiumBuildingsAssetId > 0)
	{
		if (ACesium3DTileset* Bt = SpawnTiles(Geo))
		{
			Buildings = Bt;
			Bt->SetTilesetSource(ETilesetSource::FromCesiumIon);
			Bt->SetIonAssetID(CesiumBuildingsAssetId);
			Bt->SetIonAccessToken(CesiumIonToken);
			Bt->SetGeoreference(TSoftObjectPtr<ACesiumGeoreference>(Geo));
			Bt->ForbidHoles = true;
			Bt->SetCreatePhysicsMeshes(true);
			Bt->SetMaximumScreenSpaceError(16.0);
			Bt->MaximumSimultaneousTileLoads = 12;
			UE_LOG(LogCaySim, Log, TEXT("Cesium OSM 3B binalar (ion %lld) kuruluyor"), CesiumBuildingsAssetId);
		}
	}
	if (Buildings.IsValid()) Buildings->SetActorHiddenInGame(false);
	if (GoogleTiles.IsValid()) GoogleTiles->SetActorHiddenInGame(true);
	LoadProgress = 0.f;
	SourceT0 = FPlatformTime::Seconds();
	StallProg = 0.f; StallAt = 0; bStallLogged = false;
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
		Tiles->ForbidHoles = true;   // ayrıntılı karo gelene dek kaba karo kalır (görev haritasında mavi delikler)
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
	// görüş dışı karolar da kaba LOD'da (SSE 256) yüklü kalır: 3440×1440'ta ve hızlı dönüşte kenarlarda mavi delik / boşluk kalıyordu
	T->EnableFrustumCulling = false;
	T->EnforceCulledScreenSpaceError = true;
	T->CulledScreenSpaceError = 256.0;   // 64'te karo yükü ~2 kat, Google 40 sn düşüşüne gidiyordu
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
		Tiles->ForbidHoles = true;   // ayrıntılı karo gelene dek kaba karo kalır (görev haritasında mavi delikler)
		// fotogrametri karolarında fizik ağı pişirmesi işçi iş parçacıklarını kilitliyordu (190 karo "Loading-Worker"da takılı, %5–8):
		// görüntü fiziksiz, çarpışma / yükseklik örneği gizli Cesium World Terrain'den (yalın arazi, hafif)
		Tiles->SetCreatePhysicsMeshes(bGooglePhysics);
		Tiles->SetMaximumScreenSpaceError(TilesScreenSpaceError);
		Tiles->ShowCreditsOnScreen = true;   // Google atıf zorunluluğu (logo + veri sağlayıcıları)
		Tiles->LogSelectionStats = bLogTileStats;
		Tiles->MaximumSimultaneousTileLoads = 12;
	}
	if (!bGooglePhysics && !CollisionTiles.IsValid())
	{
		if (ACesium3DTileset* Ct = SpawnTiles(Geo))
		{
			CollisionTiles = Ct;
			Ct->SetTilesetSource(ETilesetSource::FromCesiumIon);
			Ct->SetIonAssetID(CesiumTerrainAssetId);
			Ct->SetIonAccessToken(CesiumIonToken);
			Ct->SetCreatePhysicsMeshes(true);
			Ct->SetMaximumScreenSpaceError(16.0);
			Ct->SetActorHiddenInGame(true);   // yalnız çarpışma / yükseklik
			Ct->LogSelectionStats = bLogTileStats;
			Ct->MaximumSimultaneousTileLoads = 8;
		}
	}
	if (Terrain.IsValid()) Terrain->SetActorHiddenInGame(true);
	LoadProgress = 0.f;
	SourceT0 = FPlatformTime::Seconds();
	StallProg = 0.f; StallAt = 0; bStallLogged = false;
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
	return CollisionTiles.IsValid() ? CollisionTiles.Get() : ActiveTiles();
#else
	return nullptr;
#endif
}

bool ACaySimWorldSetup::TraceTiles(const FVector& Top, const FVector& Bottom, FHitResult& Out) const
{
#if CAYSIM_WITH_CESIUM
	UWorld* W = GetWorld();
	const ACesium3DTileset* T = CollisionTiles.IsValid() ? CollisionTiles.Get() : ActiveTiles();
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

void ACaySimWorldSetup::SampleSurfaceZ(const TArray<FVector>& UnrealPoints, TFunction<void(const TArray<double>& UnrealZ, const TArray<bool>& bOk)> Done)
{
#if CAYSIM_WITH_CESIUM
	ACesium3DTileset* T = CollisionTiles.IsValid() ? CollisionTiles.Get() : ActiveTiles();   // yalın arazi (ağaç / çatı değil)
	if (!T || UnrealPoints.Num() == 0) { Done(TArray<double>(), TArray<bool>()); return; }
	TArray<FVector> Llh;
	for (const FVector& P : UnrealPoints)
	{
		double Lon, Lat, H;
		UnrealToGeo(P, Lon, Lat, H);
		Llh.Add(FVector(Lon, Lat, 0.0));
	}
	TWeakObjectPtr<ACaySimWorldSetup> Self(this);
	T->SampleHeightMostDetailed(Llh, FCesiumSampleHeightMostDetailedCallback::CreateLambda(
		[Self, Done](ACesium3DTileset*, const TArray<FCesiumSampleHeightResult>& R, const TArray<FString>& Warnings)
		{
			TArray<double> Z; TArray<bool> Ok;
			ACaySimWorldSetup* S = Self.Get();
			for (const FCesiumSampleHeightResult& X : R)
			{
				Ok.Add(S && X.SampleSuccess);
				Z.Add(S && X.SampleSuccess ? S->GeoToUnreal(X.LongitudeLatitudeHeight.X, X.LongitudeLatitudeHeight.Y, X.LongitudeLatitudeHeight.Z).Z : 0.0);
			}
			if (Warnings.Num()) UE_LOG(LogCaySim, Warning, TEXT("3D Tiles yükseklik örneği uyarısı: %s"), *Warnings[0]);
			Done(Z, Ok);
		}));
#else
	Done(TArray<double>(), TArray<bool>());
#endif
}

void ACaySimWorldSetup::SampleGroundMostDetailed(const TArray<FVector2D>& LonLat, TFunction<void(const TArray<double>& HeightM, const TArray<bool>& bOk)> Done)
{
#if CAYSIM_WITH_CESIUM
	ACesium3DTileset* T = CollisionTiles.IsValid() ? CollisionTiles.Get() : ActiveTiles();   // yalın arazi (bina çatısı değil)
	if (!T || LonLat.Num() == 0) { Done(TArray<double>(), TArray<bool>()); return; }
	TArray<FVector> Llh;
	for (const FVector2D& P : LonLat) Llh.Add(FVector(P.X, P.Y, 0.0));
	T->SampleHeightMostDetailed(Llh, FCesiumSampleHeightMostDetailedCallback::CreateLambda(
		[Done](ACesium3DTileset*, const TArray<FCesiumSampleHeightResult>& R, const TArray<FString>&)
		{
			TArray<double> H; TArray<bool> Ok;
			for (const FCesiumSampleHeightResult& X : R) { Ok.Add(X.SampleSuccess); H.Add(X.SampleSuccess ? X.LongitudeLatitudeHeight.Z : 0.0); }
			Done(H, Ok);
		}));
#else
	Done(TArray<double>(), TArray<bool>());
#endif
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
	if (Buildings.IsValid() && Buildings.Get() != T)
	{
		UCesiumPolygonRasterOverlay* Ob = NewObject<UCesiumPolygonRasterOverlay>(Buildings.Get(), TEXT("SmvClipB"));
		Ob->MaterialLayerKey = TEXT("Clipping");
		Ob->Polygons.Add(TSoftObjectPtr<ACesiumCartographicPolygon>(Poly));
		Ob->InvertSelection = false;
		Ob->ExcludeSelectedTiles = true;
		Ob->RegisterComponent();
		Buildings->AddInstanceComponent(Ob);
		Ob->Refresh();
	}
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
			// dünyadaki tüm Cesium glTF ilkel bileşenleri sahip aktöre göre (karo bileşeni tileset dışında bir sahipte mi / hiç yok mu)
			TMap<FString, int32> ByOwner;
			int32 Gl = 0, GlVis = 0, GlReg = 0;
			for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
			{
				UPrimitiveComponent* C = *It;
				if (!C || C->GetWorld() != GetWorld() || !C->GetClass()->GetName().Contains(TEXT("CesiumGltf"))) continue;
				++Gl; if (C->IsVisible()) ++GlVis; if (C->IsRegistered()) ++GlReg;
				const AActor* O = C->GetOwner();
				ByOwner.FindOrAdd(O ? O->GetName() : FString(TEXT("(sahipsiz) ")) + C->GetOuter()->GetName())++;
			}
			// karo kökü bileşenleri (UCesiumGltfComponent — sahne bileşeni; ilkel değil): karo ana iş parçacığına ulaşıp ilkel üretemiyor mu?
			int32 GlRoot = 0, GlRootChildless = 0;
			for (TObjectIterator<USceneComponent> It; It; ++It)
			{
				USceneComponent* C = *It;
				if (!C || C->GetWorld() != GetWorld() || C->GetOwner() != Td || C->GetClass()->GetName() != TEXT("CesiumGltfComponent")) continue;
				++GlRoot;
				if (C->GetNumChildrenComponents() == 0) ++GlRootChildless;
			}
			ByOwner.Add(FString::Printf(TEXT("[aktif karo kökü %d, ilkelsiz %d]"), GlRoot, GlRootChildless), 0);
			// Cesium'un karo seçiminde kullandığı oyuncu kamerası: görünüm boyutu (< 1 → kamera atlanır, karolar incelmez), FOV, DPI ölçeği
			if (PC)
			{
				int32 Vx = 0, Vy = 0;
				PC->GetViewportSize(Vx, Vy);
				float Dpi = 1.f;
				if (const ULocalPlayer* Lp = Cast<ULocalPlayer>(PC->Player)) if (Lp->ViewportClient) Dpi = Lp->ViewportClient->GetDPIScale();
				ByOwner.Add(FString::Printf(TEXT("[görünüm %dx%d, FOV %.0f, DPI %.2f, SSE %.1f]"), Vx, Vy, PC->PlayerCameraManager ? PC->PlayerCameraManager->GetFOVAngle() : -1.f, Dpi, Td->GetMaximumScreenSpaceError()), 0);
			}
			FString Own;
			for (const TPair<FString, int32>& Kv : ByOwner) Own += FString::Printf(TEXT(" %s=%d"), *Kv.Key, Kv.Value);
			UE_LOG(LogCaySim, Log, TEXT("3D Tiles tanı (%s): yükleme %%%.0f, bileşen %d (görünür %d, çarpışmalı %d), gizli %d, merkez ışını %s, kamera (%.0f, %.0f, %.0f); glTF ilkel %d (görünür %d, kayıtlı %d):%s"),
				*SourceName(), Td->GetLoadProgress(), Prims.Num(), Vis, Coll, Td->IsHidden() ? 1 : 0, bHit ? *FString::Printf(TEXT("Z %.0f cm"), H.ImpactPoint.Z) : TEXT("yok"), Cam.X, Cam.Y, Cam.Z,
				Gl, GlVis, GlReg, *Own);
		}
	}
	if (!bReady && (ActiveSource == ECaySimWorldSource::CesiumIon || ActiveSource == ECaySimWorldSource::GooglePhotoreal))
	{
		ACesium3DTileset* T = (ActiveSource == ECaySimWorldSource::GooglePhotoreal) ? GoogleTiles.Get() : Terrain.Get();
		if (T)
		{
			LoadProgress = T->GetLoadProgress();
			// Google 3B hiç karo getirmiyor / görünür karo bileşeni oluşmuyor (kota / ağ / lisans; paketli oyunda işçi kuyruğunda takılma) → World Terrain + Bing
			bool bNoContent = LoadProgress <= 0.01f;
			// Google: yükleme %100 olsa bile (yalnız JSON alt ağaçları) çizilecek karo bileşeni yoksa içerik yok sayılır — eskiden %99'da hazır
			// deniyor, düşüş hiç olmuyor, kullanıcı boş gök görüyordu
			bool bGoogleEmpty = false;
			if (ActiveSource == ECaySimWorldSource::GooglePhotoreal)
			{
				TInlineComponentArray<UPrimitiveComponent*> Pc(T);
				bGoogleEmpty = Pc.Num() == 0;
				bNoContent = bNoContent || bGoogleEmpty;
			}
			if (ActiveSource == ECaySimWorldSource::GooglePhotoreal && bNoContent && SourceT0 > 0 && FPlatformTime::Seconds() - SourceT0 > GoogleFallbackS && !CesiumIonToken.IsEmpty())
			{
				UE_LOG(LogCaySim, Warning, TEXT("Google 3D Tiles %.0f sn'de yüklenmedi — Cesium World Terrain + Bing uyduya düşülüyor"), GoogleFallbackS);
				T->SetActorHiddenInGame(true);
				T->Destroy();
				GoogleTiles.Reset();
				bFellBack = true;
				if (CollisionTiles.IsValid()) { CollisionTiles->Destroy(); CollisionTiles.Reset(); }
				ActiveSource = ECaySimWorldSource::CesiumIon;
				SetupCesiumIon();
				return;
			}
			// Google: %95 (ultra geniş ekranda son karolar uzun kuyrukla gelir; kalanı uçuşta incelir)
			// durağan kuyruk (kullanıcı 2026-11-03: ara sıra "Harita yüklenemedi", Yeniden dene ile tamamlanıyor): Cesium ilerlemesi
			// görünür karoların inceltilmesiyle %80–95'te uzun süre asılı kalabiliyor ve 90 sn kapısı aşılıyordu. Kaba karolar
			// yerindeyse (≥ %70) ve ilerleme 10 sn boyunca %1'den fazla artmadıysa ya da 40 sn geçtiyse hazır sayılır; kalan
			// karolar uçuşta akmaya devam eder.
			const double NowS = FPlatformTime::Seconds();
			if (StallAt <= 0 || LoadProgress > StallProg + 1.f) { StallProg = LoadProgress; StallAt = NowS; }
			const bool bStalled = LoadProgress >= 70.f && !bNoContent && SourceT0 > 0 && (NowS - StallAt > 10.0 || NowS - SourceT0 > 40.0);
			if (bStalled && LoadProgress < 95.f && !bStallLogged) { bStallLogged = true; UE_LOG(LogCaySim, Log, TEXT("3D Tiles %%%.0f'de durağan (%.0f sn) — hazır sayılıyor, kalan karolar uçuşta akacak"), LoadProgress, NowS - SourceT0); }
			if ((LoadProgress >= (ActiveSource == ECaySimWorldSource::GooglePhotoreal ? 95.f : 99.f) || bStalled) && !bGoogleEmpty)
			{
				// yer ofseti: merkezde karo yüzeyi henüz yoksa (ilk %100 kaba karolarla gelir) 30 sn'ye dek yeniden denenir — eskiden
				// hemen geoid varsayımına (35 m) düşülüyor, Kızılay'da araç ve kamera Google yüzeyinin ~30 m altında kalıyordu
				if (bAutoGroundOffset && !bGroundOffsetMeasured)
				{
					MeasureGroundOffset();
					if (!bGroundOffsetMeasured)
					{
						if (FPlatformTime::Seconds() - SourceT0 < 30.0) return;
						bGroundOffsetMeasured = true;
						UE_LOG(LogCaySim, Warning, TEXT("Yer ofseti 30 sn'de ölçülemedi — geoid varsayımı %.1f m"), GeoidOffsetM);
					}
				}
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
		// çağıran yeniden dener (Tick: 30 sn'ye dek); bayrak burada kalkmaz
		UE_LOG(LogCaySim, Verbose, TEXT("Yer ofseti henüz ölçülemedi: merkezde 3D Tiles yüzeyi yok"));
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

int32 ACaySimWorldSetup::UpsertExtraCamera(const UObject* Ctx, int32 Id, const FVector2D& Size, const FVector& Loc, const FRotator& Rot, double FovDeg)
{
#if CAYSIM_WITH_CESIUM
	ACesiumCameraManager* M = Ctx ? ACesiumCameraManager::GetDefaultCameraManager(Ctx) : nullptr;
	if (!M) return -1;
	const FCesiumCamera C(Size, Loc, Rot, FovDeg);
	if (Id >= 0 && M->UpdateCamera(Id, C)) return Id;
	return M->AddCamera(C);
#else
	return -1;
#endif
}

void ACaySimWorldSetup::RemoveExtraCamera(const UObject* Ctx, int32 Id)
{
#if CAYSIM_WITH_CESIUM
	if (Id < 0 || !Ctx) return;
	if (ACesiumCameraManager* M = ACesiumCameraManager::GetDefaultCameraManager(Ctx)) M->RemoveCamera(Id);
#endif
}
