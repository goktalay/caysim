#include "SmvPilotFigure.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	FLinearColor PfHex(const FString& H, const FLinearColor& Def)
	{
		return H.Len() == 7 ? FLinearColor::FromSRGBColor(FColor::FromHex(H)) : Def;
	}
	FLinearColor PfSkin(const FString& S)
	{
		static const TCHAR* Sk[] = { TEXT("#ffdbac"), TEXT("#f7e0c3"), TEXT("#f1c9a5"), TEXT("#e8b58a"), TEXT("#e0ac69"), TEXT("#c68642"), TEXT("#8d5524"), TEXT("#6b4226") };
		const int32 I = FMath::Clamp(FCString::Atoi(*S.Mid(1)) - 1, 0, 7);
		return PfHex(Sk[I], FLinearColor(0.9f, 0.7f, 0.55f));
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
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BaseMat, this);
		M->SetVectorParameterValue(TEXT("Color"), Color);
		C->SetMaterial(0, M);
	}
	C->SetRelativeTransform(FTransform(Rot, Pos, SizeCm / 100.f));
	C->RegisterComponent();
	Parts.Add(C);
	return C;
}

void ASmvPilotFigure::Clear()
{
	for (TObjectPtr<UStaticMeshComponent>& P : Parts) if (P) P->DestroyComponent();
	Parts.Reset();
	ThumbL = ThumbR = nullptr;
}

void ASmvPilotFigure::Build(const FSmvAvatar& A, bool bWithRc)
{
	Clear();
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
	// bacaklar + bot
	for (float Y : { -10.f, 10.f })
	{
		Part(Root, 1, FVector(0, Y * (bF ? 0.9f : 1.f), 42.f), FVector(15, 15, 82), Legs);
		Part(Root, 0, FVector(4, Y * (bF ? 0.9f : 1.f), 5.f), FVector(26, 12, 10), Boot);
	}
	// gövde (nefes için ayrı düğüm)
	Torso->SetRelativeLocation(FVector::ZeroVector);
	Part(Torso, 0, FVector(0, 0, 88.f), FVector(22, Wb * 0.85f + (bF ? 4.f : 0.f), 16), Legs);
	Part(Torso, 1, FVector(0, 0, 112.f), FVector(24, Wb, 52), Top);
	if (A.Outfit == TEXT("jacket"))
	{
		Part(Torso, 0, FVector(10, 0, 138.f), FVector(6, Wb * 0.55f, 6), FMath::Lerp(Top, FLinearColor::White, 0.4f));   // yaka
		Part(Torso, 0, FVector(12.2f, 0, 112.f), FVector(1, 2, 44), FLinearColor(0.1f, 0.1f, 0.12f));                    // fermuar
	}
	if (A.Patch != TEXT("none") && !A.Patch.IsEmpty()) Part(Torso, 0, FVector(12.5f, Wb * 0.22f, 126.f), FVector(1, 6, 6), PatchC);
	// omuzlar, kollar, eller
	const float Sy = Wb * 0.5f + 3.f;
	for (int32 s = -1; s <= 1; s += 2)
	{
		Part(Torso, 2, FVector(0, s * Sy, 134.f), FVector(16, 16, 16), Sleeve);
		if (bWithRc)
		{
			Part(Torso, 1, FVector(4, s * (Sy + 1.f), 120.f), FVector(11, 11, 28), Sleeve, FRotator(-30.f, 0, 0));
			Part(Torso, 1, FVector(16, s * (Sy - 6.f), 106.f), FVector(10, 10, 26), A.Outfit == TEXT("casual") ? Skin : Sleeve, FRotator(-80.f, s * -15.f, 0));
			Part(Torso, 2, FVector(27, s * 9.f, 106.f), FVector(9, 9, 9), Skin);
		}
		else
		{
			Part(Torso, 1, FVector(0, s * (Sy + 2.f), 118.f), FVector(11, 11, 30), Sleeve);
			Part(Torso, 1, FVector(1, s * (Sy + 2.f), 92.f), FVector(10, 10, 26), A.Outfit == TEXT("casual") ? Skin : Sleeve);
			Part(Torso, 2, FVector(1, s * (Sy + 2.f), 76.f), FVector(9, 9, 10), Skin);
		}
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
		ThumbL = Part(Rc, 1, ThumbL0, FVector(3, 3, 4), Skin);
		ThumbR = Part(Rc, 1, ThumbR0, FVector(3, 3, 4), Skin);
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
	if (A.Face == TEXT("square")) Part(Head, 0, FVector(0, 0, 0), Hs * 0.92f, Skin); else Part(Head, 2, FVector::ZeroVector, Hs, Skin);
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
	if (A.Glasses != TEXT("none") && !A.Glasses.IsEmpty())
	{
		const FLinearColor Lens = A.Glasses == TEXT("sun") ? FLinearColor(0.03f, 0.03f, 0.04f) : FLinearColor(0.25f, 0.2f, 0.1f);
		for (int32 s = -1; s <= 1; s += 2) Part(Head, 1, FVector(Fx + 0.8f, s * 4.2f, 2.f), FVector(5.5f, 5.5f, 0.8f), Lens, FRotator(90.f, 0, 0));
		Part(Head, 0, FVector(Fx + 0.8f, 0, 2.5f), FVector(0.6f, 2.f, 0.6f), FLinearColor(0.7f, 0.6f, 0.3f));
	}
	// saç (kasklıyken kask örter)
	const bool bHelmet = A.Headgear == TEXT("helmet") || A.Headgear.IsEmpty();
	if (!bHelmet && A.Hair != TEXT("bald"))
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
		Part(Head, 2, FVector(-0.5f, 0, 3.f), FVector(Hs.X + 5, Hs.Y + 5, Hs.Z + 3), Helmet);
		const FLinearColor Vc = A.bMirror ? FMath::Lerp(Visor, FLinearColor::White, 0.25f) : Visor;
		if (A.bVisorOpen) Part(Head, 2, FVector(5.f, 0, 12.f), FVector(10, Hs.Y + 2, 6), Vc, FRotator(-25.f, 0, 0));
		else Part(Head, 2, FVector(Fx - 1.5f, 0, 1.5f), FVector(9, Hs.Y + 1, 11), Vc);
		if (A.Accessory == TEXT("crest")) Part(Head, 0, FVector(-1.f, 0, 16.f), FVector(16, 2, 4), PatchC);
		if (A.Accessory == TEXT("stripe")) Part(Head, 0, FVector(-1.f, 0, 15.5f), FVector(26, 3, 1), PatchC);
		if (A.Accessory == TEXT("antenna")) { Part(Head, 1, FVector(-4.f, 9.f, 18.f), FVector(1, 1, 14), FLinearColor(0.75f, 0.75f, 0.78f)); Part(Head, 2, FVector(-4.f, 9.f, 25.f), FVector(2.4f, 2.4f, 2.4f), FLinearColor(0.95f, 0.15f, 0.15f)); }
	}
	else if (A.Headgear == TEXT("cap"))
	{
		Part(Head, 1, FVector(-1.f, 0, 8.f), FVector(Hs.X + 2, Hs.Y + 2, 7), Helmet);
		Part(Head, 0, FVector(Fx + 3.f, 0, 5.f), FVector(10, 16, 1), Helmet);
	}
	else if (A.Headgear == TEXT("headset"))
	{
		Part(Head, 0, FVector(-1.f, 0, 13.f), FVector(3, Hs.Y + 4, 2), FLinearColor(0.1f, 0.1f, 0.12f));
		for (int32 s = -1; s <= 1; s += 2) Part(Head, 1, FVector(0, s * (Hs.Y * 0.5f + 1.5f), 0), FVector(7, 7, 3), FLinearColor(0.1f, 0.1f, 0.12f), FRotator(0, 0, 90.f));
	}
	if (A.Accessory == TEXT("mic") || A.Headgear == TEXT("headset"))
		Part(Head, 1, FVector(Fx - 2.f, Hs.Y * 0.5f - 1.f, -4.f), FVector(1, 1, 12), FLinearColor(0.12f, 0.12f, 0.14f), FRotator(0, 0, 60.f));
}

void ASmvPilotFigure::SetPose(const FVector2D& L, const FVector2D& R, const FVector& LookAt, bool bHasLook)
{
	Sl = L; Sr = R; Look = LookAt; bLook = bHasLook;
}

void ASmvPilotFigure::Tick(float Dt)
{
	Super::Tick(Dt);
	T += Dt;
	// nefes + ağırlık aktarma
	Torso->SetRelativeScale3D(FVector(1.f, 1.f, 1.f + 0.008f * FMath::Sin(T * 1.6f)));
	Torso->SetRelativeRotation(FRotator(0.f, 0.f, 1.2f * FMath::Sin(T * 0.35f)));
	if (ThumbL) ThumbL->SetRelativeLocation(ThumbL0 + FVector(-Sl.Y * 1.6f, Sl.X * 1.6f, 0));
	if (ThumbR) ThumbR->SetRelativeLocation(ThumbR0 + FVector(-Sr.Y * 1.6f, Sr.X * 1.6f, 0));
	if (bLook)
	{
		const FVector From = Head->GetComponentLocation();
		const FRotator Want = (Look - From).Rotation();
		const FRotator Body = GetActorRotation();
		FRotator Rel(FMath::Clamp(FRotator::NormalizeAxis(Want.Pitch), -35.f, 45.f), FMath::Clamp(FRotator::NormalizeAxis(Want.Yaw - Body.Yaw), -70.f, 70.f), 0.f);
		Head->SetRelativeRotation(FMath::RInterpTo(Head->GetRelativeRotation(), Rel, Dt, 4.f));
	}
}
