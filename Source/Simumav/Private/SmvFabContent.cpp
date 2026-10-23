#include "SmvFabContent.h"
#include "SimumavModule.h"
#include "SmvI18n.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"

namespace
{
	const TCHAR* GSmvFabSection = TEXT("/Script/Simumav.SmvFabContent");
	const TCHAR* GSmvFabDroneDir = TEXT("/Game/Fab/Drone");
}

float FSmvFabContent::GetFloat(const TCHAR* Key, float Default)
{
	float V = Default;
	if (GConfig) GConfig->GetFloat(GSmvFabSection, Key, V, GGameIni);
	return V;
}

const TArray<FSmvFabMap>& FSmvFabContent::Maps()
{
	static TArray<FSmvFabMap> Out;
	static bool bRead = false;
	if (!bRead)
	{
		bRead = true;
		TArray<FString> Lines;
		if (GConfig) GConfig->GetArray(GSmvFabSection, TEXT("Maps"), Lines, GGameIni);
		for (const FString& Line : Lines)
		{
			FString Key, Level;
			if (!Line.Split(TEXT("|"), &Key, &Level)) continue;
			Key.TrimStartAndEndInline();
			Level.TrimStartAndEndInline();
			if (Key.IsEmpty() || Level.IsEmpty()) continue;
			if (!FPackageName::DoesPackageExist(Level))
			{
				UE_LOG(LogSimumav, Log, TEXT("Fab haritası pakette yok, atlandı: %s (%s)"), *Key, *Level);
				continue;
			}
			FSmvFabMap M;
			M.Key = Key;
			M.Level = Level;
			Out.Add(M);
			UE_LOG(LogSimumav, Log, TEXT("Fab haritası: %s → %s"), *Key, *Level);
		}
	}
	// bilinen paketlerin görünen adı dil dosyasından; diğerleri paket anahtarıyla
	for (FSmvFabMap& M : Out)
	{
		const FString I18nKey = M.Key.Contains(TEXT("ruin")) ? FString(TEXT("fabmap.ruined"))
			: (M.Key.Contains(TEXT("brushify")) || M.Key.Contains(TEXT("forest"))) ? FString(TEXT("fabmap.forest"))
			: FString(TEXT("fabmap.")) + M.Key;
		const FText T = FSmvI18n::T(I18nKey);
		M.Name = T.ToString() == I18nKey ? FText::FromString(M.Key) : T;
	}
	return Out;
}

const FSmvFabMap* FSmvFabContent::FindMap(const FString& WorldMode)
{
	if (!IsFabWorld(WorldMode)) return nullptr;
	const FString Key = WorldMode.Mid(4);
	for (const FSmvFabMap& M : Maps()) if (M.Key == Key) return &M;
	return nullptr;
}

FSmvTrack FSmvFabContent::MakeTrack(const FSmvFabMap& Map)
{
	FSmvTrack T;
	T.Id = FString(TEXT("fab:")) + Map.Key;   // sanal: sunucuya/yerel dosyaya yazılmaz (yalnız yerel SITL ile uçulur)
	T.Name = Map.Name.ToString();
	T.Lat = 39.9334; T.Lon = 32.8597;   // SITL ev konumu; harita merkezi bu noktaya oturtulur (ASmvGameMode::HandleFabLevelShown)
	T.GroundMsl = 0;
	T.RadiusM = 500;
	T.WorldMode = FString(TEXT("fab:")) + Map.Key;
	T.WindMs = 2; T.WindDir = 270;
	T.World.bRunway = false;
	T.World.Fog = 0.1;
	return T;
}

UObject* FSmvFabContent::LoadMeshIn(const FString& Dir)
{
	// kök kümesinde tutulur (statik güçlü işaretçi süreç kapanışında GC'den sonra yıkılırdı)
	static TMap<FString, UObject*> Cache;
	if (UObject** Found = Cache.Find(Dir)) return *Found;
	UObject* Out = nullptr;
	TArray<FAssetData> Assets;
	IAssetRegistry::GetChecked().GetAssetsByPath(FName(*Dir), Assets, true);
	// önce iskeletli ağ (pervane kemikleri), yoksa statik ağ
	for (int32 Pass = 0; Pass < 2 && !Out; ++Pass)
	{
		const FTopLevelAssetPath Want = Pass == 0 ? USkeletalMesh::StaticClass()->GetClassPathName() : UStaticMesh::StaticClass()->GetClassPathName();
		for (const FAssetData& A : Assets)
		{
			if (A.AssetClassPath != Want) continue;
			Out = A.GetAsset();
			if (Out) break;
		}
	}
	if (Out)
	{
		Out->AddToRoot();
		UE_LOG(LogSimumav, Log, TEXT("Fab ağı: %s"), *Out->GetPathName());
	}
	else
	{
		UE_LOG(LogSimumav, Log, TEXT("Fab ağı yok (%s)"), *Dir);
	}
	Cache.Add(Dir, Out);
	return Out;
}

USkeletalMesh* FSmvFabContent::LoadDroneMesh()
{
	return Cast<USkeletalMesh>(LoadMeshIn(GSmvFabDroneDir));
}

const TArray<UStaticMesh*>& FSmvFabContent::Props()
{
	static TArray<UStaticMesh*> Out;
	static bool bRead = false;
	if (bRead) return Out;
	bRead = true;
	TArray<FAssetData> Assets;
	IAssetRegistry::GetChecked().GetAssetsByPath(FName(TEXT("/Game/Fab/Props")), Assets, true);
	// her alt klasörden (ürün) en büyük statik ağ değil, ilk statik ağ — fab_import her ürünü tek ağ olarak birleştirir
	TSet<FName> Seen;
	for (const FAssetData& A : Assets)
	{
		if (A.AssetClassPath != UStaticMesh::StaticClass()->GetClassPathName() || Seen.Contains(A.PackagePath)) continue;
		if (UStaticMesh* M = Cast<UStaticMesh>(A.GetAsset()))
		{
			M->AddToRoot();
			Out.Add(M);
			Seen.Add(A.PackagePath);
			UE_LOG(LogSimumav, Log, TEXT("Fab sahne dekoru: %s"), *M->GetPathName());
		}
	}
	return Out;
}

// ---------------------------------------------------------------------------
// Ultra Dynamic Sky (Fab) — gök/ışık/hava
// ---------------------------------------------------------------------------

namespace
{
	FString SmvNorm(const FString& S)
	{
		FString O;
		for (TCHAR C : S) if (FChar::IsAlnum(C)) O.AppendChar(FChar::ToLower(C));
		return O;
	}

	/** Blueprint değişkenini adla (boşluk/alt çizgi/büyük-küçük harf duyarsız) bulur. */
	FProperty* SmvFindProp(UObject* Obj, const TCHAR* Name)
	{
		if (!Obj) return nullptr;
		const FString Want = SmvNorm(Name);
		for (TFieldIterator<FProperty> It(Obj->GetClass()); It; ++It)
		{
			if (SmvNorm(It->GetName()) == Want || SmvNorm(It->GetAuthoredName()) == Want) return *It;
		}
		return nullptr;
	}

	bool SmvSetNum(UObject* Obj, const TCHAR* Name, double V)
	{
		FProperty* P = SmvFindProp(Obj, Name);
		if (!P) return false;
		void* Ptr = P->ContainerPtrToValuePtr<void>(Obj);
		if (FDoubleProperty* D = CastField<FDoubleProperty>(P)) { D->SetPropertyValue(Ptr, V); return true; }
		if (FFloatProperty* F = CastField<FFloatProperty>(P)) { F->SetPropertyValue(Ptr, (float)V); return true; }
		if (FIntProperty* I = CastField<FIntProperty>(P)) { I->SetPropertyValue(Ptr, (int32)V); return true; }
		return false;
	}

	bool SmvSetBool(UObject* Obj, const TCHAR* Name, bool V)
	{
		FBoolProperty* B = CastField<FBoolProperty>(SmvFindProp(Obj, Name));
		if (!B) return false;
		B->SetPropertyValue(B->ContainerPtrToValuePtr<void>(Obj), V);
		return true;
	}

	bool SmvSetObj(UObject* Obj, const TCHAR* Name, UObject* V)
	{
		FObjectPropertyBase* O = CastField<FObjectPropertyBase>(SmvFindProp(Obj, Name));
		if (!O || !V || !V->IsA(O->PropertyClass)) return false;
		O->SetObjectPropertyValue(O->ContainerPtrToValuePtr<void>(Obj), V);
		return true;
	}

	/** /Game/UltraDynamicSky altında adı verilen Blueprint sınıfı (ayar ile değiştirilebilir). */
	UClass* SmvUdsClass(const TCHAR* IniKey, const TCHAR* AssetName)
	{
		FString Path;
		if (GConfig && GConfig->GetString(GSmvFabSection, IniKey, Path, GGameIni) && !Path.IsEmpty())
		{
			if (UClass* C = LoadClass<AActor>(nullptr, *Path)) return C;
		}
		TArray<FAssetData> Assets;
		IAssetRegistry::GetChecked().GetAssetsByPath(FName(TEXT("/Game/UltraDynamicSky")), Assets, true);
		for (const FAssetData& A : Assets)
		{
			if (A.AssetName.ToString() != AssetName) continue;
			const FString ClassPath = FString::Printf(TEXT("%s.%s_C"), *A.PackageName.ToString(), AssetName);
			if (UClass* C = LoadClass<AActor>(nullptr, *ClassPath)) return C;
		}
		return nullptr;
	}

	/** Hava ön ayarı (UDS_Weather_Settings veri varlığı) — ad adaylarına göre. */
	UObject* SmvUdsWeatherPreset(const TArray<FString>& Names)
	{
		TArray<FAssetData> Assets;
		IAssetRegistry::GetChecked().GetAssetsByPath(FName(TEXT("/Game/UltraDynamicSky")), Assets, true);
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			for (const FString& Want : Names)
			{
				for (const FAssetData& A : Assets)
				{
					if (!A.AssetClassPath.GetAssetName().ToString().Contains(TEXT("Weather"))) continue;
					const FString N = SmvNorm(A.AssetName.ToString());
					if (Pass == 0 ? N == Want : N.Contains(Want)) return A.GetAsset();
				}
			}
		}
		return nullptr;
	}
}

bool FSmvFabContent::SpawnSky(UWorld* W, const FSmvTrack& Track, TArray<AActor*>& OutActors)
{
	if (!W) return false;
	bool bOn = true;
	if (GConfig) GConfig->GetBool(GSmvFabSection, TEXT("UseUltraDynamicSky"), bOn, GGameIni);
	if (!bOn) return false;
	UClass* SkyCls = SmvUdsClass(TEXT("UdsSkyClass"), TEXT("Ultra_Dynamic_Sky"));
	if (!SkyCls) return false;
	UClass* WeatherCls = SmvUdsClass(TEXT("UdsWeatherClass"), TEXT("Ultra_Dynamic_Weather"));

	// saat: parkur gündüz/alacakaranlık/gece → UDS "Time of Day" (0–2400)
	const double Tod = Track.TimeOfDay == TEXT("night") ? 2300.0 : Track.TimeOfDay == TEXT("dusk") ? 1845.0 : 1300.0;
	const FString& Wx = Track.Weather;
	const double Clouds = Wx == TEXT("clear") ? 1.5 : Wx == TEXT("cloudy") ? 6.0 : (Wx == TEXT("rain") || Wx == TEXT("snow")) ? 8.5 : Wx == TEXT("fog") ? 5.0 : 3.0;
	const double Fog = Wx == TEXT("fog") ? 8.0 : FMath::Clamp(Track.World.Fog * 10.0, 0.0, 10.0);

	FTransform Tr = FTransform::Identity;
	AActor* Sky = W->SpawnActorDeferred<AActor>(SkyCls, Tr, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Sky) return false;
	FString Set;
	if (SmvSetNum(Sky, TEXT("Time of Day"), Tod)) Set += TEXT(" TimeOfDay");
	if (SmvSetBool(Sky, TEXT("Animate Time of Day"), false)) Set += TEXT(" AnimateTimeOfDay");
	if (SmvSetNum(Sky, TEXT("Cloud Coverage"), Clouds)) Set += TEXT(" CloudCoverage");
	if (SmvSetNum(Sky, TEXT("Fog"), Fog)) Set += TEXT(" Fog");
	Sky->FinishSpawning(Tr);
	OutActors.Add(Sky);

	if (WeatherCls)
	{
		AActor* Weather = W->SpawnActorDeferred<AActor>(WeatherCls, Tr, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Weather)
		{
			TArray<FString> Names;
			if (Wx == TEXT("rain")) Names = { TEXT("rain"), TEXT("lightrain") };
			else if (Wx == TEXT("snow")) Names = { TEXT("snow"), TEXT("lightsnow") };
			else if (Wx == TEXT("fog")) Names = { TEXT("foggy"), TEXT("fog") };
			else if (Wx == TEXT("cloudy")) Names = { TEXT("cloudy"), TEXT("overcast"), TEXT("partlycloudy") };
			else Names = { TEXT("clearskies"), TEXT("clear"), TEXT("partlycloudy") };
			if (UObject* Preset = SmvUdsWeatherPreset(Names))
			{
				if (SmvSetObj(Weather, TEXT("Weather"), Preset)) Set += FString(TEXT(" Weather=")) + Preset->GetName();
			}
			if (SmvSetBool(Weather, TEXT("Random Weather Variation"), false)) Set += TEXT(" RandomWeatherOff");
			Weather->FinishSpawning(Tr);
			OutActors.Add(Weather);
		}
	}
	UE_LOG(LogSimumav, Log, TEXT("Ultra Dynamic Sky kuruldu (%s; hava aktörü %s); ayarlanan:%s"), *SkyCls->GetPathName(), WeatherCls ? TEXT("var") : TEXT("yok"), *Set);
	return true;
}
