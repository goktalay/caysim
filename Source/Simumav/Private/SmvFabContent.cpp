#include "SmvFabContent.h"
#include "SmvMapRegistry.h"
#include "SimumavModule.h"
#include "SmvI18n.h"
#include "SmvAtmosphere.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"
#include "UObject/EnumProperty.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Sound/SoundBase.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"

namespace
{
	const TCHAR* GSmvFabSection = TEXT("/Script/Simumav.SmvFabContent");
	const TCHAR* GSmvFabDroneDir = TEXT("/Game/Fab/Drone");
}

void FSmvFabContent::DumpClassApi(const UClass* Class, const FString& Tag, int32 MaxLines)
{
	static TSet<const UClass*> Done;
	if (!Class || Done.Contains(Class)) return;
	Done.Add(Class);
	int32 N = 0;
	UE_LOG(LogSimumav, Log, TEXT("FABAPI %s sınıf %s (üst %s)"), *Tag, *Class->GetPathName(), Class->GetSuperClass() ? *Class->GetSuperClass()->GetName() : TEXT("-"));
	for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::ExcludeSuper); It && N < MaxLines; ++It, ++N)
	{
		FString Sig;
		for (TFieldIterator<FProperty> P(*It); P; ++P)
		{
			if (!Sig.IsEmpty()) Sig += TEXT(", ");
			Sig += FString::Printf(TEXT("%s%s %s"), P->HasAnyPropertyFlags(CPF_ReturnParm) ? TEXT("ret ") : P->HasAnyPropertyFlags(CPF_OutParm) ? TEXT("out ") : TEXT(""), *P->GetCPPType(), *P->GetName());
		}
		UE_LOG(LogSimumav, Log, TEXT("FABAPI %s fn %s(%s)"), *Tag, *It->GetName(), *Sig);
	}
	for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::ExcludeSuper); It && N < MaxLines; ++It, ++N)
	{
		UE_LOG(LogSimumav, Log, TEXT("FABAPI %s var %s %s"), *Tag, *It->GetCPPType(), *It->GetName());
	}
}

void FSmvFabContent::DumpMaterialParams(UMaterialInterface* M, const FString& Tag)
{
	if (!M) { UE_LOG(LogSimumav, Log, TEXT("FABAPI %s mat yok"), *Tag); return; }
	TArray<FMaterialParameterInfo> Infos;
	TArray<FGuid> Ids;
	FString Out;
	M->GetAllScalarParameterInfo(Infos, Ids);
	for (const FMaterialParameterInfo& I : Infos) Out += TEXT("s:") + I.Name.ToString() + TEXT(" ");
	M->GetAllVectorParameterInfo(Infos, Ids);
	for (const FMaterialParameterInfo& I : Infos) Out += TEXT("v:") + I.Name.ToString() + TEXT(" ");
	M->GetAllTextureParameterInfo(Infos, Ids);
	for (const FMaterialParameterInfo& I : Infos) Out += TEXT("t:") + I.Name.ToString() + TEXT(" ");
	const UMaterial* Base = M->GetMaterial();
	UE_LOG(LogSimumav, Log, TEXT("FABAPI %s mat %s (alan %d, taban %s): %s"), *Tag, *M->GetName(), Base ? (int32)Base->MaterialDomain.GetValue() : -1, Base ? *Base->GetName() : TEXT("-"), *Out);
}

void FSmvFabContent::DumpFabApis()
{
	static bool bDone = false;
	if (bDone || !FParse::Param(FCommandLine::Get(), TEXT("SmvSmoke"))) return;
	bDone = true;
	auto Cls = [](const TCHAR* Pkg, const TCHAR* Obj) -> UClass*
	{
		return FPackageName::DoesPackageExist(Pkg) ? LoadClass<UObject>(nullptr, Obj, nullptr, LOAD_NoWarn | LOAD_Quiet) : nullptr;
	};
	auto Mat = [](const TCHAR* Pkg, const TCHAR* Obj) -> UMaterialInterface*
	{
		return FPackageName::DoesPackageExist(Pkg) ? LoadObject<UMaterialInterface>(nullptr, Obj, nullptr, LOAD_NoWarn | LOAD_Quiet) : nullptr;
	};
	DumpClassApi(Cls(TEXT("/Game/Aircraft_HUD/UI/HUD/WBP_Jet_HUD"), TEXT("/Game/Aircraft_HUD/UI/HUD/WBP_Jet_HUD.WBP_Jet_HUD_C")), TEXT("JetHUD"));
	DumpClassApi(Cls(TEXT("/Game/Aircraft_HUD/UI/HUD/WBP_C"), TEXT("/Game/Aircraft_HUD/UI/HUD/WBP_C.WBP_C_C")), TEXT("JetHUD_C"));
	DumpClassApi(Cls(TEXT("/Game/SpeedEffects/Assets/Blueprints/AC_SpeedEffects"), TEXT("/Game/SpeedEffects/Assets/Blueprints/AC_SpeedEffects.AC_SpeedEffects_C")), TEXT("SpeedFx"));
	for (const TCHAR* N : { TEXT("M_Pitch"), TEXT("M_Yaw"), TEXT("M_NUM_Altitude"), TEXT("M_NUM_Speed") })
	{
		const FString P = FString(TEXT("/Game/Aircraft_HUD/Materials/UI_Materials/")) + N;
		DumpMaterialParams(Mat(*P, *(P + TEXT(".") + N)), TEXT("JetHUD"));
	}
	DumpMaterialParams(Mat(TEXT("/Game/SpeedEffects/Assets/Materials/M_SpeedEffects_SpeedLines"), TEXT("/Game/SpeedEffects/Assets/Materials/M_SpeedEffects_SpeedLines.M_SpeedEffects_SpeedLines")), TEXT("SpeedFx"));
	DumpMaterialParams(Mat(TEXT("/Game/SpeedEffects/Assets/Materials/M_SpeedEffects_RadialBlur"), TEXT("/Game/SpeedEffects/Assets/Materials/M_SpeedEffects_RadialBlur.M_SpeedEffects_RadialBlur")), TEXT("SpeedFx"));
}

float FSmvFabContent::GetFloat(const TCHAR* Key, float Default)
{
	float V = Default;
	if (GConfig) GConfig->GetFloat(GSmvFabSection, Key, V, GGameIni);
	return V;
}

namespace
{
	FText SmvI18nOr(const FString& Key, const FString& Fallback)
	{
		if (Key.IsEmpty()) return FText::FromString(Fallback);
		const FText T = FSmvI18n::T(Key);
		return T.ToString() == Key ? FText::FromString(Fallback) : T;
	}

	/** Kayıt tablosu + ini ek haritaları (ilk çağrıda çözülür; adlar her çağrıda geçerli dile göre tazelenir). */
	TArray<FSmvFabMap>& SmvAllMapsMutable()
	{
		static TArray<FSmvFabMap> Out;
		static bool bRead = false;
		if (bRead) return Out;
		bRead = true;
		for (const FSmvMapDef& D : FSmvMapRegistry::All())
		{
			FSmvFabMap M;
			M.Key = D.Key;
			M.Kind = D.Kind;
			M.StartE = D.StartE; M.StartN = D.StartN; M.StartYaw = D.StartYaw;
			M.bDecor = D.bDecor;
			M.Package = D.Package;
			M.Surround = D.Surround ? D.Surround : TEXT("");
			M.Preview = D.Preview;
			// UFS (pak) içindeki dosya: göreli proje yolu korunur (pak bağlama noktası göreli; tam yola çevirmek bulamazdı)
			const FString PreviewPath = FPaths::Combine(FPaths::ProjectContentDir(), M.Preview);
			if (!M.Preview.IsEmpty() && FPaths::FileExists(PreviewPath)) M.PreviewFile = PreviewPath;
			if (D.Kind == ESmvMapKind::ProcCity)
			{
				// ağları yüklemeden say (menü açılışında)
				int32 N = 0;
				for (const TCHAR* Dir : FSmvMapRegistry::CityMeshDirs())
				{
					TArray<FAssetData> A;
					IAssetRegistry::GetChecked().GetAssetsByPath(FName(Dir), A, true);
					for (const FAssetData& X : A) if (X.AssetClassPath == UStaticMesh::StaticClass()->GetClassPathName()) ++N;
				}
				M.bAvailable = N > 0 || FSmvFabContent::CityTowerLevels().Num() > 0;
			}
			else
			{
				TArray<FString> Cands;
				FString(D.Levels).ParseIntoArray(Cands, TEXT(";"), true);
				for (FString& C : Cands)
				{
					C.TrimStartAndEndInline();
					if (C.IsEmpty()) continue;
					if (M.Level.IsEmpty()) M.Level = C;   // pakette yoksa ilk aday (bilgi)
					if (FPackageName::DoesPackageExist(C)) { M.Level = C; M.bAvailable = true; break; }
				}
			}
			SMV_KLOG(TEXT("Uçuş haritası %s: %s (%s)"), *M.Key, M.bAvailable ? TEXT("var") : TEXT("pakette yok"),
				M.Level.IsEmpty() ? TEXT("kodla kurulan şehir") : *M.Level);
			Out.Add(M);
		}
		// ini ek haritaları: +Maps=<anahtar>|/Game/... (aynı anahtar tablodakini geçersiz kılar)
		TArray<FString> Lines;
		if (GConfig) GConfig->GetArray(GSmvFabSection, TEXT("Maps"), Lines, GGameIni);
		for (const FString& Line : Lines)
		{
			FString Key, Level;
			if (!Line.Split(TEXT("|"), &Key, &Level)) continue;
			Key.TrimStartAndEndInline();
			Level.TrimStartAndEndInline();
			if (Key.IsEmpty() || Level.IsEmpty()) continue;
			FSmvFabMap* M = Out.FindByPredicate([&](const FSmvFabMap& X) { return X.Key == Key; });
			if (!M) { M = &Out.AddDefaulted_GetRef(); M->Key = Key; M->Kind = ESmvMapKind::Level; }
			M->Level = Level;
			M->bAvailable = FPackageName::DoesPackageExist(Level);
			SMV_KLOG(TEXT("Uçuş haritası (ini) %s → %s (%s)"), *Key, *Level, M->bAvailable ? TEXT("var") : TEXT("pakette yok"));
		}
		return Out;
	}
}

const TArray<FSmvFabMap>& FSmvFabContent::AllMaps()
{
	TArray<FSmvFabMap>& All = SmvAllMapsMutable();
	for (FSmvFabMap& M : All)
	{
		const FSmvMapDef* D = FSmvMapRegistry::Find(M.Key);
		M.Name = SmvI18nOr(D ? FString(D->NameKey) : FString(TEXT("fabmap.")) + M.Key, M.Key);
		M.Description = SmvI18nOr(D ? FString(D->DescKey) : FString(), FString());
	}
	return All;
}

const TArray<FSmvFabMap>& FSmvFabContent::Maps()
{
	static TArray<FSmvFabMap> Out;
	Out.Reset();
	for (const FSmvFabMap& M : AllMaps()) if (M.bAvailable) Out.Add(M);
	return Out;
}

const FSmvFabMap* FSmvFabContent::FindMap(const FString& WorldMode)
{
	if (!IsFabWorld(WorldMode)) return nullptr;
	const FString Key = WorldMode.Mid(4);
	// kalıcı dizi (Maps() her çağrıda yeniden kurulur; işaretçi onu göstermemeli)
	for (const FSmvFabMap& M : AllMaps()) if (M.bAvailable && M.Key == Key) return &M;
	return nullptr;
}

FSmvTrack FSmvFabContent::MakeTrack(const FSmvFabMap& Map)
{
	FSmvTrack T;
	T.Id = FString(TEXT("fab:")) + Map.Key;
	// sanal: sunucuya/yerel dosyaya yazılmaz (yalnız yerel SITL ile uçulur)
	T.Name = Map.Name.ToString();
	// SITL ev konumu = haritanın gerçek dünya demiri (kayıt tablosu; Cesium çevresi bu bölgeden gelir); harita kalkış noktası buraya oturtulur
	const FSmvMapDef* D = FSmvMapRegistry::Find(Map.Key);
	if (D && D->Lat != 0.0) { T.Lat = D->Lat; T.Lon = D->Lon; T.GroundMsl = D->GroundMsl; }
	else { T.Lat = 39.9334; T.Lon = 32.8597; T.GroundMsl = 0; }
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

const TArray<UStaticMesh*>& FSmvFabContent::StaticMeshesIn(const FString& Dir)
{
	static TMap<FString, TArray<UStaticMesh*>> Cache;
	if (const TArray<UStaticMesh*>* F = Cache.Find(Dir)) return *F;
	TArray<UStaticMesh*>& Out = Cache.Add(Dir);
	TArray<FAssetData> Assets;
	IAssetRegistry::GetChecked().GetAssetsByPath(FName(*Dir), Assets, true);
	Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.PackageName.LexicalLess(B.PackageName); });
	for (const FAssetData& A : Assets)
	{
		if (A.AssetClassPath != UStaticMesh::StaticClass()->GetClassPathName()) continue;
		if (UStaticMesh* M = Cast<UStaticMesh>(A.GetAsset())) { M->AddToRoot(); Out.Add(M); }
	}
	UE_LOG(LogSimumav, Log, TEXT("Fab statik ağlar (%s): %d"), *Dir, Out.Num());
	return Out;
}

namespace
{
	bool SmvIsPropName(const FString& N, const FString& Hint)
	{
		const FString L = N.ToLower();
		return L.Contains(TEXT("prop")) || L.Contains(TEXT("rotor")) || L.Contains(TEXT("blade")) || L.Contains(TEXT("pervane")) || L.Contains(TEXT("fan"))
			|| (!Hint.IsEmpty() && L.Contains(Hint.ToLower()));
	}

	/** SCS düğümü ağacını gezer: ağ bileşenlerinin kök bileşene göre dönüşümünü toplar */
	void SmvWalkScs(USCS_Node* N, const FTransform& Parent, const FString& Hint, TArray<FSmvBodyPart>& Out)
	{
		if (!N) return;
		FTransform Here = Parent;
		if (USceneComponent* T = Cast<USceneComponent>(N->ComponentTemplate))
		{
			Here = FTransform(T->GetRelativeRotation(), T->GetRelativeLocation(), T->GetRelativeScale3D()) * Parent;
			if (!T->IsVisible()) { for (USCS_Node* C : N->GetChildNodes()) SmvWalkScs(C, Here, Hint, Out); return; }
			FSmvBodyPart P;
			P.Xf = Here;
			P.Name = N->GetVariableName().ToString();
			if (UStaticMeshComponent* Sm = Cast<UStaticMeshComponent>(T))
			{
				P.Static = Sm->GetStaticMesh();
				for (int32 i = 0; i < Sm->OverrideMaterials.Num(); ++i) P.Materials.Add(Sm->OverrideMaterials[i]);
			}
			else if (USkeletalMeshComponent* Sk = Cast<USkeletalMeshComponent>(T))
			{
				P.Skeletal = Sk->GetSkeletalMeshAsset();
				for (int32 i = 0; i < Sk->OverrideMaterials.Num(); ++i) P.Materials.Add(Sk->OverrideMaterials[i]);
			}
			if (P.Static || P.Skeletal)
			{
				P.bProp = SmvIsPropName(P.Name, Hint) || (P.Static && SmvIsPropName(P.Static->GetName(), Hint));
				Out.Add(P);
			}
		}
		for (USCS_Node* C : N->GetChildNodes()) SmvWalkScs(C, Here, Hint, Out);
	}
}

const TArray<FSmvBodyPart>& FSmvFabContent::BlueprintBody(const FString& Dir, const FString& PropHint)
{
	static TMap<FString, TArray<FSmvBodyPart>> Cache;
	const FString Key = Dir + TEXT("|") + PropHint;
	if (const TArray<FSmvBodyPart>* F = Cache.Find(Key)) return *F;
	TArray<FSmvBodyPart>& Best = Cache.Add(Key);
	TArray<FAssetData> Assets;
	IAssetRegistry::GetChecked().GetAssetsByPath(FName(*Dir), Assets, true);
	for (const FAssetData& A : Assets)
	{
		const FString Cls = A.AssetClassPath.GetAssetName().ToString();
		if (!Cls.Contains(TEXT("Blueprint"))) continue;
		// pişmiş pakette Blueprint varlığı yok: üretilen sınıf "<paket>.<ad>_C" yolundan yüklenir
		const FString ClassPath = A.PackageName.ToString() + TEXT(".") + A.AssetName.ToString() + TEXT("_C");
		UBlueprintGeneratedClass* Bp = Cast<UBlueprintGeneratedClass>(StaticLoadClass(UObject::StaticClass(), nullptr, *ClassPath, nullptr, LOAD_NoWarn | LOAD_Quiet));
		if (!Bp) continue;
		TArray<FSmvBodyPart> Parts;
		for (UBlueprintGeneratedClass* C = Bp; C; C = Cast<UBlueprintGeneratedClass>(C->GetSuperClass()))
			if (C->SimpleConstructionScript)
				for (USCS_Node* R : C->SimpleConstructionScript->GetRootNodes()) SmvWalkScs(R, FTransform::Identity, PropHint, Parts);
		UE_LOG(LogSimumav, Log, TEXT("Fab gövde Blueprint'i %s: %d ağ parçası (%d pervane)"), *ClassPath, Parts.Num(), Parts.FilterByPredicate([](const FSmvBodyPart& P) { return P.bProp; }).Num());
		if (Parts.Num() > Best.Num()) Best = MoveTemp(Parts);
	}
	for (FSmvBodyPart& P : Best)
	{
		if (P.Static) P.Static->AddToRoot();
		if (P.Skeletal) P.Skeletal->AddToRoot();
		for (UMaterialInterface* M : P.Materials) if (M) M->AddToRoot();
	}
	return Best;
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

USoundBase* FSmvFabContent::LoadEngineSound(const TCHAR* Kind)
{
	static TMap<FString, USoundBase*> Cache;
	const FString K(Kind);
	if (USoundBase** F = Cache.Find(K)) return *F;
	// ayar ile değiştirilebilir: EngineSound<Kind>=/Game/.../Cue.Cue ; yoksa CI'nin kopyası /Game/Fab/Sounds/SC_Engine_<Kind>
	FString Path;
	if (!GConfig || !GConfig->GetString(GSmvFabSection, *(FString(TEXT("EngineSound")) + K), Path, GGameIni) || Path.IsEmpty())
	{
		Path = FString::Printf(TEXT("/Game/Fab/Sounds/SC_Engine_%s.SC_Engine_%s"), *K, *K);
	}
	if (!Path.Contains(TEXT("."))) Path += TEXT(".") + FPackageName::GetShortName(Path);   // paket yolu verildiyse nesne yolu
	// dron motor döngüleri (Fab "Drone Sounds"): çok rotor / hexa / küçük FPV (CI kopyası yoksa paketteki cue doğrudan)
	if (!FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(Path)))
	{
		const TCHAR* Cue = K == TEXT("Multi") ? TEXT("04_Multi-Rotor_Drone_Flying_Object_Loop_Cue") : K == TEXT("Hexa") ? TEXT("10_Hexacopter_Drone_Flying_Object_Loop_Cue")
			: K == TEXT("Fpv") ? TEXT("03_Small_Drone_Flying_Object_Loop_Cue") : nullptr;
		if (Cue) Path = FString::Printf(TEXT("/Game/Drone-Sounds/Cue/44_1kHz-24bit/%s.%s"), Cue, Cue);
	}
	USoundBase* S = nullptr;
	if (FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(Path))) S = LoadObject<USoundBase>(nullptr, *Path);
	if (S)
	{
		S->AddToRoot();
		UE_LOG(LogSimumav, Log, TEXT("Fab motor sesi (%s): %s"), *K, *S->GetPathName());
	}
	Cache.Add(K, S);
	return S;
}

const TArray<UStaticMesh*>& FSmvFabContent::CityMeshes()
{
	static TArray<UStaticMesh*> Out;
	static bool bRead = false;
	if (bRead) return Out;
	bRead = true;
	for (const TCHAR* Dir : FSmvMapRegistry::CityMeshDirs())
	{
		TArray<FAssetData> Assets;
		IAssetRegistry::GetChecked().GetAssetsByPath(FName(Dir), Assets, true);
		Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.AssetName.LexicalLess(B.AssetName); });
		for (const FAssetData& A : Assets)
		{
			if (A.AssetClassPath != UStaticMesh::StaticClass()->GetClassPathName()) continue;
			if (UStaticMesh* M = Cast<UStaticMesh>(A.GetAsset()))
			{
				M->AddToRoot();
				Out.Add(M);
			}
		}
	}
	UE_LOG(LogSimumav, Log, TEXT("Şehir bina ağları: %d"), Out.Num());
	return Out;
}

TArray<FString> FSmvFabContent::CityTowerLevels()
{
	static TArray<FString> Out;
	static bool bRead = false;
	if (!bRead)
	{
		bRead = true;
		for (const TCHAR* L : FSmvMapRegistry::CityTowerLevels()) if (FPackageName::DoesPackageExist(L)) Out.Add(L);
		UE_LOG(LogSimumav, Log, TEXT("Şehir bina seviyeleri (City Sample): %d"), Out.Num());
	}
	return Out;
}

// ---------------------------------------------------------------------------
// Ultra Dynamic Sky (Fab) — gök/ışık/hava
// ---------------------------------------------------------------------------

bool FSmvFabContent::SpawnSky(UWorld* W, const FSmvTrack& Track, TArray<AActor*>& OutActors)
{
	// tam atmosfer kurulumu SmvAtmosphere'de (UDS + UDW ya da yerleşik gök, post-process, gerçek güneş, SITL rüzgârı, kalite)
	const bool bCesium = Track.WorldMode == TEXT("real") || Track.WorldMode == TEXT("google3d");
	return FSmvAtmosphere::Apply(W, FSmvAtmosphereCfg::FromTrack(Track, bCesium), OutActors);
}
