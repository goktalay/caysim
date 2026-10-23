#include "SmvAtmosphere.h"
#include "SimumavModule.h"
#include "SmvSolar.h"
#include "SmvWorldActors.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Engine/Engine.h"
#include "UObject/EnumProperty.h"
#include "UObject/UnrealType.h"

namespace
{
	const TCHAR* GSec = TEXT("/Script/Simumav.SmvFabContent");
	const FName GTag(TEXT("SmvAtmosphere"));

	FString Norm(const FString& S)
	{
		FString O;
		for (TCHAR C : S) if (FChar::IsAlnum(C)) O.AppendChar(FChar::ToLower(C));
		return O;
	}

	FProperty* FindProp(UObject* Obj, const TCHAR* Name)
	{
		if (!Obj) return nullptr;
		const FString Want = Norm(Name);
		for (TFieldIterator<FProperty> It(Obj->GetClass()); It; ++It)
			if (Norm(It->GetName()) == Want || Norm(It->GetAuthoredName()) == Want) return *It;
		return nullptr;
	}

	/** Değişkeni adaylardan ilk bulunanla ayarlar; kullanılan adı Set'e, bulunamayanları Miss'e yazar. */
	struct FSetter
	{
		UObject* Obj = nullptr;
		FString Set, Miss;

		bool Num(std::initializer_list<const TCHAR*> Names, double V)
		{
			for (const TCHAR* N : Names)
			{
				FProperty* P = FindProp(Obj, N);
				if (!P) continue;
				void* Ptr = P->ContainerPtrToValuePtr<void>(Obj);
				if (FDoubleProperty* D = CastField<FDoubleProperty>(P)) D->SetPropertyValue(Ptr, V);
				else if (FFloatProperty* F = CastField<FFloatProperty>(P)) F->SetPropertyValue(Ptr, (float)V);
				else if (FIntProperty* I = CastField<FIntProperty>(P)) I->SetPropertyValue(Ptr, (int32)FMath::RoundToInt(V));
				else if (FByteProperty* B = CastField<FByteProperty>(P); B && !B->Enum) B->SetPropertyValue(Ptr, (uint8)FMath::Clamp(FMath::RoundToInt(V), 0, 255));
				else continue;
				Set += FString::Printf(TEXT(" %s=%.3g"), N, V);
				return true;
			}
			Miss += FString::Printf(TEXT(" %s"), *Names.begin());
			return false;
		}

		bool Bool(std::initializer_list<const TCHAR*> Names, bool V)
		{
			for (const TCHAR* N : Names)
			{
				if (FBoolProperty* B = CastField<FBoolProperty>(FindProp(Obj, N)))
				{
					B->SetPropertyValue(B->ContainerPtrToValuePtr<void>(Obj), V);
					Set += FString::Printf(TEXT(" %s=%s"), N, V ? TEXT("1") : TEXT("0"));
					return true;
				}
			}
			Miss += FString::Printf(TEXT(" %s"), *Names.begin());
			return false;
		}

		/** Enum değişkeni: görünen adında Contains geçen ilk değer. */
		bool EnumByName(std::initializer_list<const TCHAR*> Names, std::initializer_list<const TCHAR*> Wants)
		{
			for (const TCHAR* N : Names)
			{
				FProperty* P = FindProp(Obj, N);
				if (!P) continue;
				UEnum* E = nullptr;
				FNumericProperty* Under = nullptr;
				if (FEnumProperty* Ep = CastField<FEnumProperty>(P)) { E = Ep->GetEnum(); Under = Ep->GetUnderlyingProperty(); }
				else if (FByteProperty* Bp = CastField<FByteProperty>(P)) { E = Bp->Enum; Under = Bp; }
				if (!E || !Under) continue;
				for (const TCHAR* W : Wants)
				{
					const FString Wn = Norm(W);
					for (int32 i = 0; i < E->NumEnums() - 1; ++i)
					{
						if (Norm(E->GetDisplayNameTextByIndex(i).ToString()).Contains(Wn) || Norm(E->GetNameStringByIndex(i)).Contains(Wn))
						{
							Under->SetIntPropertyValue(P->ContainerPtrToValuePtr<void>(Obj), E->GetValueByIndex(i));
							Set += FString::Printf(TEXT(" %s=%s"), N, *E->GetDisplayNameTextByIndex(i).ToString());
							return true;
						}
					}
				}
			}
			Miss += FString::Printf(TEXT(" %s"), *Names.begin());
			return false;
		}

		bool Obj_(std::initializer_list<const TCHAR*> Names, UObject* V)
		{
			for (const TCHAR* N : Names)
			{
				FObjectPropertyBase* O = CastField<FObjectPropertyBase>(FindProp(Obj, N));
				if (!O || !V || !V->IsA(O->PropertyClass)) continue;
				O->SetObjectPropertyValue(O->ContainerPtrToValuePtr<void>(Obj), V);
				Set += FString::Printf(TEXT(" %s=%s"), N, *V->GetName());
				return true;
			}
			Miss += FString::Printf(TEXT(" %s"), *Names.begin());
			return false;
		}

		bool Color(std::initializer_list<const TCHAR*> Names, const FLinearColor& C)
		{
			for (const TCHAR* N : Names)
			{
				FStructProperty* S = CastField<FStructProperty>(FindProp(Obj, N));
				if (!S) continue;
				if (S->Struct == TBaseStructure<FLinearColor>::Get()) { *S->ContainerPtrToValuePtr<FLinearColor>(Obj) = C; Set += FString::Printf(TEXT(" %s"), N); return true; }
			}
			Miss += FString::Printf(TEXT(" %s"), *Names.begin());
			return false;
		}
	};

	UClass* UdsClass(const TCHAR* IniKey, const TCHAR* AssetName)
	{
		FString Path;
		if (GConfig && GConfig->GetString(GSec, IniKey, Path, GGameIni) && !Path.IsEmpty())
			if (UClass* C = LoadClass<AActor>(nullptr, *Path)) return C;
		TArray<FAssetData> Assets;
		IAssetRegistry::GetChecked().GetAssetsByPath(FName(TEXT("/Game/UltraDynamicSky")), Assets, true);
		for (const FAssetData& A : Assets)
		{
			if (A.AssetName.ToString() != AssetName) continue;
			if (UClass* C = LoadClass<AActor>(nullptr, *FString::Printf(TEXT("%s.%s_C"), *A.PackageName.ToString(), AssetName))) return C;
		}
		return nullptr;
	}

	UObject* WeatherPreset(const TArray<FString>& Names)
	{
		TArray<FAssetData> Assets;
		IAssetRegistry::GetChecked().GetAssetsByPath(FName(TEXT("/Game/UltraDynamicSky")), Assets, true);
		for (int32 Pass = 0; Pass < 2; ++Pass)
			for (const FString& Want : Names)
				for (const FAssetData& A : Assets)
				{
					if (!A.AssetClassPath.GetAssetName().ToString().Contains(TEXT("Weather"))) continue;
					const FString N = Norm(A.AssetName.ToString());
					if (Pass == 0 ? N == Want : N.Contains(Want)) return A.GetAsset();
				}
		return nullptr;
	}

	void SetCVar(const TCHAR* Name, float V)
	{
		if (IConsoleVariable* C = IConsoleManager::Get().FindConsoleVariable(Name)) C->Set(V, ECVF_SetByGameSetting);
	}

	/** Hava → (bulut 0..10, sis 0..10, toz, ıslaklık 0..1, kar 0..1, yağmur 0..10, kar yağışı 0..10, şimşek 0..10, yükseklik sisi yoğunluğu) */
	struct FWx { double Cloud, Fog, Dust, Wet, SnowCover, Rain, Snow, Lightning, HeightFog; };
	FWx WeatherOf(const FString& W, double Fog01)
	{
		if (W == TEXT("cloudy")) return { 6.5, 1.5, 0.5, 0.0, 0.0, 0, 0, 0, 0.006 };
		if (W == TEXT("rain")) return { 9.0, 2.5, 0.0, 0.9, 0.0, 6, 0, 2.5, 0.02 };
		if (W == TEXT("fog")) return { 5.0, 8.0, 0.0, 0.35, 0.0, 0, 0, 0, 0.06 };
		if (W == TEXT("snow")) return { 8.5, 3.0, 0.0, 0.2, 0.85, 0, 6, 0, 0.025 };
		return { 2.0, FMath::Clamp(Fog01 * 10.0, 0.3, 4.0), 0.8, 0.0, 0.0, 0, 0, 0, 0.002 + Fog01 * 0.01 };
	}

	struct FWhen { int32 Y, M, D; double LocalH, Tz, UtcH; };
	FWhen WhenOf(const FSmvAtmosphereCfg& C)
	{
		const FDateTime Now = FDateTime::UtcNow();
		FWhen W;
		W.Y = Now.GetYear(); W.M = Now.GetMonth(); W.D = Now.GetDay();
		W.Tz = SmvCore::ApproxTimeZone(C.Lat, C.Lon);
		W.LocalH = SmvCore::TimeOfDayHours(TCHAR_TO_ANSI(*C.TimeOfDay), C.Lat, C.Lon, W.Y, W.M, W.D, W.Tz);
		W.UtcH = W.LocalH - W.Tz;
		return W;
	}

	APostProcessVolume* FindPp(UWorld* W)
	{
		for (TActorIterator<APostProcessVolume> It(W); It; ++It) if (It->Tags.Contains(GTag)) return *It;
		return nullptr;
	}

	void ConfigurePp(APostProcessVolume* V, int32 Q, const FString& Tod)
	{
		if (!V) return;
		FPostProcessSettings& S = V->Settings;
		// otomatik pozlama (histogram, EV100 — DefaultEngine.ini ExtendDefaultLuminanceRange): gece biraz aydınlatılır (oynanış)
		S.bOverride_AutoExposureMethod = true; S.AutoExposureMethod = EAutoExposureMethod::AEM_Histogram;
		S.bOverride_AutoExposureMinBrightness = true; S.AutoExposureMinBrightness = Tod == TEXT("night") ? -3.f : -1.f;
		S.bOverride_AutoExposureMaxBrightness = true; S.AutoExposureMaxBrightness = 16.f;
		S.bOverride_AutoExposureSpeedUp = true; S.AutoExposureSpeedUp = 2.5f;
		S.bOverride_AutoExposureSpeedDown = true; S.AutoExposureSpeedDown = 1.2f;
		S.bOverride_AutoExposureBias = true; S.AutoExposureBias = Tod == TEXT("night") ? 1.2f : Tod == TEXT("dusk") ? 0.3f : 0.f;
		// hafif renk düzeltme / sinema dokunuşu
		S.bOverride_ColorSaturation = true; S.ColorSaturation = FVector4(1.06, 1.06, 1.06, 1.0);
		S.bOverride_ColorContrast = true; S.ColorContrast = FVector4(1.04, 1.04, 1.04, 1.0);
		S.bOverride_BloomIntensity = true; S.BloomIntensity = 0.45f;
		S.bOverride_VignetteIntensity = true; S.VignetteIntensity = 0.22f;
		S.bOverride_SceneFringeIntensity = true; S.SceneFringeIntensity = 0.f;
		S.bOverride_MotionBlurAmount = true; S.MotionBlurAmount = Q >= 4 ? 0.25f : 0.f;
		S.bOverride_LensFlareIntensity = true; S.LensFlareIntensity = Q >= 2 ? 0.15f : 0.f;
		S.bOverride_AmbientOcclusionIntensity = true; S.AmbientOcclusionIntensity = 0.5f;
		// küresel aydınlatma + yansıma: Düşük → Lumen kapalı (ekran uzayı), diğerleri Lumen (kalite ön ayara göre)
		static const float Lq[5] = { 0.5f, 0.5f, 1.f, 1.5f, 2.f };
		S.bOverride_DynamicGlobalIlluminationMethod = true;
		S.DynamicGlobalIlluminationMethod = Q <= 0 ? EDynamicGlobalIlluminationMethod::ScreenSpace : EDynamicGlobalIlluminationMethod::Lumen;
		S.bOverride_ReflectionMethod = true;
		S.ReflectionMethod = Q <= 1 ? EReflectionMethod::ScreenSpace : EReflectionMethod::Lumen;
		S.bOverride_LumenSceneLightingQuality = true; S.LumenSceneLightingQuality = Lq[FMath::Clamp(Q, 0, 4)];
		S.bOverride_LumenFinalGatherQuality = true; S.LumenFinalGatherQuality = Lq[FMath::Clamp(Q, 0, 4)];
		S.bOverride_LumenReflectionQuality = true; S.LumenReflectionQuality = Lq[FMath::Clamp(Q, 0, 4)];
	}

	void ApplyCVars(int32 Q)
	{
		Q = FMath::Clamp(Q, 0, 4);
		static const float Grid[5] = { 16, 16, 12, 8, 6 };
		static const float GridZ[5] = { 64, 64, 96, 128, 160 };
		static const float CloudRay[5] = { 256, 384, 768, 1024, 1536 };
		SetCVar(TEXT("r.VolumetricFog"), Q >= 1 ? 1.f : 0.f);
		SetCVar(TEXT("r.VolumetricFog.GridPixelSize"), Grid[Q]);
		SetCVar(TEXT("r.VolumetricFog.GridSizeZ"), GridZ[Q]);
		SetCVar(TEXT("r.VolumetricCloud.ViewRaySampleMaxCount"), CloudRay[Q]);
		SetCVar(TEXT("r.VolumetricCloud.ReflectionRaySampleMaxCount"), Q >= 3 ? 80.f : 40.f);
		SetCVar(TEXT("r.SkyAtmosphere.FastSkyLUT"), Q >= 3 ? 0.f : 1.f);
	}

	/** UDS / UDW kalite değişkenleri (adlar paket sürümüne göre değişebilir; bulunamayan atlanır). */
	void UdsQuality(FSetter& S, int32 Q)
	{
		S.EnumByName({ TEXT("Sky Mode") }, Q >= 1 ? std::initializer_list<const TCHAR*>{ TEXT("Volumetric") } : std::initializer_list<const TCHAR*>{ TEXT("2D Dynamic"), TEXT("Static") });
		S.Num({ TEXT("Volumetric Cloud Quality"), TEXT("Cloud Quality"), TEXT("Overall Quality") }, Q >= 3 ? 3 : Q >= 2 ? 2 : 1);
	}
}

FSmvAtmosphereCfg FSmvAtmosphereCfg::FromTrack(const FSmvTrack& T, bool bCesium)
{
	FSmvAtmosphereCfg C;
	C.Lat = T.Lat; C.Lon = T.Lon;
	C.TimeOfDay = T.TimeOfDay.IsEmpty() ? FString(TEXT("day")) : T.TimeOfDay;
	C.Weather = T.Weather.IsEmpty() ? FString(TEXT("clear")) : T.Weather;
	C.WindMs = T.WindMs; C.WindDirDeg = T.WindDir; C.WindGustMs = T.WindGust;
	C.Fog01 = T.World.Fog;
	C.bCesium = bCesium;
	return C;
}

int32 FSmvAtmosphere::CurrentQuality()
{
	if (UGameUserSettings* G = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		const int32 O = G->GetOverallScalabilityLevel();
		return FMath::Clamp(O >= 0 ? O : G->GetShadingQuality(), 0, 4);
	}
	return 3;
}

void FSmvAtmosphere::ApplyQuality(UWorld* W, int32 Q)
{
	ApplyCVars(Q);
	if (!W) return;
	if (APostProcessVolume* V = FindPp(W))
	{
		FString Tod = TEXT("day");
		for (const FName& T : V->Tags) { const FString S = T.ToString(); if (S.StartsWith(TEXT("tod:"))) Tod = S.Mid(4); }
		ConfigurePp(V, Q, Tod);
	}
	for (TActorIterator<AActor> It(W); It; ++It)
	{
		if (!It->Tags.Contains(GTag) || It->IsA<APostProcessVolume>()) continue;
		if (!It->GetClass()->GetName().Contains(TEXT("Ultra_Dynamic_Sky"))) continue;
		FSetter S; S.Obj = *It;
		UdsQuality(S, Q);
		UE_LOG(LogSimumav, Log, TEXT("Atmosfer kalite %d: UDS%s"), Q, *S.Set);
	}
}

bool FSmvAtmosphere::Apply(UWorld* W, const FSmvAtmosphereCfg& Cfg, TArray<AActor*>& OutActors)
{
	if (!W) return false;
	const int32 Q = Cfg.Quality >= 0 ? FMath::Clamp(Cfg.Quality, 0, 4) : CurrentQuality();
	ApplyCVars(Q);
	const FWx X = WeatherOf(Cfg.Weather, Cfg.Fog01);
	const FWhen T = WhenOf(Cfg);
	// rüzgâr: SITL SIM_WIND_DIR rüzgârın estiği yön (kuzeyden) → bulutların gittiği yön; Unreal: X doğu, Y güney → yaw = pusula − 90
	const double TravelCompass = FMath::Fmod(Cfg.WindDirDeg + 180.0, 360.0);
	const double TravelYaw = FMath::Fmod(TravelCompass - 90.0 + 360.0, 360.0);
	const double WindAloft = Cfg.WindMs * 1.6 + Cfg.WindGustMs * 0.5;   // bulut seviyesinde daha hızlı

	// ---- sınırsız post-process hacmi ----
	FActorSpawnParameters Sp;
	Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (APostProcessVolume* V = W->SpawnActor<APostProcessVolume>(APostProcessVolume::StaticClass(), FTransform::Identity, Sp))
	{
		V->bUnbound = true;
		V->Priority = 1.f;
		V->Tags.Add(GTag);
		V->Tags.Add(FName(*(FString(TEXT("tod:")) + Cfg.TimeOfDay)));
		ConfigurePp(V, Q, Cfg.TimeOfDay);
		OutActors.Add(V);
	}
	// ---- prosedürel zemin ıslaklığı / kar ----
	for (TActorIterator<ASmvProcTerrain> It(W); It; ++It) It->SetSurfaceWeather((float)X.Wet, (float)X.SnowCover);

	bool bOn = true;
	if (GConfig) GConfig->GetBool(GSec, TEXT("UseUltraDynamicSky"), bOn, GGameIni);
	UClass* SkyCls = bOn ? UdsClass(TEXT("UdsSkyClass"), TEXT("Ultra_Dynamic_Sky")) : nullptr;
	if (!SkyCls)
	{
		// ---- yerleşik gök: gerçek güneş yönü, yükseklik sisi + hacimsel sis, gök ışığı ----
		const SmvCore::FSolarPos Sun = SmvCore::SolarPosition(Cfg.Lat, Cfg.Lon, T.Y, T.M, T.D, T.UtcH);
		const double El = FMath::DegreesToRadians(Sun.ElevationDeg), Az = FMath::DegreesToRadians(Sun.AzimuthDeg);
		const FVector ToSun(FMath::Sin(Az) * FMath::Cos(El), -FMath::Cos(Az) * FMath::Cos(El), FMath::Sin(El));
		FString Done;
		for (TActorIterator<ADirectionalLight> It(W); It; ++It)
		{
			It->SetActorRotation((-ToSun).Rotation());
			if (UDirectionalLightComponent* L = Cast<UDirectionalLightComponent>(It->GetLightComponent()))
			{
				const float Day = FMath::Clamp((float)FMath::Sin(El) * 3.f, 0.f, 1.f);
				L->SetIntensity(FMath::Lerp(0.08f, 10.f, Day) * (X.Cloud > 7 ? 0.45f : 1.f));
				L->SetAtmosphereSunLight(true);
				L->SetLightColor(FLinearColor::White);
			}
			Done += TEXT(" güneş");
			break;
		}
		for (TActorIterator<AExponentialHeightFog> It(W); It; ++It)
		{
			if (UExponentialHeightFogComponent* F = It->GetComponent())
			{
				F->SetFogDensity((float)X.HeightFog);
				F->SetFogHeightFalloff(0.15f);
				F->SetStartDistance(0.f);
				F->SetVolumetricFog(Q >= 1);
				F->SetVolumetricFogScatteringDistribution(0.6f);
				F->SetVolumetricFogExtinctionScale(Cfg.Weather == TEXT("fog") ? 3.f : 1.f);
			}
			Done += TEXT(" sis");
			break;
		}
		for (TActorIterator<ASkyLight> It(W); It; ++It)
		{
			if (USkyLightComponent* S = It->GetLightComponent()) { S->SetRealTimeCaptureEnabled(true); S->SetIntensity(Cfg.TimeOfDay == TEXT("night") ? 0.4f : 1.f); }
			Done += TEXT(" gökışığı");
			break;
		}
		// Cesium güneş-gök: tarih / saat dilimi / yerel saat
		for (TActorIterator<AActor> It(W); It; ++It)
		{
			if (!It->GetClass()->GetName().Contains(TEXT("CesiumSunSky"))) continue;
			FSetter S; S.Obj = *It;
			S.Num({ TEXT("SolarTime") }, T.LocalH);
			S.Num({ TEXT("Day") }, T.D); S.Num({ TEXT("Month") }, T.M); S.Num({ TEXT("Year") }, T.Y);
			S.Num({ TEXT("TimeZone") }, T.Tz);
			S.Bool({ TEXT("UseDaylightSavingTime") }, false);
			if (UFunction* Fn = It->FindFunction(TEXT("UpdateSun"))) It->ProcessEvent(Fn, nullptr);
			Done += TEXT(" CesiumSunSky:") + S.Set;
			break;
		}
		UE_LOG(LogSimumav, Log, TEXT("Atmosfer (yerleşik gök; UDS yok): yerel %.2f sa (UTC%+.0f), güneş yükseklik %.1f° yön %.0f°, hava %s, sis %.4f, kalite %d —%s"),
			T.LocalH, T.Tz, Sun.ElevationDeg, Sun.AzimuthDeg, *Cfg.Weather, X.HeightFog, Q, *Done);
		return false;
	}

	// ---- Ultra Dynamic Sky ----
	const FTransform Tr = FTransform::Identity;
	AActor* Sky = W->SpawnActorDeferred<AActor>(SkyCls, Tr, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Sky) return false;
	FSetter S; S.Obj = Sky;
	S.Num({ TEXT("Time of Day") }, T.LocalH * 100.0);
	S.Bool({ TEXT("Animate Time of Day") }, false);
	S.Bool({ TEXT("Simulate Real Sun") }, true);
	S.Bool({ TEXT("Simulate Real Moon") }, true);
	S.Bool({ TEXT("Simulate Real Stars") }, true);
	S.Num({ TEXT("Latitude") }, Cfg.Lat);
	S.Num({ TEXT("Longitude") }, Cfg.Lon);
	S.Num({ TEXT("Time Zone") }, T.Tz);
	S.Bool({ TEXT("Daylight Savings Time"), TEXT("Daylight Saving Time"), TEXT("DST") }, false);
	S.Num({ TEXT("Year") }, T.Y); S.Num({ TEXT("Month") }, T.M); S.Num({ TEXT("Day") }, T.D);
	S.Num({ TEXT("Cloud Coverage") }, X.Cloud);
	S.Num({ TEXT("Fog") }, X.Fog);
	S.Num({ TEXT("Dust Amount"), TEXT("Dust") }, X.Dust);
	S.Num({ TEXT("Cloud Direction") }, TravelYaw);
	S.Num({ TEXT("Cloud Speed") }, FMath::Clamp(WindAloft * 0.5, 0.3, 10.0));
	S.Num({ TEXT("Light Pollution") }, 0.25);
	S.Bool({ TEXT("Volumetric Fog") }, Q >= 1);
	UdsQuality(S, Q);
	Sky->Tags.Add(GTag);
	Sky->FinishSpawning(Tr);
	OutActors.Add(Sky);

	UClass* WeatherCls = UdsClass(TEXT("UdsWeatherClass"), TEXT("Ultra_Dynamic_Weather"));
	FSetter Wx;
	if (WeatherCls)
	{
		AActor* Weather = W->SpawnActorDeferred<AActor>(WeatherCls, Tr, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Weather)
		{
			Wx.Obj = Weather;
			TArray<FString> Names;
			if (Cfg.Weather == TEXT("rain")) Names = { TEXT("rain"), TEXT("rainlight"), TEXT("thunderstorm") };
			else if (Cfg.Weather == TEXT("snow")) Names = { TEXT("snow"), TEXT("snowlight") };
			else if (Cfg.Weather == TEXT("fog")) Names = { TEXT("foggy"), TEXT("fog") };
			else if (Cfg.Weather == TEXT("cloudy")) Names = { TEXT("cloudy"), TEXT("overcast"), TEXT("partlycloudy") };
			else Names = { TEXT("clearskies"), TEXT("clear"), TEXT("partlycloudy") };
			if (UObject* Preset = WeatherPreset(Names)) Wx.Obj_({ TEXT("Weather") }, Preset);
			if (!Wx.Bool({ TEXT("Random Weather Variation") }, false)) Wx.EnumByName({ TEXT("Random Weather Variation") }, { TEXT("Disabled"), TEXT("Off") });
			auto Values = [&]()
			{
				Wx.Num({ TEXT("Cloud Coverage") }, X.Cloud);
				Wx.Num({ TEXT("Fog") }, X.Fog);
				Wx.Num({ TEXT("Rain") }, X.Rain);
				Wx.Num({ TEXT("Snow") }, X.Snow);
				Wx.Num({ TEXT("Thunder/Lightning"), TEXT("Thunder Lightning"), TEXT("Lightning") }, X.Lightning);
				Wx.Num({ TEXT("Dust") }, X.Dust * 0.5);
				Wx.Num({ TEXT("Wind Intensity") }, FMath::Clamp(Cfg.WindMs / 1.5, 0.0, 10.0));
				Wx.Num({ TEXT("Wind Direction") }, TravelYaw);
				Wx.Num({ TEXT("Material Wetness"), TEXT("Wetness") }, X.Wet * 10.0);
				Wx.Num({ TEXT("Snow Coverage"), TEXT("Material Snow Coverage") }, X.SnowCover * 10.0);
				Wx.Bool({ TEXT("Enable Puddles"), TEXT("Puddles") }, X.Wet > 0.5);
			};
			Values();
			Weather->Tags.Add(GTag);
			Weather->FinishSpawning(Tr);
			// ön ayar BeginPlay'de değerleri yazabilir → rüzgâr/yağış yeniden (SITL ile aynı yön/hız)
			Wx.Set += TEXT(" |");
			Values();
			OutActors.Add(Weather);
		}
	}
	UE_LOG(LogSimumav, Log, TEXT("Atmosfer: Ultra Dynamic Sky (%s; hava aktörü %s), yerel %.2f sa UTC%+.0f %04d-%02d-%02d, enlem %.4f boylam %.4f, hava %s, rüzgâr %.1f m/s %03.0f° (bulut yönü yaw %.0f°), kalite %d"),
		*SkyCls->GetPathName(), WeatherCls ? TEXT("var") : TEXT("yok"), T.LocalH, T.Tz, T.Y, T.M, T.D, Cfg.Lat, Cfg.Lon, *Cfg.Weather, Cfg.WindMs, Cfg.WindDirDeg, TravelYaw, Q);
	UE_LOG(LogSimumav, Log, TEXT("Atmosfer UDS ayarlanan:%s | bulunamayan:%s"), *S.Set, *S.Miss);
	if (WeatherCls) UE_LOG(LogSimumav, Log, TEXT("Atmosfer UDW ayarlanan:%s | bulunamayan:%s"), *Wx.Set, *Wx.Miss);
	return true;
}

FString USmvAtmosphereLibrary::DumpUdsProperties()
{
	FString Out;
	for (const TCHAR* Name : { TEXT("Ultra_Dynamic_Sky"), TEXT("Ultra_Dynamic_Weather") })
	{
		UClass* C = UdsClass(TEXT("__yok__"), Name);
		if (!C) { Out += FString::Printf(TEXT("%s: yok\n"), Name); continue; }
		UObject* Cdo = C->GetDefaultObject();
		int32 N = 0;
		for (TFieldIterator<FProperty> It(C, EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			FProperty* P = *It;
			if (!P->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible)) continue;
			FString Val;
			P->ExportTextItem_Direct(Val, P->ContainerPtrToValuePtr<void>(Cdo), nullptr, nullptr, PPF_None);
			if (Val.Len() > 80) Val = Val.Left(80) + TEXT("…");
			FString Enum;
			if (FEnumProperty* Ep = CastField<FEnumProperty>(P)) Enum = Ep->GetEnum()->GetName();
			else if (FByteProperty* Bp = CastField<FByteProperty>(P); Bp && Bp->Enum) Enum = Bp->Enum->GetName();
			Out += FString::Printf(TEXT("UDSPROP %s | %s | %s%s | %s\n"), Name, *P->GetAuthoredName(), *P->GetCPPType(), Enum.IsEmpty() ? TEXT("") : *(TEXT(" ") + Enum), *Val);
			++N;
		}
		Out += FString::Printf(TEXT("%s: %d özellik (%s)\n"), Name, N, *C->GetPathName());
	}
	TArray<FString> Lines;
	Out.ParseIntoArrayLines(Lines);
	for (const FString& L : Lines) UE_LOG(LogSimumav, Display, TEXT("%s"), *L);
	return Out;
}
