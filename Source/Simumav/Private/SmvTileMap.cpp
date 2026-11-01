#include "SmvTileMap.h"
#include "SmvUiKit.h"
#include "SmvI18n.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "ImageUtils.h"
#include "Engine/Texture2D.h"
#include "UObject/StrongObjectPtr.h"
#include "Styling/CoreStyle.h"
#include "Rendering/DrawElements.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "GenericPlatform/GenericPlatformHttp.h"

using namespace SmvUi;

namespace
{
	struct FTile { TStrongObjectPtr<UTexture2D> Tex; TSharedPtr<FSlateBrush> Brush; double Used = 0; bool bPending = false, bFailed = false; };
	TMap<FString, FTile>& Tiles() { static TMap<FString, FTile> T; return T; }

	void Trim()
	{
		TMap<FString, FTile>& T = Tiles();
		if (T.Num() <= 300) return;
		TArray<TPair<double, FString>> Ord;
		for (const auto& Kv : T) if (!Kv.Value.bPending) Ord.Add(TPair<double, FString>(Kv.Value.Used, Kv.Key));
		Ord.Sort([](const TPair<double, FString>& A, const TPair<double, FString>& B) { return A.Key < B.Key; });
		for (int32 i = 0; i < Ord.Num() && T.Num() > 240; ++i) T.Remove(Ord[i].Value);
	}

	const FSlateBrush* TileBrush(int32 Z, int32 X, int32 Y)
	{
		const int32 N = 1 << Z;
		X = ((X % N) + N) % N;
		if (Y < 0 || Y >= N) return nullptr;
		const FString Key = FString::Printf(TEXT("%d/%d/%d"), Z, Y, X);
		FTile& T = Tiles().FindOrAdd(Key);
		T.Used = FPlatformTime::Seconds();
		if (T.Brush.IsValid()) return T.Brush.Get();
		if (T.bPending || T.bFailed) return nullptr;
		T.bPending = true;
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> R = FHttpModule::Get().CreateRequest();
		R->SetURL(FString::Printf(TEXT("https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/%s"), *Key));
		R->SetVerb(TEXT("GET"));
		R->SetHeader(TEXT("User-Agent"), TEXT("CAYSIM/1.0"));
		R->OnProcessRequestComplete().BindLambda([Key](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
		{
			FTile* Tp = Tiles().Find(Key);
			if (!Tp) return;
			Tp->bPending = false;
			if (!bOk || !Resp.IsValid() || Resp->GetResponseCode() != 200) { Tp->bFailed = true; return; }
			UTexture2D* Tex = FImageUtils::ImportBufferAsTexture2D(Resp->GetContent());
			if (!Tex) { Tp->bFailed = true; return; }
			Tp->Tex.Reset(Tex);
			Tp->Brush = MakeShared<FSlateBrush>();
			Tp->Brush->DrawAs = ESlateBrushDrawType::Image;
			Tp->Brush->SetResourceObject(Tex);
			Tp->Brush->ImageSize = FVector2D(256, 256);
			Trim();
		});
		R->ProcessRequest();
		return nullptr;
	}
}

void SSmvTileMap::Construct(const FArguments& A)
{
	Size = A._Size; Lat = A._Lat; Lon = A._Lon; Gates = A._Gates; RadiusM = A._RadiusM; RingMode = A._RingMode; OnChanged = A._OnChanged;
}

FVector2D SSmvTileMap::Project(double La, double Lo, int32 Z)
{
	const double N = 256.0 * (1 << Z);
	const double S = FMath::Sin(FMath::DegreesToRadians(FMath::Clamp(La, -85.0, 85.0)));
	return FVector2D((Lo + 180.0) / 360.0 * N, (0.5 - FMath::Loge((1 + S) / (1 - S)) / (4 * PI)) * N);
}

void SSmvTileMap::Unproject(double X, double Y, int32 Z, double& La, double& Lo)
{
	const double N = 256.0 * (1 << Z);
	Lo = X / N * 360.0 - 180.0;
	const double M = PI - 2.0 * PI * Y / N;
	La = FMath::RadiansToDegrees(FMath::Atan(0.5 * (FMath::Exp(M) - FMath::Exp(-M))));
}

void SSmvTileMap::EnsureInit()
{
	if (bInit || !Lat || !Lon) return;
	bInit = true;
	const FVector2D P = Project(*Lat, *Lon, Zoom);
	Cx = P.X; Cy = P.Y;
}

void SSmvTileMap::CenterOn(double La, double Lo, int32 Z)
{
	if (Z > 0) Zoom = FMath::Clamp(Z, 3, 19);
	const FVector2D P = Project(La, Lo, Zoom);
	Cx = P.X; Cy = P.Y; bInit = true;
}

FVector2D SSmvTileMap::ToLocal(const FGeometry& G, double La, double Lo) const
{
	const FVector2D P = Project(La, Lo, Zoom), Sz = FVector2D(G.GetLocalSize());
	return FVector2D(P.X - Cx + Sz.X * 0.5, P.Y - Cy + Sz.Y * 0.5);
}

void SSmvTileMap::ToGeo(const FGeometry& G, const FVector2D& P, double& La, double& Lo) const
{
	const FVector2D Sz = FVector2D(G.GetLocalSize());
	Unproject(Cx + P.X - Sz.X * 0.5, Cy + P.Y - Sz.Y * 0.5, Zoom, La, Lo);
}

int32 SSmvTileMap::HitGate(const FGeometry& G, const FVector2D& P) const
{
	if (!Gates) return -1;
	for (int32 i = Gates->Num() - 1; i >= 0; --i) if (FVector2D::Distance(ToLocal(G, (*Gates)[i].Lat, (*Gates)[i].Lon), P) < 14.0) return i;
	return -1;
}

int32 SSmvTileMap::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 L, const FWidgetStyle& Style, bool bParentEnabled) const
{
	const_cast<SSmvTileMap*>(this)->EnsureInit();
	const FVector2D Sz = FVector2D(G.GetLocalSize());
	const FSlateRect Clip = G.GetLayoutBoundingRect();
	Out.PushClip(FSlateClippingZone(Clip));
	DrBox(Out, L, G, FVector2D::ZeroVector, Sz, Hex(0x0b1222), nullptr);
	const double X0 = Cx - Sz.X * 0.5, Y0 = Cy - Sz.Y * 0.5;
	const int32 Tx0 = FMath::FloorToInt(X0 / 256.0), Ty0 = FMath::FloorToInt(Y0 / 256.0);
	const int32 Tx1 = FMath::FloorToInt((X0 + Sz.X) / 256.0), Ty1 = FMath::FloorToInt((Y0 + Sz.Y) / 256.0);
	for (int32 ty = Ty0; ty <= Ty1; ++ty)
		for (int32 tx = Tx0; tx <= Tx1; ++tx)
		{
			const FVector2D P(tx * 256.0 - X0, ty * 256.0 - Y0);
			if (const FSlateBrush* B = TileBrush(Zoom, tx, ty))
				FSlateDrawElement::MakeBox(Out, L + 1, G.ToPaintGeometry(FVector2f(256.f, 256.f), FSlateLayoutTransform(FVector2f(P))), B, ESlateDrawEffect::None, FLinearColor::White);
			else DrBox(Out, L + 1, G, P + FVector2D(1, 1), FVector2D(254, 254), FLinearColor(1, 1, 1, 0.03f), nullptr);
		}
	// metre/piksel
	const double La0 = Lat ? *Lat : 0.0;
	const double Mpp = 156543.03392 * FMath::Cos(FMath::DegreesToRadians(La0)) / (1 << Zoom);
	if (Lat && Lon)
	{
		const FVector2D H = ToLocal(G, *Lat, *Lon);
		if (RadiusM && *RadiusM > 0) DrArc(Out, L + 2, G, H, (float)(*RadiusM / Mpp), 0.f, 360.f, Accent().CopyWithNewOpacity(0.6f), 1.5f);
		DrBox(Out, L + 3, G, H - FVector2D(9, 9), FVector2D(18, 18), FLinearColor(0, 0, 0, 0.5f), Circle());
		DrBox(Out, L + 4, G, H - FVector2D(6, 6), FVector2D(12, 12), Accent(), Circle());
	}
	if (Gates)
	{
		TArray<FVector2D> Path;
		for (const FSmvGate& Gt : *Gates) Path.Add(ToLocal(G, Gt.Lat, Gt.Lon));
		if (Path.Num() > 1) { { const FVector2D First = Path[0]; Path.Add(First); } DrLine(Out, L + 3, G, Path, Hex(0xf472b6, 0.6f), 1.5f); Path.Pop(); }
		for (int32 i = 0; i < Gates->Num(); ++i)
		{
			const float R = FMath::Max(8.f, (float)((*Gates)[i].RadiusM / Mpp));
			DrArc(Out, L + 4, G, Path[i], R, 0.f, 360.f, i == DragGate ? Hex(0xfbbf24) : Hex(0xf472b6), 2.5f);
			DrText(Out, L + 5, G, FString::FromInt(i + 1), Path[i], Font(10, "Bold"), FLinearColor::White, 0);
		}
	}
	// ölçek + atıf + kip ipucu
	DrBox(Out, L + 6, G, FVector2D(0, Sz.Y - 20), FVector2D(Sz.X, 20), FLinearColor(0, 0, 0, 0.55f), nullptr);
	DrText(Out, L + 7, G, TEXT("Esri, Maxar, Earthstar Geographics"), FVector2D(Sz.X - 8, Sz.Y - 10), Font(8), FLinearColor(1, 1, 1, 0.7f), 1);
	DrText(Out, L + 7, G, FString::Printf(TEXT("%.0f m"), Mpp * 100.0) + TEXT("  ") + FSmvI18n::T(RingMode.Get() ? TEXT("map.hint_rings") : TEXT("map.hint_pick")).ToString(),
		FVector2D(8, Sz.Y - 10), Font(8), FLinearColor(1, 1, 1, 0.85f), -1);
	DrSeg(Out, L + 7, G, FVector2D(8, Sz.Y - 24), FVector2D(108, Sz.Y - 24), FLinearColor::White, 2.f);
	Out.PopClip();
	return L + 8;
}

FReply SSmvTileMap::OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E)
{
	const FVector2D P = FVector2D(G.AbsoluteToLocal(E.GetScreenSpacePosition()));
	if (E.GetEffectingButton() == EKeys::RightMouseButton)
	{
		const int32 Hg = HitGate(G, P);
		if (Hg >= 0 && Gates && RingMode.Get())
		{
			Gates->RemoveAt(Hg);
			for (int32 i = 0; i < Gates->Num(); ++i) (*Gates)[i].I = i;
			if (OnChanged) OnChanged();
		}
		return FReply::Handled();
	}
	if (E.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	DownAt = LastAt = P; bMoved = false;
	DragGate = RingMode.Get() ? HitGate(G, P) : -1;
	// işaretin üstünden basılırsa işaret sürüklenir (harita kaymaz)
	bDragPin = DragGate < 0 && !RingMode.Get() && Lat && Lon && FVector2D::Distance(ToLocal(G, *Lat, *Lon), P) < 18.0;
	bPan = DragGate < 0 && !bDragPin;
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SSmvTileMap::OnMouseMove(const FGeometry& G, const FPointerEvent& E)
{
	if (!HasMouseCapture()) return FReply::Unhandled();
	const FVector2D P = FVector2D(G.AbsoluteToLocal(E.GetScreenSpacePosition()));
	if (FVector2D::Distance(P, DownAt) > 4.0) bMoved = true;
	if (DragGate >= 0 && Gates && Gates->IsValidIndex(DragGate)) ToGeo(G, P, (*Gates)[DragGate].Lat, (*Gates)[DragGate].Lon);
	else if (bDragPin && Lat && Lon) ToGeo(G, P, *Lat, *Lon);
	else if (bPan) { Cx -= P.X - LastAt.X; Cy -= P.Y - LastAt.Y; }
	LastAt = P;
	return FReply::Handled();
}

FReply SSmvTileMap::OnMouseButtonUp(const FGeometry& G, const FPointerEvent& E)
{
	if (!HasMouseCapture()) return FReply::Unhandled();
	const FVector2D P = FVector2D(G.AbsoluteToLocal(E.GetScreenSpacePosition()));
	if (DragGate >= 0 || bDragPin) { if (OnChanged) OnChanged(); }
	else if (!bMoved)
	{
		double La, Lo;
		ToGeo(G, P, La, Lo);
		if (RingMode.Get() && Gates)
		{
			FSmvGate Gt;
			Gt.I = Gates->Num(); Gt.Lat = La; Gt.Lon = Lo;
			if (Gates->Num()) { const FSmvGate& Pv = Gates->Last(); Gt.AglM = Pv.AglM; Gt.RadiusM = Pv.RadiusM; Gt.Kind = Pv.Kind; }
			Gates->Add(Gt);
		}
		else if (Lat && Lon) { *Lat = La; *Lon = Lo; }
		if (OnChanged) OnChanged();
	}
	DragGate = -1; bPan = false; bDragPin = false;
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SSmvTileMap::OnMouseWheel(const FGeometry& G, const FPointerEvent& E)
{
	const FVector2D P = FVector2D(G.AbsoluteToLocal(E.GetScreenSpacePosition())), Sz = FVector2D(G.GetLocalSize());
	const int32 Nz = FMath::Clamp(Zoom + (E.GetWheelDelta() > 0 ? 1 : -1), 3, 19);
	if (Nz == Zoom) return FReply::Handled();
	// imleç altındaki nokta sabit kalsın
	const double Wx = Cx + P.X - Sz.X * 0.5, Wy = Cy + P.Y - Sz.Y * 0.5, K = Nz > Zoom ? 2.0 : 0.5;
	Cx = Wx * K - (P.X - Sz.X * 0.5); Cy = Wy * K - (P.Y - Sz.Y * 0.5);
	Zoom = Nz;
	return FReply::Handled();
}

void SSmvTileMap::Search(const FString& Query, TFunction<void(bool, double, double, const FString&)> Done)
{
	if (Query.TrimStartAndEnd().IsEmpty()) { Done(false, 0, 0, FString()); return; }
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> R = FHttpModule::Get().CreateRequest();
	R->SetURL(TEXT("https://nominatim.openstreetmap.org/search?format=json&limit=1&q=") + FGenericPlatformHttp::UrlEncode(Query));
	R->SetVerb(TEXT("GET"));
	R->SetHeader(TEXT("User-Agent"), TEXT("CAYSIM/1.0 (simumav.com)"));
	R->SetHeader(TEXT("Accept-Language"), FSmvI18n::Language());
	R->OnProcessRequestComplete().BindLambda([Done](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		if (bOk && Resp.IsValid() && Resp->GetResponseCode() == 200 && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Resp->GetContentAsString()), Arr) && Arr.Num())
		{
			const TSharedPtr<FJsonObject> O = Arr[0]->AsObject();
			if (O.IsValid()) { Done(true, FCString::Atod(*O->GetStringField(TEXT("lat"))), FCString::Atod(*O->GetStringField(TEXT("lon"))), O->GetStringField(TEXT("display_name"))); return; }
		}
		Done(false, 0, 0, FString());
	});
	R->ProcessRequest();
}

void SSmvTileMap::PaintImagery(FSlateWindowElementList& Out, int32 L, const FGeometry& G, double CLat, double CLon, double Mpp, TFunction<FVector2D(double, double)> ToScr, float Alpha)
{
	if (Mpp <= 0 || !ToScr) return;
	// karonun 1 pikseli ≈ ekranın 1 pikseli olacak yakınlaştırma
	const double Base = 156543.03392 * FMath::Cos(FMath::DegreesToRadians(CLat));
	int32 Z = FMath::Clamp(FMath::RoundToInt(FMath::Log2(Base / Mpp)), 3, 19);
	const FVector2D Sz = FVector2D(G.GetLocalSize());
	// büyük alan (tam ekran yükleme arka planı 1600×900+): karo sayısı sınırı aşınca HİÇ çizilmiyordu (gerçek dünya yükleme ekranı boştu)
	// → sınıra sığana dek bir alt yakınlaştırmaya inilir (karolar büyütülerek çizilir); yarı genişlik eksen başına
	FVector2D C; double SpanX = 0, SpanY = 0;
	int32 Tx0 = 0, Tx1 = 0, Ty0 = 0, Ty1 = 0;
	for (;;)
	{
		C = Project(CLat, CLon, Z);
		const double Px = Mpp / (Base / (1 << Z));   // ekran pikseli başına karo pikseli
		SpanX = Sz.X * 0.5 * Px + 256.0; SpanY = Sz.Y * 0.5 * Px + 256.0;
		Tx0 = FMath::FloorToInt((C.X - SpanX) / 256.0); Tx1 = FMath::FloorToInt((C.X + SpanX) / 256.0);
		Ty0 = FMath::FloorToInt((C.Y - SpanY) / 256.0); Ty1 = FMath::FloorToInt((C.Y + SpanY) / 256.0);
		if ((Tx1 - Tx0 + 1) * (Ty1 - Ty0 + 1) <= 96 || Z <= 3) break;
		--Z;
	}
	for (int32 ty = Ty0; ty <= Ty1; ++ty)
		for (int32 tx = Tx0; tx <= Tx1; ++tx)
		{
			const FSlateBrush* B = TileBrush(Z, tx, ty);
			if (!B) continue;
			double La0, Lo0, La1, Lo1;
			Unproject(tx * 256.0, ty * 256.0, Z, La0, Lo0);
			Unproject((tx + 1) * 256.0, (ty + 1) * 256.0, Z, La1, Lo1);
			const FVector2D P0 = ToScr(La0, Lo0), P1 = ToScr(La1, Lo1);
			const FVector2D Pmin(FMath::Min(P0.X, P1.X), FMath::Min(P0.Y, P1.Y)), Psz(FMath::Abs(P1.X - P0.X), FMath::Abs(P1.Y - P0.Y));
			if (Pmin.X > Sz.X || Pmin.Y > Sz.Y || Pmin.X + Psz.X < 0 || Pmin.Y + Psz.Y < 0) continue;
			FSlateDrawElement::MakeBox(Out, L, G.ToPaintGeometry(FVector2f(Psz), FSlateLayoutTransform(FVector2f(Pmin))), B, ESlateDrawEffect::None, FLinearColor(1, 1, 1, Alpha));
		}
}
