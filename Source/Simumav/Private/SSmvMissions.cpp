// Görevler (kullanıcı 2026-11-01): solda görev listesi, ortada görevin gerçek dünya (Cesium) üzerindeki 3B çizimi, sağda özet;
// altta sağda Yeni görev / Düzenle / Oynat. Yeni görev sihirbazı (Koridor / İlaçlama / Spiral / Serbest + ad) → tam ekran Cesium
// düzenleyici: üstte yüzen başlık (Geri · ad · Kaydet), altta türe göre ayarlar. Çizim: noktalar arası silik sarı çizgi + mesafe,
// zeminden noktaya dik silik mavi çizgi + yükseklik. Rota üreteçleri: Core/SmvMissionGen. Eski türler (yarış kapıları, savunma,
// tarama, bayrak) eski düzenleyicide (MissionsLegacy) açılır.
#include "SSmvShell.h"
#include "SimumavModule.h"
#include "SmvDataService.h"
#include "SmvFlow.h"
#include "SmvMenuScene.h"
#include "SmvUiPrefs.h"
#include "SmvI18n.h"
#include "SmvTileMap.h"
#include "SmvLocalFlightLink.h"
#include "SmvMissionGen.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SViewport.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

using namespace SmvUi;

/** düzenlenen görev (v2 planı) */
struct FSmvMissionEdit
{
	FString Id, TrackId, Name, Kind = TEXT("corridor");
	double HomeLat = 41.0082, HomeLon = 28.9784;
	TArray<SmvCore::FMissionPoint> Pts;
	SmvCore::FMissionParams Par;
	SmvCore::FMissionResult Res;
	bool bLegacy = false;
	FString LoadedFor;
	// düzenleyici durumu
	int32 Drag = -1, Hover = -1;
	bool bHomePick = false, bPanning = false, bRotating = false, bMoved = false;
	FVector2D DownAt = FVector2D::ZeroVector, LastAt = FVector2D::ZeroVector;
	// serbest nokta penceresi
	bool bDlg = false;
	int32 DlgIndex = -1;
	SmvCore::FMissionPoint Draft;
	FString DraftAction = TEXT("next"), DraftScript;
	int32 DraftTurns = 1;
	void Rebuild()
	{
		std::vector<SmvCore::FMissionPoint> P(Pts.GetData(), Pts.GetData() + Pts.Num());
		Res = SmvCore::BuildMission(std::string(TCHAR_TO_UTF8(*Kind)), HomeLat, HomeLon, P, Par);
	}
	bool IsNewKind() const { return Kind == TEXT("corridor") || Kind == TEXT("spray") || Kind == TEXT("spiral") || Kind == TEXT("free"); }
};

namespace
{
	const TCHAR* GMKinds[] = { TEXT("corridor"), TEXT("spray"), TEXT("spiral"), TEXT("free") };

	ESmvIcon MKindIcon(const FString& K)
	{
		return K == TEXT("corridor") ? ESmvIcon::Map : K == TEXT("spray") ? ESmvIcon::Rain : K == TEXT("spiral") ? ESmvIcon::Wind
			: K == TEXT("free") ? ESmvIcon::Route : K == TEXT("defend") ? ESmvIcon::Shield : K == TEXT("fpv_gates") ? ESmvIcon::Fpv : ESmvIcon::Mission;
	}

	FString MTime(double S)
	{
		const int32 M = FMath::FloorToInt(S / 60.0);
		return M >= 60 ? FString::Printf(TEXT("%d sa %02d dk"), M / 60, M % 60) : FString::Printf(TEXT("%d dk %02d sn"), M, FMath::FloorToInt(S) % 60);
	}

	FString MDist(double M) { return M >= 1000.0 ? FString::Printf(TEXT("%.2f km"), M / 1000.0) : FString::Printf(TEXT("%.0f m"), M); }

	/** cihazlardaki (dron klasörlerindeki) Lua betikleri — serbest görevde "Betik" işlemi */
	TArray<FString> MAllScripts()
	{
		TArray<FString> Out;
		const FString Root = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Simumav"), TEXT("Drones")));
		TArray<FString> Dirs;
		IFileManager::Get().FindFiles(Dirs, *FPaths::Combine(Root, TEXT("*")), false, true);
		for (const FString& D : Dirs)
		{
			TArray<FString> L;
			IFileManager::Get().FindFiles(L, *FPaths::Combine(Root, D, TEXT("scripts"), TEXT("*.lua")), true, false);
			for (const FString& F : L) Out.AddUnique(F);
		}
		Out.Sort();
		return Out;
	}

	/** görüntü pikseli (oyun görüntü alanı) ↔ bileşen yerel konumu */
	bool MViewport(FGeometry& OutVp, FVector2D& OutPx)
	{
		if (!GEngine || !GEngine->GameViewport || !GEngine->GameViewport->Viewport) return false;
		TSharedPtr<SViewport> V = GEngine->GameViewport->GetGameViewportWidget();
		if (!V.IsValid()) return false;
		OutVp = V->GetCachedGeometry();
		const FIntPoint S = GEngine->GameViewport->Viewport->GetSizeXY();
		OutPx = FVector2D(FMath::Max(1, S.X), FMath::Max(1, S.Y));
		return OutVp.GetLocalSize().X > 1.f;
	}

	FVector2D MToLocal(const FGeometry& G, const FVector2D& Px)
	{
		FGeometry Vp; FVector2D Vs;
		if (!MViewport(Vp, Vs)) return Px;
		const FVector2D Abs = FVector2D(Vp.LocalToAbsolute(FVector2f(Px * FVector2D(Vp.GetLocalSize()) / Vs)));
		return FVector2D(G.AbsoluteToLocal(FVector2f(Abs)));
	}

	FVector2D MToPx(const FGeometry& G, const FVector2D& Local)
	{
		FGeometry Vp; FVector2D Vs;
		if (!MViewport(Vp, Vs)) return Local;
		const FVector2D VpLocal = FVector2D(Vp.AbsoluteToLocal(G.LocalToAbsolute(FVector2f(Local))));
		return VpLocal * Vs / FVector2D(Vp.GetLocalSize());
	}
}

// ---------------------------------------------------------------------------
// yükle / kaydet
// ---------------------------------------------------------------------------

void SSmvShell::MEdLoad(const FSmvMission& M)
{
	if (!MEd.IsValid()) MEd = MakeShared<FSmvMissionEdit>();
	FSmvMissionEdit& E = *MEd;
	E = FSmvMissionEdit();
	E.Id = M.Id; E.TrackId = M.TrackId; E.Name = M.Name; E.Kind = M.Kind.IsEmpty() ? FString(TEXT("corridor")) : M.Kind;
	E.LoadedFor = M.Id + TEXT("|") + M.Name;
	// ev: parkur konumu (eski görevler) → sonra plandaki ev
	if (USmvDataService* D = Data())
	{
		const FSmvTrack* T = nullptr;
		for (const FSmvTrack& X : D->Tracks()) if (X.Id == M.TrackId) T = &X;
		if (!T) T = D->DefaultTrack();
		if (T && (T->Lat != 0.0 || T->Lon != 0.0)) { E.HomeLat = T->Lat; E.HomeLon = T->Lon; }
	}
	if (Flow.IsValid() && M.Id.IsEmpty() && (Flow->Draft.Track.Lat != 0.0 || Flow->Draft.Track.Lon != 0.0)) { E.HomeLat = Flow->Draft.Track.Lat; E.HomeLon = Flow->Draft.Track.Lon; }
	TSharedPtr<FJsonObject> O;
	const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(M.PlanJson);
	if (FJsonSerializer::Deserialize(R, O) && O.IsValid())
	{
		const TSharedPtr<FJsonObject>* H;
		if (O->TryGetObjectField(TEXT("home"), H)) { E.HomeLat = (*H)->GetNumberField(TEXT("lat")); E.HomeLon = (*H)->GetNumberField(TEXT("lon")); }
		const TArray<TSharedPtr<FJsonValue>>* A;
		// v2 noktaları; eski planlarda "shape"
		if (O->TryGetArrayField(TEXT("points"), A) || O->TryGetArrayField(TEXT("shape"), A))
		{
			for (const TSharedPtr<FJsonValue>& V : *A)
			{
				const TSharedPtr<FJsonObject> P = V->AsObject();
				if (!P.IsValid()) continue;
				SmvCore::FMissionPoint Q;
				Q.Lat = P->GetNumberField(TEXT("lat")); Q.Lon = P->GetNumberField(TEXT("lon"));
				double X;
				if (P->TryGetNumberField(TEXT("alt"), X)) Q.Alt = X;
				if (P->TryGetNumberField(TEXT("speed"), X)) Q.Speed = X;
				if (P->TryGetNumberField(TEXT("action"), X)) Q.Action = (int)X;
				if (P->TryGetNumberField(TEXT("turns"), X)) Q.Turns = (int)X;
				if (P->TryGetNumberField(TEXT("diameter"), X)) Q.Radius = X;
				FString Sc;
				if (P->TryGetStringField(TEXT("script"), Sc)) Q.Script = std::string(TCHAR_TO_UTF8(*Sc));
				const TArray<TSharedPtr<FJsonValue>>* Ar;
				if (P->TryGetArrayField(TEXT("args"), Ar)) for (int32 i = 0; i < FMath::Min(4, Ar->Num()); ++i) Q.Args[i] = (*Ar)[i]->AsNumber();
				E.Pts.Add(Q);
			}
		}
		const TSharedPtr<FJsonObject>* Pp;
		if (O->TryGetObjectField(TEXT("params"), Pp))
		{
			SmvCore::FMissionParams& P = E.Par;
			auto Num = [&Pp](const TCHAR* K, double& V) { double X; if ((*Pp)->TryGetNumberField(K, X)) V = X; };
			Num(TEXT("corridor_width"), P.CorridorWidth); Num(TEXT("corridor_alt"), P.CorridorAlt); Num(TEXT("takeoff_alt"), P.TakeoffAlt);
			Num(TEXT("spray_step"), P.SprayStep); Num(TEXT("spray_alt"), P.SprayAlt); Num(TEXT("spray_angle"), P.SprayAngle);
			Num(TEXT("spiral_d0"), P.SpiralD0); Num(TEXT("spiral_d1"), P.SpiralD1); Num(TEXT("spiral_h0"), P.SpiralH0); Num(TEXT("spiral_h1"), P.SpiralH1);
			// eski planlar: alt / width / spacing / angle
			Num(TEXT("alt"), E.Kind == TEXT("spray") ? P.SprayAlt : P.CorridorAlt);
			if (E.Kind == TEXT("corridor")) Num(TEXT("width"), P.CorridorWidth);
			Num(TEXT("spacing"), P.SprayStep); Num(TEXT("angle"), P.SprayAngle);
		}
	}
	E.bLegacy = !E.IsNewKind();
	E.Rebuild();
}

FString SSmvShell::MEdSave() const
{
	const FSmvMissionEdit& E = *MEd;
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetNumberField(TEXT("v"), 2);
	O->SetStringField(TEXT("kind"), E.Kind);
	TSharedRef<FJsonObject> H = MakeShared<FJsonObject>();
	H->SetNumberField(TEXT("lat"), E.HomeLat); H->SetNumberField(TEXT("lon"), E.HomeLon);
	O->SetObjectField(TEXT("home"), H);
	TArray<TSharedPtr<FJsonValue>> Pa;
	for (const SmvCore::FMissionPoint& Q : E.Pts)
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetNumberField(TEXT("lat"), Q.Lat); P->SetNumberField(TEXT("lon"), Q.Lon); P->SetNumberField(TEXT("alt"), Q.Alt);
		P->SetNumberField(TEXT("speed"), Q.Speed); P->SetNumberField(TEXT("action"), Q.Action);
		P->SetNumberField(TEXT("turns"), Q.Turns); P->SetNumberField(TEXT("diameter"), Q.Radius);
		P->SetStringField(TEXT("script"), UTF8_TO_TCHAR(Q.Script.c_str()));
		TArray<TSharedPtr<FJsonValue>> Ar;
		for (int32 i = 0; i < 4; ++i) Ar.Add(MakeShared<FJsonValueNumber>(Q.Args[i]));
		P->SetArrayField(TEXT("args"), Ar);
		Pa.Add(MakeShared<FJsonValueObject>(P));
	}
	O->SetArrayField(TEXT("points"), Pa);
	{
		const SmvCore::FMissionParams& P = E.Par;
		TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
		J->SetNumberField(TEXT("corridor_width"), P.CorridorWidth); J->SetNumberField(TEXT("corridor_alt"), P.CorridorAlt); J->SetNumberField(TEXT("takeoff_alt"), P.TakeoffAlt);
		J->SetNumberField(TEXT("spray_step"), P.SprayStep); J->SetNumberField(TEXT("spray_alt"), P.SprayAlt); J->SetNumberField(TEXT("spray_angle"), P.SprayAngle);
		J->SetNumberField(TEXT("spiral_d0"), P.SpiralD0); J->SetNumberField(TEXT("spiral_d1"), P.SpiralD1); J->SetNumberField(TEXT("spiral_h0"), P.SpiralH0); J->SetNumberField(TEXT("spiral_h1"), P.SpiralH1);
		O->SetObjectField(TEXT("params"), J);
	}
	// ArduPilot görev öğeleri (eski plan biçimiyle uyumlu "route.points": seq, cmd, lat, lon, alt_rel, frame + p1..p4)
	{
		TArray<TSharedPtr<FJsonValue>> Rp;
		int32 Seq = 0;
		for (const SmvCore::FMissionItem& I : E.Res.Items)
		{
			TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
			P->SetNumberField(TEXT("seq"), Seq++); P->SetNumberField(TEXT("cmd"), I.Cmd);
			P->SetNumberField(TEXT("lat"), I.Lat); P->SetNumberField(TEXT("lon"), I.Lon); P->SetNumberField(TEXT("alt_rel"), I.Alt);
			P->SetNumberField(TEXT("frame"), 3);
			P->SetNumberField(TEXT("p1"), I.P1); P->SetNumberField(TEXT("p2"), I.P2); P->SetNumberField(TEXT("p3"), I.P3); P->SetNumberField(TEXT("p4"), I.P4);
			Rp.Add(MakeShared<FJsonValueObject>(P));
		}
		TSharedRef<FJsonObject> Ro = MakeShared<FJsonObject>();
		Ro->SetArrayField(TEXT("points"), Rp);
		O->SetObjectField(TEXT("route"), Ro);
		const SmvCore::FMissionSummary& S = E.Res.Summary;
		TSharedRef<FJsonObject> Sj = MakeShared<FJsonObject>();
		Sj->SetNumberField(TEXT("dist_m"), S.DistM); Sj->SetNumberField(TEXT("time_s"), S.TimeS); Sj->SetNumberField(TEXT("max_alt"), S.MaxAlt);
		Sj->SetNumberField(TEXT("max_speed"), S.MaxSpeed); Sj->SetNumberField(TEXT("waypoints"), S.Waypoints); Sj->SetNumberField(TEXT("area_m2"), S.AreaM2);
		O->SetObjectField(TEXT("summary"), Sj);
	}
	FString Out;
	const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(O, W);
	return Out;
}

// ---------------------------------------------------------------------------
// 3B çizim (görev dünyası üzerine izdüşüm)
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SSmvShell::MissionOverlay(bool bEditor)
{
	SSmvShell* Self = this;
	TSharedRef<SSmvCanvas> C = SNew(SSmvCanvas).Size(FVector2D(100, 100));
	C->LayersUsed = 40;
	C->Painter = [Self, bEditor](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
	{
		ASmvMenuScene* Sc = Self->Scene();
		if (!Sc || !Sc->IsMissionMode() || !Self->MEd.IsValid()) return;
		FSmvMissionEdit& E = *Self->MEd;
		E.Rebuild();
		auto Proj = [Sc, &G](double La, double Lo, double Alt, bool bGround, FVector2D& OutL)
		{
			FVector2D Px;
			if (!Sc->MissionProject(La, Lo, Alt, bGround, Px)) return false;
			OutL = MToLocal(G, Px);
			return true;
		};
		const FSlateFontInfo Fl = Font(10, "Bold");
		const FLinearColor Yel(1.f, 0.86f, 0.25f, 0.62f), Blu(0.35f, 0.7f, 1.f, 0.5f), Lab(1.f, 1.f, 1.f, 0.92f);
		// koridor / ilaçlama kenarı (zeminde)
		if (E.Res.Outline.size() >= 3)
		{
			TArray<FVector2D> Ol;
			for (const auto& P : E.Res.Outline) { FVector2D Q; if (Proj(P.first, P.second, 0, true, Q)) Ol.Add(Q); }
			if (Ol.Num() >= 3) { Ol.Add(Ol[0]); DrLine(Out, L + 1, G, Ol, FLinearColor(0.2f, 0.95f, 0.8f, 0.55f), 2.f); }
		}
		// rota: konumlu öğeler (kalkış evde)
		struct FN { double La, Lo, Alt; int Cmd; };
		TArray<FN> Ns;
		for (const SmvCore::FMissionItem& I : E.Res.Items) if (I.bNav) Ns.Add({ I.Cmd == 22 ? E.HomeLat : I.Lat, I.Cmd == 22 ? E.HomeLon : I.Lon, I.Alt, I.Cmd });
		const bool bDense = Ns.Num() > 40;   // tarama / spiral: etiketler seyreltilir
		const int32 Every = bDense ? FMath::Max(1, Ns.Num() / 14) : 1;
		FVector2D Prev; bool bPrev = false;
		for (int32 i = 0; i < Ns.Num(); ++i)
		{
			const FN& N = Ns[i];
			FVector2D A, Gd;
			const bool bA = Proj(N.La, N.Lo, N.Alt, false, A);
			const bool bG = Proj(N.La, N.Lo, 0, true, Gd);
			// zeminden dik silik mavi çizgi + yükseklik
			if (bA && bG && (i % Every == 0 || i == Ns.Num() - 1))
			{
				DrSeg(Out, L + 2, G, Gd, A, Blu, 1.5f);
				if (FVector2D::Distance(Gd, A) > 26.f) DrText(Out, L + 6, G, FString::Printf(TEXT("%.0f m"), N.Alt), (Gd + A) * 0.5 + FVector2D(6, 0), Font(9, "Bold"), FLinearColor(0.6f, 0.85f, 1.f, 0.95f), -1);
			}
			// noktalar arası silik sarı çizgi + mesafe
			if (bA && bPrev)
			{
				DrSeg(Out, L + 3, G, Prev, A, Yel, 2.f);
				if (i > 0 && (!bDense || i % Every == 0) && FVector2D::Distance(Prev, A) > 60.f)
				{
					const FN& P0 = Ns[i - 1];
					const double D = SmvCore::DistM(P0.La, P0.Lo, N.La, N.Lo);
					if (D > 0.5) DrText(Out, L + 6, G, MDist(D), (Prev + A) * 0.5 + FVector2D(0, -10), Fl, Lab, 0);
				}
			}
			if (bA) { DrBox(Out, L + 4, G, A - FVector2D(3, 3), FVector2D(6, 6), FLinearColor(1.f, 0.86f, 0.25f, 0.9f), Circle()); Prev = A; bPrev = true; }
			else bPrev = false;
		}
		// ev (kalkış)
		{
			FVector2D H;
			if (Proj(E.HomeLat, E.HomeLon, 0, true, H))
			{
				DrBox(Out, L + 7, G, H - FVector2D(12, 12), FVector2D(24, 24), FLinearColor(0.1f, 0.85f, 0.45f, 0.95f), Circle());
				DrText(Out, L + 8, G, TEXT("H"), H, Font(11, "Black"), FLinearColor::White, 0);
			}
		}
		// kullanıcı noktaları (düzenlenebilir): numaralı turuncu daire
		for (int32 i = 0; i < E.Pts.Num(); ++i)
		{
			const SmvCore::FMissionPoint& Q = E.Pts[i];
			const double Alt = E.Kind == TEXT("free") ? Q.Alt : E.Kind == TEXT("corridor") ? E.Par.CorridorAlt : E.Kind == TEXT("spray") ? 0.0 : Q.Alt;
			FVector2D P;
			if (!Proj(Q.Lat, Q.Lon, Alt, E.Kind == TEXT("spray"), P)) continue;
			const bool bH = (i == E.Hover || i == E.Drag) && bEditor;
			DrBox(Out, L + 9, G, P - FVector2D(11, 11), FVector2D(22, 22), bH ? FLinearColor(1.f, 0.55f, 0.15f, 1.f) : FLinearColor(1.f, 0.45f, 0.1f, 0.92f), Circle());
			DrText(Out, L + 10, G, FString::FromInt(i + 1), P, Font(10, "Black"), FLinearColor::White, 0);
			if (Q.Action == 1) DrArc(Out, L + 9, G, P, 17.f, 0.f, 360.f, FLinearColor(1.f, 0.85f, 0.3f, 0.9f), 1.5f);
			if (Q.Action == 2) DrText(Out, L + 10, G, TEXT("λ"), P + FVector2D(16, -14), Font(10, "Bold"), Accent3(), -1);
		}
	};
	if (bEditor)
	{
		TWeakPtr<SSmvCanvas> Wc = C;
		C->OnHover = [Self, Wc](const FVector2D& P)
		{
			TSharedPtr<SSmvCanvas> Cp = Wc.Pin();
			if (!Cp.IsValid()) return;
			if (!Self->MEd.IsValid()) return;
			FSmvMissionEdit& E = *Self->MEd;
			E.Hover = -1;
			ASmvMenuScene* Sc = Self->Scene();
			if (!Sc) return;
			for (int32 i = 0; i < E.Pts.Num(); ++i)
			{
				const SmvCore::FMissionPoint& Q = E.Pts[i];
				const double Alt = E.Kind == TEXT("free") ? Q.Alt : E.Kind == TEXT("corridor") ? E.Par.CorridorAlt : 0.0;
				FVector2D Px;
				if (Sc->MissionProject(Q.Lat, Q.Lon, Alt, E.Kind == TEXT("spray"), Px) && FVector2D::Distance(MToLocal(Cp->GetCachedGeometry(), Px), P) < 14.f) E.Hover = i;
			}
		};
	}
	// fare: sol tık nokta ekle / sürükle taşı / boşta sürükle kaydır · sağ tık nokta sil / sağ sürükle döndür-eğ · tekerlek yakınlaş
	C->OnPointer = [Self, bEditor](const FGeometry& G, const FPointerEvent& Ev, const FVector2D& P, int32 Phase)
	{
		ASmvMenuScene* Sc = Self->Scene();
		if (!Sc || !Self->MEd.IsValid()) return;
		FSmvMissionEdit& E = *Self->MEd;
		const bool bRight = Ev.GetEffectingButton() == EKeys::RightMouseButton || Ev.IsMouseButtonDown(EKeys::RightMouseButton);
		if (Phase == 0)
		{
			E.DownAt = P; E.LastAt = P; E.bMoved = false;
			E.bRotating = bRight || Ev.IsShiftDown();
			E.Drag = (bEditor && !E.bRotating) ? E.Hover : -1;
			E.bPanning = !E.bRotating && E.Drag < 0;
			return;
		}
		if (Phase == 1)
		{
			const FVector2D D = P - E.LastAt;
			E.LastAt = P;
			if (FVector2D::Distance(P, E.DownAt) > 4.f) E.bMoved = true;
			if (!E.bMoved) return;
			if (E.Drag >= 0 && E.Pts.IsValidIndex(E.Drag))
			{
				double La, Lo;
				if (Sc->MissionPick(MToPx(G, P), La, Lo)) { E.Pts[E.Drag].Lat = La; E.Pts[E.Drag].Lon = Lo; }
			}
			else if (E.bRotating) Sc->MissionOrbit(D.X * 0.3f, -D.Y * 0.25f, 1.f, FVector2D::ZeroVector);
			else if (E.bPanning) Sc->MissionOrbit(0.f, 0.f, 1.f, D);
			return;
		}
		// bırak: hareketsiz tık
		const int32 WasDrag = E.Drag;
		E.Drag = -1;
		if (E.bMoved || !bEditor) { E.bRotating = E.bPanning = false; return; }
		if (bRight)
		{
			if (E.Hover >= 0 && E.Pts.IsValidIndex(E.Hover)) { E.Pts.RemoveAt(E.Hover); E.Hover = -1; }
			E.bRotating = false;
			return;
		}
		E.bPanning = false;
		if (WasDrag >= 0)
		{
			// serbest görevde noktaya tık: ayarlar penceresi
			if (E.Kind == TEXT("free")) Self->MOpenPointDlg(WasDrag);
			return;
		}
		double La, Lo;
		if (!Sc->MissionPick(MToPx(G, P), La, Lo)) return;
		if (E.bHomePick) { E.HomeLat = La; E.HomeLon = Lo; E.bHomePick = false; Self->Refresh(); return; }
		if (E.Kind == TEXT("spiral")) return;   // spiral noktasız (ev çevresinde)
		SmvCore::FMissionPoint Q;
		Q.Lat = La; Q.Lon = Lo;
		Q.Alt = E.Pts.Num() ? E.Pts.Last().Alt : 30.0;
		if (E.Kind == TEXT("free")) { E.Pts.Add(Q); Self->MOpenPointDlg(E.Pts.Num() - 1); }
		else { E.Pts.Add(Q); Self->Refresh(); }
	};
	C->OnWheel = [Self](const FVector2D&, float Dl) { if (ASmvMenuScene* Sc = Self->Scene()) Sc->MissionOrbit(0.f, 0.f, Dl > 0 ? 0.85f : 1.18f, FVector2D::ZeroVector); };
	return C;
}

void SSmvShell::MOpenPointDlg(int32 Index)
{
	if (!MEd.IsValid() || !MEd->Pts.IsValidIndex(Index)) return;
	FSmvMissionEdit& E = *MEd;
	E.bDlg = true; E.DlgIndex = Index;
	E.Draft = E.Pts[Index];
	if (E.Draft.Speed <= 0.0) E.Draft.Speed = 10.0;
	E.DraftAction = E.Draft.Action == 1 ? TEXT("loiter") : E.Draft.Action == 2 ? TEXT("script") : TEXT("next");
	E.DraftScript = UTF8_TO_TCHAR(E.Draft.Script.c_str());
	E.DraftTurns = FMath::Max(1, E.Draft.Turns);
	Refresh();
}

// ---------------------------------------------------------------------------
// ekranlar
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SSmvShell::Missions()
{
	USmvDataService* D = Data();
	if (!D || !Flow.IsValid()) return SNullWidget::NullWidget;
	if (!MEd.IsValid() || MEd->LoadedFor != EditMission.Id + TEXT("|") + EditMission.Name) MEdLoad(EditMission);
	if (MEd->bLegacy && bMLegacy) return MissionsLegacy();
	SSmvShell* Self = this;
	TWeakObjectPtr<USmvFlow> F = Flow;
	// sol: görev listesi
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	for (const FSmvMission& X : D->Missions())
	{
		const FSmvMission Copy = X;
		List->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[ListItem(FText::FromString(X.Name), FSmvI18n::T(FString(TEXT("mission.")) + X.Kind),
			MKindIcon(X.Kind), X.Id == EditMission.Id, false, [Self, Copy]() { Self->EditMission = Copy; Self->MEdLoad(Copy); if (ASmvMenuScene* Sc = Self->Scene()) Sc->MissionStop(); Self->Refresh(); }, Accent2())];
	}
	if (D->Missions().Num() == 0) List->AddSlot().AutoHeight()[SmvUi::Text(Tx(TEXT("mis.empty")), 12, "Regular", Dim())];
	// sağ: özet
	const bool bSel = !EditMission.Id.IsEmpty();
	TSharedRef<SVerticalBox> Sum = SNew(SVerticalBox);
	if (bSel)
	{
		const FSmvMissionEdit& E = *MEd;
		const SmvCore::FMissionSummary& S = E.Res.Summary;
		Sum->AddSlot().AutoHeight()[Title(FText::FromString(EditMission.Name), 22)];
		Sum->AddSlot().AutoHeight().Padding(0, 2, 0, 12)[SmvUi::Text(FSmvI18n::T(FString(TEXT("mission.")) + E.Kind), 12, "Regular", Dim(), false)];
		auto Line = [&Sum](const FText& K, const FString& V)
		{
			Sum->AddSlot().AutoHeight().Padding(0, 4)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)[SmvUi::Text(K, 12, "Regular", Dim(), false)]
				+ SHorizontalBox::Slot().AutoWidth()[SmvUi::Text(FText::FromString(V), 13, "Bold", Fg(), false)]
			];
		};
		Line(Tx(TEXT("mis.time")), MTime(S.TimeS));
		Line(Tx(TEXT("mis.dist")), MDist(S.DistM));
		Line(Tx(TEXT("mis.max_alt")), FString::Printf(TEXT("%.0f m"), S.MaxAlt));
		Line(Tx(TEXT("mis.max_speed")), FString::Printf(TEXT("%.0f m/s"), S.MaxSpeed));
		Line(Tx(TEXT("mis.wps")), FString::FromInt(S.Waypoints));
		Line(Tx(TEXT("mis.items")), FString::FromInt(S.Items));
		if (S.AreaM2 > 1.0) Line(Tx(TEXT("mis.area")), S.AreaM2 >= 10000.0 ? FString::Printf(TEXT("%.2f ha"), S.AreaM2 / 10000.0) : FString::Printf(TEXT("%.0f m²"), S.AreaM2));
		Line(Tx(TEXT("mis.home")), FString::Printf(TEXT("%.5f, %.5f"), E.HomeLat, E.HomeLon));
		Sum->AddSlot().AutoHeight().Padding(0, 10, 0, 0)[SmvUi::Text(TAttribute<FText>::CreateLambda([Self]() { ASmvMenuScene* Sc = Self->Scene(); return FText::FromString(Sc ? Sc->MissionWorldInfo() : FString()); }), 10, "Regular", Mute(), false)];
		if (E.bLegacy) Sum->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[SmvUi::Text(Tx(TEXT("mis.legacy")), 10, "Regular", Warn())];
	}
	else Sum->AddSlot().AutoHeight()[SmvUi::Text(Tx(TEXT("mis.pick")), 12, "Regular", Dim())];
	// altta sağda: Oynat · Düzenle · Yeni görev
	TSharedRef<SWidget> Buttons = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
		[
			SNew(SSmvButton).Icon(ESmvIcon::Play).IsEnabled(bSel)
			.Text_Lambda([Self]() { ASmvMenuScene* Sc = Self->Scene(); return Sc && Sc->IsMissionPlaying() ? FSmvI18n::T(TEXT("mis.stop")) : FSmvI18n::T(TEXT("mis.play")); })
			.OnClick([Self]() { Self->MTogglePlay(); })
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
		[
			SNew(SSmvButton).Icon(ESmvIcon::Gear).Text(Tx(TEXT("mis.edit"))).IsEnabled(bSel)
			.OnClick([Self, F]()
			{
				if (!F.IsValid() || !Self->MEd.IsValid()) return;
				if (Self->MEd->bLegacy) { Self->bMLegacy = true; Self->MPlan.LoadedFor.Empty(); Self->Refresh(); return; }
				F->SetScreen(ESmvScreen::MissionEditor);
			})
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SSmvButton).Kind(ESmvBtn::Primary).Icon(ESmvIcon::Plus).Text(Tx(TEXT("mis.new")))
			.OnClick([Self, F]()
			{
				FSmvMission M;
				M.Name = FSmvI18n::T(TEXT("missions.new_name")).ToString();
				M.Kind = TEXT("corridor");
				if (const FSmvTrack* T = Self->Data()->DefaultTrack()) M.TrackId = T->Id;
				Self->EditMission = M;
				Self->MEdLoad(M);
				if (F.IsValid()) F->SetScreen(ESmvScreen::MissionWizard);
			})
		];
	TSharedRef<SWidget> Body = SNew(SOverlay)
		+ SOverlay::Slot()[MissionOverlay(false)]
		+ SOverlay::Slot()
		[
			SNew(SHorizontalBox).Visibility(EVisibility::SelfHitTestInvisible)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)[SNew(SBox).WidthOverride(320.f)[Glass(SNew(SScrollBox) + SScrollBox::Slot()[List], FMargin(14.f), 0.9f)]]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)[SNew(SBox).WidthOverride(330.f)[Glass(Sum, FMargin(18.f), 0.9f)]]
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(0, 0, 0, 6)[Buttons];
	return YhPage(Tx(TEXT("menu.missions")), Tx(TEXT("mis.sub")), Body);
}

void SSmvShell::MTogglePlay()
{
	ASmvMenuScene* Sc = Scene();
	if (!Sc || !MEd.IsValid()) return;
	if (Sc->IsMissionPlaying()) { Sc->MissionStop(); return; }
	FSmvMissionEdit& E = *MEd;
	E.Rebuild();
	TArray<FVector> Path;
	Path.Add(FVector(E.HomeLat, E.HomeLon, 0.0));
	for (const SmvCore::FMissionItem& I : E.Res.Items)
	{
		if (!I.bNav) continue;
		if (I.Cmd == 22) { Path.Add(FVector(E.HomeLat, E.HomeLon, I.Alt)); continue; }
		Path.Add(FVector(I.Lat, I.Lon, I.Alt));
	}
	if (Path.Num() > 1) { const FVector& Lp = Path.Last(); Path.Add(FVector(E.HomeLat, E.HomeLon, Lp.Z)); Path.Add(FVector(E.HomeLat, E.HomeLon, 0.0)); }
	// oynatma hızı: gerçek seyir hızının 4 katı (uzun görevler beklenmeden izlensin)
	Sc->MissionPlay(Path, (float)FMath::Max(8.0, E.Res.Summary.MaxSpeed * 4.0));
}

TSharedRef<SWidget> SSmvShell::MissionWizard()
{
	if (!MEd.IsValid()) MEdLoad(EditMission);
	SSmvShell* Self = this;
	TWeakObjectPtr<USmvFlow> F = Flow;
	FSmvMissionEdit& E = *MEd;
	TSharedRef<SHorizontalBox> Hb = SNew(SHorizontalBox);
	int32 i = 0;
	for (const TCHAR* K : GMKinds)
	{
		const FString Kind = K;
		TSharedRef<SSmvButton> B = SNew(SSmvButton).Kind(ESmvBtn::Tile).Accent(Accent2()).MinWidth(280.f).MinHeight(200.f).Icon(MKindIcon(Kind)).IntroDelay(0.06f * i++)
			.Selected(E.Kind == Kind)
			.Text(FSmvI18n::T(FString(TEXT("mission.")) + Kind)).SubText(FSmvI18n::T(FString(TEXT("mis.kind_d.")) + Kind))
			.OnClick([Self, Kind]() { if (!Self->MEd.IsValid()) return; Self->MEd->Kind = Kind; Self->MEd->Pts.Reset(); Self->Refresh(); });
		if (E.Kind == Kind) DefaultFocus = B;
		Hb->AddSlot().AutoWidth().Padding(0, 0, 14, 0)[B];
	}
	TSharedRef<SWidget> Body = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 10, 0, 22)
		[
			Glass(SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)[SmvUi::Text(Tx(TEXT("missions.name")), 13, "Bold", Fg(), false)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[TextEdit(&E.Name, false, FText::GetEmpty(), 360.f)], FMargin(18.f, 12.f), 0.9f)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Hb]
		+ SVerticalBox::Slot().FillHeight(1.f)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
		[
			SNew(SSmvButton).Kind(ESmvBtn::Primary).Icon(ESmvIcon::Next).Text(Tx(TEXT("mis.next")))
			.OnClick([Self, F]() { if (Self->MEd.IsValid()) Self->MEd->Name = Self->MEd->Name.TrimStartAndEnd().Left(48); if (F.IsValid()) F->SetScreen(ESmvScreen::MissionEditor); })
		];
	return YhPage(Tx(TEXT("mis.new")), Tx(TEXT("mis.wiz_sub")), Body);
}

TSharedRef<SWidget> SSmvShell::MissionEditor()
{
	if (!MEd.IsValid()) MEdLoad(EditMission);
	SSmvShell* Self = this;
	TWeakObjectPtr<USmvFlow> F = Flow;
	FSmvMissionEdit& E = *MEd;
	SmvCore::FMissionParams& P = E.Par;
	auto Mf = [](double V) { return FString::Printf(TEXT("%.0f m"), V); };
	auto Df = [](double V) { return FString::Printf(TEXT("%.0f°"), V); };
	// üstte ortada yüzen başlık: Geri · görev adı + tür · Kaydet (en az 3 nokta; spiral noktasız)
	TSharedRef<SWidget> Header = SNew(SBorder).BorderImage(Round(18.f)).BorderBackgroundColor(Hex(0x0b1222, 0.86f)).Padding(FMargin(14.f, 10.f))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 16, 0)
		[
			SNew(SSmvButton).Kind(ESmvBtn::Ghost).Icon(ESmvIcon::Back).Text(Tx(TEXT("common.back"))).OnClick([F]() { if (F.IsValid()) F->Back(); })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)[Icon(MKindIcon(E.Kind), 20.f, Accent2())]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)[TextEdit(&E.Name, false, FText::GetEmpty(), 260.f)]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 16, 0)
		[
			SmvUi::Text(TAttribute<FText>::CreateLambda([Self]()
			{
				if (!Self->MEd.IsValid()) return FText::GetEmpty();
				const FSmvMissionEdit& X = *Self->MEd;
				return FText::FromString(FString::Printf(TEXT("%s · %d · %s · %s"), *FSmvI18n::T(FString(TEXT("mission.")) + X.Kind).ToString(), X.Pts.Num(), *MDist(X.Res.Summary.DistM), *MTime(X.Res.Summary.TimeS)));
			}), 11, "Regular", Dim(), false)
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SSmvButton).Kind(ESmvBtn::Primary).Icon(ESmvIcon::Save).Text(Tx(TEXT("common.save")))
			.IsEnabled_Lambda([Self]() { return Self->MEd.IsValid() && Self->MEd->Pts.Num() >= SmvCore::MinPoints(std::string(TCHAR_TO_UTF8(*Self->MEd->Kind))) && !Self->MEd->Name.TrimStartAndEnd().IsEmpty(); })
			.OnClick([Self, F]()
			{
				if (!Self->MEd.IsValid()) return;
				FSmvMissionEdit& X = *Self->MEd;
				X.Rebuild();
				FSmvMission M = Self->EditMission;
				M.Id = X.Id; M.Name = X.Name.TrimStartAndEnd(); M.Kind = X.Kind; M.TrackId = X.TrackId;
				M.PlanJson = Self->MEdSave();
				Self->Data()->SaveMission(M, FSmvDone::CreateLambda([Self, F, M](bool bOk, const FString& Msg)
				{
					Self->Toast(bOk ? FSmvI18n::T(TEXT("common.saved")).ToString() : Msg, bOk ? 6 : 3);
					if (!bOk) return;
					// kayıttan sonra listeye: kaydedilen görev seçili (yeni görevde sunucu kimliği atanır → adla bulunur)
					for (const FSmvMission& Y : Self->Data()->Missions()) if (Y.Name == M.Name && Y.Kind == M.Kind) Self->EditMission = Y;
					if (Self->MEd.IsValid()) Self->MEd->LoadedFor.Empty();
					if (F.IsValid()) F->ResetTo(ESmvScreen::Missions, ESmvScreen::Menu);
				}));
			})
		]
	];
	// altta ortada: türe göre ayarlar
	TSharedRef<SVerticalBox> Ft = SNew(SVerticalBox);
	auto Add = [&Ft](const FText& K, TSharedRef<SWidget> W) { Ft->AddSlot().AutoHeight().Padding(0, 2)[SNew(SBox).WidthOverride(560.f)[Row(K, W)]]; };
	if (E.Kind == TEXT("corridor"))
	{
		Add(Tx(TEXT("mis.c_width")), Slider(&P.CorridorWidth, 100, 3000, 10, Mf));
		Add(Tx(TEXT("mis.c_alt")), Slider(&P.CorridorAlt, 30, 3000, 5, Mf));
		Add(Tx(TEXT("mis.takeoff_alt")), Slider(&P.TakeoffAlt, 10, 3000, 5, Mf));
	}
	else if (E.Kind == TEXT("spray"))
	{
		Add(Tx(TEXT("mis.s_step")), Slider(&P.SprayStep, 2, 5, 0.5, [](double V) { return FString::Printf(TEXT("%.1f m"), V); }));
		Add(Tx(TEXT("mis.s_alt")), Slider(&P.SprayAlt, 2, 30, 1, Mf));
		Add(Tx(TEXT("mis.s_angle")), Slider(&P.SprayAngle, 1, 360, 1, Df));
	}
	else if (E.Kind == TEXT("spiral"))
	{
		Add(Tx(TEXT("mis.sp_d0")), Slider(&P.SpiralD0, 10, 3000, 10, Mf));
		Add(Tx(TEXT("mis.sp_d1")), Slider(&P.SpiralD1, 30, 3000, 10, Mf));
		Add(Tx(TEXT("mis.sp_h0")), Slider(&P.SpiralH0, 10, 3000, 5, Mf));
		Add(Tx(TEXT("mis.sp_h1")), Slider(&P.SpiralH1, 20, 3000, 5, Mf));
	}
	else Ft->AddSlot().AutoHeight()[SmvUi::Text(Tx(TEXT("mis.free_hint")), 11, "Regular", Dim())];
	Ft->AddSlot().AutoHeight().Padding(0, 8, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
		[
			SNew(SSmvButton).Kind(ESmvBtn::Ghost).Icon(ESmvIcon::Home).Selected_Lambda([Self]() { return Self->MEd.IsValid() && Self->MEd->bHomePick; })
			.Text(Tx(TEXT("mis.set_home"))).OnClick([Self]() { if (Self->MEd.IsValid()) Self->MEd->bHomePick = !Self->MEd->bHomePick; })
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
		[
			SNew(SSmvButton).Kind(ESmvBtn::Ghost).Icon(ESmvIcon::Trash).Text(Tx(TEXT("missions.clear"))).OnClick([Self]() { if (Self->MEd.IsValid()) { Self->MEd->Pts.Reset(); Self->Refresh(); } })
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
		[
			SNew(SSmvButton).Kind(ESmvBtn::Ghost).Icon(ESmvIcon::Play)
			.Text_Lambda([Self]() { ASmvMenuScene* Sc = Self->Scene(); return Sc && Sc->IsMissionPlaying() ? FSmvI18n::T(TEXT("mis.stop")) : FSmvI18n::T(TEXT("mis.play")); })
			.OnClick([Self]() { Self->MTogglePlay(); })
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[SmvUi::Text(Tx(TEXT("mis.mouse_help")), 10, "Regular", Mute())]
	];
	TSharedRef<SWidget> Footer = SNew(SBorder).BorderImage(Round(18.f)).BorderBackgroundColor(Hex(0x0b1222, 0.86f)).Padding(FMargin(18.f, 12.f))[Ft];
	TSharedRef<SOverlay> O = SNew(SOverlay)
		+ SOverlay::Slot()[MissionOverlay(true)]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 18, 0, 0)[Header]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0, 0, 0, 22)[Footer];
	if (E.bDlg) O->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Center)[MPointDialog()];
	return O;
}

TSharedRef<SWidget> SSmvShell::MPointDialog()
{
	SSmvShell* Self = this;
	FSmvMissionEdit& E = *MEd;
	SmvCore::FMissionPoint& Q = E.Draft;
	auto Mf = [](double V) { return FString::Printf(TEXT("%.0f m"), V); };
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Title(FSmvI18n::Format(TEXT("mis.pt_title"), { { TEXT("n"), FString::FromInt(E.DlgIndex + 1) } }), 20)];
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 10)[SmvUi::Text(FText::FromString(FString::Printf(TEXT("%.6f, %.6f"), Q.Lat, Q.Lon)), 11, "Mono", Dim(), false)];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("mis.pt_speed")), Slider(&Q.Speed, 1, 40, 0.5, [](double X) { return FString::Printf(TEXT("%.1f m/s"), X); }))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("mis.pt_alt")), Slider(&Q.Alt, 5, 3000, 1, Mf))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("mis.pt_action")), Chips(&E.DraftAction, { TEXT("next"), TEXT("loiter"), TEXT("script") },
		{ Tx(TEXT("mis.act_next")), Tx(TEXT("mis.act_loiter")), Tx(TEXT("mis.act_script")) }, { ESmvIcon::Next, ESmvIcon::Replay, ESmvIcon::Book }, [Self]() { Self->Refresh(); }, 3))];
	if (E.DraftAction == TEXT("loiter"))
	{
		V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("mis.pt_turns")), SliderI(&E.DraftTurns, 1, 50))];
		V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("mis.pt_diam")), Slider(&Q.Radius, 10, 3000, 5, Mf))];
	}
	if (E.DraftAction == TEXT("script"))
	{
		const TArray<FString> Scr = MAllScripts();
		if (Scr.Num() == 0) V->AddSlot().AutoHeight().Padding(0, 3)[SmvUi::Text(Tx(TEXT("mis.no_scripts")), 11, "Regular", Warn())];
		else
		{
			TArray<FText> L; for (const FString& S : Scr) L.Add(FText::FromString(S));
			if (!Scr.Contains(E.DraftScript)) E.DraftScript = Scr[0];
			V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("mis.pt_script")), Chips(&E.DraftScript, Scr, L, {}, nullptr, 2))];
		}
		// betik argümanları: elle ya da noktanın enlem / boylamı
		for (int32 a = 0; a < 4; ++a)
		{
			V->AddSlot().AutoHeight().Padding(0, 2)
			[
				Row(FSmvI18n::Format(TEXT("mis.pt_arg"), { { TEXT("n"), FString::FromInt(a + 1) } }), SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Spin(&Q.Args[a], -1.0e7, 1.0e7, 1.0)]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)[SNew(SSmvButton).Kind(ESmvBtn::Ghost).FontSize(11).Text(Tx(TEXT("mis.use_lat"))).OnClick([Self, a]() { if (Self->MEd.IsValid()) { Self->MEd->Draft.Args[a] = Self->MEd->Draft.Lat; Self->Refresh(); } })]
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SSmvButton).Kind(ESmvBtn::Ghost).FontSize(11).Text(Tx(TEXT("mis.use_lon"))).OnClick([Self, a]() { if (Self->MEd.IsValid()) { Self->MEd->Draft.Args[a] = Self->MEd->Draft.Lon; Self->Refresh(); } })])
			];
		}
	}
	V->AddSlot().AutoHeight().Padding(0, 14, 0, 0).HAlign(HAlign_Right)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[SNew(SSmvButton).Kind(ESmvBtn::Danger).Icon(ESmvIcon::Trash).Text(Tx(TEXT("common.delete"))).OnClick([Self]()
		{
			if (!Self->MEd.IsValid()) return;
			FSmvMissionEdit& X = *Self->MEd;
			if (X.Pts.IsValidIndex(X.DlgIndex)) X.Pts.RemoveAt(X.DlgIndex);
			X.bDlg = false; Self->Refresh();
		})]
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SSmvButton).Kind(ESmvBtn::Primary).Icon(ESmvIcon::Check).Text(Tx(TEXT("mis.ok"))).OnClick([Self]()
		{
			if (!Self->MEd.IsValid()) return;
			FSmvMissionEdit& X = *Self->MEd;
			SmvCore::FMissionPoint D = X.Draft;
			D.Action = X.DraftAction == TEXT("loiter") ? 1 : X.DraftAction == TEXT("script") ? 2 : 0;
			D.Turns = X.DraftTurns;
			D.Script = std::string(TCHAR_TO_UTF8(*X.DraftScript));
			if (X.Pts.IsValidIndex(X.DlgIndex)) X.Pts[X.DlgIndex] = D;
			X.bDlg = false; Self->Refresh();
		})]
	];
	return SNew(SBox).WidthOverride(620.f)[Glass(V, FMargin(22.f), 0.96f)];
}

FVector2D SSmvShell::MissionHome() const
{
	return MEd.IsValid() ? FVector2D(MEd->HomeLat, MEd->HomeLon) : FVector2D(41.0082, 28.9784);
}
