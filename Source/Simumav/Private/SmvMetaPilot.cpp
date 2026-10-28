#include "SmvMetaPilot.h"
#include "SimumavModule.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"

namespace
{
	const TCHAR* GSmvPilotKitPkg = TEXT("/Game/Simumav/Pilot/DA_SmvPilotKit");

	USmvPilotKit* SmvKit()
	{
		static TWeakObjectPtr<USmvPilotKit> Kit;
		static bool bTried = false;
		if (!bTried)
		{
			bTried = true;
			bool bOn = true;
			if (GConfig) GConfig->GetBool(TEXT("/Script/Simumav.SmvPilot"), TEXT("MetaHuman"), bOn, GGameIni);
			if (bOn && FPackageName::DoesPackageExist(GSmvPilotKitPkg))
			{
				if (USmvPilotKit* K = LoadObject<USmvPilotKit>(nullptr, TEXT("/Game/Simumav/Pilot/DA_SmvPilotKit.DA_SmvPilotKit"), nullptr, LOAD_NoWarn | LOAD_Quiet))
				{
					K->AddToRoot();
					Kit = K;
				}
			}
			int32 NFace = 0;
			if (Kit.IsValid()) for (const FSmvPilotKitEntry& E : Kit->Entries) if (E.Kind == TEXT("face")) ++NFace;
			UE_LOG(LogSimumav, Log, TEXT("MetaHuman pilot kiti: %s (%d yüz, %d girdi)"), Kit.IsValid() ? TEXT("var") : TEXT("yok — temel şekil pilot"), NFace, Kit.IsValid() ? Kit->Entries.Num() : 0);
		}
		return Kit.Get();
	}

	/** Uygulama saç modeli → MCC groom adı */
	FString SmvHairGroom(const FString& H, bool bF)
	{
		if (H == TEXT("bald") || H.IsEmpty()) return FString();
		if (H == TEXT("buzz")) return TEXT("Hair_S_BuzzCut");
		if (H == TEXT("spiky")) return TEXT("Hair_S_Messy");
		if (H == TEXT("sidepart")) return TEXT("Hair_S_SideSweptFringe");
		if (H == TEXT("curly")) return bF ? TEXT("Hair_M_BobCurly") : TEXT("Hair_S_CurlyFade");
		if (H == TEXT("mohawk")) return TEXT("Hair_M_Mohawk");
		if (H == TEXT("long")) return TEXT("Hair_L_Straight");
		if (H == TEXT("ponytail")) return TEXT("Hair_S_LowPonytail");
		if (H == TEXT("bun")) return TEXT("Hair_S_UpdoBuns");
		if (H == TEXT("bob")) return TEXT("Hair_M_BobStraight");
		return bF ? TEXT("Hair_S_PulledBack") : TEXT("Hair_S_Casual");
	}
}

bool FSmvMetaPilot::Available()
{
	return SmvKit() != nullptr;
}

bool FSmvMetaPilot::Resolve(const FSmvAvatar& A, FSmvMetaPilotParts& Out)
{
	USmvPilotKit* Kit = SmvKit();
	if (!Kit) return false;
	const bool bF = A.Body == TEXT("f");
	const int32 Tone = FMath::Clamp(FCString::Atoi(*A.Skin.Mid(1)), 1, 8);
	// gövde
	for (const FSmvPilotKitEntry& E : Kit->Entries)
	{
		if (E.Kind == (bF ? TEXT("body_f") : TEXT("body_m"))) Out.Body = Cast<USkeletalMesh>(E.Asset.LoadSynchronous());
	}
	// yüz: aynı cinsiyet, tene en yakın kademe (±1), aralarından yüz/burun/göz/ağız seçimine göre
	TArray<const FSmvPilotKitEntry*> Faces;
	for (const FSmvPilotKitEntry& E : Kit->Entries) if (E.Kind == TEXT("face") && E.bFemale == bF) Faces.Add(&E);
	if (Faces.Num() == 0) for (const FSmvPilotKitEntry& E : Kit->Entries) if (E.Kind == TEXT("face")) Faces.Add(&E);
	if (Faces.Num() == 0 || !Out.Body) return false;
	int32 Best = 99;
	for (const FSmvPilotKitEntry* E : Faces) Best = FMath::Min(Best, FMath::Abs(E->Tone - Tone));
	TArray<const FSmvPilotKitEntry*> Near;
	for (const FSmvPilotKitEntry* E : Faces) if (FMath::Abs(E->Tone - Tone) <= Best + 1) Near.Add(E);
	Near.Sort([Tone](const FSmvPilotKitEntry& X, const FSmvPilotKitEntry& Y) { const int32 Dx = FMath::Abs(X.Tone - Tone), Dy = FMath::Abs(Y.Tone - Tone); return Dx != Dy ? Dx < Dy : X.Name < Y.Name; });
	// en yakın kademe ağırlıklı: yüz şekli seçimi önce en yakınlar arasında döner
	int32 NBest = 0;
	for (const FSmvPilotKitEntry* E : Near) if (FMath::Abs(E->Tone - Tone) == Best) ++NBest;
	const uint32 H = GetTypeHash(A.Face + TEXT("|") + A.Nose + TEXT("|") + A.Eyes + TEXT("|") + A.Mouth);
	const FSmvPilotKitEntry* FaceE = Near[(int32)(H % (uint32)FMath::Max(1, (H % 4 == 0) ? Near.Num() : NBest))];
	Out.Face = Cast<USkeletalMesh>(FaceE->Asset.LoadSynchronous());
	Out.FaceName = FaceE->Name;
	if (!Out.Face) return false;
	// tüyler: saç (kask yokken), kaş, sakal/bıyık (erkek), kirpik
	TArray<FString> Want;
	const bool bHelmet = A.Headgear == TEXT("helmet");
	// şapkada saç tüyü şapkanın içinden taşıyordu: yalnız kaskısız ve şapkasızken
	if (!bHelmet && A.Headgear != TEXT("cap")) { const FString Hg = SmvHairGroom(A.Hair, bF); if (!Hg.IsEmpty()) Want.Add(Hg); }
	Want.Add(A.BrowW == TEXT("thin") ? TEXT("Eyebrows_M_Thin") : A.BrowW == TEXT("thick") ? TEXT("Eyebrows_M_Thick") : TEXT("Eyebrows_M_Natural"));
	if (!bF)
	{
		if (A.Beard == TEXT("stubble")) Want.Add(TEXT("Beard_S_Stubble"));
		else if (A.Beard == TEXT("mustache")) Want.Add(TEXT("Mustache_S_Full"));
		else if (A.Beard == TEXT("goatee")) Want.Add(TEXT("Goatee_M_Pointed"));
		else if (A.Beard == TEXT("full")) Want.Add(TEXT("Beard_S_Full"));
	}
	Want.Add(bF ? TEXT("Eyelashes_L_SlightCurl") : TEXT("Eyelashes_S_Thin"));
	for (const FString& W : Want)
	{
		UObject* G = nullptr;
		UObject* B = nullptr;
		for (const FSmvPilotKitEntry& E : Kit->Entries)
		{
			if (E.Kind == TEXT("groom") && E.Name == W) G = E.Asset.LoadSynchronous();
			else if (E.Kind == TEXT("binding") && E.Name == W && E.Extra == Out.FaceName) B = E.Asset.LoadSynchronous();
		}
		if (G) Out.Grooms.Add(TPair<UObject*, UObject*>(G, B));
	}
	return true;
}
