// Parça paketi sahası (bkz. SmvKitYard.h).
#include "SmvKitYard.h"
#include "SimumavModule.h"
#include "SmvUiKit.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"

AActor* FSmvKitYard::Build(UWorld* W, ULevel* Into, const FString& Dir, int32 Seed)
{
	if (!W || Dir.IsEmpty()) return nullptr;
	TArray<FAssetData> Assets;
	IAssetRegistry::GetChecked().GetAssetsByPath(FName(*Dir), Assets, true);
	struct FItem { UStaticMesh* M; FBoxSphereBounds B; double Area; };
	TArray<FItem> Items;
	for (const FAssetData& A : Assets)
	{
		if (A.AssetClassPath != UStaticMesh::StaticClass()->GetClassPathName()) continue;
		UStaticMesh* M = Cast<UStaticMesh>(A.GetAsset());
		if (!M) continue;
		const FBoxSphereBounds B = M->GetBounds();
		const FVector E = B.BoxExtent;
		// çok küçük (cıvata, vana: < 3 m), yassı (zemin plakası) ve dev (> 250 m) parçalar dizilmez
		if (FMath::Max(E.X, E.Y) * 2.0 < 300.0 || E.Z * 2.0 < 150.0 || FMath::Max(E.X, E.Y) * 2.0 > 25000.0) continue;
		Items.Add({ M, B, 4.0 * E.X * E.Y });
		if (Items.Num() >= 160) break;
	}
	if (Items.Num() == 0)
	{
		UE_LOG(LogSimumav, Warning, TEXT("Parça sahası: %s altında uygun ağ yok (%d varlık)"), *Dir, Assets.Num());
		return nullptr;
	}
	Items.Sort([](const FItem& X, const FItem& Y) { return X.Area > Y.Area; });
	if (Items.Num() > 70) Items.SetNum(70);
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	P.OverrideLevel = Into;
	AActor* Yard = W->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, P);
	if (!Yard) return nullptr;
	USceneComponent* Root = NewObject<USceneComponent>(Yard, TEXT("Root"));
	Yard->SetRootComponent(Root);
	Root->RegisterComponent();
	// raf yerleşimi: satır genişliği toplam alanın kareköküyle; her satır en yüksek parçası kadar derin, aralar 9 m yol
	double Sum = 0;
	for (const FItem& I : Items) Sum += I.Area;
	const double Gap = 900.0;
	const double RowW = FMath::Max(8000.0, FMath::Sqrt(Sum) * 1.5);
	FRandomStream R(Seed * 31 + 5);
	double X = 0, Y = 0, RowD = 0;
	FBox Fp(ForceInit);
	int32 N = 0;
	for (const FItem& I : Items)
	{
		const bool bTurn = R.FRand() < 0.5f;
		const double Sx = (bTurn ? I.B.BoxExtent.Y : I.B.BoxExtent.X) * 2.0, Sy = (bTurn ? I.B.BoxExtent.X : I.B.BoxExtent.Y) * 2.0;
		if (X > 0 && X + Sx > RowW) { X = 0; Y += RowD + Gap; RowD = 0; }
		const FVector Slot(X + Sx * 0.5, Y + Sy * 0.5, 0.0);
		const FRotator Rot(0.0, bTurn ? 90.0 : 0.0, 0.0);
		// ağ kökü sınırın merkezinde olmayabilir: sınır merkezi yuvaya, tabanı z = 0'a
		const FVector Oc = Rot.RotateVector(I.B.Origin);
		const FVector Loc(Slot.X - Oc.X, Slot.Y - Oc.Y, -(I.B.Origin.Z - I.B.BoxExtent.Z));
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Yard);
		C->SetupAttachment(Root);
		C->SetStaticMesh(I.M);
		C->SetRelativeLocationAndRotation(Loc, Rot);
		C->SetCollisionProfileName(TEXT("BlockAll"));
		C->RegisterComponent();
		Fp += FBox(FVector(X, Y, 0.0), FVector(X + Sx, Y + Sy, I.B.BoxExtent.Z * 2.0));
		X += Sx + Gap;
		RowD = FMath::Max(RowD, Sy);
		++N;
	}
	// sahayı ortala (kalkış: merkeze en yakın açıklık — yollar)
	const FVector Ctr(Fp.GetCenter().X, Fp.GetCenter().Y, 0.0);
	for (USceneComponent* Ch : Root->GetAttachChildren()) if (Ch) Ch->AddRelativeLocation(-Ctr);
	// beton zemin (çarpışmalı; 25 m kenar payı), üst yüzü z = 0
	if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		const FVector Sz = Fp.GetSize() + FVector(5000.0, 5000.0, 0.0);
		UStaticMeshComponent* G = NewObject<UStaticMeshComponent>(Yard, TEXT("YardGround"));
		G->SetupAttachment(Root);
		G->SetStaticMesh(Cube);
		G->SetRelativeLocation(FVector(0.0, 0.0, -50.0));
		G->SetRelativeScale3D(FVector(Sz.X / 100.0, Sz.Y / 100.0, 1.0));
		G->SetCollisionProfileName(TEXT("BlockAll"));
		if (UMaterialInterface* Sm = SmvSolidMat(G->GetMaterial(0)))
		{
			UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(Sm, Yard);
			SmvSetSolidColor(D, FLinearColor(0.23f, 0.23f, 0.22f));
			D->SetScalarParameterValue(TEXT("Roughness"), 0.9f);
			G->SetMaterial(0, D);
		}
		G->RegisterComponent();
	}
	UE_LOG(LogSimumav, Log, TEXT("Parça sahası %s: %d / %d ağ dizildi, saha %.0f × %.0f m"), *Dir, N, Assets.Num(), Fp.GetSize().X / 100.0, Fp.GetSize().Y / 100.0);
	return Yard;
}
