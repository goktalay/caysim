#include "SmvGlbBody.h"
#include "SimumavModule.h"
#include "SmvGlb.h"
#include "ProceduralMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Modules/ModuleManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// Editor Python (Scripts/Python/simumav_varliklar.py) üretir; yoksa BasicShapeMaterial (yalnız Color) ile çizilir
	const TCHAR* GPbr = TEXT("/Game/Simumav/Materials/M_SmvPbr.M_SmvPbr");
	const TCHAR* GPbr2S = TEXT("/Game/Simumav/Materials/M_SmvPbr2S.M_SmvPbr2S");
	const TCHAR* GDiscMat = TEXT("/Game/Simumav/Materials/M_SmvPropDisc.M_SmvPropDisc");
	const TCHAR* GGlow = TEXT("/Game/Simumav/Materials/M_SmvGlow.M_SmvGlow");
	const TCHAR* GBasic = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	const TCHAR* GCyl = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const TCHAR* GSphere = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	constexpr float GSurfaceMaxDeg = 25.f;

	bool StartsWith(const FString& S, const TCHAR* P) { return S.StartsWith(P, ESearchCase::IgnoreCase); }

	/** glTF ağ alt düğümü ("rotor_0_v0", "base_link_g3") — hareketli parça adı değildir */
	bool IsSubMeshName(const FString& N)
	{
		int32 U;
		if (!N.FindLastChar(TEXT('_'), U) || U + 2 > N.Len()) return false;
		const TCHAR C = FChar::ToLower(N[U + 1]);
		if (C != TEXT('v') && C != TEXT('g')) return false;
		for (int32 i = U + 2; i < N.Len(); ++i) if (!FChar::IsDigit(N[i])) return false;
		return U + 2 < N.Len();
	}

	/** -1 değil: hareketli parça türü (ada göre) */
	int32 ClassifyName(const FString& Raw)
	{
		if (Raw.IsEmpty() || IsSubMeshName(Raw)) return -1;
		const FString N = Raw.ToLower();
		if (StartsWith(N, TEXT("nav_")) || StartsWith(N, TEXT("beacon")) || StartsWith(N, TEXT("strobe"))) return 4;   // ışık
		if (StartsWith(N, TEXT("steer_"))) return 3;
		if (StartsWith(N, TEXT("wheel_"))) return 2;
		if (StartsWith(N, TEXT("aileron")) || StartsWith(N, TEXT("elevon")) || StartsWith(N, TEXT("elevator")) || StartsWith(N, TEXT("rudder"))
			|| StartsWith(N, TEXT("ruddervator")) || StartsWith(N, TEXT("flap"))) return 1;
		if (StartsWith(N, TEXT("rotor")) || StartsWith(N, TEXT("prop")) || StartsWith(N, TEXT("blade")) || N == TEXT("fan")) return 0;
		return -1;
	}

	/** Sütun öncelikli 3×3 determinant (ayna dönüşümlü düğümde sarım yönü ters) */
	double Det3(const double M[16])
	{
		return M[0] * (M[5] * M[10] - M[9] * M[6]) - M[4] * (M[1] * M[10] - M[9] * M[2]) + M[8] * (M[1] * M[6] - M[5] * M[2]);
	}

	/** Kovaryansın en büyük öz vektörü (güç yinelemesi) */
	FVector Principal(const TArray<FVector>& P, const FVector& Mean)
	{
		double C[3][3] = { { 0 } };
		for (const FVector& V : P)
		{
			const FVector D = V - Mean;
			for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b) C[a][b] += D[a] * D[b];
		}
		FVector X(1, 0.37, 0.21);
		for (int it = 0; it < 40; ++it)
		{
			FVector Y(C[0][0] * X.X + C[0][1] * X.Y + C[0][2] * X.Z, C[1][0] * X.X + C[1][1] * X.Y + C[1][2] * X.Z, C[2][0] * X.X + C[2][1] * X.Y + C[2][2] * X.Z);
			if (!Y.Normalize()) break;
			X = Y;
		}
		return X.GetSafeNormal();
	}

	struct FSection
	{
		TArray<FVector> V;
		TArray<FVector> N;
		TArray<FVector2D> Uv;
		TArray<int32> T;
	};
}

USmvGlbBody::USmvGlbBody()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UMaterialInterface* USmvGlbBody::LoadMat(const TCHAR* Path)
{
	return LoadObject<UMaterialInterface>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
}

void USmvGlbBody::Clear()
{
	for (TObjectPtr<USceneComponent>& C : Owned) if (IsValid(C)) C->DestroyComponent();
	Owned.Reset();
	Parts.Reset();
	Mids.Reset();
	Textures.Reset();
	Bounds = FBox(ForceInit);
	RotorSpool = 0.f;
}

USmvGlbBody::FPart& USmvGlbBody::AddPart(const FString& Name, EKind Kind)
{
	FPart& P = Parts.AddDefaulted_GetRef();
	P.Name = Name;
	P.Kind = Kind;
	return P;
}

bool USmvGlbBody::Build(const TArray<uint8>& Bytes, const FSmvGlbBuildOptions& Opt, const FString& DebugName)
{
	Clear();
	Options = Opt;
	SmvCore::FGlbModel M;
	std::string Err;
	if (!M.Parse(Bytes.GetData(), (size_t)Bytes.Num(), Err))
	{
		UE_LOG(LogSimumav, Warning, TEXT("GLB okunamadı (%s): %s"), *DebugName, UTF8_TO_TCHAR(Err.c_str()));
		return false;
	}
	const double Yaw = FMath::DegreesToRadians((double)Opt.YawDeg);
	const double Cy = FMath::Cos(Yaw), Sy = FMath::Sin(Yaw);
	// glTF (dünya) → Unreal gövde çerçevesi (cm): Y ekseni etrafında ön döndürme, sonra (x, z, y)·100
	auto ToUe = [Cy, Sy](const double G[3]) { const double X = G[0] * Cy + G[2] * Sy, Z = -G[0] * Sy + G[2] * Cy; return FVector(X * 100.0, Z * 100.0, G[1] * 100.0); };
	auto ToUeDir = [Cy, Sy](const double G[3]) { const double X = G[0] * Cy + G[2] * Sy, Z = -G[0] * Sy + G[2] * Cy; return FVector(X, Z, G[1]).GetSafeNormal(); };

	// 1) düğüm → parça (en yakın adlı hareketli ata)
	const int32 NN = (int32)M.Nodes.size();
	TArray<int32> NodePart;
	NodePart.Init(INDEX_NONE, NN);
	TMap<int32, int32> PartOfNode;   // hareketli düğüm → parça
	for (int32 i = 0; i < NN; ++i)
	{
		const int32 K = ClassifyName(UTF8_TO_TCHAR(M.Nodes[i].Name.c_str()));
		if (K < 0) continue;
		const EKind Kinds[] = { EKind::Spinner, EKind::Surface, EKind::Wheel, EKind::Steer, EKind::Light };
		const int32 Pi = Parts.Num();
		AddPart(UTF8_TO_TCHAR(M.Nodes[i].Name.c_str()), Kinds[K]);
		PartOfNode.Add(i, Pi);
	}
	for (int32 i = 0; i < NN; ++i)
	{
		for (int32 A = i, Guard = 0; A >= 0 && Guard < 64; A = M.Nodes[A].Parent, ++Guard)
		{
			if (const int32* P = PartOfNode.Find(A)) { NodePart[i] = *P; break; }
		}
	}
	// parça ebeveyni (direksiyon → teker gibi iç içe parçalar)
	for (const TPair<int32, int32>& Kv : PartOfNode)
	{
		for (int32 A = M.Nodes[Kv.Key].Parent, Guard = 0; A >= 0 && Guard < 64; A = M.Nodes[A].Parent, ++Guard)
		{
			if (const int32* P = PartOfNode.Find(A)) { Parts[Kv.Value].ParentPart = *P; break; }
		}
	}

	// 2) ilkelleri gövde çerçevesine çevir; (parça, malzeme) bölümlerine topla
	TMap<int64, FSection> Sections;   // anahtar: (parça+1) << 20 | (malzeme+1)
	TArray<TArray<FVector>> PartPts;
	PartPts.SetNum(Parts.Num());
	int32 Tris = 0;
	for (int32 i = 0; i < NN; ++i)
	{
		const SmvCore::FGlbNode& Nd = M.Nodes[i];
		if (Nd.Mesh < 0) continue;
		const bool bMirror = Det3(Nd.World) < 0;
		for (const SmvCore::FGlbPrimitive& G : M.Meshes[Nd.Mesh].Prims)
		{
			const int32 Pi = NodePart[i];
			const int64 Key = ((int64)(Pi + 1) << 20) | (int64)(G.Material + 1);
			FSection& S = Sections.FindOrAdd(Key);
			const int32 Base = S.V.Num();
			const int32 Nv = (int32)G.Pos.size() / 3;
			for (int32 v = 0; v < Nv; ++v)
			{
				double W[3];
				SmvCore::FGlbModel::TransformPoint(Nd.World, &G.Pos[v * 3], W);
				const FVector P = ToUe(W);
				S.V.Add(P);
				Bounds += P;
				if (Pi != INDEX_NONE) PartPts[Pi].Add(P);
				if (!G.Nrm.empty())
				{
					double Dn[3];
					SmvCore::FGlbModel::TransformDir(Nd.World, &G.Nrm[v * 3], Dn);
					S.N.Add(ToUeDir(Dn));
				}
				else S.N.Add(FVector::ZeroVector);
				S.Uv.Add(G.Uv.empty() ? FVector2D::ZeroVector : FVector2D(G.Uv[v * 2], G.Uv[v * 2 + 1]));
			}
			// eksen değişimi (x, z, y) aynalamadır → sarım ters çevrilir; ayna dönüşümlü düğümde bir kez daha
			for (size_t t = 0; t + 2 < G.Idx.size(); t += 3)
			{
				const int32 A = Base + (int32)G.Idx[t], B = Base + (int32)G.Idx[t + 1], C = Base + (int32)G.Idx[t + 2];
				if (bMirror) { S.T.Add(A); S.T.Add(B); S.T.Add(C); }
				else { S.T.Add(A); S.T.Add(C); S.T.Add(B); }
				++Tris;
			}
			if (G.Nrm.empty())
			{
				// normal yok: yüz normallerinin köşe ortalaması
				for (int32 t = 0; t < S.T.Num(); t += 3)
				{
					if (S.T[t] < Base) continue;
					const FVector& A = S.V[S.T[t]]; const FVector& B = S.V[S.T[t + 1]]; const FVector& C = S.V[S.T[t + 2]];
					const FVector Fn = FVector::CrossProduct(B - A, C - A);
					S.N[S.T[t]] += Fn; S.N[S.T[t + 1]] += Fn; S.N[S.T[t + 2]] += Fn;
				}
				for (int32 v = Base; v < S.N.Num(); ++v) S.N[v] = S.N[v].GetSafeNormal();
			}
		}
	}
	if (Tris == 0 || !Bounds.IsValid)
	{
		UE_LOG(LogSimumav, Warning, TEXT("GLB boş (%s)"), *DebugName);
		Clear();
		return false;
	}

	// 3) parça pivot / eksen
	for (int32 Pi = 0; Pi < Parts.Num(); ++Pi)
	{
		FPart& P = Parts[Pi];
		const TArray<FVector>& Pts = PartPts[Pi];
		if (Pts.Num() == 0)
		{
			// ağsız düğüm (ör. standard_vtol elevator/rudder boş): hareketsiz
			P.Kind = (P.Kind == EKind::Light) ? EKind::Light : EKind::Static;
			for (int32 i = 0; i < NN; ++i)
			{
				if (P.Name == UTF8_TO_TCHAR(M.Nodes[i].Name.c_str()))
				{
					const double O[3] = { M.Nodes[i].World[12], M.Nodes[i].World[13], M.Nodes[i].World[14] };
					P.PivotBody = ToUe(O);
					break;
				}
			}
			continue;
		}
		FBox B(ForceInit);
		FVector Mean = FVector::ZeroVector;
		for (const FVector& V : Pts) { B += V; Mean += V; }
		Mean /= Pts.Num();
		const FVector E = B.GetExtent();
		P.Extent = E;
		P.PivotBody = B.GetCenter();
		const FString L = P.Name.ToLower();
		if (P.Kind == EKind::Spinner)
		{
			// disk ekseni = en ince boyut (pervane diski kalınlığı); yarıçap = diğer iki boyutun büyüğü
			int32 Ax = 0;
			if (E.Y < E[Ax]) Ax = 1;
			if (E.Z < E[Ax]) Ax = 2;
			P.Axis = Ax == 0 ? FVector::ForwardVector : Ax == 1 ? FVector::RightVector : FVector::UpVector;
			P.RadiusCm = (float)FMath::Max(E[(Ax + 1) % 3], E[(Ax + 2) % 3]);
			const bool bHeli = Opt.Type == TEXT("heli");
			if (bHeli && Ax == 2) P.Role = ESpinRole::HeliMain;
			else if (bHeli) P.Role = ESpinRole::HeliTail;
			else P.Role = (Ax == 2) ? ESpinRole::Lift : ESpinRole::Forward;
		}
		else if (P.Kind == EKind::Surface)
		{
			// menteşe ekseni = açıklık (PCA), menteşe noktası = ön kenar (en ileri köşeler)
			FVector Span = Principal(Pts, Mean);
			if (FMath::Abs(Span.X) > 0.85)
			{
				// açıklık gövde ekseninde çıkarsa (kısa yüzey) yan/dik eksenlerden uzun olanı
				Span = E.Y >= E.Z ? FVector::RightVector : FVector::UpVector;
			}
			Span.X = 0;
			Span = Span.GetSafeNormal();
			if (Span.IsNearlyZero()) Span = FVector::RightVector;
			P.Axis = Span;
			P.PivotBody = FVector(B.Max.X, B.GetCenter().Y, B.GetCenter().Z);
			// kuyruk kenarının "aşağı/sola" gideceği yön (bkz. Animate): dikey yüzey (dümen) → −Y; diğerleri X × açıklık, aşağı bakan
			FVector Down = FVector::CrossProduct(FVector::ForwardVector, Span).GetSafeNormal();
			if (FMath::Abs(Span.Z) > 0.8) Down = -FVector::RightVector;
			else if (Down.Z > 0) Down = -Down;
			// +açı ile kuyruk kenarının gittiği yön Down ise Dir +1 (UE dönüş el kuralı sayısal olarak sınanır)
			const FVector Te(-1, 0, 0);
			const FVector Moved = FQuat(Span, FMath::DegreesToRadians(5.f)).RotateVector(Te) - Te;
			P.Dir = FVector::DotProduct(Moved, Down) >= 0 ? 1.f : -1.f;
		}
		else if (P.Kind == EKind::Wheel)
		{
			P.Axis = FVector::RightVector;
			P.RadiusCm = FMath::Max(1.f, (float)FMath::Max(E.X, E.Z));
		}
		else if (P.Kind == EKind::Steer)
		{
			P.Axis = FVector::UpVector;
		}
		else if (P.Kind == EKind::Light)
		{
			P.LightColor = L.Contains(TEXT("_r")) && !L.Contains(TEXT("tail")) ? FLinearColor(0.05f, 1.f, 0.15f)
				: (L.Contains(TEXT("tail")) || L.Contains(TEXT("strobe"))) ? FLinearColor(1.f, 1.f, 1.f) : FLinearColor(1.f, 0.05f, 0.03f);
			P.bBeacon = L.StartsWith(TEXT("beacon")) || L.StartsWith(TEXT("strobe"));
		}
	}

	// 4) bileşenler: parça pivotları (iç içe), ağ bölümleri
	UMaterialInterface* Pbr = LoadMat(GPbr);
	UMaterialInterface* Pbr2S = LoadMat(GPbr2S);
	UMaterialInterface* Basic = LoadMat(GBasic);
	UMaterialInterface* Glow = LoadMat(GGlow);
	auto PivotOf = [this](int32 Pi) -> USceneComponent* { return Pi >= 0 && Pi < Parts.Num() ? Parts[Pi].Pivot : nullptr; };
	// ebeveynler önce kurulsun
	TArray<int32> Order;
	for (int32 Pi = 0; Pi < Parts.Num(); ++Pi) Order.Add(Pi);
	Order.Sort([this](int32 A, int32 B)
	{
		auto Depth = [this](int32 X) { int32 D = 0; for (int32 P = Parts[X].ParentPart; P >= 0 && D < 16; P = Parts[P].ParentPart) ++D; return D; };
		return Depth(A) < Depth(B);
	});
	for (int32 Pi : Order)
	{
		FPart& P = Parts[Pi];
		USceneComponent* Parent = PivotOf(P.ParentPart);
		USceneComponent* Pv = NewObject<USceneComponent>(GetOwner(), NAME_None);
		Pv->SetupAttachment(Parent ? Parent : (USceneComponent*)this);
		Pv->RegisterComponent();
		const FVector ParentPivot = P.ParentPart >= 0 ? Parts[P.ParentPart].PivotBody : FVector::ZeroVector;
		Pv->SetRelativeLocation(P.PivotBody - ParentPivot);
		P.Pivot = Pv;
		Owned.Add(Pv);
	}

	// malzemeler + dokular
	IImageWrapperModule* Iw = FModuleManager::LoadModulePtr<IImageWrapperModule>(TEXT("ImageWrapper"));
	TArray<UTexture2D*> ImgTex;
	ImgTex.Init(nullptr, (int32)M.Images.size());
	auto TexOf = [&](int32 Img) -> UTexture2D*
	{
		if (!Iw || Img < 0 || Img >= ImgTex.Num()) return nullptr;
		if (ImgTex[Img]) return ImgTex[Img];
		const SmvCore::FGlbImage& I = M.Images[Img];
		if (I.Bytes.empty()) return nullptr;
		const EImageFormat F = Iw->DetectImageFormat(I.Bytes.data(), (int64)I.Bytes.size());
		TSharedPtr<IImageWrapper> W;
		if (F != EImageFormat::Invalid) W = Iw->CreateImageWrapper(F);
		TArray64<uint8> Raw;
		if (!W.IsValid() || !W->SetCompressed(I.Bytes.data(), (int64)I.Bytes.size()) || !W->GetRaw(ERGBFormat::BGRA, 8, Raw)) return nullptr;
		const int32 Tw = (int32)W->GetWidth(), Th = (int32)W->GetHeight();
		UTexture2D* T = UTexture2D::CreateTransient(Tw, Th, PF_B8G8R8A8);
		if (!T) return nullptr;
		T->SRGB = true;
		void* Dst = T->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Dst, Raw.GetData(), FMath::Min<int64>(Raw.Num(), (int64)Tw * Th * 4));
		T->GetPlatformData()->Mips[0].BulkData.Unlock();
		T->UpdateResource();
		Textures.Add(T);
		ImgTex[Img] = T;
		return T;
	};
	TMap<int32, UMaterialInstanceDynamic*> MatCache;
	auto MatOf = [&](int32 Mi) -> UMaterialInterface*
	{
		if (UMaterialInstanceDynamic** F = MatCache.Find(Mi)) return *F;
		SmvCore::FGlbMaterial Gm;
		if (Mi >= 0 && Mi < (int32)M.Materials.size()) Gm = M.Materials[Mi];
		else { Gm.BaseColor[0] = Gm.BaseColor[1] = Gm.BaseColor[2] = 0.6f; Gm.Metallic = 0.1f; Gm.Roughness = 0.6f; }
		UMaterialInterface* Parent = (Gm.bDoubleSided && Pbr2S) ? Pbr2S : Pbr ? Pbr : Basic;
		if (!Parent) return nullptr;
		UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(Parent, this);
		const FLinearColor C(Gm.BaseColor[0], Gm.BaseColor[1], Gm.BaseColor[2], Gm.BaseColor[3]);
		D->SetVectorParameterValue(TEXT("BaseColor"), C);
		D->SetVectorParameterValue(TEXT("Color"), C);   // BasicShapeMaterial yedeği
		D->SetScalarParameterValue(TEXT("Metallic"), Gm.Metallic);
		D->SetScalarParameterValue(TEXT("Roughness"), Gm.Roughness);
		D->SetVectorParameterValue(TEXT("Emissive"), FLinearColor(Gm.Emissive[0], Gm.Emissive[1], Gm.Emissive[2]));
		if (UTexture2D* T = TexOf(Gm.BaseColorImage))
		{
			D->SetTextureParameterValue(TEXT("BaseColorTex"), T);
			D->SetScalarParameterValue(TEXT("UseTex"), 1.f);
		}
		Mids.Add(D);
		MatCache.Add(Mi, D);
		return D;
	};

	TMap<int32, UProceduralMeshComponent*> Pmcs;   // parça (+1) → bileşen
	TMap<int32, int32> NextSection;
	int32 Sec = 0;
	for (TPair<int64, FSection>& Kv : Sections)
	{
		const int32 Pi = (int32)(Kv.Key >> 20) - 1;
		const int32 Mi = (int32)(Kv.Key & 0xFFFFF) - 1;
		FSection& S = Kv.Value;
		if (S.T.Num() == 0) continue;
		UProceduralMeshComponent*& Pmc = Pmcs.FindOrAdd(Pi + 1);
		if (!Pmc)
		{
			Pmc = NewObject<UProceduralMeshComponent>(GetOwner(), NAME_None);
			Pmc->SetupAttachment(Pi >= 0 ? Parts[Pi].Pivot : (USceneComponent*)this);
			Pmc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Pmc->SetCastShadow(true);
			Pmc->bUseAsyncCooking = true;
			Pmc->RegisterComponent();
			Owned.Add(Pmc);
		}
		// parçada köşeler pivot göreli
		if (Pi >= 0) for (FVector& V : S.V) V -= Parts[Pi].PivotBody;
		const int32 Si = NextSection.FindOrAdd(Pi + 1)++;
		Pmc->CreateMeshSection_LinearColor(Si, S.V, S.T, S.N, S.Uv, TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
		UMaterialInterface* Mat = nullptr;
		if (Pi >= 0 && Parts[Pi].Kind == EKind::Light && Glow)
		{
			UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(Glow, this);
			D->SetVectorParameterValue(TEXT("Color"), Parts[Pi].LightColor);
			D->SetScalarParameterValue(TEXT("Glow"), 6.f);
			Mids.Add(D);
			Mat = D;
		}
		else Mat = MatOf(Mi);
		if (Mat) Pmc->SetMaterial(Si, Mat);
		++Sec;
	}

	// 5) seyir ışıkları (modelde yoksa sınır kutusundan), pervane diskleri, VTOL kaldırma rotorları
	bool bHasLights = false;
	for (const FPart& P : Parts) if (P.Kind == EKind::Light) { bHasLights = true; break; }
	if (Opt.bAddLiftRotors) AddLiftRotors();
	if (Opt.bNavLights && Opt.Type != TEXT("rover"))
	{
		if (!bHasLights)
		{
			const FVector C = Bounds.GetCenter();
			AddNavLight(TEXT("nav_l"), FVector(C.X, Bounds.Min.Y, C.Z), FLinearColor(1.f, 0.05f, 0.03f), false);
			AddNavLight(TEXT("nav_r"), FVector(C.X, Bounds.Max.Y, C.Z), FLinearColor(0.05f, 1.f, 0.15f), false);
			AddNavLight(TEXT("nav_tail"), FVector(Bounds.Min.X, C.Y, C.Z), FLinearColor::White, false);
			AddNavLight(TEXT("beacon"), FVector(C.X, C.Y, Bounds.Max.Z), FLinearColor(1.f, 0.05f, 0.03f), true);
		}
		else
		{
			for (FPart& P : Parts)
			{
				if (P.Kind != EKind::Light || !P.Pivot) continue;
				UPointLightComponent* Lc = NewObject<UPointLightComponent>(GetOwner(), NAME_None);
				Lc->SetupAttachment(P.Pivot);
				Lc->SetIntensityUnits(ELightUnits::Candelas);
				Lc->SetIntensity(P.bBeacon ? 0.f : 12.f);
				Lc->SetLightColor(P.LightColor);
				Lc->SetAttenuationRadius(350.f);
				Lc->SetCastShadows(false);
				Lc->RegisterComponent();
				P.Light = Lc;
				Owned.Add(Lc);
			}
		}
	}
	FinishSpinners();

	int32 Ns = 0, Nf = 0, Nw = 0, Nl = 0;
	FString Names;
	for (const FPart& P : Parts)
	{
		if (P.Kind == EKind::Spinner) ++Ns; else if (P.Kind == EKind::Surface) ++Nf; else if (P.Kind == EKind::Wheel || P.Kind == EKind::Steer) ++Nw; else if (P.Kind == EKind::Light) ++Nl;
		if (P.Kind != EKind::Light && P.Kind != EKind::Static)
		{
			Names += FString::Printf(TEXT("%s%s[%s %.0f,%.0f,%.0f]"), Names.IsEmpty() ? TEXT("") : TEXT(" "), *P.Name,
				P.Kind == EKind::Spinner ? (P.Role == ESpinRole::Lift ? TEXT("kaldırma") : P.Role == ESpinRole::Forward ? TEXT("itki") : P.Role == ESpinRole::HeliMain ? TEXT("ana") : TEXT("kuyruk"))
				: P.Kind == EKind::Surface ? TEXT("yüzey") : TEXT("teker"), P.Axis.X, P.Axis.Y, P.Axis.Z);
		}
	}
	const FVector Sz = Bounds.GetSize() / 100.0;
	BuildSummary = FString::Printf(TEXT("%s: %d üçgen, %d bölüm, %d doku, boyut %.2f×%.2f×%.2f m; pervane %d, yüzey %d, teker %d, ışık %d — %s"),
		*DebugName, Tris, Sec, Textures.Num(), Sz.X, Sz.Y, Sz.Z, Ns, Nf, Nw, Nl, *Names);
	UE_LOG(LogSimumav, Log, TEXT("GLB gövdesi %s"), *BuildSummary);
	return true;
}

void USmvGlbBody::AddNavLight(const FString& Name, const FVector& PosCm, const FLinearColor& Color, bool bBeacon)
{
	FPart& P = AddPart(Name, EKind::Light);
	P.PivotBody = PosCm;
	P.LightColor = Color;
	P.bBeacon = bBeacon;
	USceneComponent* Pv = NewObject<USceneComponent>(GetOwner(), NAME_None);
	Pv->SetupAttachment(this);
	Pv->RegisterComponent();
	Pv->SetRelativeLocation(PosCm);
	P.Pivot = Pv;
	Owned.Add(Pv);
	if (UStaticMesh* Sph = LoadObject<UStaticMesh>(nullptr, GSphere))
	{
		UStaticMeshComponent* S = NewObject<UStaticMeshComponent>(GetOwner(), NAME_None);
		S->SetStaticMesh(Sph);
		S->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		S->SetCastShadow(false);
		S->SetupAttachment(Pv);
		S->RegisterComponent();
		const float D = FMath::Clamp((float)Bounds.GetSize().GetMax() * 0.012f, 1.2f, 6.f);   // cm çap
		S->SetRelativeScale3D(FVector(D / 100.f));
		if (UMaterialInterface* G = LoadMat(GGlow))
		{
			UMaterialInstanceDynamic* Dm = UMaterialInstanceDynamic::Create(G, this);
			Dm->SetVectorParameterValue(TEXT("Color"), Color);
			Dm->SetScalarParameterValue(TEXT("Glow"), 6.f);
			S->SetMaterial(0, Dm);
			Mids.Add(Dm);
		}
		Owned.Add(S);
	}
	UPointLightComponent* Lc = NewObject<UPointLightComponent>(GetOwner(), NAME_None);
	Lc->SetupAttachment(Pv);
	Lc->SetIntensityUnits(ELightUnits::Candelas);
	Lc->SetIntensity(bBeacon ? 0.f : 12.f);
	Lc->SetLightColor(Color);
	Lc->SetAttenuationRadius(350.f);
	Lc->SetCastShadows(false);
	Lc->RegisterComponent();
	P.Light = Lc;
	Owned.Add(Lc);
}

void USmvGlbBody::AddLiftRotors()
{
	// VTOL-Caymaz: kanat altına iki boylamasına kol, uçlarında dört dikey rotor (HEPGO VTOL gösterimi gibi)
	UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, GCyl);
	if (!Cyl) return;
	UMaterialInterface* Base = LoadMat(GPbr) ? LoadMat(GPbr) : LoadMat(GBasic);
	const FVector C = Bounds.GetCenter();
	const float Span = (float)Bounds.GetSize().Y, Len = (float)Bounds.GetSize().X;
	const float Yb = Span * 0.22f, Xb = Len * 0.36f, Z = (float)Bounds.Min.Z + 2.f;
	auto Mid = [this, Base](const FLinearColor& Col) -> UMaterialInstanceDynamic*
	{
		if (!Base) return nullptr;
		UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(Base, this);
		D->SetVectorParameterValue(TEXT("BaseColor"), Col); D->SetVectorParameterValue(TEXT("Color"), Col);
		D->SetScalarParameterValue(TEXT("Roughness"), 0.5f); D->SetScalarParameterValue(TEXT("Metallic"), 0.2f);
		Mids.Add(D);
		return D;
	};
	UMaterialInstanceDynamic* Boom = Mid(FLinearColor(0.05f, 0.05f, 0.06f));
	UMaterialInstanceDynamic* Blade = Mid(FLinearColor(0.03f, 0.03f, 0.035f));
	auto Part = [&](USceneComponent* Parent, const FVector& Pos, const FVector& ScaleM, const FRotator& Rot, UMaterialInstanceDynamic* Mat)
	{
		UStaticMeshComponent* S = NewObject<UStaticMeshComponent>(GetOwner(), NAME_None);
		S->SetStaticMesh(Cyl);
		S->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		S->SetupAttachment(Parent);
		S->RegisterComponent();
		S->SetRelativeLocationAndRotation(Pos, Rot);
		S->SetRelativeScale3D(ScaleM);
		if (Mat) S->SetMaterial(0, Mat);
		Owned.Add(S);
	};
	for (const float Ys : { -1.f, 1.f })
	{
		Part(this, FVector(C.X, Ys * Yb, Z), FVector(0.05f, 0.05f, Xb * 2.f / 100.f), FRotator(90.f, 0.f, 0.f), Boom);
		for (const float Xs : { -1.f, 1.f })
		{
			FPart& P = AddPart(FString::Printf(TEXT("rotor_lift_%s%s"), Xs > 0 ? TEXT("f") : TEXT("r"), Ys > 0 ? TEXT("r") : TEXT("l")), EKind::Spinner);
			P.PivotBody = FVector(C.X + Xs * Xb, Ys * Yb, Z + 8.f);
			P.Axis = FVector::UpVector;
			P.Role = ESpinRole::Lift;
			P.RadiusCm = 23.f;
			USceneComponent* Pv = NewObject<USceneComponent>(GetOwner(), NAME_None);
			Pv->SetupAttachment(this);
			Pv->RegisterComponent();
			Pv->SetRelativeLocation(P.PivotBody);
			P.Pivot = Pv;
			Owned.Add(Pv);
			Part(this, P.PivotBody - FVector(0, 0, 5.f), FVector(0.07f, 0.07f, 0.08f), FRotator::ZeroRotator, Boom);   // motor
			Part(Pv, FVector::ZeroVector, FVector(0.46f, 0.035f, 0.006f), FRotator(0.f, 0.f, 0.f), Blade);                // pala (silindir yassı)
			Part(Pv, FVector::ZeroVector, FVector(0.035f, 0.46f, 0.006f), FRotator(0.f, 0.f, 0.f), Blade);
		}
	}
}

void USmvGlbBody::FinishSpinners()
{
	// dönüş yönleri: aynı eksen konumundaki rotorlar (koaksiyel) karşıt; çevresel sırada ardışık rotorlar karşıt
	TArray<int32> Lift;
	for (int32 i = 0; i < Parts.Num(); ++i) if (Parts[i].Kind == EKind::Spinner && Parts[i].Role == ESpinRole::Lift) Lift.Add(i);
	const FVector C = Bounds.GetCenter();
	Lift.Sort([this, C](int32 A, int32 B)
	{
		const FVector Pa = Parts[A].PivotBody - C, Pb = Parts[B].PivotBody - C;
		return FMath::Atan2(Pa.Y, Pa.X) < FMath::Atan2(Pb.Y, Pb.X);
	});
	int32 Slot = 0;
	FVector2D Last(1e9, 1e9);
	for (int32 k = 0; k < Lift.Num(); ++k)
	{
		FPart& P = Parts[Lift[k]];
		const FVector2D Xy(P.PivotBody.X, P.PivotBody.Y);
		if (FVector2D::Distance(Xy, Last) < 3.0) P.Dir = -Parts[Lift[k - 1]].Dir;   // koaksiyel çift
		else { P.Dir = (Slot % 2 == 0) ? 1.f : -1.f; ++Slot; }
		Last = Xy;
	}
	int32 Hm = 0;
	for (FPart& P : Parts)
	{
		if (P.Kind != EKind::Spinner) continue;
		if (P.Role == ESpinRole::HeliMain) P.Dir = (Hm++ % 2 == 0) ? 1.f : -1.f;   // tandem / koaksiyel ana rotorlar karşıt
		const FString L = P.Name.ToLower();
		if (L.Contains(TEXT("lower")) || L.EndsWith(TEXT("main2"))) P.Dir = -1.f;
		if (L.Contains(TEXT("upper"))) P.Dir = 1.f;
		// bulanık disk (hızda görünür; yarı saydam malzeme yoksa eklenmez)
		UMaterialInterface* DiscBase = LoadMat(GDiscMat);
		UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, GCyl);
		if (DiscBase && Cyl && P.Pivot && P.RadiusCm > 3.f)
		{
			UStaticMeshComponent* D = NewObject<UStaticMeshComponent>(GetOwner(), NAME_None);
			D->SetStaticMesh(Cyl);
			D->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			D->SetCastShadow(false);
			D->SetupAttachment(P.Pivot->GetAttachParent());
			D->RegisterComponent();
			const float Dia = P.RadiusCm * 2.f / 100.f;
			D->SetRelativeLocation(P.Pivot->GetRelativeLocation());
			D->SetRelativeRotation(FRotationMatrix::MakeFromZ(P.Axis).Rotator());
			D->SetRelativeScale3D(FVector(Dia, Dia, 0.004f));
			UMaterialInstanceDynamic* Dm = UMaterialInstanceDynamic::Create(DiscBase, this);
			Dm->SetScalarParameterValue(TEXT("Opacity"), 0.f);
			D->SetMaterial(0, Dm);
			D->SetVisibility(false);
			P.Disc = D;
			P.DiscMat = Dm;
			Mids.Add(Dm);
			Owned.Add(D);
		}
	}
}

void USmvGlbBody::Animate(float Dt, const FSmvGlbInputs& In)
{
	TimeS += Dt;
	const TArray<int32> Empty;
	const TArray<int32>& Sv = In.Servo ? *In.Servo : Empty;
	const TArray<int32>& Fn = In.ServoFn ? *In.ServoFn : Empty;
	auto Pwm = [&Sv](int32 Ch) -> int32 { return Sv.IsValidIndex(Ch) ? Sv[Ch] : 0; };
	auto Norm = [](int32 P) -> float { return P >= 800 ? FMath::Clamp((P - 1000) / 1000.f, 0.f, 1.f) : -1.f; };
	auto Angle = [](int32 P) -> float { return P >= 800 ? FMath::Clamp((P - 1500) / 500.f, -1.f, 1.f) : 0.f; };
	const FString& T = Options.Type;
	const float Thr = In.bArmed ? FMath::Clamp(In.ThrottlePct / 100.f, 0.f, 1.f) : 0.f;

	// motor ortalaması (SERVOn_FUNCTION 33..40) ve gaz kanalı (70); işlevler bilinmiyorsa tip varsayılanı
	float MotorSum = 0.f;
	int32 MotorN = 0;
	float ThrCh = -1.f;
	const bool bPlaneLike = T == TEXT("plane") || T == TEXT("vtol") || T == TEXT("interceptor");
	for (int32 i = 0; i < Sv.Num(); ++i)
	{
		const int32 F = Fn.IsValidIndex(i) ? Fn[i] : 0;
		const float V = Norm(Pwm(i));
		if (V < 0.f) continue;
		bool bMotor = F >= 33 && F <= 40;
		if (F == 0 && !bPlaneLike && T != TEXT("heli") && T != TEXT("rover")) bMotor = i < 8;   // kopter: ilk 8 kanal motor
		if (F == 0 && T == TEXT("vtol") && i >= 4 && i < 8) bMotor = true;                        // quadplane: SERVO5..8 motor 1..4
		if (bMotor) { MotorSum += V; ++MotorN; }
		if (F == 70 || (F == 0 && bPlaneLike && i == 2)) ThrCh = V;
	}
	const float MotorAvg = MotorN ? MotorSum / MotorN : -1.f;
	const float LiftTarget = !In.bArmed ? 0.f : (MotorAvg >= 0.f ? MotorAvg : Thr);
	const float ForwardTarget = !In.bArmed ? 0.f : (ThrCh >= 0.f ? ThrCh : (T == TEXT("interceptor") && MotorAvg >= 0.f ? MotorAvg : Thr));
	// helikopter rotor devri: ARM'da ~4 sn'de tam, kapatınca ~8 sn'de durur
	RotorSpool = In.bArmed ? FMath::Min(1.f, RotorSpool + Dt / 4.f) : FMath::Max(0.f, RotorSpool - Dt / 8.f);

	// SITL'in kendi kanal yorumu (SIM_Plane): kanal 1 = aileron, 2 = elevator, 4 = rudder; elevon/vtail karışımı
	float A = Angle(Pwm(0)), E = Angle(Pwm(1)), R = Angle(Pwm(3));
	if (Options.Airframe == TEXT("elevon")) { const float C1 = A, C2 = E; A = (C2 - C1) / 2.f; E = -(C2 + C1) / 2.f; R = 0.f; }
	else if (Options.Airframe == TEXT("vtail")) { const float C1 = E, C2 = R; E = (C2 - C1) / 2.f; R = (C2 + C1) / 2.f; }
	float Flap = 0.f;
	for (int32 i = 0; i < Sv.Num(); ++i) if (Fn.IsValidIndex(i) && (Fn[i] == 2 || Fn[i] == 3)) Flap = FMath::Max(Flap, Norm(Pwm(i)));
	const float Steer = Angle(Pwm(0));

	const bool bNightBlink = FMath::Fmod(TimeS, 1.2) < 0.08;
	for (FPart& P : Parts)
	{
		if (!P.Pivot) continue;
		switch (P.Kind)
		{
		case EKind::Spinner:
		{
			float Target = 0.f;
			float MaxRps = 9.f;
			if (P.Role == ESpinRole::HeliMain) { Target = RotorSpool; MaxRps = 4.5f; }
			else if (P.Role == ESpinRole::HeliTail) { Target = RotorSpool; MaxRps = 13.f; }
			else if (P.Role == ESpinRole::Lift) Target = (T == TEXT("plane")) ? ForwardTarget : LiftTarget;
			else Target = (T == TEXT("vtol") || T == TEXT("plane")) ? ForwardTarget : LiftTarget;
			if (In.bArmed && P.Role != ESpinRole::HeliMain && P.Role != ESpinRole::HeliTail) Target = FMath::Max(Target, 0.08f);   // ARM rölantisi
			P.Speed = FMath::FInterpTo(P.Speed, Target, Dt, 3.f);
			P.Phase = FMath::Fmod(P.Phase + Dt * MaxRps * 360.f * P.Speed * P.Dir, 360.f);
			P.Pivot->SetRelativeRotation(FQuat(P.Axis, FMath::DegreesToRadians(P.Phase)));
			if (P.Disc && P.DiscMat)
			{
				const float Op = FMath::Clamp((P.Speed - 0.2f) / 0.5f, 0.f, 1.f) * 0.35f;
				P.Disc->SetVisibility(Op > 0.01f);
				P.DiscMat->SetScalarParameterValue(TEXT("Opacity"), Op);
			}
			break;
		}
		case EKind::Surface:
		{
			const FString L = P.Name.ToLower();
			const bool bLeft = L.EndsWith(TEXT("_l")) || L.Contains(TEXT("left"));
			float Down = 0.f;   // kuyruk kenarı aşağı (dümende sola) birim sapma
			if (L.StartsWith(TEXT("aileron"))) Down = bLeft ? A : -A;
			else if (L.StartsWith(TEXT("elevon"))) Down = bLeft ? (A - E) : (-A - E);
			else if (L.StartsWith(TEXT("ruddervator"))) Down = bLeft ? (-E - R) : (-E + R);
			else if (L.StartsWith(TEXT("elevator"))) Down = -E;
			else if (L.StartsWith(TEXT("rudder"))) Down = -R;
			else if (L.StartsWith(TEXT("flap"))) Down = Flap;
			const float Deg = FMath::Clamp(Down, -1.f, 1.f) * GSurfaceMaxDeg * P.Dir;
			P.Pivot->SetRelativeRotation(FQuat(P.Axis, FMath::DegreesToRadians(Deg)));
			break;
		}
		case EKind::Wheel:
		{
			const float Rm = P.RadiusCm / 100.f;
			P.Phase = FMath::Fmod(P.Phase - Dt * In.GroundspeedMs / FMath::Max(0.05f, Rm) * 57.2958f, 360.f);
			P.Pivot->SetRelativeRotation(FQuat(P.Axis, FMath::DegreesToRadians(P.Phase)));
			break;
		}
		case EKind::Steer:
			P.Pivot->SetRelativeRotation(FQuat(P.Axis, FMath::DegreesToRadians(Steer * 28.f)));
			break;
		case EKind::Light:
			if (P.Light && P.bBeacon) P.Light->SetIntensity(In.bArmed && bNightBlink ? 60.f : 0.f);
			break;
		default:
			break;
		}
	}
}
