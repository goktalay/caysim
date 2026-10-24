// Hangar donanımının gövdede görünmesi (yol haritası "Hangar › Eklentiler / görünüm"): ilaçlama aparatı, paket bırakma tutucusu, paraşüt
// kabı, çıkartma/logo, kaplama. Parçalar /Engine/BasicShapes ağlarıyla, gövde uzayında (görünürlük ölçeği dahil) kurulur; uçuşta paraşüt
// açılır, yük bırakılınca kutu kaybolur, ilaçlama sırasında püskürtme sisi görünür. Hepsi YALNIZ görseldir (fizik ArduPilot SITL'de).
#include "SmvWorldActors.h"
#include "SimumavModule.h"
#include "SmvUiKit.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DecalComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	UTexture2D* SmvDecalTexture(const FString& File)
	{
		static TMap<FString, TStrongObjectPtr<UTexture2D>> Cache;
		if (File.IsEmpty()) return nullptr;
		if (TStrongObjectPtr<UTexture2D>* T = Cache.Find(File)) return T->Get();
		TArray<uint8> Bytes;
		UTexture2D* Tex = nullptr;
		if (FFileHelper::LoadFileToArray(Bytes, *File) && Bytes.Num() > 0 && Bytes.Num() < 8 * 1024 * 1024) Tex = FImageUtils::ImportBufferAsTexture2D(Bytes);
		Cache.Add(File, TStrongObjectPtr<UTexture2D>(Tex));
		if (!Tex) UE_LOG(LogSimumav, Warning, TEXT("Çıkartma okunamadı: %s"), *File);
		return Tex;
	}

	UMaterialInterface* SmvLoadMat(const TCHAR* Path)
	{
		const FString P(Path);
		const FString Pkg = P.Left(P.Find(TEXT(".")));
		if (!FPackageName::DoesPackageExist(Pkg)) return nullptr;
		return LoadObject<UMaterialInterface>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	FLinearColor SmvFinishTint(const FString& Finish, const FLinearColor& C)
	{
		return Finish == TEXT("carbon") ? FLinearColor(0.035f, 0.037f, 0.04f) : C;
	}
}

void ASmvDroneActor::ClearDress()
{
	for (TObjectPtr<UStaticMeshComponent>& C : DressParts) if (C) C->DestroyComponent();
	for (TObjectPtr<UDecalComponent>& D : DressDecals) if (D) D->DestroyComponent();
	DressParts.Reset();
	DressDecals.Reset();
	ChuteParts.Reset();
	SprayParts.Reset();
	PayloadBox = nullptr;
}

UStaticMeshComponent* ASmvDroneActor::AddDress(UStaticMesh* M, const FVector& PosM, const FVector& ScaleM, const FRotator& Rot, const FLinearColor& Color, float Rough, float Metal)
{
	if (!M || !Body) return nullptr;
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetStaticMesh(M);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetupAttachment(Body);
	C->RegisterComponent();
	C->SetRelativeLocationAndRotation(PosM * 100.f, Rot);
	C->SetRelativeScale3D(ScaleM);
	if (UMaterialInterface* Base = SmvSolidMat(BaseMaterial))
	{
		UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(Base, C);
		SmvSetSolidColor(D, Color);
		D->SetScalarParameterValue(TEXT("Roughness"), Rough);
		D->SetScalarParameterValue(TEXT("Metallic"), Metal);
		C->SetMaterial(0, D);
	}
	DressParts.Add(C);
	return C;
}

void ASmvDroneActor::ApplyEquipment(const FSmvEquipment& E, const FLinearColor& InBodyColor)
{
	ClearDress();
	bChuteOut = false;
	if (!Body) return;
	const float S = FMath::Max(0.5f, VisScale);
	// gövde uzayında yarıçap ve alt yüz (m)
	const float R = FMath::Clamp(VisRadiusM / S, 0.08f, 6.f);
	const float Bot = FMath::Min(-0.04f, GroundOffsetM / S);
	const FLinearColor Accent = E.Color2.Len() == 7 ? FLinearColor::FromSRGBColor(FColor::FromHex(E.Color2)) : FLinearColor(0.08f, 0.08f, 0.09f);
	const FLinearColor White(0.82f, 0.84f, 0.86f), Steel(0.55f, 0.57f, 0.6f), Dark(0.05f, 0.05f, 0.06f);
	const bool bPlane = SmvType == TEXT("plane") || SmvType == TEXT("vtol") || SmvType == TEXT("interceptor");

	// ---- kaplama: gövde malzemelerinin pürüzlülük / metal değeri ----
	{
		float Rough = 0.32f, Metal = 0.08f;
		if (E.Finish == TEXT("matte")) { Rough = 0.82f; Metal = 0.0f; }
		else if (E.Finish == TEXT("metal")) { Rough = 0.28f; Metal = 0.9f; }
		else if (E.Finish == TEXT("carbon")) { Rough = 0.42f; Metal = 0.25f; }
		TInlineComponentArray<UMeshComponent*> Meshes(this);
		for (UMeshComponent* Mc : Meshes)
		{
			if (!Mc || DressParts.Contains(Cast<UStaticMeshComponent>(Mc))) continue;
			for (int32 i = 0; i < Mc->GetNumMaterials(); ++i)
			{
				if (UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(Mc->GetMaterial(i)))
				{
					Mid->SetScalarParameterValue(TEXT("Roughness"), Rough);
					Mid->SetScalarParameterValue(TEXT("Metallic"), Metal);
					if (E.Finish == TEXT("carbon")) SmvSetSolidColor(Mid, SmvFinishTint(E.Finish, InBodyColor));
				}
			}
		}
	}

	// ---- ilaçlama aparatı: tank + yatay bom + aşağı bakan memeler; püskürtme sisi (uçuşta) ----
	if (E.bSprayer && FSmvEquipment::AddonFits(TEXT("sprayer"), SmvType))
	{
		const float TankR = FMath::Clamp((float)E.SprayTankL / 40.f, 0.15f, 1.f) * 0.34f * R + 0.06f * R;
		AddDress(CylinderMesh, FVector(0.f, 0.f, Bot * 0.55f), FVector(TankR, TankR, 0.26f * R), FRotator::ZeroRotator, White, 0.35f, 0.f);
		AddDress(SphereMesh, FVector(0.f, 0.f, Bot * 0.55f + 0.14f * R), FVector(TankR * 0.5f, TankR * 0.5f, 0.05f * R), FRotator::ZeroRotator, Accent, 0.5f, 0.2f);
		const float Span = FMath::Clamp((float)E.SprayWidthM / 4.f, 0.6f, 1.6f) * 1.1f * R;
		AddDress(CylinderMesh, FVector(-0.04f * R, 0.f, Bot * 0.98f), FVector(0.022f * R, 0.022f * R, Span * 2.f), FRotator(0.f, 0.f, 90.f), Steel, 0.3f, 0.8f);
		for (int32 i = 0; i < 6; ++i)
		{
			const float Y = FMath::Lerp(-Span * 0.92f, Span * 0.92f, i / 5.f);
			AddDress(ConeMesh, FVector(-0.04f * R, Y, Bot * 0.98f - 0.035f * R), FVector(0.045f * R, 0.045f * R, 0.05f * R), FRotator(180.f, 0.f, 0.f), Accent, 0.4f, 0.3f);
			// püskürtme sisi: memelerden aşağı açılan ince koniler (uçuşta açılır)
			if (UStaticMeshComponent* Mist = AddDress(ConeMesh, FVector(-0.04f * R, Y, Bot * 0.98f - 0.32f * R), FVector(0.22f * R, 0.22f * R, 0.55f * R), FRotator(0.f, 0.f, 0.f),
				FLinearColor(0.75f, 0.85f, 0.95f), 0.9f, 0.f))
			{
				Mist->SetVisibility(false);
				Mist->SetCastShadow(false);
				if (UMaterialInterface* Fx = SmvLoadMat(TEXT("/Game/Simumav/Materials/M_SmvFx.M_SmvFx")))
				{
					UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(Fx, Mist);
					D->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.7f, 0.8f, 0.9f));
					D->SetScalarParameterValue(TEXT("Opacity"), 0.18f);
					Mist->SetMaterial(0, D);
				}
				SprayParts.Add(Mist);
			}
		}
	}

	// ---- paket bırakma aparatı: tutucu + çene + yük kutusu ----
	if (E.bGripper && FSmvEquipment::AddonFits(TEXT("gripper"), SmvType))
	{
		const float Z0 = bPlane ? Bot * 0.9f : Bot * 0.85f;
		const float Kg = FMath::Clamp((float)E.PayloadKg, 0.1f, 20.f);
		const float Box = FMath::Clamp(0.16f + 0.05f * FMath::Sqrt(Kg), 0.16f, 0.42f) * R;
		AddDress(CubeMesh, FVector(0.f, 0.f, Z0), FVector(0.16f * R, 0.16f * R, 0.05f * R), FRotator::ZeroRotator, Dark, 0.5f, 0.6f);
		AddDress(CubeMesh, FVector(0.f, 0.07f * R, Z0 - 0.06f * R), FVector(0.03f * R, 0.02f * R, 0.1f * R), FRotator(0.f, 0.f, -12.f), Steel, 0.3f, 0.8f);
		AddDress(CubeMesh, FVector(0.f, -0.07f * R, Z0 - 0.06f * R), FVector(0.03f * R, 0.02f * R, 0.1f * R), FRotator(0.f, 0.f, 12.f), Steel, 0.3f, 0.8f);
		PayloadBox = AddDress(CubeMesh, FVector(0.f, 0.f, Z0 - 0.08f * R - Box * 0.45f), FVector(Box * 1.1f, Box, Box * 0.85f), FRotator::ZeroRotator, FLinearColor(0.46f, 0.31f, 0.16f), 0.85f, 0.f);
	}

	// ---- paraşüt: sırtta kap; uçuşta açılınca kubbe + askı ipleri ----
	if (E.bParachute && FSmvEquipment::AddonFits(TEXT("parachute"), SmvType))
	{
		const float Top = FMath::Max(0.06f, -Bot * 0.9f);
		AddDress(CylinderMesh, FVector(-0.12f * R, 0.f, Top + 0.06f * R), FVector(0.17f * R, 0.17f * R, 0.13f * R), FRotator::ZeroRotator, Dark, 0.6f, 0.1f);
		AddDress(SphereMesh, FVector(-0.12f * R, 0.f, Top + 0.13f * R), FVector(0.17f * R, 0.17f * R, 0.06f * R), FRotator::ZeroRotator, FLinearColor(0.85f, 0.12f, 0.08f), 0.5f, 0.f);
		const float Can = 1.5f * R + 0.5f;
		const FVector CanAt(-0.12f * R, 0.f, Top + 2.2f * R + 1.2f);
		if (UStaticMeshComponent* Dome = AddDress(SphereMesh, CanAt, FVector(Can, Can, Can * 0.45f), FRotator::ZeroRotator, FLinearColor(0.95f, 0.42f, 0.05f), 0.9f, 0.f))
		{
			Dome->SetVisibility(false);
			ChuteParts.Add(Dome);
		}
		for (int32 i = 0; i < 6; ++i)
		{
			const float A = i * PI / 3.f;
			const FVector Rim = CanAt + FVector(FMath::Cos(A) * Can * 45.f, FMath::Sin(A) * Can * 45.f, -Can * 10.f) / 100.f;
			const FVector Base(-0.12f * R, 0.f, Top + 0.13f * R);
			const FVector Mid = (Rim + Base) * 0.5f, Dir = (Rim - Base);
			if (UStaticMeshComponent* Line = AddDress(CylinderMesh, Mid, FVector(0.006f, 0.006f, Dir.Size()), FRotationMatrix::MakeFromZ(Dir.GetSafeNormal()).Rotator(), White, 0.8f, 0.f))
			{
				Line->SetVisibility(false);
				ChuteParts.Add(Line);
			}
		}
	}

	// ---- çıkartma / logo (en çok 6): M_SmvDecal varsa decal, yoksa yüzeye ince levha ----
	UMaterialInterface* DecalMat = SmvLoadMat(TEXT("/Game/Simumav/Materials/M_SmvDecal.M_SmvDecal"));
	UMaterialInterface* PbrMat = SmvLoadMat(TEXT("/Game/Simumav/Materials/M_SmvPbr2S.M_SmvPbr2S"));
	int32 Placed = 0;
	for (const FSmvDecal& Dc : E.Decals)
	{
		if (Placed >= 6) break;
		UTexture2D* Tex = SmvDecalTexture(Dc.File);
		if (!Tex) continue;
		const float Sz = 0.32f * R * FMath::Clamp((float)Dc.Scale, 0.2f, 2.f);
		FVector At(0.f, 0.f, -Bot * 0.6f);
		FRotator Rot(-90.f, 0.f, 0.f);
		if (Dc.Slot == TEXT("bottom")) { At = FVector(0.f, 0.f, Bot * 0.8f); Rot = FRotator(90.f, 0.f, 0.f); }
		else if (Dc.Slot == TEXT("left")) { At = FVector(0.f, -0.7f * R, 0.f); Rot = FRotator(0.f, 90.f, 0.f); }
		else if (Dc.Slot == TEXT("right")) { At = FVector(0.f, 0.7f * R, 0.f); Rot = FRotator(0.f, -90.f, 0.f); }
		else if (Dc.Slot == TEXT("front")) { At = FVector(0.7f * R, 0.f, 0.f); Rot = FRotator(0.f, 180.f, 0.f); }
		else if (Dc.Slot == TEXT("rear")) { At = FVector(-0.7f * R, 0.f, 0.f); Rot = FRotator(0.f, 0.f, 0.f); }
		else if (Dc.Slot == TEXT("tail")) { At = FVector(-0.75f * R, 0.f, 0.15f * R); Rot = FRotator(0.f, 0.f, 0.f); }
		Rot.Roll += (float)Dc.Rot;
		if (DecalMat)
		{
			UDecalComponent* D = NewObject<UDecalComponent>(this);
			D->SetupAttachment(Body);
			D->RegisterComponent();
			UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(DecalMat, D);
			Mid->SetTextureParameterValue(TEXT("Tex"), Tex);
			D->SetDecalMaterial(Mid);
			D->DecalSize = FVector(0.6f * R * 100.f, Sz * 100.f, Sz * 100.f);
			D->SetRelativeLocationAndRotation(At * 100.f, Rot);
			DressDecals.Add(D);
		}
		else if (PbrMat)
		{
			// yedek: yüzeyin hemen üstünde ince levha (saydamlık yok)
			const FVector N = Rot.Vector();
			if (UStaticMeshComponent* Pl = AddDress(CubeMesh, At - N * 0.02f * R, FVector(0.002f, Sz, Sz), Rot, FLinearColor::White, 0.5f, 0.f))
			{
				UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(PbrMat, Pl);
				Mid->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor::White);
				Mid->SetTextureParameterValue(TEXT("BaseColorTex"), Tex);
				Mid->SetScalarParameterValue(TEXT("UseTex"), 1.f);
				Pl->SetMaterial(0, Mid);
			}
		}
		++Placed;
	}
}

void ASmvDroneActor::SetChuteDeployed(bool bOn)
{
	bChuteOut = bOn;
	for (TObjectPtr<UStaticMeshComponent>& C : ChuteParts) if (C) C->SetVisibility(bOn);
}

void ASmvDroneActor::SetPayloadAttached(bool bOn)
{
	if (PayloadBox) PayloadBox->SetVisibility(bOn);
}

void ASmvDroneActor::SetSprayActive(bool bOn)
{
	for (TObjectPtr<UStaticMeshComponent>& C : SprayParts) if (C) C->SetVisibility(bOn);
}
