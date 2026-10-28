#include "SmvPilotFigure.h"
#include "Components/StaticMeshComponent.h"
#include "SmvUiKit.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "SmvMetaPilot.h"
#include "Misc/ConfigCacheIni.h"
#include "SimumavModule.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GroomComponent.h"
#include "GroomAsset.h"
#include "GroomBindingAsset.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/AssetData.h"

namespace
{
	FLinearColor PfHex(const FString& H, const FLinearColor& Def)
	{
		return H.Len() == 7 ? FLinearColor::FromSRGBColor(FColor::FromHex(H)) : Def;
	}
	FLinearColor PfSkin(const FString& S);
}

/** MetaHuman baş merkezi: head kemiğine göre ileri / yukarı kaydırma (cm; [/Script/Simumav.SmvPilot] HeadFwdCm / HeadUpCm) */
struct FSmvPilotHeadCfg
{
	static float Get(const TCHAR* K, float D) { float V = D; if (GConfig) GConfig->GetFloat(TEXT("/Script/Simumav.SmvPilot"), K, V, GGameIni); return V; }
	static float Fwd() { return Get(TEXT("HeadFwdCm"), 2.f); }
	static float Up() { return Get(TEXT("HeadUpCm"), 3.f); }
};

namespace
{
	FLinearColor PfSkin(const FString& S)
	{
		static const TCHAR* Sk[] = { TEXT("#ffdbac"), TEXT("#f7e0c3"), TEXT("#f1c9a5"), TEXT("#e8b58a"), TEXT("#e0ac69"), TEXT("#c68642"), TEXT("#8d5524"), TEXT("#6b4226") };
		const int32 I = FMath::Clamp(FCString::Atoi(*S.Mid(1)) - 1, 0, 7);
		return PfHex(Sk[I], FLinearColor(0.9f, 0.7f, 0.55f));
	}
}

namespace
{
	/** Fab MetaHuman giysi paketi (/Game/Fab/<paket>, fab-hazirla genel ürün içe aktarması): MetaHuman iskeletine deri bağlamalı
	 *  (≥ 20 kemik) iskeletli ağ parçaları; tek kemikli "SM_*" giysiler (MetaHuman Creator'da uydurulmak üzere statik) atlanır */
	const TArray<USkeletalMesh*>& PfOutfitParts(const FString& Set)
	{
		static TMap<FString, TArray<USkeletalMesh*>> Cache;
		if (const TArray<USkeletalMesh*>* F = Cache.Find(Set)) return *F;
		TArray<USkeletalMesh*>& Out = Cache.Add(Set);
		TArray<FAssetData> As;
		// "/Game/..." tam yol: UE biçimli paket (Workbrand Techwear); değilse fab-hazirla içe aktarması /Game/Fab/<paket>
		IAssetRegistry::GetChecked().GetAssetsByPath(FName(*(Set.StartsWith(TEXT("/Game/")) ? Set : FString(TEXT("/Game/Fab/")) + Set)), As, true);
		As.Sort([](const FAssetData& A, const FAssetData& B) { return A.PackageName.LexicalLess(B.PackageName); });
		for (const FAssetData& A : As)
		{
			if (A.AssetClassPath != USkeletalMesh::StaticClass()->GetClassPathName()) continue;
			USkeletalMesh* M = Cast<USkeletalMesh>(A.GetAsset());
			if (!M) continue;
			const int32 Nb = M->GetRefSkeleton().GetNum();
			const bool bBody = M->GetRefSkeleton().FindBoneIndex(TEXT("spine_03")) != INDEX_NONE || M->GetRefSkeleton().FindBoneIndex(TEXT("pelvis")) != INDEX_NONE;
			if (Nb < 20 || !bBody) { UE_LOG(LogSimumav, Log, TEXT("Pilot giysi parçası atlandı (%s: %d kemik, MetaHuman gövde kemiği %s)"), *M->GetName(), Nb, bBody ? TEXT("var") : TEXT("yok")); continue; }
			M->AddToRoot(); Out.Add(M);
		}
		UE_LOG(LogSimumav, Log, TEXT("Pilot giysi paketi %s: %d parça"), *Set, Out.Num());
		return Out;
	}

	/** Workbrand Techwear (Fab, MetaHuman giysisi): bir üst + bir alt; Chaos kumaş sürümleri atlanır. Seçim avatardan
	 *  (her pilot hep aynı), [/Script/Simumav.SmvPilot] TechwearTop / TechwearBottom (1 tabanlı) ile sabitlenebilir. */
	TArray<USkeletalMesh*> PfTechwear(bool bF, uint32 Seed)
	{
		const TArray<USkeletalMesh*>& All = PfOutfitParts(bF ? TEXT("/Game/WB_Techwear/SkeletalMeshes/FTN") : TEXT("/Game/WB_Techwear/SkeletalMeshes/MTN"));
		TArray<USkeletalMesh*> Tops, Bots, Out;
		for (USkeletalMesh* M : All)
		{
			const FString N = M->GetName();
			if (N.Contains(TEXT("Chaos"))) continue;
			if (N.Contains(TEXT("Tops"))) Tops.Add(M); else if (N.Contains(TEXT("Bottoms"))) Bots.Add(M);
		}
		int32 Ti = -1, Bi = -1;
		if (GConfig) { GConfig->GetInt(TEXT("/Script/Simumav.SmvPilot"), TEXT("TechwearTop"), Ti, GGameIni); GConfig->GetInt(TEXT("/Script/Simumav.SmvPilot"), TEXT("TechwearBottom"), Bi, GGameIni); }
		if (Tops.Num()) Out.Add(Tops[Ti > 0 ? (Ti - 1) % Tops.Num() : Seed % Tops.Num()]);
		if (Bots.Num()) Out.Add(Bots[Bi > 0 ? (Bi - 1) % Bots.Num() : (Seed / 7) % Bots.Num()]);
		return Out;
	}

	/** Avatar → giysi parçaları: kıyafet türü (suit / jacket / casual) ve cinsiyete göre Fab paketleri; ini ile değiştirilebilir
	 *  ([/Script/Simumav.SmvPilot] Outfit_suit_m=Paket1+Paket2 …). Başlık parçaları (kask / başlık / şapka) atlanır — kask avatardan. */
	TArray<USkeletalMesh*> PfOutfitFor(const FSmvAvatar& A)
	{
		const bool bF = A.Body == TEXT("f");
		const FString Kind = A.Outfit.IsEmpty() ? FString(TEXT("suit")) : A.Outfit;
		FString Cfg;
		if (GConfig) GConfig->GetString(TEXT("/Script/Simumav.SmvPilot"), *FString::Printf(TEXT("Outfit_%s_%s"), *Kind, bF ? TEXT("f") : TEXT("m")), Cfg, GGameIni);
		TArray<TArray<FString>> Choices;   // sırayla denenen paket grupları (grubun ilk paketi varsa grup kullanılır)
		if (!Cfg.IsEmpty()) { TArray<FString> G; Cfg.ParseIntoArray(G, TEXT("+")); Choices.Add(G); }
		// "@WB": Workbrand Techwear üst + alt (ayakkabı yok → bomber botları)
		if (bF) { if (Kind != TEXT("casual")) Choices.Add({ TEXT("@WB"), TEXT("FemaleOutfitBomberMetahuman:Boots") }); Choices.Add({ TEXT("FemaleOutfitBomberMetahuman") }); }
		else if (Kind == TEXT("jacket")) { Choices.Add({ TEXT("@WB"), TEXT("BomberJeansOutfitMetahuman:Boots") }); Choices.Add({ TEXT("BomberJeansOutfitMetahuman") }); Choices.Add({ TEXT("BomberMetahuman"), TEXT("ShirtCombatPantsMetahuman") }); }
		else if (Kind == TEXT("casual")) { Choices.Add({ TEXT("ShirtCombatPantsMetahuman"), TEXT("BomberJeansOutfitMetahuman:Boots") }); }
		else { Choices.Add({ TEXT("AstronautsFlightSuitsMetahumanClothing") }); Choices.Add({ TEXT("@WB"), TEXT("BomberJeansOutfitMetahuman:Boots") }); Choices.Add({ TEXT("TacticalSoldierOutfitforMetaHumanUE56") }); }
		if (!bF) Choices.Add({ TEXT("TacticalSoldierOutfitforMetaHumanUE56") });
		TArray<USkeletalMesh*> Out;
		for (const TArray<FString>& G : Choices)
		{
			for (int32 i = 0; i < G.Num(); ++i)
			{
				if (G[i] == TEXT("@WB"))
				{
					const TArray<USkeletalMesh*> Wb = PfTechwear(bF, GetTypeHash(A.Skin + A.Outfit + A.Body) ^ 0x5bd1e995u);
					if (i == 0 && Wb.Num() == 0) break;
					Out.Append(Wb);
					continue;
				}
				FString Set = G[i], Only;
				G[i].Split(TEXT(":"), &Set, &Only);
				if (Set.IsEmpty()) Set = G[i];
				const TArray<USkeletalMesh*>& Ps = PfOutfitParts(Set);
				if (i == 0 && Ps.Num() == 0) break;
				for (USkeletalMesh* M : Ps)
				{
					const FString N = M->GetName().ToLower();
					if (N.Contains(TEXT("helmet")) || N.Contains(TEXT("hood")) || N.Contains(TEXT("hat")) || N.Contains(TEXT("cap_"))) continue;
					if (!Only.IsEmpty() && !N.Contains(Only.ToLower())) continue;
					Out.Add(M);
				}
			}
			if (Out.Num()) break;
		}
		return Out;
	}
}

ASmvPilotFigure::ASmvPilotFigure()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Torso = CreateDefaultSubobject<USceneComponent>(TEXT("Torso"));
	Torso->SetupAttachment(Root);
	Head = CreateDefaultSubobject<USceneComponent>(TEXT("Head"));
	Head->SetupAttachment(Torso);
	Rc = CreateDefaultSubobject<USceneComponent>(TEXT("Rc"));
	Rc->SetupAttachment(Torso);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sph(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	CubeMesh = Cube.Object; CylMesh = Cyl.Object; SphMesh = Sph.Object; ConeMesh = Cone.Object; BaseMat = Mat.Object;
}

UStaticMeshComponent* ASmvPilotFigure::Part(USceneComponent* Parent, int32 Shape, const FVector& Pos, const FVector& SizeCm, const FLinearColor& Color, const FRotator& Rot)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetupAttachment(Parent ? Parent : Root.Get());
	C->SetStaticMesh(Shape == 0 ? CubeMesh.Get() : Shape == 1 ? CylMesh.Get() : Shape == 2 ? SphMesh.Get() : ConeMesh.Get());
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(true);
	if (BaseMat)
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(SmvSolidMat(BaseMat), this);
		SmvSetSolidColor(M, Color);
		C->SetMaterial(0, M);
	}
	C->SetRelativeTransform(FTransform(Rot, Pos, SizeCm / 100.f));
	C->RegisterComponent();
	Parts.Add(C);
	return C;
}

UStaticMeshComponent* ASmvPilotFigure::Mat(UStaticMeshComponent* C, float Rough, float Metal, const FLinearColor& Emissive, const FRotator& Rot)
{
	if (!C) return C;
	if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(C->GetMaterial(0)))
	{
		M->SetScalarParameterValue(TEXT("Roughness"), Rough);
		M->SetScalarParameterValue(TEXT("Metallic"), Metal);
		if (!Emissive.Equals(FLinearColor::Black)) M->SetVectorParameterValue(TEXT("Emissive"), Emissive);
	}
	if (!Rot.IsZero()) C->SetRelativeRotation(Rot);
	return C;
}

void ASmvPilotFigure::Clear()
{
	for (TObjectPtr<UStaticMeshComponent>& P : Parts) if (P) P->DestroyComponent();
	Parts.Reset();
	ThumbL = ThumbR = nullptr;
	for (TObjectPtr<USceneComponent>& G : MhGrooms) if (G) G->DestroyComponent();
	MhGrooms.Reset();
	if (MhFace) { MhFace->DestroyComponent(); MhFace = nullptr; }
	if (MhBody) { MhBody->DestroyComponent(); MhBody = nullptr; }
	bMh = false;
	HeadRel = FRotator::ZeroRotator;
	if (Head) Head->SetRelativeRotation(FRotator::ZeroRotator);
	if (Torso) { Torso->SetRelativeScale3D(FVector::OneVector); Torso->SetRelativeRotation(FRotator::ZeroRotator); }
}

void ASmvPilotFigure::Build(const FSmvAvatar& A, bool bWithRc)
{
	Clear();
	// MetaHuman (MCC) pilot: kit pakette varsa gerçek yüz + gövde + tüyler; yoksa / küçük resimde temel şekil figürü
	if (!bLowDetail && BuildMetaHuman(A, bWithRc)) return;
	const bool bF = A.Body == TEXT("f");
	const float Wb = (A.Build == TEXT("slim") ? 34.f : A.Build == TEXT("broad") ? 46.f : 40.f) * (bF ? 0.88f : 1.f);
	const FLinearColor Skin = PfSkin(A.Skin);
	const FLinearColor Suit = PfHex(A.Suit, FLinearColor(0.12f, 0.2f, 0.3f));
	const FLinearColor Helmet = PfHex(A.Helmet, FLinearColor(0.12f, 0.2f, 0.3f));
	const FLinearColor Visor = PfHex(A.Visor, FLinearColor(0.9f, 0.75f, 0.2f));
	const FLinearColor Hair = PfHex(A.HairColor, FLinearColor(0.2f, 0.14f, 0.1f));
	const FLinearColor EyeC = PfHex(A.EyeColor, FLinearColor(0.3f, 0.2f, 0.1f));
	const FLinearColor PatchC = PfHex(A.PatchColor, FLinearColor(0.95f, 0.75f, 0.25f));
	FLinearColor Top = Suit, Legs = Suit, Sleeve = Suit;
	if (A.Outfit == TEXT("jacket")) { Legs = PfHex(TEXT("#1f2937"), Legs); }
	else if (A.Outfit == TEXT("casual")) { Top = FMath::Lerp(Suit, FLinearColor::White, 0.35f); Sleeve = Top; Legs = PfHex(TEXT("#3b5b8a"), Legs); }
	const FLinearColor Boot(0.06f, 0.06f, 0.07f);
	const FLinearColor Glove = A.Outfit == TEXT("casual") ? Skin : FLinearColor(0.07f, 0.07f, 0.08f);
	const FLinearColor Strap(0.09f, 0.1f, 0.11f), Metal(0.55f, 0.57f, 0.6f);
	const FLinearColor LegsD = Legs * 0.72f, TopD = Top * 0.72f;
	const float Lw = bF ? 0.9f : 1.f;
	// bacaklar: bot (gövde + burun + taban), baldır, diz koruyucu, uyluk + cep
	for (float Y0 : { -10.f, 10.f })
	{
		const float Y = Y0 * Lw;
		Mat(Part(Root, 0, FVector(3, Y, 6.f), FVector(24, 11.5f, 11), Boot), 0.45f, 0.f);
		Mat(Part(Root, 2, FVector(13.5f, Y, 5.f), FVector(11, 11.5f, 9), Boot), 0.4f, 0.f);
		Part(Root, 0, FVector(3, Y, 0.8f), FVector(28, 12.5f, 1.6f), FLinearColor(0.02f, 0.02f, 0.02f));
		Part(Root, 1, FVector(0, Y, 31.f), FVector(13, 13, 40), Legs);
		Mat(Part(Root, 2, FVector(5.5f, Y, 51.f), FVector(9, 9, 10), LegsD), 0.5f, 0.f);
		Part(Root, 1, FVector(0, Y, 71.f), FVector(16, 16, 42), Legs);
		Part(Root, 0, FVector(1.5f, Y + (Y0 < 0 ? -7.6f : 7.6f), 69.f), FVector(9, 1.6f, 12), LegsD);   // uyluk cebi (yan)
	}
	// gövde (nefes için ayrı düğüm): kalça, kemer + toka, karın, göğüs (omuzlara doğru genişler), göğüs takımı kayışları, fermuar, yaka
	Torso->SetRelativeLocation(FVector::ZeroVector);
	Part(Torso, 2, FVector(0, 0, 93.f), FVector(23, Wb * 0.95f + (bF ? 5.f : 0.f), 19), Legs);
	Mat(Part(Torso, 1, FVector(0, 0, 100.f), FVector(24.5f, Wb * 0.9f + (bF ? 3.f : 0.f), 5), Strap), 0.4f, 0.f);
	Mat(Part(Torso, 0, FVector(12.4f, 0, 100.f), FVector(1.5f, 6.5f, 4.2f), Metal), 0.25f, 0.9f);
	Part(Torso, 1, FVector(0, 0, 110.f), FVector(22, Wb * (bF ? 0.74f : 0.82f), 18), Top);
	Part(Torso, 2, FVector(0, 0, 126.f), FVector(25, Wb * 1.02f, 34), Top);
	Part(Torso, 1, FVector(0, 0, 121.f), FVector(23.5f, Wb * 0.92f, 20), Top);
	if (A.Outfit != TEXT("casual"))
	{
		for (int32 s = -1; s <= 1; s += 2) Mat(Part(Torso, 0, FVector(12.6f, s * 7.5f, 119.f), FVector(1.2f, 3.6f, 38), Strap, FRotator(0, 0, s * 16.f)), 0.45f, 0.f);
		Mat(Part(Torso, 0, FVector(12.9f, 0, 113.f), FVector(1.4f, 13.f, 3.4f), Strap), 0.45f, 0.f);   // göğüs kayışı
		Mat(Part(Torso, 0, FVector(13.2f, 0, 113.f), FVector(1.2f, 4.f, 4.f), Metal), 0.25f, 0.9f);   // kayış tokası
	}
	Mat(Part(Torso, 0, FVector(12.8f, -3.f, 124.f), FVector(0.8f, 1.4f, 26), Metal), 0.3f, 0.8f);   // fermuar
	Part(Torso, 1, FVector(0, 0, 141.f), FVector(15, 15, 4.5f), TopD);
	if (A.Outfit == TEXT("jacket"))
	{
		Part(Torso, 0, FVector(10, 0, 139.f), FVector(6, Wb * 0.55f, 6), FMath::Lerp(Top, FLinearColor::White, 0.4f));   // yaka
	}
	if (A.Patch != TEXT("none") && !A.Patch.IsEmpty()) Mat(Part(Torso, 0, FVector(12.9f, Wb * 0.25f, 129.f), FVector(1, 7, 6), PatchC), 0.5f, 0.f, PatchC * 0.15f);
	// omuzlar, kollar (hafif A-duruşu), eldivenler; sol omuzda arma
	const float Sy = Wb * 0.5f + 2.f;
	for (int32 s = -1; s <= 1; s += 2)
	{
		Part(Torso, 2, FVector(0, s * Sy, 135.f), FVector(15, 15, 14), Sleeve);
		if (bWithRc)
		{
			Part(Torso, 1, FVector(4, s * (Sy + 1.f), 120.f), FVector(11, 11, 28), Sleeve, FRotator(-30.f, 0, 0));
			Part(Torso, 1, FVector(16, s * (Sy - 6.f), 106.f), FVector(10, 10, 26), A.Outfit == TEXT("casual") ? Skin : Sleeve, FRotator(-80.f, s * -15.f, 0));
			Mat(Part(Torso, 2, FVector(27, s * 9.f, 106.f), FVector(9, 8, 9), Glove), 0.5f, 0.f);
		}
		else
		{
			Part(Torso, 1, FVector(0, s * (Sy + 2.5f), 120.f), FVector(11, 11, 30), Sleeve, FRotator(0, 0, s * -7.f));
			Part(Torso, 2, FVector(0.5f, s * (Sy + 4.2f), 105.f), FVector(10, 10, 9), Sleeve);   // dirsek
			Part(Torso, 1, FVector(1.5f, s * (Sy + 5.f), 93.f), FVector(10, 10, 24), A.Outfit == TEXT("casual") ? Skin : Sleeve, FRotator(-4.f, 0, s * -4.f));
			Mat(Part(Torso, 1, FVector(1.5f, s * (Sy + 5.2f), 82.f), FVector(10.5f, 10.5f, 4), A.Outfit == TEXT("casual") ? Skin : Strap), 0.5f, 0.f);   // manşet
			Mat(Part(Torso, 2, FVector(2.f, s * (Sy + 5.5f), 75.f), FVector(8.5f, 7.5f, 11), Glove), 0.55f, 0.f);
		}
		if (s < 0 && A.Patch != TEXT("none") && !A.Patch.IsEmpty()) Mat(Part(Torso, 0, FVector(0, s * (Sy + 7.9f), 126.f), FVector(6, 0.8f, 6), PatchC, FRotator(0, 0, s * -7.f)), 0.5f, 0.f, PatchC * 0.15f);
	}
	// RC kumanda (iki elle) + başparmaklar
	if (bWithRc)
	{
		Rc->SetRelativeLocation(FVector(30, 0, 108));
		Rc->SetRelativeRotation(FRotator(35.f, 0, 0));
		Part(Rc, 0, FVector::ZeroVector, FVector(10, 24, 14), FLinearColor(0.12f, 0.12f, 0.14f));
		Part(Rc, 1, FVector(-2, 0, 9), FVector(1, 1, 12), FLinearColor(0.7f, 0.7f, 0.72f));      // anten
		for (int32 s = -1; s <= 1; s += 2) Part(Rc, 1, FVector(0, s * 6.f, 7.5f), FVector(4, 4, 1), FLinearColor(0.3f, 0.3f, 0.32f));   // çubuk yuvası
		ThumbL0 = FVector(0, -6.f, 9.f); ThumbR0 = FVector(0, 6.f, 9.f);
		ThumbL = Part(Rc, 1, ThumbL0, FVector(3, 3, 4), Glove);
		ThumbR = Part(Rc, 1, ThumbR0, FVector(3, 3, 4), Glove);
	}
	// boyun + baş
	Part(Torso, 1, FVector(0, 0, 146.f), FVector(11, 11, 10), Skin);
	HeadZ = 160.f;
	Head->SetRelativeLocation(FVector(0, 0, HeadZ));
	FVector Hs(21, 19, 25);
	if (A.Face == TEXT("round")) Hs = FVector(22, 22, 22);
	else if (A.Face == TEXT("square")) Hs = FVector(21, 21, 23);
	else if (A.Face == TEXT("long")) Hs = FVector(19, 18, 27);
	else if (A.Face == TEXT("heart")) Hs = FVector(21, 20, 24);
	if (bF) Hs *= 0.95f;
	if (A.Face == TEXT("square")) Mat(Part(Head, 0, FVector(0, 0, 0), Hs * 0.92f, Skin), 0.55f, 0.f); else Mat(Part(Head, 2, FVector::ZeroVector, Hs, Skin), 0.55f, 0.f);
	// çene / yanak hacmi ve kulaklar (düz küre yerine insan başı silueti)
	Mat(Part(Head, 2, FVector(1.2f, 0, -6.5f), FVector(Hs.X * 0.82f, Hs.Y * (bF ? 0.74f : 0.8f), Hs.Z * 0.52f), Skin), 0.55f, 0.f);
	for (int32 s = -1; s <= 1; s += 2) Mat(Part(Head, 2, FVector(-0.5f, s * Hs.Y * 0.5f, 0.5f), FVector(3.5f, 2.2f, 6.f), Skin * 0.96f), 0.55f, 0.f);
	const float Fx = Hs.X * 0.5f - 0.8f;
	// gözler
	FVector Es(3, 3, 3);
	if (A.Eyes == TEXT("narrow")) Es = FVector(3, 3.4f, 1.5f); else if (A.Eyes == TEXT("wide")) Es = FVector(3.6f, 3.6f, 3.6f); else if (A.Eyes == TEXT("almond")) Es = FVector(3, 3.4f, 2.2f);
	for (int32 s = -1; s <= 1; s += 2)
	{
		Part(Head, 2, FVector(Fx - 0.6f, s * 4.2f, 2.f), Es, FLinearColor(0.95f, 0.95f, 0.95f));
		Part(Head, 2, FVector(Fx + 0.4f, s * 4.2f, 2.f), Es * 0.6f, EyeC);
		const float Bt = A.BrowW == TEXT("thin") ? 0.7f : A.BrowW == TEXT("thick") ? 1.8f : 1.2f;
		const float Ang = A.Brows == TEXT("angled") ? s * -14.f : A.Brows == TEXT("arched") ? s * -6.f : A.Brows == TEXT("soft") ? s * 4.f : 0.f;
		Part(Head, 0, FVector(Fx, s * 4.2f, 5.6f), FVector(1, 5.5f, Bt), Hair, FRotator(0, 0, Ang));
	}
	// burun
	if (A.Nose == TEXT("small")) Part(Head, 2, FVector(Fx + 0.6f, 0, -0.5f), FVector(2.4f, 2.4f, 2.4f), Skin * 0.95f);
	else if (A.Nose == TEXT("wide")) Part(Head, 2, FVector(Fx + 0.6f, 0, -0.8f), FVector(3, 5, 3), Skin * 0.95f);
	else Part(Head, 3, FVector(Fx + 1.f, 0, -0.5f), A.Nose == TEXT("pointed") ? FVector(3, 3, 6) : FVector(3, 3, 4.5f), Skin * 0.95f, FRotator(-90.f, 0, 0));
	// ağız
	const FLinearColor Lip(0.62f, 0.3f, 0.3f);
	if (A.Mouth == TEXT("grin")) Part(Head, 0, FVector(Fx - 0.3f, 0, -5.5f), FVector(1, 7, 1.6f), FLinearColor(0.95f, 0.95f, 0.95f));
	else if (A.Mouth == TEXT("smile")) { for (int32 s = -1; s <= 1; s += 2) Part(Head, 0, FVector(Fx - 0.3f, s * 1.6f, -5.4f), FVector(1, 3.6f, 0.9f), Lip, FRotator(0, 0, s * 12.f)); }
	else Part(Head, 0, FVector(Fx - 0.3f, 0, -5.5f), FVector(1, A.Mouth == TEXT("serious") ? 5.f : 6.f, A.Mouth == TEXT("serious") ? 0.6f : 0.9f), Lip);
	// sakal (yalnız erkek)
	if (!bF && A.Beard != TEXT("none") && !A.Beard.IsEmpty())
	{
		const FLinearColor Bc = Hair * 0.9f;
		if (A.Beard == TEXT("mustache")) Part(Head, 0, FVector(Fx, 0, -3.6f), FVector(1.5f, 7, 1.6f), Bc);
		else if (A.Beard == TEXT("goatee")) Part(Head, 0, FVector(Fx - 1.f, 0, -9.f), FVector(2.5f, 4, 5), Bc);
		else if (A.Beard == TEXT("full")) Part(Head, 2, FVector(1.f, 0, -6.f), FVector(Hs.X * 1.02f, Hs.Y * 1.02f, 14.f), Bc);
		else Part(Head, 2, FVector(0.5f, 0, -5.f), FVector(Hs.X * 1.005f, Hs.Y * 1.005f, 12.f), FMath::Lerp(Skin, Bc, 0.35f));   // stubble
	}
	if (A.bFreckles) for (int32 k = 0; k < 6; ++k) Part(Head, 2, FVector(Fx - 0.2f, (k % 2 ? 1.f : -1.f) * (5.5f + (k / 2) * 1.1f), -1.f - (k / 2) * 0.8f), FVector(0.7f, 0.7f, 0.7f), Skin * 0.7f);
	if (A.bMole) Part(Head, 2, FVector(Fx - 0.3f, 3.5f, -3.5f), FVector(0.9f, 0.9f, 0.9f), FLinearColor(0.2f, 0.12f, 0.08f));
	HeadGear(A, Hs, Fx, true);
}

void ASmvPilotFigure::HeadGear(const FSmvAvatar& A, const FVector& Hs, float Fx, bool bPrimHair)
{
	const FLinearColor Helmet = PfHex(A.Helmet, FLinearColor(0.12f, 0.2f, 0.3f));
	const FLinearColor Visor = PfHex(A.Visor, FLinearColor(0.9f, 0.75f, 0.2f));
	const FLinearColor Hair = PfHex(A.HairColor, FLinearColor(0.2f, 0.14f, 0.1f));
	const FLinearColor PatchC = PfHex(A.PatchColor, FLinearColor(0.95f, 0.75f, 0.25f));
	const FLinearColor Strap(0.09f, 0.1f, 0.11f), Metal(0.55f, 0.57f, 0.6f);
	if (A.Glasses != TEXT("none") && !A.Glasses.IsEmpty())
	{
		const FLinearColor Lens = A.Glasses == TEXT("sun") ? FLinearColor(0.03f, 0.03f, 0.04f) : FLinearColor(0.25f, 0.2f, 0.1f);
		for (int32 s = -1; s <= 1; s += 2) Part(Head, 1, FVector(Fx + 0.8f, s * 4.2f, 2.f), FVector(5.5f, 5.5f, 0.8f), Lens, FRotator(90.f, 0, 0));
		Part(Head, 0, FVector(Fx + 0.8f, 0, 2.5f), FVector(0.6f, 2.f, 0.6f), FLinearColor(0.7f, 0.6f, 0.3f));
	}
	// saç (kasklıyken kask örter)
	const bool bHelmet = A.Headgear == TEXT("helmet");
	if (bPrimHair && !bHelmet && A.Hair != TEXT("bald"))
	{
		const FString& H = A.Hair;
		if (H == TEXT("bob")) Part(Head, 2, FVector(-1.5f, 0, 2.f), FVector(Hs.X + 3, Hs.Y + 3, Hs.Z - 2), Hair);
		else Part(Head, 2, FVector(-1.5f, 0, H == TEXT("buzz") ? 4.f : 5.5f), FVector(Hs.X + (H == TEXT("buzz") ? 0.6f : 2.f), Hs.Y + (H == TEXT("buzz") ? 0.6f : 2.f), Hs.Z * 0.62f), Hair);
		if (H == TEXT("spiky")) for (int32 k = 0; k < 7; ++k) Part(Head, 3, FVector(-4.f + (k % 3) * 4.f, -6.f + k * 2.f, 12.f), FVector(4, 4, 6), Hair, FRotator(0, 0, (k - 3) * 8.f));
		if (H == TEXT("sidepart")) Part(Head, 0, FVector(2.f, -6.f, 10.f), FVector(10, 6, 3), Hair, FRotator(0, 0, -15.f));
		if (H == TEXT("curly")) for (int32 k = 0; k < 9; ++k) Part(Head, 2, FVector(-5.f + (k % 3) * 5.f, -7.f + (k / 3) * 7.f, 10.f), FVector(7, 7, 7), Hair);
		if (H == TEXT("mohawk")) Part(Head, 0, FVector(-1.f, 0, 13.f), FVector(18, 3, 9), Hair);
		if (H == TEXT("long")) Part(Head, 0, FVector(-8.f, 0, -8.f), FVector(6, Hs.Y + 1, 26), Hair);
		if (H == TEXT("ponytail")) Part(Head, 1, FVector(-13.f, 0, -4.f), FVector(5, 5, 18), Hair, FRotator(-25.f, 0, 0));
		if (H == TEXT("bun")) Part(Head, 2, FVector(-10.f, 0, 8.f), FVector(10, 10, 10), Hair);
	}
	if (bHelmet)
	{
		// pilot kaskı: parlak kabuk, kenar bandı, kulaklık çanakları, vizör rayları, NVG yuvası, çene kayışı; vizör (ayna: metalik, pürüzsüz)
		Mat(Part(Head, 2, FVector(-1.f, 0, 4.f), FVector(Hs.X + 7, Hs.Y + 6.5f, Hs.Z + 4), Helmet), 0.22f, 0.15f);
		Mat(Part(Head, 1, FVector(-1.5f, 0, -1.5f), FVector(Hs.X + 6.6f, Hs.Y + 6.2f, 1.6f), Helmet * 0.45f), 0.4f, 0.f, FLinearColor::Black, FRotator(-8.f, 0, 0));
		for (int32 s = -1; s <= 1; s += 2)
		{
			Mat(Part(Head, 1, FVector(-0.5f, s * (Hs.Y * 0.5f + 3.2f), -1.f), FVector(9.5f, 9.5f, 3.f), Helmet * 0.6f, FRotator(0, 0, 90.f)), 0.35f, 0.1f);
			Mat(Part(Head, 0, FVector(Fx - 2.f, s * (Hs.Y * 0.5f + 2.6f), 6.f), FVector(6.f, 1.2f, 2.2f), Metal), 0.3f, 0.85f, FLinearColor::Black, FRotator(-20.f, 0, 0));
		}
		Mat(Part(Head, 0, FVector(Hs.X * 0.5f + 2.f, 0, 12.5f), FVector(3.f, 5.f, 3.5f), Strap), 0.35f, 0.6f);   // NVG yuvası
		if (!A.bVisorOpen) Part(Head, 0, FVector(1.f, 0, -12.5f), FVector(4.f, Hs.Y * 0.7f, 1.2f), Strap);       // çene kayışı
		const FLinearColor Vc = A.bMirror ? FMath::Lerp(Visor, FLinearColor::White, 0.15f) : Visor;
		if (A.bVisorOpen) Mat(Part(Head, 2, FVector(4.f, 0, 13.f), FVector(11, Hs.Y + 3.5f, 6.5f), Vc, FRotator(-25.f, 0, 0)), 0.06f, A.bMirror ? 0.85f : 0.3f);
		else Mat(Part(Head, 2, FVector(Fx - 0.5f, 0, 2.f), FVector(10.5f, Hs.Y + 3.f, 12.5f), Vc), 0.05f, A.bMirror ? 0.85f : 0.3f, Vc * 0.04f);
		if (A.Accessory == TEXT("crest")) Part(Head, 0, FVector(-1.f, 0, 16.f), FVector(16, 2, 4), PatchC);
		if (A.Accessory == TEXT("stripe")) Part(Head, 0, FVector(-1.f, 0, 15.5f), FVector(26, 3, 1), PatchC);
		if (A.Accessory == TEXT("antenna")) { Part(Head, 1, FVector(-4.f, 9.f, 18.f), FVector(1, 1, 14), FLinearColor(0.75f, 0.75f, 0.78f)); Part(Head, 2, FVector(-4.f, 9.f, 25.f), FVector(2.4f, 2.4f, 2.4f), FLinearColor(0.95f, 0.15f, 0.15f)); }
	}
	else if (A.Headgear == TEXT("cap"))
	{
		// şapka: başın tepesine oturan kubbe + öne eğik siper (eski silindir alına / yüze iniyordu — kullanıcı, 2026-10-28)
		Mat(Part(Head, 2, FVector(-1.5f, 0, 8.5f), FVector(Hs.X + 3.f, Hs.Y + 3.f, 13.f), Helmet), 0.75f, 0.f);
		Mat(Part(Head, 1, FVector(-1.5f, 0, 3.2f), FVector(Hs.X + 3.2f, Hs.Y + 3.2f, 1.6f), Helmet * 0.8f), 0.75f, 0.f);
		Mat(Part(Head, 0, FVector(Fx + 4.5f, 0, 3.6f), FVector(11.f, 16.5f, 0.9f), Helmet * 0.9f, FRotator(-10.f, 0, 0)), 0.7f, 0.f);
	}
	else if (A.Headgear == TEXT("headset"))
	{
		Part(Head, 0, FVector(-1.f, 0, 13.f), FVector(3, Hs.Y + 4, 2), FLinearColor(0.1f, 0.1f, 0.12f));
		for (int32 s = -1; s <= 1; s += 2) Part(Head, 1, FVector(0, s * (Hs.Y * 0.5f + 1.5f), 0), FVector(7, 7, 3), FLinearColor(0.1f, 0.1f, 0.12f), FRotator(0, 0, 90.f));
	}
	if (A.Accessory == TEXT("mic") || A.Headgear == TEXT("headset"))
		Part(Head, 1, FVector(Fx - 2.f, Hs.Y * 0.5f - 1.f, -4.f), FVector(1, 1, 12), FLinearColor(0.12f, 0.12f, 0.14f), FRotator(0, 0, 60.f));
}

bool ASmvPilotFigure::BuildMetaHuman(const FSmvAvatar& A, bool bWithRc)
{
	FSmvMetaPilotParts P;
	if (!FSmvMetaPilot::Available() || !FSmvMetaPilot::Resolve(A, P) || !P.Body || !P.Face) return false;
	MhBody = NewObject<UPoseableMeshComponent>(this);
	MhBody->SetupAttachment(Root);
	MhBody->SetSkinnedAssetAndUpdate(P.Body);
	MhBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MhBody->RegisterComponent();
	// bileşen uzayında ileri yön: ayak bileğinden parmak köküne (MetaHuman gövdesi +Y'ye bakar); aktörün +X'ine çevrilir
	const FName FootL(TEXT("foot_l")), BallL(TEXT("ball_l"));
	FVector F = FVector(0.0, 1.0, 0.0);
	if (MhBody->GetBoneIndex(FootL) != INDEX_NONE && MhBody->GetBoneIndex(BallL) != INDEX_NONE)
	{
		FVector D = MhBody->GetBoneLocationByName(BallL, EBoneSpaces::ComponentSpace) - MhBody->GetBoneLocationByName(FootL, EBoneSpaces::ComponentSpace);
		D.Z = 0.0;
		if (D.Size() > 1.0) F = D.GetSafeNormal();
	}
	MhBody->SetRelativeRotation(FRotator(0.0, -FMath::RadiansToDegrees(FMath::Atan2(F.Y, F.X)), 0.0));
	// vücut tipi: kit'te cinsiyet başına tek MetaHuman gövdesi var → yatayda ölçek (ince / orta / geniş; "vücut tipi hiç değişmiyor" —
	// kullanıcı, 2026-10-28). Giysiler gövdeye bağlı (birlikte ölçeklenir); yüz ters ölçekle düzeltilir.
	const float Bs = A.Build == TEXT("slim") ? 0.9f : A.Build == TEXT("broad") ? 1.13f : 1.f;
	MhBody->SetRelativeScale3D(FVector(Bs, Bs, 1.f));
	// Fab MetaHuman giysi paketi varsa parçalar gövdeye lider pozla giydirilir (gövde kendi deri malzemesinde kalır: eller, boyun);
	// yoksa gövde ağının tüm yuvaları kıyafet rengi (tek parça uçuş tulumu) + temel şekil katmanları
	const FLinearColor Suit = PfHex(A.Suit, FLinearColor(0.12f, 0.2f, 0.3f));
	bool bOutfitCfg = true;
	if (GConfig) GConfig->GetBool(TEXT("/Script/Simumav.SmvPilot"), TEXT("FabOutfit"), bOutfitCfg, GGameIni);
	const TArray<USkeletalMesh*> Garments = bOutfitCfg ? PfOutfitFor(A) : TArray<USkeletalMesh*>();
	for (USkeletalMesh* Gm : Garments)
	{
		USkeletalMeshComponent* Gc = NewObject<USkeletalMeshComponent>(this);
		Gc->SetupAttachment(MhBody);
		Gc->SetSkinnedAssetAndUpdate(Gm);
		Gc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Gc->RegisterComponent();
		Gc->SetLeaderPoseComponent(MhBody);
		// Fab giysi FBX'leri dokusuz geliyor (malzemeler beyaz): yuvalar tulum rengi tonlarıyla boyanır (kumaş / teçhizat / kayış)
		bool bTint = true;
		if (GConfig) GConfig->GetBool(TEXT("/Script/Simumav.SmvPilot"), TEXT("TintOutfit"), bTint, GGameIni);
		if (UMaterialInterface* Sm = bTint ? SmvSolidMat(BaseMat) : nullptr)
		{
			const FLinearColor Tones[] = { Suit, Suit * 0.55f, FLinearColor(0.05f, 0.055f, 0.05f), FMath::Lerp(Suit, FLinearColor(0.35f, 0.33f, 0.27f), 0.6f) };
			for (int32 i = 0; i < Gc->GetNumMaterials(); ++i)
			{
				UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Sm, this);
				SmvSetSolidColor(M, Tones[i % UE_ARRAY_COUNT(Tones)]);
				M->SetScalarParameterValue(TEXT("Roughness"), 0.8f);
				Gc->SetMaterial(i, M);
			}
		}
		MhGrooms.Add(Gc);
	}
	if (Garments.Num())
	{
		FString Names;
		for (USkeletalMesh* Gm : Garments) Names += (Names.IsEmpty() ? TEXT("") : TEXT(", ")) + Gm->GetName();
		UE_LOG(LogSimumav, Log, TEXT("MetaHuman pilot giysisi (%s / %s): %s"), *A.Body, *A.Outfit, *Names);
	}
	// tam boy giysi (≥ 120 cm yüksek parça) gövdeyi örter → gövde kendi malzemesinde; yalnız kemer / teçhizat gibi kısmi parçalar varsa
	// gövde tulum renginde kalır ve temel şekil katmanları (yelek, bot) da kurulur
	// parçaların BİRLEŞİK yüksekliği ölçülür: Techwear üst + alt + ayakkabı ayrı parçalar (her biri < 120 cm) — eski parça başına eşik
	// tam kıyafeti kısmi sayıp üstüne temel şekil yelek ve KUTU botlar kuruyordu ("ayakkabılar kare" — kullanıcı, 2026-10-28)
	bool bFullOutfit = false, bFeet = false;
	{
		FBox U(ForceInit);
		for (USkeletalMesh* Gm : Garments) if (Gm) U += Gm->GetBounds().GetBox();
		bFullOutfit = U.IsValid && U.GetSize().Z > 110.0;
		bFeet = U.IsValid && U.Min.Z < 14.0;
	}
	if (UMaterialInterface* Sm = SmvSolidMat(BaseMat))
	{
		for (int32 i = 0; i < MhBody->GetNumMaterials(); ++i)
		{
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Sm, this);
			// tam boy giysi altında gövde yalnız ellerde / boyunda görünür → ten rengi
			SmvSetSolidColor(M, bFullOutfit ? PfSkin(A.Skin) : A.Outfit == TEXT("casual") ? FMath::Lerp(Suit, FLinearColor::White, 0.3f) : Suit);
			M->SetScalarParameterValue(TEXT("Roughness"), 0.72f);
			MhBody->SetMaterial(i, M);
		}
	}
	// yüz: gövdeyi lider poz olarak izler (baş/boyun kemikleri ortak)
	MhFace = NewObject<USkeletalMeshComponent>(this);
	MhFace->SetupAttachment(MhBody);
	MhFace->SetSkinnedAssetAndUpdate(P.Face);
	MhFace->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MhFace->RegisterComponent();
	MhFace->SetLeaderPoseComponent(MhBody);
	MhFace->SetRelativeScale3D(FVector(1.f / Bs, 1.f / Bs, 1.f));
	// tüyler: saç / kaş / sakal / kirpik (yüze bağlı groom; saç rengi melanin/kızıllık parametreleriyle)
	const FLinearColor Hc = PfHex(A.HairColor, FLinearColor(0.2f, 0.14f, 0.1f));
	const float Lum = 0.3f * Hc.R + 0.59f * Hc.G + 0.11f * Hc.B;
	for (const TPair<UObject*, UObject*>& G : P.Grooms)
	{
		UGroomAsset* Ga = Cast<UGroomAsset>(G.Key);
		if (!Ga) continue;
		UGroomComponent* Gc = NewObject<UGroomComponent>(this);
		Gc->SetupAttachment(MhFace);
		Gc->SetGroomAsset(Ga, Cast<UGroomBindingAsset>(G.Value));
		// PSO ön derlemesi kayıttan ÖNCE (yoksa CreateSceneProxy'de "!IsComponentPSOPrecachingEnabled() || bPSOPrecacheCalled" ensure'ü — her
		// menü açılışında çökme raporu klasörü üretiyordu)
		Gc->PrecachePSOs();
		Gc->RegisterComponent();
		for (int32 i = 0; i < Gc->GetNumMaterials(); ++i)
		{
			if (UMaterialInterface* Gm = Gc->GetMaterial(i))
			{
				UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(Gm, this);
				D->SetScalarParameterValue(TEXT("hairMelanin"), FMath::Clamp(1.f - Lum * 1.8f, 0.f, 1.f));
				D->SetScalarParameterValue(TEXT("hairRedness"), FMath::Clamp((Hc.R - Hc.G) * 2.5f, 0.f, 1.f));
				Gc->SetMaterial(i, D);
			}
		}
		MhGrooms.Add(Gc);
	}
	PoseMhArms(bWithRc);
	// uçuş kıyafeti katmanları (Fab giysi paketi yoksa; MCC'de uygun giysi yok: mayo/şort): göğüs takımı yeleği, kemer, kalça/uyluk örtüsü,
	// botlar — gövde kemiklerinin (aktör uzayı) konumundan; gövde pozlanmadığı için sabit
	if (!bFullOutfit)
	{
		const FTransform Rel = MhBody->GetRelativeTransform();
		auto B = [this, &Rel](const TCHAR* N, FVector& Out) { if (MhBody->GetBoneIndex(FName(N)) == INDEX_NONE) return false; Out = Rel.TransformPosition(MhBody->GetBoneLocationByName(FName(N), EBoneSpaces::ComponentSpace)); return true; };
		const FLinearColor Vest(0.035f, 0.04f, 0.035f), Strap(0.02f, 0.02f, 0.022f), Boot(0.03f, 0.03f, 0.035f), Collar = Suit * 0.8f;
		FVector Sp3, Sp5, Pel, ClL, ClR, ThL, ThR, FtL, FtR, CaL, CaR, Nk;
		if (B(TEXT("spine_03"), Sp3) && B(TEXT("spine_05"), Sp5) && B(TEXT("clavicle_l"), ClL) && B(TEXT("clavicle_r"), ClR))
		{
			// göğüs takımı yeleği (koyu, gövdeden taşar) + omuz kayışları + göğüs kayışı + arma
			const float Wd = (float)FVector::Dist(ClL, ClR) * 1.05f + 10.f;
			const float H = (float)(Sp5.Z - Sp3.Z) + 16.f;
			const FVector C = FVector(0.5 * (Sp3.X + Sp5.X) + 3.0, 0.5 * (Sp3.Y + Sp5.Y), 0.5 * (Sp3.Z + Sp5.Z) - 2.0);
			Mat(Part(Torso, 0, C, FVector(30.f, Wd, H), Vest), 0.8f, 0.f);
			for (int32 s2 = -1; s2 <= 1; s2 += 2) Mat(Part(Torso, 0, C + FVector(15.2, s2 * Wd * 0.24, H * 0.1), FVector(1.2f, 4.f, H * 0.9f), Strap), 0.5f, 0.f);
			Mat(Part(Torso, 0, C + FVector(15.4, 0.0, -H * 0.15), FVector(1.f, Wd * 0.7f, 3.2f), Strap), 0.5f, 0.f);
			const FLinearColor PatchC = PfHex(A.PatchColor, FLinearColor(0.95f, 0.75f, 0.25f));
			if (A.Patch != TEXT("none") && !A.Patch.IsEmpty()) Mat(Part(Torso, 0, C + FVector(15.6, Wd * 0.26, H * 0.28), FVector(1.f, 7.f, 6.f), PatchC), 0.5f, 0.f, PatchC * 0.15f);
		}
		if (B(TEXT("neck_01"), Nk)) Mat(Part(Torso, 1, Nk + FVector(0.0, 0.0, -3.0), FVector(17.f, 17.f, 7.f), Collar), 0.7f, 0.f);   // yaka (yüz ağının boyun derisini örter)
		if (B(TEXT("pelvis"), Pel) && B(TEXT("thigh_l"), ThL) && B(TEXT("thigh_r"), ThR))
		{
			const float Hw = (float)FVector::Dist(ThL, ThR) + 22.f;
			Mat(Part(Torso, 1, Pel + FVector(0.0, 0.0, 8.0), FVector(25.f, Hw, 6.f), Strap), 0.4f, 0.f);                // kemer
			Mat(Part(Torso, 0, Pel + FVector(13.0, 0.0, 8.0), FVector(1.5f, 6.5f, 4.2f), FLinearColor(0.55f, 0.57f, 0.6f)), 0.25f, 0.9f);
			Mat(Part(Torso, 1, Pel + FVector(0.0, 0.0, -3.0), FVector(23.f, Hw - 4.f, 15.f), Suit * 0.7f), 0.75f, 0.f);  // kalça örtüsü
		}
		if (!bFeet && B(TEXT("foot_l"), FtL) && B(TEXT("foot_r"), FtR) && B(TEXT("calf_l"), CaL) && B(TEXT("calf_r"), CaR))
		{
			for (const FVector& Ft : { FtL, FtR }) Mat(Part(Torso, 0, FVector(Ft.X + 8.0, Ft.Y, 5.5), FVector(31.f, 12.f, 11.f), Boot), 0.45f, 0.f);
			for (const FVector& Ca : { CaL, CaR }) Mat(Part(Torso, 1, FVector(Ca.X, Ca.Y, 15.0), FVector(13.f, 13.f, 20.f), Boot), 0.45f, 0.f);
		}
	}
	// baş: kemik konumu (aktör uzayı) → kask/vizör düğümü; göz yüksekliği
	const FName HeadB(TEXT("head")), NeckB(TEXT("neck_01"));
	MhHeadRef = MhBody->GetBoneTransformByName(HeadB, EBoneSpaces::ComponentSpace).GetRotation();
	MhNeckRef = MhBody->GetBoneIndex(NeckB) != INDEX_NONE ? MhBody->GetBoneTransformByName(NeckB, EBoneSpaces::ComponentSpace).GetRotation() : FQuat::Identity;
	MhHeadLoc = MhBody->GetRelativeTransform().TransformPosition(MhBody->GetBoneLocationByName(HeadB, EBoneSpaces::ComponentSpace));
	MhHeadOff = FVector(FSmvPilotHeadCfg::Fwd(), 0.f, FSmvPilotHeadCfg::Up());
	HeadZ = (float)(MhHeadLoc.Z + MhHeadOff.Z + 1.f);
	Head->SetRelativeLocation(MhHeadLoc + MhHeadOff);
	const FVector Hs(21.f, 17.5f, 24.f);
	HeadGear(A, Hs, Hs.X * 0.5f - 0.8f, false);
	// RC kumanda: iki elin ortasında, biraz önde
	if (bWithRc)
	{
		const FName HandL(TEXT("hand_l")), HandR(TEXT("hand_r"));
		FVector Mid(28.0, 0.0, 108.0);
		if (MhBody->GetBoneIndex(HandL) != INDEX_NONE && MhBody->GetBoneIndex(HandR) != INDEX_NONE)
		{
			const FTransform Rel = MhBody->GetRelativeTransform();
			Mid = 0.5 * (Rel.TransformPosition(MhBody->GetBoneLocationByName(HandL, EBoneSpaces::ComponentSpace)) + Rel.TransformPosition(MhBody->GetBoneLocationByName(HandR, EBoneSpaces::ComponentSpace))) + FVector(5.0, 0.0, 3.0);
		}
		const FLinearColor Glove(0.07f, 0.07f, 0.08f);
		Rc->SetRelativeLocation(Mid);
		Rc->SetRelativeRotation(FRotator(35.f, 0, 0));
		Part(Rc, 0, FVector::ZeroVector, FVector(10, 24, 14), FLinearColor(0.12f, 0.12f, 0.14f));
		Part(Rc, 1, FVector(-2, 0, 9), FVector(1, 1, 12), FLinearColor(0.7f, 0.7f, 0.72f));
		for (int32 s = -1; s <= 1; s += 2) Part(Rc, 1, FVector(0, s * 6.f, 7.5f), FVector(4, 4, 1), FLinearColor(0.3f, 0.3f, 0.32f));
		ThumbL0 = FVector(0, -6.f, 9.f); ThumbR0 = FVector(0, 6.f, 9.f);
		ThumbL = Part(Rc, 1, ThumbL0, FVector(3, 3, 4), Glove);
		ThumbR = Part(Rc, 1, ThumbR0, FVector(3, 3, 4), Glove);
	}
	bMh = true;
	UE_LOG(LogSimumav, Log, TEXT("MetaHuman pilot: yüz %s, %d tüy, %s"), *P.FaceName, MhGrooms.Num(), bWithRc ? TEXT("RC duruşu") : TEXT("serbest duruş"));
	return true;
}

void ASmvPilotFigure::PoseMhArms(bool bWithRc)
{
	if (!MhBody) return;
	auto Has = [this](const TCHAR* B) { return MhBody->GetBoneIndex(FName(B)) != INDEX_NONE; };
	auto Loc = [this](const TCHAR* B) { return MhBody->GetBoneLocationByName(FName(B), EBoneSpaces::ComponentSpace); };
	if (!Has(TEXT("upperarm_l")) || !Has(TEXT("upperarm_r")) || !Has(TEXT("lowerarm_l")) || !Has(TEXT("hand_l"))) return;
	// bileşen uzayı eksenleri: ileri (ayaklar), yukarı +Z, sol (sol omuz yönü)
	FVector F = Has(TEXT("ball_l")) && Has(TEXT("foot_l")) ? Loc(TEXT("ball_l")) - Loc(TEXT("foot_l")) : FVector(0, 1, 0);
	F.Z = 0.0;
	F = F.GetSafeNormal();
	if (F.IsNearlyZero()) F = FVector(0, 1, 0);
	const FVector U(0, 0, 1);
	FVector Lf = Loc(TEXT("upperarm_l")) - Loc(TEXT("upperarm_r"));
	Lf -= F * FVector::DotProduct(Lf, F);
	Lf.Z = 0.0;
	Lf = Lf.GetSafeNormal();
	auto Aim = [this, &Has, &Loc](const TCHAR* Bone, const TCHAR* Child, const FVector& Dir)
	{
		if (!Has(Bone) || !Has(Child)) return;
		const FTransform Tb = MhBody->GetBoneTransformByName(FName(Bone), EBoneSpaces::ComponentSpace);
		const FVector Cur = (Loc(Child) - Tb.GetLocation()).GetSafeNormal();
		const FQuat Q = FQuat::FindBetweenNormals(Cur, Dir.GetSafeNormal());
		MhBody->SetBoneRotationByName(FName(Bone), (Q * Tb.GetRotation()).Rotator(), EBoneSpaces::ComponentSpace);
	};
	for (int32 k = 0; k < 2; ++k)
	{
		const bool bL = k == 0;
		const FVector S = bL ? Lf : -Lf;   // o kolun dış yanı
		const TCHAR* Up = bL ? TEXT("upperarm_l") : TEXT("upperarm_r");
		const TCHAR* Lo = bL ? TEXT("lowerarm_l") : TEXT("lowerarm_r");
		const TCHAR* Ha = bL ? TEXT("hand_l") : TEXT("hand_r");
		const TCHAR* Fi = bL ? TEXT("middle_metacarpal_l") : TEXT("middle_metacarpal_r");
		const TCHAR* Fi2 = bL ? TEXT("middle_01_l") : TEXT("middle_01_r");
		if (bWithRc)
		{
			// RC tutma: dirsekler gövdeye yakın, önkollar öne-içe ve hafif yukarı, eller kumandanın iki yanında
			Aim(Up, Lo, F * 0.32 + S * 0.10 - U * 0.94);
			Aim(Lo, Ha, F * 0.82 - S * 0.42 + U * 0.28);
			Aim(Ha, Has(Fi) ? Fi : Fi2, F * 0.78 - S * 0.22 + U * 0.5);
		}
		else
		{
			// serbest duruş: kollar gövde yanında, hafif dirsek bükümü
			Aim(Up, Lo, F * 0.06 + S * 0.2 - U * 0.98);
			Aim(Lo, Ha, F * 0.2 + S * 0.1 - U * 0.97);
			Aim(Ha, Has(Fi) ? Fi : Fi2, F * 0.12 + S * 0.04 - U * 0.99);
		}
	}
}

void ASmvPilotFigure::SetPose(const FVector2D& L, const FVector2D& R, const FVector& LookAt, bool bHasLook)
{
	Sl = L; Sr = R; Look = LookAt; bLook = bHasLook;
}

void ASmvPilotFigure::Tick(float Dt)
{
	Super::Tick(Dt);
	T += Dt;
	// nefes + ağırlık aktarma (temel şekil figürü; MetaHuman'da gövde kemikleri pozludur)
	if (!bMh)
	{
		Torso->SetRelativeScale3D(FVector(1.f, 1.f, 1.f + 0.008f * FMath::Sin(T * 1.6f)));
		Torso->SetRelativeRotation(FRotator(0.f, 0.f, 1.2f * FMath::Sin(T * 0.35f)));
	}
	if (ThumbL) ThumbL->SetRelativeLocation(ThumbL0 + FVector(-Sl.Y * 1.6f, Sl.X * 1.6f, 0));
	if (ThumbR) ThumbR->SetRelativeLocation(ThumbR0 + FVector(-Sr.Y * 1.6f, Sr.X * 1.6f, 0));
	if (bLook)
	{
		const FVector From = Head->GetComponentLocation();
		const FRotator Want = (Look - From).Rotation();
		const FRotator Body = GetActorRotation();
		FRotator Rel(FMath::Clamp(FRotator::NormalizeAxis(Want.Pitch), -35.f, 45.f), FMath::Clamp(FRotator::NormalizeAxis(Want.Yaw - Body.Yaw), -70.f, 70.f), 0.f);
		HeadRel = FMath::RInterpTo(HeadRel, Rel, Dt, 4.f);
		if (!bMh) Head->SetRelativeRotation(HeadRel);
	}
	if (bMh && MhBody)
	{
		// baş kemiği aktör uzayındaki bakış dönüşüyle (boyun %40); kask düğümü aynı dönüşle baş merkezinde
		const FQuat Qa = HeadRel.Quaternion();
		const FQuat Cr = MhBody->GetRelativeRotation().Quaternion();
		const FQuat Qc = Cr.Inverse() * Qa * Cr;
		if (MhBody->GetBoneIndex(FName(TEXT("neck_01"))) != INDEX_NONE)
			MhBody->SetBoneRotationByName(FName(TEXT("neck_01")), (FQuat::Slerp(FQuat::Identity, Qc, 0.4f) * MhNeckRef).Rotator(), EBoneSpaces::ComponentSpace);
		MhBody->SetBoneRotationByName(FName(TEXT("head")), (Qc * MhHeadRef).Rotator(), EBoneSpaces::ComponentSpace);
		const FVector Hb = MhBody->GetRelativeTransform().TransformPosition(MhBody->GetBoneLocationByName(FName(TEXT("head")), EBoneSpaces::ComponentSpace));
		Head->SetRelativeLocationAndRotation(Hb + Qa.RotateVector(MhHeadOff), HeadRel);
	}
}
