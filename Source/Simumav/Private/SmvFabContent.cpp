#include "SmvFabContent.h"
#include "SimumavModule.h"
#include "SmvI18n.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"

namespace
{
	const TCHAR* GSmvFabSection = TEXT("/Script/Simumav.SmvFabContent");
	const TCHAR* GSmvFabDroneDir = TEXT("/Game/Fab/Drone");
	/** fab_import.py bu adı vermeye çalışır; olmazsa klasördeki ilk SkeletalMesh kullanılır */
	const TCHAR* GSmvFabDronePath = TEXT("/Game/Fab/Drone/SK_FabDrone.SK_FabDrone");
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
		const FString I18nKey = M.Key.Contains(TEXT("ruin")) ? FString(TEXT("fabmap.ruined")) : FString(TEXT("fabmap.")) + M.Key;
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

USkeletalMesh* FSmvFabContent::LoadDroneMesh()
{
	// kök kümesinde tutulur (statik güçlü işaretçi süreç kapanışında GC'den sonra yıkılırdı)
	static USkeletalMesh* Cache = nullptr;
	static bool bTried = false;
	if (bTried) return Cache;
	bTried = true;
	USkeletalMesh* M = nullptr;
	if (FPackageName::DoesPackageExist(FSoftObjectPath(GSmvFabDronePath).GetLongPackageName()))
	{
		M = Cast<USkeletalMesh>(FSoftObjectPath(GSmvFabDronePath).TryLoad());
	}
	if (!M)
	{
		TArray<FAssetData> Assets;
		IAssetRegistry::GetChecked().GetAssetsByPath(FName(GSmvFabDroneDir), Assets, true);
		for (const FAssetData& A : Assets)
		{
			if (A.AssetClassPath == USkeletalMesh::StaticClass()->GetClassPathName())
			{
				M = Cast<USkeletalMesh>(A.GetAsset());
				if (M) break;
			}
		}
	}
	if (M)
	{
		M->AddToRoot();
		Cache = M;
		UE_LOG(LogSimumav, Log, TEXT("Fab dron gövdesi: %s"), *M->GetPathName());
	}
	else
	{
		UE_LOG(LogSimumav, Log, TEXT("Fab dron gövdesi yok (%s) — BasicShapes gövdesi"), GSmvFabDroneDir);
	}
	return M;
}
