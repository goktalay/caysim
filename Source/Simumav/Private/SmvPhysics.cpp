// Simumav — gerçek fizik çarpışması (bkz. SmvPhysics.h, docs/fizik-carpisma.md).
#include "SmvPhysics.h"
#include "SimumavModule.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "CollisionShape.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Materials/MaterialInterface.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	TAutoConsoleVariable<int32> CVarSmvFizikCiz(
		TEXT("smv.Fizik.Ciz"), -1,
		TEXT("Simumav fizik hata ayıklama çizimi: -1 ayar (DefaultGame.ini [/Script/Simumav.SmvPhysics] Debug), 0 kapalı, 1 gövde çarpışma şekli + temas normali, 2 + sensör ışınları"),
		ECVF_Default);

	const TCHAR* GSmvPhysSection = TEXT("/Script/Simumav.SmvPhysics");

	FString Lower(const FString& S) { return S.ToLower(); }

	bool AnyOf(const FString& S, std::initializer_list<const TCHAR*> Keys)
	{
		for (const TCHAR* K : Keys) if (S.Contains(K)) return true;
		return false;
	}

	/** Çim / çiçek / küçük süs: çarpışma açılmaz (dron çimen yaprağına çarpmaz). */
	bool IsGrassLike(const UStaticMeshComponent* C, const UStaticMesh* M)
	{
		const FString N = Lower(M->GetName());
		if (AnyOf(N, { TEXT("grass"), TEXT("flower"), TEXT("weed"), TEXT("clover"), TEXT("moss"), TEXT("decal"), TEXT("pebble"), TEXT("ivy"), TEXT("dandelion") })) return true;
		const float R = M->GetBounds().SphereRadius * (float)C->GetComponentScale().GetAbsMax();
		return R < 30.f;
	}

	FColor ClassColor(SmvCore::EContact C, bool bTouch)
	{
		if (!bTouch) return FColor(40, 220, 90);
		if (SmvCore::ContactIsCrash(C)) return FColor(240, 40, 30);
		return FColor(250, 200, 30);
	}
}

// ---------------------------------------------------------------------------
// ayarlar
// ---------------------------------------------------------------------------

const FSmvPhysicsSettings& FSmvPhysicsSettings::Get()
{
	static FSmvPhysicsSettings S;
	static bool bLoaded = false;
	if (bLoaded || !GConfig) return S;
	bLoaded = true;
	GConfig->GetBool(GSmvPhysSection, TEXT("Enabled"), S.bEnabled, GGameIni);
	GConfig->GetFloat(GSmvPhysSection, TEXT("ShapeScale"), S.ShapeScale, GGameIni);
	GConfig->GetFloat(GSmvPhysSection, TEXT("SkinCm"), S.SkinCm, GGameIni);
	GConfig->GetBool(GSmvPhysSection, TEXT("TraceComplex"), S.bTraceComplex, GGameIni);
	GConfig->GetInt(GSmvPhysSection, TEXT("Sectors"), S.Sectors, GGameIni);
	GConfig->GetFloat(GSmvPhysSection, TEXT("ProxMaxM"), S.ProxMaxM, GGameIni);
	GConfig->GetFloat(GSmvPhysSection, TEXT("RngMaxM"), S.RngMaxM, GGameIni);
	GConfig->GetFloat(GSmvPhysSection, TEXT("SensorHz"), S.SensorHz, GGameIni);
	GConfig->GetBool(GSmvPhysSection, TEXT("SitlSensors"), S.bSitlSensors, GGameIni);
	GConfig->GetBool(GSmvPhysSection, TEXT("SitlAvoid"), S.bSitlAvoid, GGameIni);
	GConfig->GetFloat(GSmvPhysSection, TEXT("PenCrashM"), S.PenCrashM, GGameIni);
	GConfig->GetFloat(GSmvPhysSection, TEXT("SustainCrashS"), S.SustainCrashS, GGameIni);
	GConfig->GetFloat(GSmvPhysSection, TEXT("RestDisarmS"), S.RestDisarmS, GGameIni);
	GConfig->GetFloat(GSmvPhysSection, TEXT("ProxWarnM"), S.ProxWarnM, GGameIni);
	GConfig->GetInt(GSmvPhysSection, TEXT("Debug"), S.Debug, GGameIni);
	FString Ch;
	if (GConfig->GetString(GSmvPhysSection, TEXT("Channel"), Ch, GGameIni))
	{
		if (Ch.Equals(TEXT("WorldDynamic"), ESearchCase::IgnoreCase)) S.Channel = ECC_WorldDynamic;
		else if (Ch.Equals(TEXT("Camera"), ESearchCase::IgnoreCase)) S.Channel = ECC_Camera;
		else if (Ch.Equals(TEXT("WorldStatic"), ESearchCase::IgnoreCase)) S.Channel = ECC_WorldStatic;
	}
	S.Sectors = S.Sectors <= 8 ? 8 : 72;
	S.ShapeScale = FMath::Clamp(S.ShapeScale, 0.2f, 2.f);
	S.SkinCm = FMath::Clamp(S.SkinCm, 0.1f, 20.f);
	S.SensorHz = FMath::Clamp(S.SensorHz, 1.f, 50.f);
	S.ProxMaxM = FMath::Clamp(S.ProxMaxM, 2.f, 600.f);
	S.RngMaxM = FMath::Clamp(S.RngMaxM, 2.f, 600.f);
	UE_LOG(LogSimumav, Log, TEXT("Fizik: %s, şekil ölçeği %.2f, %s, kanal %d, %d sektör, yakınlık %.0f m, menzil bulucu %.0f m, %.0f Hz, SITL sensör %s, kaçınma %s"),
		S.bEnabled ? TEXT("açık") : TEXT("KAPALI"), S.ShapeScale, S.bTraceComplex ? TEXT("karmaşık çarpışma") : TEXT("basit çarpışma"), (int32)S.Channel,
		S.Sectors, S.ProxMaxM, S.RngMaxM, S.SensorHz, S.bSitlSensors ? TEXT("açık") : TEXT("kapalı"), S.bSitlAvoid ? TEXT("açık") : TEXT("kapalı"));
	return S;
}

int32 FSmvPhysicsSettings::DebugLevel()
{
	const int32 C = CVarSmvFizikCiz.GetValueOnGameThread();
	return C >= 0 ? C : Get().Debug;
}

FString FSmvContactEvent::Describe() const
{
	return FString::Printf(TEXT("sınıf=%s%s tür=%s aktör=%s bileşen=%s malzeme=%s fizik_malzemesi=%s normal=(%.2f, %.2f, %.2f) yukarı·n=%.2f Vn=%.2f Vt=%.2f hız=%.2f m/s batma=%.2f m süre=%.1f sn nokta=(%.0f, %.0f, %.0f)%s"),
		UTF8_TO_TCHAR(SmvCore::ContactName(Class)), bEscalated ? TEXT(" (sürekli temas)") : TEXT(""), *Kind, *Actor, *Component, *Material, *PhysMaterial,
		Normal.X, Normal.Y, Normal.Z, Nz, Vn, Vt, Speed, PenetrationM, DurationS, Point.X, Point.Y, Point.Z, bRestDisarm ? TEXT(" → yapı üstünde iniş, motor durdurma") : TEXT(""));
}

// ---------------------------------------------------------------------------
// yardımcılar
// ---------------------------------------------------------------------------

FCollisionQueryParams SmvPhysics::MakeQuery(FName Tag, const AActor* Self, const TArray<TWeakObjectPtr<AActor>>* Ignore)
{
	const FSmvPhysicsSettings& S = FSmvPhysicsSettings::Get();
	FCollisionQueryParams Q(Tag, S.bTraceComplex, Self);
	Q.bReturnPhysicalMaterial = true;
	Q.bReturnFaceIndex = S.bTraceComplex;
	Q.bFindInitialOverlaps = true;
	if (Ignore) for (const TWeakObjectPtr<AActor>& A : *Ignore) if (A.IsValid()) Q.AddIgnoredActor(A.Get());
	if (Self) IgnoreClippedTiles(Q, Self->GetActorLocation());
	return Q;
}

namespace
{
	TWeakObjectPtr<const AActor> GClipTiles;
	FBox2D GClipBox(ForceInit);
}

void SmvPhysics::SetClipRegion(const AActor* Tiles, const FBox2D& Box)
{
	GClipTiles = Tiles;
	GClipBox = Box;
}

void SmvPhysics::IgnoreClippedTiles(FCollisionQueryParams& Q, const FVector& P)
{
	// Fab haritası alanı 3D Tiles'tan görsel olarak kırpılır; kenarı kesen karoların fizik ağı orada hâlâ var → içerideyken yok sayılır
	if (GClipTiles.IsValid() && GClipBox.bIsValid && GClipBox.IsInside(FVector2D(P.X, P.Y))) Q.AddIgnoredActor(GClipTiles.Get());
}

FString SmvPhysics::MaterialName(const FHitResult& H)
{
	UPrimitiveComponent* C = H.GetComponent();
	if (!C) return TEXT("-");
	if (H.FaceIndex >= 0)
	{
		int32 Section = 0;
		if (UMaterialInterface* M = C->GetMaterialFromCollisionFaceIndex(H.FaceIndex, Section)) return M->GetName();
	}
	if (C->GetNumMaterials() > 0)
	{
		if (UMaterialInterface* M = C->GetMaterial(0)) return M->GetName();
	}
	return TEXT("-");
}

FString SmvPhysics::SurfaceKind(const FHitResult& H, const FVector& Up)
{
	const UPrimitiveComponent* C = H.GetComponent();
	const AActor* A = H.GetActor();
	if (C)
	{
		if (C->ComponentHasTag(TEXT("water"))) return TEXT("water");
		if (C->ComponentHasTag(TEXT("building"))) return TEXT("building");
		if (C->ComponentHasTag(TEXT("tree"))) return TEXT("tree");
		if (C->ComponentHasTag(TEXT("ground")) || C->ComponentHasTag(TEXT("terrain"))) return TEXT("terrain");
		const FString Cn = C->GetClass()->GetName();
		if (Cn.Contains(TEXT("Landscape"))) return TEXT("terrain");
		// Cesium 3B karoları (fotogrametri: arazi + binalar tek ağ) → yüzey yönüne göre
		if (Cn.Contains(TEXT("Cesium"))) return FVector::DotProduct(H.ImpactNormal, Up) >= 0.7 ? TEXT("terrain") : TEXT("building");
	}
	if (A && A->GetClass()->GetName().Contains(TEXT("InstancedFoliage"))) return TEXT("tree");
	FString N;
	if (const UStaticMeshComponent* SC = Cast<UStaticMeshComponent>(C)) if (SC->GetStaticMesh()) N += SC->GetStaticMesh()->GetName() + TEXT(" ");
	if (C) N += C->GetName() + TEXT(" ");
	if (A) N += A->GetName();
	N = Lower(N);
	if (AnyOf(N, { TEXT("water"), TEXT("ocean"), TEXT("lake"), TEXT("river"), TEXT("sea_") })) return TEXT("water");
	if (AnyOf(N, { TEXT("tree"), TEXT("trunk"), TEXT("branch"), TEXT("foliage"), TEXT("pine"), TEXT("oak"), TEXT("birch"), TEXT("spruce"), TEXT("fir_"),
		TEXT("bush"), TEXT("palm"), TEXT("conifer"), TEXT("beech"), TEXT("crown") })) return TEXT("tree");
	if (AnyOf(N, { TEXT("build"), TEXT("house"), TEXT("wall"), TEXT("roof"), TEXT("tower"), TEXT("apartment"), TEXT("facade"), TEXT("bldg"), TEXT("window"),
		TEXT("door"), TEXT("floor"), TEXT("ceiling"), TEXT("stair"), TEXT("concrete"), TEXT("brick"), TEXT("hangar"), TEXT("tank_"), TEXT("pipe"), TEXT("refiner") })) return TEXT("building");
	if (AnyOf(N, { TEXT("rock"), TEXT("cliff"), TEXT("stone"), TEXT("boulder"), TEXT("terrain"), TEXT("ground"), TEXT("mountain"), TEXT("hill"), TEXT("road"), TEXT("asphalt") })) return TEXT("terrain");
	return TEXT("obstacle");
}

// ---------------------------------------------------------------------------
// gövde çözücüsü
// ---------------------------------------------------------------------------

void FSmvContactSolver::Configure(const TArray<FSmvBodyBox>& InBoxes, SmvCore::EVehicleClass InVehicle)
{
	Boxes = InBoxes;
	Vehicle = InVehicle;
	MinHalfCm = 1e9;
	double Bottom = 0;
	for (const FSmvBodyBox& B : Boxes)
	{
		MinHalfCm = FMath::Min(MinHalfCm, B.Half.GetMin());
		Bottom = FMath::Min(Bottom, B.Center.Z - B.Half.Z);
	}
	if (Boxes.Num() == 0) MinHalfCm = 20;
	RestPen0M = -Bottom / 100.0;
	Reset();
}

void FSmvContactSolver::Reset()
{
	bHas = false;
	bInContact = false;
	CurClass = SmvCore::EContact::None;
	RestSince = -1;
	bRestSent = false;
	PenM = 0;
}

bool FSmvContactSolver::SweepBody(UWorld* W, const FVector& From, const FVector& To, const FQuat& Rot, const FCollisionQueryParams& Q, FHitResult& OutHit, FVector& OutPos) const
{
	const FSmvPhysicsSettings& S = FSmvPhysicsSettings::Get();
	bool bAny = false;
	float BestT = 2.f;
	TArray<FHitResult> Hits;
	for (const FSmvBodyBox& B : Boxes)
	{
		const FVector Off = Rot.RotateVector(B.Center);
		Hits.Reset();
		W->SweepMultiByChannel(Hits, From + Off, To + Off, Rot, S.Channel, FCollisionShape::MakeBox(B.Half), Q);
		for (const FHitResult& H : Hits)
		{
			if (!H.bBlockingHit) continue;
			const float T = H.bStartPenetrating ? -1.f : H.Time;
			if (T < BestT)
			{
				BestT = T;
				OutHit = H;
				OutPos = H.Location - Off;
				bAny = true;
			}
		}
	}
	return bAny;
}

bool FSmvContactSolver::Depenetrate(UWorld* W, FVector& Pos, const FQuat& Rot, const FCollisionQueryParams& Q, FHitResult* OutHit) const
{
	const FSmvPhysicsSettings& S = FSmvPhysicsSettings::Get();
	TArray<FHitResult> Hits;
	for (int32 It = 0; It < 4; ++It)
	{
		bool bMoved = false;
		for (const FSmvBodyBox& B : Boxes)
		{
			const FVector C = Pos + Rot.RotateVector(B.Center);
			Hits.Reset();
			// çok kısa süpürme: başlangıç çakışmasında motor MTD'yi (çıkış yönü + derinlik) hesaplar (karmaşık çarpışma dahil)
			W->SweepMultiByChannel(Hits, C, C + FVector(0.0, 0.0, 0.1), Rot, S.Channel, FCollisionShape::MakeBox(B.Half), Q);
			for (const FHitResult& H : Hits)
			{
				if (!H.bBlockingHit || !H.bStartPenetrating) continue;
				Pos += H.Normal * (FMath::Max(0.0, (double)H.PenetrationDepth) + S.SkinCm);
				if (OutHit && !OutHit->bBlockingHit) *OutHit = H;
				bMoved = true;
				break;
			}
			if (bMoved) break;
		}
		if (!bMoved) return true;
	}
	return false;
}

FVector FSmvContactSolver::Resolve(UWorld* W, const AActor* Self, const FVector& Desired, const FQuat& Rot, const FQuat& EnuRot, const FVector& VelMs, bool bArmed)
{
	const FSmvPhysicsSettings& S = FSmvPhysicsSettings::Get();
	if (!W || Boxes.Num() == 0) return Desired;
	const FCollisionQueryParams Q = SmvPhysics::MakeQuery(TEXT("SmvBody"), Self, &Ignore);
	// ilk kare / sıfırlama / SITL'in kendi konumunun büyük sıçraması (yeniden başlatma, ışınlama): ışınla, yalnız çakışmadan çıkar, olay üretme.
	// Sıçrama İSTENEN konumun önceki istenen konuma göre farkıdır — çözülmüş konuma uzaklık DEĞİL: duvara dayanmış aracın SITL konumu
	// binanın içinden / arkasından uzaklaşmayı sürdürür; eskiden 50 m'yi geçince araç duvarın öbür yüzüne ışınlanıyordu (binanın içinden geçme).
	const bool bJump = bHas && FVector::DistSquared(Desired, LastDesired) > FMath::Square(20000.0);   // 200 m tek karede (uçak + takılma dahil değil)
	LastDesired = Desired;
	if (!bHas || bJump)
	{
		FVector P = Desired;
		Depenetrate(W, P, Rot, Q, nullptr);
		Resolved = P;
		bHas = true;
		bInContact = false;
		CurClass = SmvCore::EContact::None;
		RestSince = -1;
		return Resolved;
	}
	FVector Pos = Resolved;
	FHitResult First;
	bool bTouch = false;
	// sürekli çarpışma: her parça önceki çözülmüş konumdan süpürülür (atlama yok); uzun yol en çok 4 parçaya bölünür, temas yüzey boyunca kaydırır
	const FVector Full = Desired - Pos;
	const int32 Parts = FMath::Clamp(FMath::CeilToInt(Full.Size() / FMath::Max(4.0 * MinHalfCm, 50.0)), 1, 4);
	for (int32 Part = 1; Part <= Parts; ++Part)
	{
		const FVector Target = Resolved + Full * ((double)Part / Parts);
		FVector Delta = Target - Pos;
		for (int32 It = 0; It < 4 && Delta.SizeSquared() > 0.01; ++It)
		{
			FHitResult H;
			FVector HitPos;
			if (!SweepBody(W, Pos, Pos + Delta, Rot, Q, H, HitPos)) { Pos += Delta; Delta = FVector::ZeroVector; break; }
			if (H.bStartPenetrating)
			{
				// başlangıçta çakışma: önceki kare serbest bırakıldığı için araç buraya kendisi girmedi — geometri belirdi (akış / LOD / Cesium
				// karosu) ya da temas hâlindeki gövde döndü. Yalnız dışarı itilir; süren temas yoksa yeni temas olayı sayılmaz.
				if (!bTouch && bInContact) { First = H; bTouch = true; }
				Pos += H.Normal * (FMath::Max(0.0, (double)H.PenetrationDepth) + S.SkinCm);
				continue;
			}
			if (!bTouch) { First = H; bTouch = true; }
			Pos = HitPos + H.Normal * S.SkinCm;
			const FVector Rem = Delta * (1.0 - H.Time);
			Delta = Rem - FVector(H.Normal) * FVector::DotProduct(Rem, H.Normal);   // yüzey boyunca kaydır
		}
	}
	FHitResult Pen;
	if (!Depenetrate(W, Pos, Rot, Q, &Pen))
	{
		Pos = Resolved;   // çözülemeyen çakışma (ince/kapalı geometri): son güvenli konumda kal
	}
	if (!bTouch && Pen.bBlockingHit && bInContact) { First = Pen; bTouch = true; }
	UpdateContact(bTouch, First, Desired, Pos, EnuRot, VelMs, bArmed);
	Resolved = Pos;
	return Pos;
}

void FSmvContactSolver::UpdateContact(bool bTouch, const FHitResult& H, const FVector& Desired, const FVector& Pos, const FQuat& EnuRot, const FVector& VelMs, bool bArmed)
{
	const FSmvPhysicsSettings& S = FSmvPhysicsSettings::Get();
	const double Now = FPlatformTime::Seconds();
	if (!bTouch)
	{
		// kısa boşluk (bir iki kare) teması bitirmez
		if (bInContact && Now - LastTouch > 0.2)
		{
			bInContact = false;
			CurClass = SmvCore::EContact::None;
			RestSince = -1;
			PenM = 0;
		}
		if (!bArmed) bRestSent = false;
		return;
	}
	LastTouch = Now;
	LastHit = H;
	const FVector Up = EnuRot.RotateVector(FVector::UpVector);
	FVector N = H.ImpactNormal.IsNearlyZero() ? H.Normal : H.ImpactNormal;
	N = N.GetSafeNormal();
	if (N.IsNearlyZero()) N = Up;
	const FQuat Inv = EnuRot.Inverse();
	const FVector Nl = Inv.RotateVector(N), Vl = Inv.RotateVector(VelMs);
	const FString Kind = SmvPhysics::SurfaceKind(H, Up);
	SmvCore::FContactIn In;
	In.Nx = Nl.X; In.Ny = Nl.Y; In.Nz = Nl.Z;
	In.Vx = Vl.X; In.Vy = Vl.Y; In.Vz = Vl.Z;
	In.Vehicle = Vehicle;
	In.bWater = Kind == TEXT("water");
	const SmvCore::FContactOut O = SmvCore::ClassifyContact(In);
	PenM = FMath::Max(0.0, FVector::DotProduct(Desired - Pos, -N)) / 100.0;
	if (!bInContact) { bInContact = true; ContactStart = Now; CurClass = SmvCore::EContact::None; }
	const double Dur = Now - ContactStart;
	// zemindeki olağan batma (gövde altı) çıkarılır: yerde duran aracın SITL konumu yüzeyde
	const double PenExtra = O.Class == SmvCore::EContact::Landing ? FMath::Max(0.0, PenM - RestPen0M) : PenM;
	const SmvCore::EContact C = SmvCore::EscalateSustained(O.Class, PenExtra, Dur, S.PenCrashM, S.SustainCrashS);
	auto Emit = [&](bool bRest)
	{
		FSmvContactEvent E;
		E.Class = C;
		E.Kind = (C == SmvCore::EContact::CeilingHit || C == SmvCore::EContact::CeilingTouch) ? FString(TEXT("ceiling")) : Kind;
		E.Point = H.ImpactPoint;
		E.Normal = N;
		E.Nz = Nl.Z;
		E.Vn = O.Vn; E.Vt = O.Vt; E.Speed = O.Speed;
		E.PenetrationM = PenExtra;
		E.DurationS = Dur;
		E.Actor = H.GetActor() ? H.GetActor()->GetName() : TEXT("-");
		E.Component = H.GetComponent() ? H.GetComponent()->GetName() : TEXT("-");
		E.Material = SmvPhysics::MaterialName(H);
		E.PhysMaterial = H.PhysMaterial.IsValid() ? H.PhysMaterial->GetName() : TEXT("-");
		E.bEscalated = C != O.Class;
		E.bRestDisarm = bRest;
		E.Wall = Now;
		LastEvent = Now;
		if (Events.Num() < 32) Events.Add(MoveTemp(E));
	};
	const bool bNew = CurClass == SmvCore::EContact::None;
	const bool bWorse = SmvCore::ContactIsCrash(C) && !SmvCore::ContactIsCrash(CurClass);
	if (C != SmvCore::EContact::None && (bNew || bWorse || (C != CurClass && Now - LastEvent > 1.0))) Emit(false);
	CurClass = C;
	// yapı üstüne iniş: dikey araç zeminde, kollu, SITL hâlâ yüzeyin altına iniyor (SITL zemini aşağıda) → motor durdurma isteği
	if (C == SmvCore::EContact::Landing && Vehicle == SmvCore::EVehicleClass::Vertical && bArmed && PenExtra > 0.5 && Vl.Z <= 0.3)
	{
		if (RestSince < 0) RestSince = Now;
		if (!bRestSent && Now - RestSince >= S.RestDisarmS) { bRestSent = true; Emit(true); }
	}
	else RestSince = -1;
	if (!bArmed) bRestSent = false;
}

void FSmvContactSolver::DrawDebug(UWorld* W, const FVector& Pos, const FQuat& Rot) const
{
#if ENABLE_DRAW_DEBUG
	if (!W) return;
	const FColor Col = ClassColor(CurClass, bInContact);
	for (const FSmvBodyBox& B : Boxes) DrawDebugBox(W, Pos + Rot.RotateVector(B.Center), B.Half, Rot, Col, false, -1.f, 0, 1.5f);
	if (bInContact && LastHit.bBlockingHit)
	{
		DrawDebugPoint(W, LastHit.ImpactPoint, 12.f, Col, false, 0.5f);
		DrawDebugLine(W, LastHit.ImpactPoint, LastHit.ImpactPoint + LastHit.ImpactNormal * 150.0, Col, false, 0.5f, 0, 2.f);
	}
#endif
}

// ---------------------------------------------------------------------------
// engel sensörleri
// ---------------------------------------------------------------------------

void SmvPhysics::Scan(UWorld* W, const FVector& Origin, const FQuat& BodyRot, const FQuat& EnuRot, const FCollisionQueryParams& Q, FSmvSensorScan& Out, bool bDraw)
{
	const FSmvPhysicsSettings& S = FSmvPhysicsSettings::Get();
	Out = FSmvSensorScan();
	if (!W) return;
	Out.bValid = true;
	const FVector Up = EnuRot.RotateVector(FVector::UpVector);
	auto Ray = [&](const FVector& Dir, float MaxM) -> float
	{
		FHitResult H;
		const FVector End = Origin + Dir * (MaxM * 100.0);
		const bool bHit = W->LineTraceSingleByChannel(H, Origin, End, S.Channel, Q);
#if ENABLE_DRAW_DEBUG
		if (bDraw) DrawDebugLine(W, Origin, bHit ? H.ImpactPoint : End, bHit ? FColor(255, 120, 0) : FColor(60, 160, 255), false, 1.f / S.SensorHz + 0.02f, 0, 0.8f);
#endif
		return bHit ? (float)(H.Distance / 100.0) : -1.f;
	};
	// aşağı menzil bulucu: gövde ekseni boyunca (ArduPilot eğim düzeltmesini kendisi yapar)
	Out.DownM = Ray(-BodyRot.GetUpVector(), S.RngMaxM);
	Out.UpM = Ray(Up, S.ProxMaxM);
	// 360° yakınlık: burun yönünün yatay izdüşümü, saat yönünde (UE'de +Z çevresinde pozitif dönüş yukarıdan saat yönüdür)
	FVector Fwd = BodyRot.GetForwardVector();
	Fwd = (Fwd - FVector::DotProduct(Fwd, Up) * Up).GetSafeNormal();
	if (Fwd.IsNearlyZero()) Fwd = (BodyRot.GetUpVector() - FVector::DotProduct(BodyRot.GetUpVector(), Up) * Up).GetSafeNormal();
	if (Fwd.IsNearlyZero()) return;
	Out.Sectors = S.Sectors;
	Out.SectorM.Init(-1.f, S.Sectors);
	const int32 Sub = S.Sectors <= 8 ? 5 : 1;
	for (int32 i = 0; i < S.Sectors; ++i)
	{
		float Best = -1.f;
		for (double B : SmvCore::SectorSubRays(i, S.Sectors, Sub))
		{
			const FVector Dir = FQuat(Up, FMath::DegreesToRadians(B)).RotateVector(Fwd);
			const float D = Ray(Dir, S.ProxMaxM);
			if (D >= 0 && (Best < 0 || D < Best)) Best = D;
		}
		Out.SectorM[i] = Best;
		if (Best >= 0 && (Out.NearestM < 0 || Best < Out.NearestM)) { Out.NearestM = Best; Out.NearestBearingDeg = (float)SmvCore::SectorBearingDeg(i, S.Sectors); }
	}
}

// ---------------------------------------------------------------------------
// Fab seviyeleri: çarpışma hazırlığı
// ---------------------------------------------------------------------------

FString SmvPhysics::PrepareLevelCollision(ULevel* Level)
{
	if (!Level) return FString();
	const FSmvPhysicsSettings& S = FSmvPhysicsSettings::Get();
	const UPhysicsSettings* PhysSet = UPhysicsSettings::Get();
	const ECollisionTraceFlag Def = PhysSet ? (ECollisionTraceFlag)PhysSet->DefaultShapeComplexity.GetValue() : CTF_UseSimpleAndComplex;
	int32 Comps = 0, Inst = 0, Land = 0, Opened = 0, Grass = 0, Hidden = 0;
	TSet<const UStaticMesh*> Meshes, Simple, ComplexOnly, NoColl;
	TArray<FString> NoCollNames;
	for (AActor* A : Level->Actors)
	{
		if (!IsValid(A)) continue;
		TInlineComponentArray<UPrimitiveComponent*> Prims(A);
		for (UPrimitiveComponent* C : Prims)
		{
			if (!C || !C->IsRegistered()) continue;
			if (C->GetClass()->GetName().Contains(TEXT("Landscape")))
			{
				++Land;
				if (!CollisionEnabledHasQuery(C->GetCollisionEnabled())) { C->SetCollisionEnabled(ECollisionEnabled::QueryOnly); ++Opened; }
				if (C->GetCollisionResponseToChannel(S.Channel) != ECR_Block) C->SetCollisionResponseToChannel(S.Channel, ECR_Block);
				continue;
			}
			UStaticMeshComponent* SC = Cast<UStaticMeshComponent>(C);
			UStaticMesh* M = SC ? SC->GetStaticMesh() : nullptr;
			if (!M) continue;
			++Comps;
			if (Cast<UInstancedStaticMeshComponent>(SC)) ++Inst;
			if (!Meshes.Contains(M))
			{
				Meshes.Add(M);
				const UBodySetup* BS = M->GetBodySetup();
				const bool bSimple = BS && BS->AggGeom.GetElementCount() > 0;
				ECollisionTraceFlag F = BS ? (ECollisionTraceFlag)BS->CollisionTraceFlag.GetValue() : CTF_UseSimpleAsComplex;
				if (F == CTF_UseDefault) F = Def;
				if (bSimple) Simple.Add(M);
				else if (BS && F != CTF_UseSimpleAsComplex) ComplexOnly.Add(M);
				else { NoColl.Add(M); if (NoCollNames.Num() < 12) NoCollNames.Add(M->GetName()); }
			}
			if (!C->IsVisible() || A->IsHidden()) { ++Hidden; continue; }
			if (IsGrassLike(SC, M)) { ++Grass; continue; }
			bool bChanged = false;
			if (!CollisionEnabledHasQuery(C->GetCollisionEnabled())) { C->SetCollisionEnabled(ECollisionEnabled::QueryOnly); bChanged = true; }
			if (C->GetCollisionResponseToChannel(S.Channel) != ECR_Block) { C->SetCollisionResponseToChannel(S.Channel, ECR_Block); bChanged = true; }
			if (bChanged) ++Opened;
		}
	}
	return FString::Printf(TEXT("%s: %d ağ bileşeni (%d örnekli), %d Landscape; çarpışması açılan %d, çim/küçük %d, görünmez %d; benzersiz ağ %d — basit çarpışmalı %d, yalnız karmaşık %d, çarpışmasız ağ sayısı %d%s%s"),
		*Level->GetOuter()->GetName(), Comps, Inst, Land, Opened, Grass, Hidden, Meshes.Num(), Simple.Num(), ComplexOnly.Num(), NoColl.Num(),
		NoCollNames.Num() ? TEXT(" (ör. ") : TEXT(""), NoCollNames.Num() ? *(FString::Join(NoCollNames, TEXT(", ")) + TEXT(")")) : TEXT(""));
}

// ---------------------------------------------------------------------------
// kalkış noktası
// ---------------------------------------------------------------------------

bool SmvPhysics::EvaluateSpot(UWorld* W, double X, double Y, double ZTop, double ZBot, float RadiusCm, float HeightCm, bool bRequireLandscape,
	const FCollisionQueryParams& Q, double& OutZ, FString* OutWhy)
{
	const FSmvPhysicsSettings& S = FSmvPhysicsSettings::Get();
	auto Fail = [OutWhy](const FString& Why) { if (OutWhy) *OutWhy = Why; return false; };
	if (!W) return Fail(TEXT("dünya yok"));
	FHitResult H;
	if (!W->LineTraceSingleByChannel(H, FVector(X, Y, ZTop), FVector(X, Y, ZBot), S.Channel, Q)) return Fail(TEXT("zemin yok"));
	if (H.ImpactNormal.Z < 0.95) return Fail(FString::Printf(TEXT("eğim %.0f°"), FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(H.ImpactNormal.Z, -1.0, 1.0)))));
	const bool bLand = H.GetComponent() && H.GetComponent()->GetClass()->GetName().Contains(TEXT("Landscape"));
	if (bRequireLandscape && !bLand) return Fail(FString::Printf(TEXT("arazi değil (%s)"), H.GetActor() ? *H.GetActor()->GetName() : TEXT("-")));
	const FVector G = H.ImpactPoint;
	const float R = FMath::Max(RadiusCm, 50.f) * 1.25f;
	const float HalfH = FMath::Max(HeightCm * 0.5f, R);
	// üstü açık mı: araç genişliğinde küre yukarı süpürülür (tavan, saçak, ağaç dalı) — 60 m
	FHitResult U;
	if (W->SweepSingleByChannel(U, G + FVector(0, 0, HalfH * 2.0 + 40.0), G + FVector(0, 0, 6000.0), FQuat::Identity, S.Channel, FCollisionShape::MakeSphere(R), Q))
		return Fail(FString::Printf(TEXT("üstü kapalı %.1f m (%s)"), (U.ImpactPoint.Z - G.Z) / 100.0, U.GetActor() ? *U.GetActor()->GetName() : TEXT("-")));
	// boşluk: araç boyutunda kapsül (yerden 20 cm yukarıda) hiçbir şeye değmemeli
	if (W->OverlapBlockingTestByChannel(G + FVector(0, 0, HalfH + 20.0), FQuat::Identity, S.Channel, FCollisionShape::MakeCapsule(R, HalfH), Q))
		return Fail(TEXT("dar (gövde boyutunda boşluk yok)"));
	// çevre düzlüğü: kapsül yarıçapının iki katında dört nokta ±(arazide 1,2 / yapıda 0,6 m)
	const double Tol = bLand ? 120.0 : 60.0;
	for (const FVector2D& O : { FVector2D(2.0 * R, 0), FVector2D(-2.0 * R, 0), FVector2D(0, 2.0 * R), FVector2D(0, -2.0 * R) })
	{
		FHitResult H2;
		if (!W->LineTraceSingleByChannel(H2, FVector(X + O.X, Y + O.Y, G.Z + 500.0), FVector(X + O.X, Y + O.Y, G.Z - 1000.0), S.Channel, Q)) return Fail(TEXT("kenar"));
		if (FMath::Abs(H2.ImpactPoint.Z - G.Z) > Tol) return Fail(TEXT("engebeli"));
	}
	OutZ = G.Z;
	if (OutWhy) *OutWhy = FString::Printf(TEXT("uygun (%s)"), bLand ? TEXT("arazi") : (H.GetActor() ? *H.GetActor()->GetName() : TEXT("-")));
	return true;
}
