#include "SmvUiKit.h"
#include "SmvI18n.h"
#include "SmvUiPrefs.h"
#include "SmvInputGlyphs.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/Clipping.h"
#include "Math/TransformCalculus2D.h"
#include "InputCoreTypes.h"

// ---------------------------------------------------------------------------
// ses
// ---------------------------------------------------------------------------

namespace
{
	TFunction<void(int32)>& SmvSoundSink()
	{
		static TFunction<void(int32)> S;
		return S;
	}
}

void FSmvUiSound::SetSink(TFunction<void(int32)> InSink) { SmvSoundSink() = MoveTemp(InSink); }

void FSmvUiSound::Play(ESmvSnd S)
{
	static double LastHover = 0;
	const double Now = FPlatformTime::Seconds();
	if (S == ESmvSnd::Hover)
	{
		if (Now - LastHover < 0.06) return;
		LastHover = Now;
	}
	if (SmvSoundSink()) SmvSoundSink()((int32)S);
}

// ---------------------------------------------------------------------------
// tema
// ---------------------------------------------------------------------------

namespace SmvUi
{
	FLinearColor Hex(uint32 Rgb, float A)
	{
		FLinearColor C = FLinearColor::FromSRGBColor(FColor((uint8)((Rgb >> 16) & 0xff), (uint8)((Rgb >> 8) & 0xff), (uint8)(Rgb & 0xff), 255));
		C.A = A;
		return C;
	}
	bool& Light() { static bool b = false; return b; }
	void ApplyTheme(const FString& Theme)
	{
		if (Theme == TEXT("light")) Light() = true;
		else if (Theme == TEXT("auto")) { const int32 H = FDateTime::Now().GetHour(); Light() = H >= 7 && H < 19; }
		else Light() = false;
	}
	// koyu / açık tema (web theme.css :root ve [data-theme=light])
#define SMV_THEMED(Name, Dark, Light_) const FLinearColor& Name() { static const FLinearColor D = Dark; static const FLinearColor L = Light_; return Light() ? L : D; }
	SMV_THEMED(Bg, Hex(0x070b14), Hex(0xe9eef8))
	SMV_THEMED(Bg2, Hex(0x0d1526), Hex(0xf4f7fd))
	SMV_THEMED(Card, Hex(0x10182c, 0.78f), Hex(0xffffff, 0.80f))
	SMV_THEMED(CardHover, Hex(0x182440, 0.92f), Hex(0xffffff, 0.96f))
	SMV_THEMED(Edge, Hex(0x8eaae6, 0.20f), Hex(0x283c78, 0.16f))
	SMV_THEMED(Fg, Hex(0xe8eefb), Hex(0x0f1a33))
	SMV_THEMED(Dim, Hex(0x9aa8c7), Hex(0x44547a))
	SMV_THEMED(Mute, Hex(0x5f6c8a), Hex(0x8391b3))
	SMV_THEMED(Accent, Hex(0x38bdf8), Hex(0x0284c7))
	SMV_THEMED(Accent2, Hex(0xa78bfa), Hex(0x7c3aed))
	SMV_THEMED(Accent3, Hex(0x34d399), Hex(0x059669))
	SMV_THEMED(Warn, Hex(0xfbbf24), Hex(0xb45309))
	SMV_THEMED(Danger, Hex(0xf87171), Hex(0xdc2626))
#undef SMV_THEMED

	FSlateFontInfo Font(int32 Size, const ANSICHAR* Weight, int32 Spacing)
	{
		FSlateFontInfo F = FCoreStyle::GetDefaultFontStyle(Weight, Size);
		F.LetterSpacing = Spacing;
		return F;
	}

	const FSlateBrush* White() { return FCoreStyle::Get().GetBrush("WhiteBrush"); }

	const FSlateBrush* Round(float Radius)
	{
		static TMap<int32, TSharedPtr<FSlateBrush>> Cache;
		const int32 K = FMath::RoundToInt(Radius * 10.f);
		if (TSharedPtr<FSlateBrush>* F = Cache.Find(K)) return F->Get();
		TSharedPtr<FSlateBrush> B = MakeShared<FSlateRoundedBoxBrush>(FLinearColor::White, Radius);
		Cache.Add(K, B);
		return B.Get();
	}

	const FSlateBrush* RoundOutline(float Radius, float Width)
	{
		static TMap<int32, TSharedPtr<FSlateBrush>> Cache;
		const int32 K = FMath::RoundToInt(Radius * 10.f) * 1000 + FMath::RoundToInt(Width * 10.f);
		if (TSharedPtr<FSlateBrush>* F = Cache.Find(K)) return F->Get();
		TSharedPtr<FSlateBrush> B = MakeShared<FSlateRoundedBoxBrush>(FLinearColor::Transparent, Radius, FLinearColor::White, Width);
		Cache.Add(K, B);
		return B.Get();
	}

	const FSlateBrush* Circle()
	{
		static const FSlateRoundedBoxBrush B(FLinearColor::White);
		return &B;
	}

	const FSlateBrush* GlassCard()
	{
		static const FSlateRoundedBoxBrush B(Hex(0x10182c, 0.80f), 18.f, Hex(0x8eaae6, 0.18f), 1.f);
		static const FSlateRoundedBoxBrush Lb(Hex(0xffffff, 0.82f), 18.f, Hex(0x283c78, 0.14f), 1.f);
		return Light() ? &Lb : &B;
	}

	bool& ReducedMotion() { static bool b = false; return b; }
	bool& PadActive() { static bool b = false; return b; }
	double Now()
	{
		static const double Start = FPlatformTime::Seconds();
		return ReducedMotion() ? 12.0 : FPlatformTime::Seconds() - Start;
	}

	// ---- çizim ----

	void DrBox(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const FVector2D& P, const FVector2D& S, const FLinearColor& C, const FSlateBrush* B)
	{
		if (S.X <= 0 || S.Y <= 0 || C.A <= 0.001f) return;
		FSlateDrawElement::MakeBox(Out, L, G.ToPaintGeometry(FVector2f(S), FSlateLayoutTransform(FVector2f(P))), B ? B : White(), ESlateDrawEffect::None, C);
	}

	void DrLine(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const TArray<FVector2D>& Pts, const FLinearColor& C, float Thick)
	{
		if (Pts.Num() < 2 || C.A <= 0.001f) return;
		TArray<FVector2f> P;
		P.Reserve(Pts.Num());
		for (const FVector2D& V : Pts) P.Add(FVector2f((float)V.X, (float)V.Y));
		FSlateDrawElement::MakeLines(Out, L, G.ToPaintGeometry(), P, ESlateDrawEffect::None, C, true, Thick);
	}

	void DrSeg(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const FVector2D& A, const FVector2D& B, const FLinearColor& C, float Thick)
	{
		DrLine(Out, L, G, { A, B }, C, Thick);
	}

	void DrArc(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const FVector2D& C, float R, float A0, float A1, const FLinearColor& Col, float Thick)
	{
		const int32 N = FMath::Clamp((int32)(FMath::Abs(A1 - A0) / 5.f), 3, 96);
		TArray<FVector2D> P;
		P.Reserve(N + 1);
		for (int32 i = 0; i <= N; ++i)
		{
			const float A = FMath::DegreesToRadians(FMath::Lerp(A0, A1, (float)i / N));
			P.Add(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R);
		}
		DrLine(Out, L, G, P, Col, Thick);
	}

	void DrText(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const FString& S, FVector2D Pos, const FSlateFontInfo& F, const FLinearColor& C, int32 Align)
	{
		if (S.IsEmpty() || C.A <= 0.001f) return;
		const TSharedRef<FSlateFontMeasure> M = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const FVector2D Sz = M->Measure(S, F);
		if (Align == 0) Pos.X -= Sz.X * 0.5;
		else if (Align > 0) Pos.X -= Sz.X;
		Pos.Y -= Sz.Y * 0.5;
		FSlateDrawElement::MakeText(Out, L, G.ToPaintGeometry(FVector2f(Sz), FSlateLayoutTransform(FVector2f(Pos))), S, F, ESlateDrawEffect::None, C);
	}

	void DrGlow(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const FVector2D& C, float R, const FLinearColor& Col)
	{
		const int32 N = 9;
		for (int32 i = 0; i < N; ++i)
		{
			const float Rr = R * (1.f - (float)i / N);
			DrBox(Out, L, G, C - FVector2D(Rr, Rr), FVector2D(2 * Rr, 2 * Rr), Col.CopyWithNewOpacity(Col.A / N), Circle());
		}
	}

	void DrGradient(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const FVector2D& P, const FVector2D& S, const TArray<FSmvStop>& Stops, bool bVertical)
	{
		if (Stops.Num() < 2 || S.X <= 0 || S.Y <= 0) return;
		TArray<FSlateGradientStop> St;
		for (const FSmvStop& X : Stops) St.Add(FSlateGradientStop(FVector2D(S.X * X.P, S.Y * X.P), X.C));
		// Orient_Horizontal: renk dikeyde değişir (yatay bantlar); Orient_Vertical: yatayda değişir
		FSlateDrawElement::MakeGradient(Out, L, G.ToPaintGeometry(FVector2f(S), FSlateLayoutTransform(FVector2f(P))), St,
			bVertical ? EOrientation::Orient_Horizontal : EOrientation::Orient_Vertical, ESlateDrawEffect::None);
	}

	/** Yuvarlak köşeli dikdörtgen çizgisi */
	static void DrRoundRect(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const FVector2D& P, const FVector2D& S, float R, const FLinearColor& C, float Th)
	{
		R = FMath::Min(R, (float)FMath::Min(S.X, S.Y) * 0.5f);
		TArray<FVector2D> Pts;
		auto Arc = [&Pts, R](const FVector2D& Ctr, float A0, float A1)
		{
			for (int32 i = 0; i <= 6; ++i)
			{
				const float A = FMath::DegreesToRadians(FMath::Lerp(A0, A1, i / 6.f));
				Pts.Add(Ctr + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R);
			}
		};
		Arc(P + FVector2D(R, R), 180.f, 270.f);
		Arc(P + FVector2D(S.X - R, R), 270.f, 360.f);
		Arc(P + FVector2D(S.X - R, S.Y - R), 0.f, 90.f);
		Arc(P + FVector2D(R, S.Y - R), 90.f, 180.f);
		{ const FVector2D First = Pts[0]; Pts.Add(First); }
		DrLine(Out, L, G, Pts, C, Th);
	}

	ESmvIcon IconForType(const FString& T)
	{
		if (T == TEXT("plane")) return ESmvIcon::Plane;
		if (T == TEXT("vtol")) return ESmvIcon::Vtol;
		if (T == TEXT("heli")) return ESmvIcon::Heli;
		if (T == TEXT("rover")) return ESmvIcon::Rover;
		if (T == TEXT("fpv")) return ESmvIcon::Fpv;
		if (T == TEXT("interceptor")) return ESmvIcon::Rocket;
		return ESmvIcon::Drone;
	}

	void DrIcon(FSlateWindowElementList& Out, int32 L, const FGeometry& G, ESmvIcon I, const FVector2D& C, float Sz, const FLinearColor& Col, float Th)
	{
		const float h = Sz * 0.5f;
		auto P = [&C, h](float x, float y) { return C + FVector2D(x, y) * h; };
		auto Ln = [&](std::initializer_list<FVector2D> Pts) { DrLine(Out, L, G, TArray<FVector2D>(Pts), Col, Th); };
		auto Ci = [&](float x, float y, float r, float a0 = 0.f, float a1 = 360.f) { DrArc(Out, L, G, P(x, y), r * h, a0, a1, Col, Th); };
		auto Dot = [&](float x, float y, float r) { DrBox(Out, L, G, P(x, y) - FVector2D(r * h, r * h), FVector2D(2 * r * h, 2 * r * h), Col, Circle()); };
		switch (I)
		{
		case ESmvIcon::None: break;
		case ESmvIcon::Play: Ln({ P(-0.45f, -0.7f), P(0.7f, 0.f), P(-0.45f, 0.7f), P(-0.45f, -0.7f) }); break;
		case ESmvIcon::Drone: case ESmvIcon::Fpv:
			Ln({ P(-0.55f, -0.55f), P(0.55f, 0.55f) }); Ln({ P(0.55f, -0.55f), P(-0.55f, 0.55f) });
			Ci(-0.6f, -0.6f, 0.3f); Ci(0.6f, -0.6f, 0.3f); Ci(-0.6f, 0.6f, 0.3f); Ci(0.6f, 0.6f, 0.3f);
			if (I == ESmvIcon::Fpv) Ln({ P(-0.15f, -0.2f), P(0.15f, -0.2f), P(0.15f, 0.15f), P(-0.15f, 0.15f), P(-0.15f, -0.2f) });
			else Dot(0, 0, 0.14f);
			break;
		case ESmvIcon::Plane:
			Ln({ P(0.f, -0.9f), P(0.f, 0.8f) }); Ln({ P(-0.85f, -0.05f), P(0.f, -0.25f), P(0.85f, -0.05f) });
			Ln({ P(-0.35f, 0.75f), P(0.f, 0.6f), P(0.35f, 0.75f) }); break;
		case ESmvIcon::Vtol:
			Ln({ P(0.f, -0.8f), P(0.f, 0.8f) }); Ln({ P(-0.9f, 0.f), P(0.9f, 0.f) });
			Ci(-0.55f, -0.45f, 0.25f); Ci(0.55f, -0.45f, 0.25f); Ci(-0.55f, 0.45f, 0.25f); Ci(0.55f, 0.45f, 0.25f); break;
		case ESmvIcon::Heli:
			Ln({ P(-0.9f, -0.55f), P(0.9f, -0.55f) }); Ln({ P(0.f, -0.55f), P(0.f, -0.3f) });
			Ci(-0.15f, 0.05f, 0.38f); Ln({ P(0.2f, 0.05f), P(0.9f, -0.05f) }); Ln({ P(-0.5f, 0.6f), P(0.3f, 0.6f) }); break;
		case ESmvIcon::Rover:
			Ln({ P(-0.85f, 0.25f), P(-0.85f, -0.25f), P(-0.4f, -0.25f), P(-0.2f, -0.55f), P(0.45f, -0.55f), P(0.65f, -0.25f), P(0.85f, -0.25f), P(0.85f, 0.25f), P(-0.85f, 0.25f) });
			Ci(-0.45f, 0.42f, 0.22f); Ci(0.45f, 0.42f, 0.22f); break;
		case ESmvIcon::Rocket:
			Ln({ P(0.f, -0.9f), P(0.3f, -0.5f), P(0.3f, 0.5f), P(-0.3f, 0.5f), P(-0.3f, -0.5f), P(0.f, -0.9f) });
			Ln({ P(0.3f, 0.2f), P(0.6f, 0.7f), P(0.3f, 0.6f) }); Ln({ P(-0.3f, 0.2f), P(-0.6f, 0.7f), P(-0.3f, 0.6f) }); break;
		case ESmvIcon::Pilot: case ESmvIcon::User:
			Ci(0.f, -0.35f, 0.35f); Ci(0.f, 0.95f, 0.75f, 200.f, 340.f); break;
		case ESmvIcon::Route:
			Dot(-0.7f, 0.6f, 0.12f); Dot(0.7f, -0.6f, 0.12f);
			Ln({ P(-0.7f, 0.6f), P(-0.2f, 0.6f), P(0.2f, -0.6f), P(0.7f, -0.6f) }); Ci(0.f, 0.f, 0.22f); break;
		case ESmvIcon::Mission: case ESmvIcon::Flag:
			Ln({ P(-0.6f, 0.9f), P(-0.6f, -0.85f) }); Ln({ P(-0.6f, -0.85f), P(0.7f, -0.55f), P(-0.6f, -0.2f) }); break;
		case ESmvIcon::Match:
			Ln({ P(-0.75f, -0.75f), P(0.55f, 0.55f) }); Ln({ P(0.75f, -0.75f), P(-0.55f, 0.55f) });
			Ln({ P(0.35f, 0.75f), P(0.75f, 0.35f) }); Ln({ P(-0.35f, 0.75f), P(-0.75f, 0.35f) }); break;
		case ESmvIcon::Trophy:
			Ln({ P(-0.5f, -0.8f), P(0.5f, -0.8f), P(0.4f, -0.1f), P(0.f, 0.15f), P(-0.4f, -0.1f), P(-0.5f, -0.8f) });
			Ci(-0.55f, -0.5f, 0.22f, 90.f, 270.f); Ci(0.55f, -0.5f, 0.22f, -90.f, 90.f);
			Ln({ P(0.f, 0.15f), P(0.f, 0.55f) }); Ln({ P(-0.4f, 0.75f), P(0.4f, 0.75f) }); break;
		case ESmvIcon::Book: case ESmvIcon::Replay:
			Ln({ P(0.f, -0.6f), P(0.f, 0.75f) }); Ln({ P(0.f, -0.6f), P(-0.8f, -0.75f), P(-0.8f, 0.6f), P(0.f, 0.75f), P(0.8f, 0.6f), P(0.8f, -0.75f), P(0.f, -0.6f) }); break;
		case ESmvIcon::Gear:
			Ci(0.f, 0.f, 0.32f);
			for (int32 k = 0; k < 8; ++k)
			{
				const float A = k * UE_PI / 4.f;
				Ln({ P(FMath::Cos(A) * 0.5f, FMath::Sin(A) * 0.5f), P(FMath::Cos(A) * 0.85f, FMath::Sin(A) * 0.85f) });
			}
			Ci(0.f, 0.f, 0.58f); break;
		case ESmvIcon::Power:
			Ci(0.f, 0.08f, 0.7f, -60.f, 240.f); Ln({ P(0.f, -0.9f), P(0.f, -0.05f) }); break;
		case ESmvIcon::Back: case ESmvIcon::Prev:
			Ln({ P(0.4f, -0.7f), P(-0.3f, 0.f), P(0.4f, 0.7f) }); break;
		case ESmvIcon::Next:
			Ln({ P(-0.4f, -0.7f), P(0.3f, 0.f), P(-0.4f, 0.7f) }); break;
		case ESmvIcon::Help:
			Ci(0.f, 0.f, 0.85f); Ci(0.f, -0.25f, 0.3f, 180.f, 405.f); Ln({ P(0.f, 0.05f), P(0.f, 0.3f) }); Dot(0.f, 0.55f, 0.08f); break;
		case ESmvIcon::Info:
			Ci(0.f, 0.f, 0.85f); Ln({ P(0.f, -0.1f), P(0.f, 0.5f) }); Dot(0.f, -0.4f, 0.08f); break;
		case ESmvIcon::Globe:
			Ci(0.f, 0.f, 0.85f); Ln({ P(-0.85f, 0.f), P(0.85f, 0.f) });
			DrArc(Out, L, G, P(0.f, 0.f), 0.85f * h, -90.f, 90.f, Col, Th);
			Ln({ P(0.f, -0.85f), P(0.f, 0.85f) }); break;
		case ESmvIcon::Plus: Ln({ P(-0.7f, 0.f), P(0.7f, 0.f) }); Ln({ P(0.f, -0.7f), P(0.f, 0.7f) }); break;
		case ESmvIcon::Check: Ln({ P(-0.7f, 0.05f), P(-0.2f, 0.55f), P(0.75f, -0.5f) }); break;
		case ESmvIcon::Save:
			Ln({ P(-0.75f, -0.75f), P(0.5f, -0.75f), P(0.75f, -0.5f), P(0.75f, 0.75f), P(-0.75f, 0.75f), P(-0.75f, -0.75f) });
			Ln({ P(-0.4f, -0.75f), P(-0.4f, -0.3f), P(0.35f, -0.3f), P(0.35f, -0.75f) }); Ci(0.f, 0.3f, 0.2f); break;
		case ESmvIcon::Trash:
			Ln({ P(-0.75f, -0.55f), P(0.75f, -0.55f) }); Ln({ P(-0.25f, -0.55f), P(-0.25f, -0.8f), P(0.25f, -0.8f), P(0.25f, -0.55f) });
			Ln({ P(-0.55f, -0.55f), P(-0.45f, 0.8f), P(0.45f, 0.8f), P(0.55f, -0.55f) }); break;
		case ESmvIcon::Star:
		{
			TArray<FVector2D> S;
			for (int32 k = 0; k <= 10; ++k)
			{
				const float A = -UE_PI / 2 + k * UE_PI / 5.f, R = (k % 2) ? 0.38f : 0.9f;
				S.Add(P(FMath::Cos(A) * R, FMath::Sin(A) * R));
			}
			DrLine(Out, L, G, S, Col, Th);
			break;
		}
		case ESmvIcon::Map:
			Ln({ P(-0.85f, -0.6f), P(-0.3f, -0.8f), P(0.3f, -0.6f), P(0.85f, -0.8f), P(0.85f, 0.6f), P(0.3f, 0.8f), P(-0.3f, 0.6f), P(-0.85f, 0.8f), P(-0.85f, -0.6f) });
			Ln({ P(-0.3f, -0.8f), P(-0.3f, 0.6f) }); Ln({ P(0.3f, -0.6f), P(0.3f, 0.8f) }); break;
		case ESmvIcon::Sun:
			Ci(0.f, 0.f, 0.38f);
			for (int32 k = 0; k < 8; ++k)
			{
				const float A = k * UE_PI / 4.f;
				Ln({ P(FMath::Cos(A) * 0.58f, FMath::Sin(A) * 0.58f), P(FMath::Cos(A) * 0.9f, FMath::Sin(A) * 0.9f) });
			}
			break;
		case ESmvIcon::Sunset:
			Ci(0.f, 0.2f, 0.45f, 180.f, 360.f); Ln({ P(-0.9f, 0.2f), P(0.9f, 0.2f) }); Ln({ P(-0.6f, 0.55f), P(0.6f, 0.55f) });
			Ln({ P(0.f, -0.35f), P(0.f, -0.75f) }); Ln({ P(-0.55f, -0.15f), P(-0.8f, -0.4f) }); Ln({ P(0.55f, -0.15f), P(0.8f, -0.4f) }); break;
		case ESmvIcon::Moon: Ci(0.f, 0.f, 0.75f, 60.f, 300.f); Ci(0.35f, -0.05f, 0.55f, 100.f, 260.f); break;
		case ESmvIcon::Cloud: case ESmvIcon::Rain: case ESmvIcon::Snow: case ESmvIcon::Fog:
			Ci(-0.35f, -0.05f, 0.35f, 90.f, 270.f); Ci(0.1f, -0.25f, 0.45f, 180.f, 360.f); Ci(0.5f, 0.0f, 0.3f, -90.f, 90.f);
			Ln({ P(-0.35f, 0.3f), P(0.5f, 0.3f) });
			if (I == ESmvIcon::Rain) { Ln({ P(-0.3f, 0.5f), P(-0.4f, 0.85f) }); Ln({ P(0.1f, 0.5f), P(0.f, 0.85f) }); Ln({ P(0.5f, 0.5f), P(0.4f, 0.85f) }); }
			if (I == ESmvIcon::Snow) { Dot(-0.35f, 0.65f, 0.07f); Dot(0.05f, 0.8f, 0.07f); Dot(0.45f, 0.62f, 0.07f); }
			if (I == ESmvIcon::Fog) { Ln({ P(-0.8f, 0.55f), P(0.8f, 0.55f) }); Ln({ P(-0.6f, 0.8f), P(0.6f, 0.8f) }); }
			break;
		case ESmvIcon::Wind:
			Ln({ P(-0.85f, -0.3f), P(0.35f, -0.3f) }); Ci(0.35f, -0.52f, 0.22f, -90.f, 90.f);
			Ln({ P(-0.85f, 0.05f), P(0.55f, 0.05f) }); Ln({ P(-0.85f, 0.4f), P(0.2f, 0.4f) }); Ci(0.2f, 0.62f, 0.22f, -90.f, 90.f); break;
		case ESmvIcon::Gamepad:
			Ln({ P(-0.85f, -0.3f), P(0.85f, -0.3f), P(0.95f, 0.5f), P(0.6f, 0.6f), P(0.35f, 0.25f), P(-0.35f, 0.25f), P(-0.6f, 0.6f), P(-0.95f, 0.5f), P(-0.85f, -0.3f) });
			Ln({ P(-0.6f, -0.05f), P(-0.3f, -0.05f) }); Ln({ P(-0.45f, -0.2f), P(-0.45f, 0.1f) }); Dot(0.45f, -0.12f, 0.07f); Dot(0.6f, 0.05f, 0.07f); break;
		case ESmvIcon::Keyboard:
			Ln({ P(-0.9f, -0.45f), P(0.9f, -0.45f), P(0.9f, 0.45f), P(-0.9f, 0.45f), P(-0.9f, -0.45f) });
			for (int32 k = 0; k < 5; ++k) Dot(-0.6f + k * 0.3f, -0.15f, 0.05f);
			Ln({ P(-0.45f, 0.2f), P(0.45f, 0.2f) }); break;
		case ESmvIcon::Stick:
			Ci(0.f, 0.f, 0.85f); Ci(0.25f, -0.25f, 0.28f); Ln({ P(0.f, 0.f), P(0.25f, -0.25f) }); break;
		case ESmvIcon::Pause: Ln({ P(-0.3f, -0.65f), P(-0.3f, 0.65f) }); Ln({ P(0.3f, -0.65f), P(0.3f, 0.65f) }); break;
		case ESmvIcon::Home: Ln({ P(-0.85f, 0.f), P(0.f, -0.8f), P(0.85f, 0.f) }); Ln({ P(-0.6f, -0.2f), P(-0.6f, 0.8f), P(0.6f, 0.8f), P(0.6f, -0.2f) }); break;
		case ESmvIcon::Camera:
			Ln({ P(-0.85f, -0.45f), P(0.85f, -0.45f), P(0.85f, 0.65f), P(-0.85f, 0.65f), P(-0.85f, -0.45f) });
			Ln({ P(-0.3f, -0.45f), P(-0.15f, -0.7f), P(0.25f, -0.7f), P(0.4f, -0.45f) }); Ci(0.f, 0.1f, 0.32f); break;
		case ESmvIcon::Volume:
			Ln({ P(-0.85f, -0.25f), P(-0.45f, -0.25f), P(0.05f, -0.7f), P(0.05f, 0.7f), P(-0.45f, 0.25f), P(-0.85f, 0.25f), P(-0.85f, -0.25f) });
			Ci(0.05f, 0.f, 0.45f, -45.f, 45.f); Ci(0.05f, 0.f, 0.75f, -45.f, 45.f); break;
		case ESmvIcon::Monitor:
			Ln({ P(-0.9f, -0.65f), P(0.9f, -0.65f), P(0.9f, 0.4f), P(-0.9f, 0.4f), P(-0.9f, -0.65f) }); Ln({ P(0.f, 0.4f), P(0.f, 0.7f) }); Ln({ P(-0.45f, 0.75f), P(0.45f, 0.75f) }); break;
		case ESmvIcon::Bolt: Ln({ P(0.2f, -0.9f), P(-0.45f, 0.1f), P(0.05f, 0.1f), P(-0.2f, 0.9f), P(0.45f, -0.15f), P(-0.05f, -0.15f), P(0.2f, -0.9f) }); break;
		case ESmvIcon::Shield: Ln({ P(0.f, -0.85f), P(0.75f, -0.55f), P(0.65f, 0.25f), P(0.f, 0.9f), P(-0.65f, 0.25f), P(-0.75f, -0.55f), P(0.f, -0.85f) }); break;
		case ESmvIcon::Ghost:
			Ci(0.f, -0.15f, 0.6f, 180.f, 360.f);
			Ln({ P(-0.6f, -0.15f), P(-0.6f, 0.75f), P(-0.3f, 0.5f), P(0.f, 0.75f), P(0.3f, 0.5f), P(0.6f, 0.75f), P(0.6f, -0.15f) });
			Dot(-0.2f, -0.2f, 0.08f); Dot(0.2f, -0.2f, 0.08f); break;
		}
	}

	// ---- bileşen yardımcıları ----

	/** Büyük harf (ICU; Türkçede i → İ, ı → I) — FString::ToUpper yalnız ASCII'yi çevirir (ğ, ş, ö kalıyordu) */
	FString SmvUpper(const FString& In)
	{
		FString S = In;
		if (FSmvI18n::Language() == TEXT("tr")) { S.ReplaceInline(TEXT("i"), TEXT("İ"), ESearchCase::CaseSensitive); S.ReplaceInline(TEXT("ı"), TEXT("I"), ESearchCase::CaseSensitive); }
		return FText::FromString(S).ToUpper().ToString();
	}

	/** Açık temada saf beyaz metin rengi koyu mürekkebe döner */
	static FLinearColor Ink(const FLinearColor& C) { return (Light() && C.R > 0.95f && C.G > 0.95f && C.B > 0.95f) ? Fg() : C; }

	TSharedRef<SWidget> Title(const FText& T, int32 Size, const FLinearColor& Color)
	{
		return SNew(STextBlock).Text(FText::FromString(SmvUpper(T.ToString()))).Font(Font(Size, "Bold", 180)).ColorAndOpacity(Ink(Color))
			.ShadowOffset(FVector2D(0, 2)).ShadowColorAndOpacity(Accent().CopyWithNewOpacity(0.35f));
	}

	TSharedRef<SWidget> Text(const TAttribute<FText>& T, int32 Size, const ANSICHAR* Weight, const FLinearColor& Color, bool bWrap)
	{
		return SNew(STextBlock).Text(T).Font(Font(Size, Weight)).ColorAndOpacity(Ink(Color)).AutoWrapText(bWrap);
	}

	TSharedRef<SWidget> Section(const FText& T, const FLinearColor& Color)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)
			[
				SNew(SBox).WidthOverride(4.f).HeightOverride(16.f)[SNew(SBorder).BorderImage(Round(2.f)).BorderBackgroundColor(Color)]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(SmvUpper(T.ToString()))).Font(Font(12, "Bold", 160)).ColorAndOpacity(Color)
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(12, 0, 0, 0)
			[
				SNew(SBox).HeightOverride(1.f)[SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Edge())]
			];
	}

	TSharedRef<SWidget> Glass(TSharedRef<SWidget> Inner, FMargin Pad, float Alpha)
	{
		return SNew(SBorder).BorderImage(GlassCard()).BorderBackgroundColor(FLinearColor(1, 1, 1, Alpha)).Padding(Pad)[Inner];
	}

	TSharedRef<SWidget> Row(const FText& Label, TSharedRef<SWidget> Editor, const FText& Hint)
	{
		TSharedRef<SVerticalBox> Lb = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[Text(Label, 13, "Regular", Fg())];
		if (!Hint.IsEmpty()) Lb->AddSlot().AutoHeight().Padding(0, 2, 0, 0)[Text(Hint, 10, "Regular", Mute())];
		return SNew(SBorder).BorderImage(Round(10.f)).BorderBackgroundColor(FLinearColor(1, 1, 1, 0.025f)).Padding(FMargin(14.f, 7.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.45f).VAlign(VAlign_Center).Padding(0, 0, 14, 0)[Lb]
			+ SHorizontalBox::Slot().FillWidth(0.55f).VAlign(VAlign_Center).HAlign(HAlign_Left)[Editor]
		];
	}

	TSharedRef<SWidget> Icon(ESmvIcon I, float Size, const FLinearColor& Color)
	{
		TSharedRef<SSmvCanvas> C = SNew(SSmvCanvas).Size(FVector2D(Size, Size)).Visibility(EVisibility::HitTestInvisible);
		C->Painter = [I, Color, Size](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
		{
			DrIcon(Out, L, G, I, FVector2D(G.GetLocalSize()) * 0.5, Size * 0.86f, Color, FMath::Max(1.5f, Size / 14.f));
		};
		return C;
	}

	FString KeyLabel(const FKey& K)
	{
		if (!K.IsValid()) return FString(TEXT("—"));
		if (K == EKeys::Gamepad_FaceButton_Bottom) return TEXT("pad:A");
		if (K == EKeys::Gamepad_FaceButton_Right) return TEXT("pad:B");
		if (K == EKeys::Gamepad_FaceButton_Left) return TEXT("pad:X");
		if (K == EKeys::Gamepad_FaceButton_Top) return TEXT("pad:Y");
		if (K == EKeys::Gamepad_LeftShoulder) return TEXT("pad:LB");
		if (K == EKeys::Gamepad_RightShoulder) return TEXT("pad:RB");
		if (K == EKeys::Gamepad_LeftTrigger) return TEXT("pad:LT");
		if (K == EKeys::Gamepad_RightTrigger) return TEXT("pad:RT");
		if (K == EKeys::Gamepad_Special_Right) return TEXT("pad:Menu");
		if (K == EKeys::Gamepad_Special_Left) return TEXT("pad:View");
		if (K == EKeys::Gamepad_LeftThumbstick) return TEXT("pad:LS");
		if (K == EKeys::Gamepad_RightThumbstick) return TEXT("pad:RS");
		if (K == EKeys::Gamepad_DPad_Up) return TEXT("pad:D↑");
		if (K == EKeys::Gamepad_DPad_Down) return TEXT("pad:D↓");
		if (K == EKeys::Gamepad_DPad_Left) return TEXT("pad:D←");
		if (K == EKeys::Gamepad_DPad_Right) return TEXT("pad:D→");
		if (K == EKeys::SpaceBar) return TEXT("Space");
		if (K == EKeys::Escape) return TEXT("Esc");
		if (K == EKeys::BackSpace) return TEXT("Backspace");
		if (K == EKeys::Add) return TEXT("Num +");
		if (K == EKeys::Subtract) return TEXT("Num −");
		return K.GetDisplayName().ToString();
	}

	TSharedRef<SWidget> KeyCapText(const FString& Key);

	/** Tuş başlığı: Joystix simgesi (etkin cihaz ailesine göre; çizim anında çözülür → cihaz değişince hemen güncellenir), yoksa metin rozeti.
	 *  "W A S D" gibi birleşik metinler parça parça simgelenir. */
	TSharedRef<SWidget> KeyCap(const FString& Key)
	{
		FSmvInputGlyphs::Init();
		if (!FSmvInputGlyphs::HasPack()) return KeyCapText(Key);
		TArray<FString> Parts;
		if (!Key.StartsWith(TEXT("pad:")) && Key.Contains(TEXT(" ")) && Key.ParseIntoArray(Parts, TEXT(" ")) > 1)
		{
			bool bAll = true;
			for (const FString& P : Parts) if (!FSmvInputGlyphs::Brush(P)) bAll = false;
			if (bAll)
			{
				TSharedRef<SHorizontalBox> H = SNew(SHorizontalBox);
				for (const FString& P : Parts) H->AddSlot().AutoWidth().Padding(0, 0, 2, 0)[KeyCap(P)];
				return H;
			}
		}
		return SNew(SOverlay)
			+ SOverlay::Slot().VAlign(VAlign_Center)
			[
				SNew(SImage).Image_Lambda([Key]() { return FSmvInputGlyphs::Brush(Key); })
				.Visibility_Lambda([Key]() { return FSmvInputGlyphs::Brush(Key) ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			]
			+ SOverlay::Slot().VAlign(VAlign_Center)
			[
				SNew(SBox).Visibility_Lambda([Key]() { return FSmvInputGlyphs::Brush(Key) ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })[KeyCapText(Key)]
			];
	}

	TSharedRef<SWidget> KeyCapText(const FString& Key)
	{
		if (Key.StartsWith(TEXT("pad:")))
		{
			const FString N = Key.Mid(4);
			FLinearColor C = Hex(0x334155);
			if (N == TEXT("A")) C = Hex(0x22c55e);
			else if (N == TEXT("B")) C = Hex(0xef4444);
			else if (N == TEXT("X")) C = Hex(0x3b82f6);
			else if (N == TEXT("Y")) C = Hex(0xeab308);
			const bool bRound = N.Len() == 1;
			return SNew(SBox).MinDesiredWidth(26.f).HeightOverride(26.f)
			[
				SNew(SBorder).BorderImage(bRound ? Circle() : Round(8.f)).BorderBackgroundColor(C).Padding(FMargin(bRound ? 0.f : 7.f, 0.f))
				.HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(N)).Font(Font(10, "Bold")).ColorAndOpacity(FLinearColor::White)
				]
			];
		}
		return SNew(SBorder).BorderImage(Round(6.f)).BorderBackgroundColor(FLinearColor(1, 1, 1, 0.12f)).Padding(FMargin(7.f, 2.f))
		[
			SNew(STextBlock).Text(FText::FromString(Key)).Font(Font(10, "Bold")).ColorAndOpacity(Fg())
		];
	}

	TSharedRef<SWidget> HintBar(const TArray<FSmvHint>& Kb, const TArray<FSmvHint>& Pad)
	{
		auto Build = [](const TArray<FSmvHint>& Items) -> TSharedRef<SHorizontalBox>
		{
			TSharedRef<SHorizontalBox> H = SNew(SHorizontalBox);
			for (const FSmvHint& It : Items)
			{
				H->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)[KeyCap(It.Key)];
				H->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 20, 0)[Text(It.Label, 11, "Regular", Dim(), false)];
			}
			return H;
		};
		return SNew(SOverlay)
			+ SOverlay::Slot()[SNew(SBox).Visibility_Lambda([]() { return PadActive() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })[Build(Kb)]]
			+ SOverlay::Slot()[SNew(SBox).Visibility_Lambda([]() { return PadActive() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })[Build(Pad)]];
	}

	void StickMap(int32 Mode, FString Out[4])
	{
		static const TCHAR* M[5][4] = {
			{ TEXT("yaw"), TEXT("thr"), TEXT("roll"), TEXT("pitch") },
			{ TEXT("yaw"), TEXT("pitch"), TEXT("roll"), TEXT("thr") },
			{ TEXT("yaw"), TEXT("thr"), TEXT("roll"), TEXT("pitch") },
			{ TEXT("roll"), TEXT("pitch"), TEXT("yaw"), TEXT("thr") },
			{ TEXT("roll"), TEXT("thr"), TEXT("yaw"), TEXT("pitch") } };
		const int32 Md = (Mode >= 1 && Mode <= 4) ? Mode : 2;
		for (int32 i = 0; i < 4; ++i) Out[i] = M[Md][i];
	}

	FText AxisName(const FString& Axis) { return FSmvI18n::T(FString(TEXT("axis.")) + Axis); }

	FString StickKeyHint(int32 Mode)
	{
		FString A[4];
		StickMap(Mode, A);
		auto N = [](const FString& X) { return AxisName(X).ToString().ToLower(); };
		return FString::Printf(TEXT("W/S %s · A/D %s · ↑↓ %s · ←→ %s"), *N(A[1]), *N(A[0]), *N(A[3]), *N(A[2]));
	}

	TSharedRef<SWidget> StickKeyHintWidget(int32 Mode, int32 Size, const FLinearColor& Color)
	{
		FString A[4];
		StickMap(Mode, A);
		auto N = [](const FString& X) { return AxisName(X).ToString().ToLower(); };
		auto Build = [&](bool bPad) -> TSharedRef<SWidget>
		{
			TSharedRef<SHorizontalBox> H = SNew(SHorizontalBox);
			auto Pair = [&](const TCHAR* K1, const TCHAR* K2, const FString& Label, bool bLast)
			{
				H->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 2, 0)[KeyCap(K1)];
				if (K2) H->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)[KeyCap(K2)];
				H->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0, bLast ? 0 : 12, 0)[Text(FText::FromString(Label), Size, "Bold", Color, false)];
			};
			if (bPad)
			{
				Pair(TEXT("pad:LS"), nullptr, TEXT("↕ ") + N(A[1]) + TEXT(" · ↔ ") + N(A[0]), false);
				Pair(TEXT("pad:RS"), nullptr, TEXT("↕ ") + N(A[3]) + TEXT(" · ↔ ") + N(A[2]), true);
			}
			else
			{
				Pair(TEXT("W"), TEXT("S"), N(A[1]), false);
				Pair(TEXT("A"), TEXT("D"), N(A[0]), false);
				Pair(TEXT("↑"), TEXT("↓"), N(A[3]), false);
				Pair(TEXT("←"), TEXT("→"), N(A[2]), true);
			}
			return H;
		};
		return SNew(SOverlay)
			+ SOverlay::Slot()[SNew(SBox).Visibility_Lambda([]() { return PadActive() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })[Build(false)]]
			+ SOverlay::Slot()[SNew(SBox).Visibility_Lambda([]() { return PadActive() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })[Build(true)]];
	}

	void DrStickMode(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const FVector2D& P, const FVector2D& S, int32 Mode, const FLinearColor& Col)
	{
		FString A[4];
		StickMap(Mode, A);
		float R = (float)FMath::Min(S.X * 0.22, S.Y * 0.36);
		// Joystix kumanda görseli varsa: görsel ortada, çubuk eksenleri görselin üzerindeki gerçek çubuk konumlarında
		FVector2D StL(0.27, 0.52), StR(0.73, 0.52);
		FVector2D ImgP = P, ImgS = S;
		if (const FSlateBrush* Cb = FSmvInputGlyphs::ControllerBrush(StL, StR))
		{
			const FVector2D Isz = Cb->ImageSize;
			const float K = (float)FMath::Min(S.X / FMath::Max(1.0, Isz.X), S.Y * 0.9 / FMath::Max(1.0, Isz.Y));
			ImgS = Isz * K;
			ImgP = P + (S - ImgS) * 0.5;
			DrBox(Out, L, G, ImgP, ImgS, FLinearColor(1, 1, 1, 0.85f), Cb);
			R = (float)ImgS.Y * 0.16f;
		}
		else { StL = FVector2D(0.27, 0.52); StR = FVector2D(0.73, 0.52); }
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const FVector2D St = Side == 0 ? StL : StR;
			const FVector2D C = ImgP + FVector2D(ImgS.X * St.X, ImgS.Y * St.Y);
			DrArc(Out, L, G, C, R, 0.f, 360.f, Col.CopyWithNewOpacity(0.5f), 1.5f);
			DrSeg(Out, L, G, C - FVector2D(R * 0.75f, 0), C + FVector2D(R * 0.75f, 0), Col.CopyWithNewOpacity(0.35f), 1.f);
			DrSeg(Out, L, G, C - FVector2D(0, R * 0.75f), C + FVector2D(0, R * 0.75f), Col.CopyWithNewOpacity(0.35f), 1.f);
			DrBox(Out, L + 1, G, C - FVector2D(5, 5), FVector2D(10, 10), Col, Circle());
			const FString& Ax = A[Side * 2], &Ay = A[Side * 2 + 1];
			const FLinearColor Thr = Warn();
			DrText(Out, L + 1, G, AxisName(Ay).ToString().ToUpper(), C - FVector2D(0, R + 9.f), Font(8, "Bold"), Ay == TEXT("thr") ? Thr : Col, 0);
			DrText(Out, L + 1, G, AxisName(Ax).ToString().ToUpper(), C + FVector2D(0, R + 9.f), Font(8, "Bold"), Ax == TEXT("thr") ? Thr : Col, 0);
		}
	}

	TSharedRef<SWidget> HelpOverlay(TFunction<void()> OnClose, bool bFlight, int32 StickMode)
	{
		const FSmvUiPrefs& Pf = FSmvUiPrefs::Get();
		auto K = [&Pf](const TCHAR* Action) { return KeyLabel(Pf.KeyFor(FName(Action), false)); };
		auto KP = [&Pf](const TCHAR* Action) { return KeyLabel(Pf.KeyFor(FName(Action), true)); };
		auto Line = [](const FString& Key, const FString& Pad, const FText& What) -> TSharedRef<SWidget>
		{
			TSharedRef<SHorizontalBox> H = SNew(SHorizontalBox);
			H->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)[SNew(SBox).MinDesiredWidth(64.f).HAlign(HAlign_Left)[KeyCap(Key)]];
			H->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)[SNew(SBox).MinDesiredWidth(44.f).HAlign(HAlign_Left)[Pad.IsEmpty() || Pad == TEXT("—") ? SNullWidget::NullWidget : KeyCap(Pad)]];
			H->AddSlot().FillWidth(1.f).VAlign(VAlign_Center)[Text(What, 12, "Regular", Fg(), false)];
			return SNew(SBox).Padding(FMargin(0, 3))[H];
		};
		auto T = [](const TCHAR* Key) { return FSmvI18n::T(Key); };
		TSharedRef<SVerticalBox> C1 = SNew(SVerticalBox), C2 = SNew(SVerticalBox), C3 = SNew(SVerticalBox);
		C1->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Section(T(TEXT("help.sec_flight")))];
		C1->AddSlot().AutoHeight()[Line(TEXT("W A S D"), TEXT("pad:LS"), T(TEXT("help.left_stick")))];
		C1->AddSlot().AutoHeight()[Line(TEXT("↑ ↓ ← →"), TEXT("pad:RS"), T(TEXT("help.right_stick")))];
		C1->AddSlot().AutoHeight()[Line(K(TEXT("ArmToggle")), KP(TEXT("ArmToggle")), T(TEXT("bind.arm")))];
		C1->AddSlot().AutoHeight()[Line(K(TEXT("Takeoff")), KP(TEXT("Takeoff")), T(TEXT("bind.takeoff")))];
		C1->AddSlot().AutoHeight()[Line(K(TEXT("Land")), KP(TEXT("Land")), T(TEXT("bind.land")))];
		C1->AddSlot().AutoHeight()[Line(K(TEXT("Rtl")), KP(TEXT("Rtl")), T(TEXT("bind.rtl")))];
		C1->AddSlot().AutoHeight()[Line(K(TEXT("ModeMenu")), KP(TEXT("ModeMenu")), T(TEXT("bind.mode_menu")))];
		C1->AddSlot().AutoHeight()[Line(K(TEXT("Ch5Down")) + TEXT(" / ") + K(TEXT("Ch5Up")), TEXT("pad:D←"), T(TEXT("help.ch5")))];
		C1->AddSlot().AutoHeight()[Line(K(TEXT("ThrCenter")), FString(), T(TEXT("bind.thr_center")))];
		C1->AddSlot().AutoHeight()[Line(K(TEXT("MotorCut")), FString(), T(TEXT("bind.motor_cut")))];
		C1->AddSlot().AutoHeight()[Line(K(TEXT("Reset")), FString(), T(TEXT("bind.reset")))];

		C2->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Section(T(TEXT("help.sec_camera")), Accent2())];
		C2->AddSlot().AutoHeight()[Line(K(TEXT("CamCycle")), KP(TEXT("CamCycle")), T(TEXT("bind.cam_cycle")))];
		C2->AddSlot().AutoHeight()[Line(TEXT("1 – 7"), FString(), T(TEXT("help.cam_direct")))];
		C2->AddSlot().AutoHeight()[Line(K(TEXT("CamType")), FString(), T(TEXT("bind.cam_type")))];
		C2->AddSlot().AutoHeight()[Line(K(TEXT("Pip")), KP(TEXT("Pip")), T(TEXT("bind.pip")))];
		C2->AddSlot().AutoHeight()[Line(K(TEXT("GimbalUp")) + TEXT(" / ") + K(TEXT("GimbalDown")), FString(), T(TEXT("help.gimbal")))];
		C2->AddSlot().AutoHeight()[Line(TEXT("Mouse"), FString(), T(TEXT("help.mouse")))];
		C2->AddSlot().AutoHeight()[Line(K(TEXT("Hud")), FString(), T(TEXT("bind.hud")))];
		C2->AddSlot().AutoHeight()[Line(K(TEXT("Panel")), FString(), T(TEXT("bind.panel")))];
		C2->AddSlot().AutoHeight()[Line(K(TEXT("Sticks")), FString(), T(TEXT("bind.sticks")))];
		C2->AddSlot().AutoHeight()[Line(K(TEXT("Cockpit")), FString(), T(TEXT("bind.cockpit")))];

		C3->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Section(T(TEXT("help.sec_general")), Accent3())];
		C3->AddSlot().AutoHeight()[Line(K(TEXT("Pause")), KP(TEXT("Pause")), T(TEXT("bind.pause")))];
		C3->AddSlot().AutoHeight()[Line(K(TEXT("Help")), FString(), T(TEXT("bind.help")))];
		C3->AddSlot().AutoHeight()[Line(K(TEXT("Ack")), KP(TEXT("Ack")), T(TEXT("bind.ack")))];
		C3->AddSlot().AutoHeight()[Line(K(TEXT("Lock")), KP(TEXT("Lock")), T(TEXT("bind.lock")))];
		C3->AddSlot().AutoHeight()[Line(K(TEXT("SpeedUp")) + TEXT(" / ") + K(TEXT("SpeedDown")), FString(), T(TEXT("help.speed")))];
		C3->AddSlot().AutoHeight().Padding(0, 12, 0, 8)[Section(T(TEXT("help.sec_menu")), Warn())];
		C3->AddSlot().AutoHeight()[Line(TEXT("↑ ↓ ← →"), TEXT("pad:LS"), T(TEXT("help.menu_nav")))];
		C3->AddSlot().AutoHeight()[Line(TEXT("Enter"), TEXT("pad:A"), T(TEXT("help.menu_ok")))];
		C3->AddSlot().AutoHeight()[Line(TEXT("Esc"), TEXT("pad:B"), T(TEXT("help.menu_back")))];
		C3->AddSlot().AutoHeight()[Line(TEXT("Q / E"), TEXT("pad:LB"), T(TEXT("help.menu_tabs")))];

		// kumanda modu şeması
		TSharedRef<SSmvCanvas> Sm = SNew(SSmvCanvas).Size(FVector2D(300, 120));
		Sm->Painter = [StickMode](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
		{
			DrStickMode(Out, L, G, FVector2D::ZeroVector, FVector2D(G.GetLocalSize()), StickMode, Accent());
		};

		return SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FLinearColor(0.01f, 0.015f, 0.03f, 0.78f)).Padding(0)
			.HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(SBox).MaxDesiredWidth(1320.f)
				[
					Glass(SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 14)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)[Icon(ESmvIcon::Keyboard, 30, Accent())]
							+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[Title(T(TEXT("help.title")), 24)]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[
								SNew(SSmvButton).Text(T(TEXT("common.close"))).Icon(ESmvIcon::Check).Kind(ESmvBtn::Primary).OnClick(OnClose)
							]
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 26, 0)[C1]
							+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 26, 0)[C2]
							+ SHorizontalBox::Slot().FillWidth(1.f)[C3]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 0)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 18, 0)[Sm]
							+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight()[Text(FSmvI18n::Format(TEXT("help.stick_mode"), { { TEXT("m"), FString::FromInt(StickMode) } }), 13, "Bold", Accent())]
								+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)[StickKeyHintWidget(StickMode, 12, Fg())]
								+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)[Text(T(bFlight ? TEXT("help.flight_note") : TEXT("help.menu_note")), 11, "Regular", Mute())]
							]
						], FMargin(30.f, 24.f))
				]
			];
	}
}

using namespace SmvUi;

// ---------------------------------------------------------------------------
// SSmvCanvas
// ---------------------------------------------------------------------------

int32 SSmvCanvas::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 Layer,
	const FWidgetStyle& Style, bool bParentEnabled) const
{
	if (Painter) Painter(G, Out, Layer);
	return Layer + LayersUsed;
}

FReply SSmvCanvas::OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E)
{
	if (!OnDrag) return FReply::Unhandled();
	OnDrag(G, FVector2D(G.AbsoluteToLocal(E.GetScreenSpacePosition())), 0);
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SSmvCanvas::OnMouseMove(const FGeometry& G, const FPointerEvent& E)
{
	const FVector2D P = FVector2D(G.AbsoluteToLocal(E.GetScreenSpacePosition()));
	if (OnHover) OnHover(P);
	if (!OnDrag || !HasMouseCapture()) return FReply::Unhandled();
	OnDrag(G, P, 1);
	return FReply::Handled();
}

FReply SSmvCanvas::OnMouseButtonUp(const FGeometry& G, const FPointerEvent& E)
{
	if (!OnDrag || !HasMouseCapture()) return FReply::Unhandled();
	OnDrag(G, FVector2D(G.AbsoluteToLocal(E.GetScreenSpacePosition())), 2);
	return FReply::Handled().ReleaseMouseCapture();
}

void SSmvCanvas::OnMouseCaptureLost(const FCaptureLostEvent& E)
{
	if (OnDrag) OnDrag(FGeometry(), FVector2D::ZeroVector, 2);
}

// ---------------------------------------------------------------------------
// SSmvBackdrop
// ---------------------------------------------------------------------------

void SSmvBackdrop::Construct(const FArguments& A)
{
	Opacity = A._Opacity;
	Scene = A._Scene;
	bDrones = A._Drones;
	bGrid = A._Grid;
	Tint = A._Tint;
	Cinematic = A._Cinematic;
	SetVisibility(EVisibility::HitTestInvisible);
}

namespace
{
	float SmvHash(int32 i, int32 k)
	{
		const float X = FMath::Sin(i * 12.9898f + k * 78.233f) * 43758.5453f;
		return X - FMath::FloorToFloat(X);
	}

	void SmvDroneSil(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const FVector2D& C, float S, float Tilt, float A, double T, int32 Seed)
	{
		const FLinearColor Body = Hex(0xcfe7ff, 0.55f * A), EdgeC = Accent().CopyWithNewOpacity(0.55f * A);
		const float Sy = 0.45f;   // perspektif basıklığı
		auto P = [&C, S, Sy, Tilt](float x, float y)
		{
			const float c = FMath::Cos(Tilt), s = FMath::Sin(Tilt);
			const FVector2D V(x * S, y * S * Sy);
			return C + FVector2D(V.X * c - V.Y * s, V.X * s + V.Y * c);
		};
		DrSeg(Out, L, G, P(-1, -1), P(1, 1), Body, FMath::Max(1.2f, S * 0.08f));
		DrSeg(Out, L, G, P(1, -1), P(-1, 1), Body, FMath::Max(1.2f, S * 0.08f));
		const FVector2D Rot[4] = { FVector2D(-1, -1), FVector2D(1, -1), FVector2D(-1, 1), FVector2D(1, 1) };
		for (int32 k = 0; k < 4; ++k)
		{
			const FVector2D Rc = P((float)Rot[k].X, (float)Rot[k].Y);
			TArray<FVector2D> E;
			for (int32 j = 0; j <= 16; ++j)
			{
				const float a = j * UE_TWO_PI / 16.f;
				E.Add(Rc + FVector2D(FMath::Cos(a) * S * 0.55f, FMath::Sin(a) * S * 0.55f * Sy));
			}
			DrLine(Out, L, G, E, EdgeC, 1.f);
			const float Ph = (float)(T * 40.0 + k * 1.3 + Seed);
			const FVector2D Bl(FMath::Cos(Ph) * S * 0.5f, FMath::Sin(Ph) * S * 0.5f * Sy);
			DrSeg(Out, L, G, Rc - Bl, Rc + Bl, Body.CopyWithNewOpacity(0.35f * A), 1.f);
		}
		DrBox(Out, L, G, C - FVector2D(S * 0.22f, S * 0.12f), FVector2D(S * 0.44f, S * 0.24f), Body, Round(3.f));
		const bool bBlink = FMath::Fmod(T + Seed * 0.37, 1.2) < 0.12;
		DrBox(Out, L + 1, G, P(-1, -1) - FVector2D(2, 2), FVector2D(4, 4), Hex(0xff4040, (bBlink ? 1.f : 0.45f) * A), Circle());
		DrBox(Out, L + 1, G, P(1, -1) - FVector2D(2, 2), FVector2D(4, 4), Hex(0x40ff70, (bBlink ? 1.f : 0.45f) * A), Circle());
	}
}

int32 SSmvBackdrop::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 L,
	const FWidgetStyle& Style, bool bParentEnabled) const
{
	const FVector2D Sz = FVector2D(G.GetLocalSize());
	const float W = (float)Sz.X, H = (float)Sz.Y;
	const float Op = FMath::Clamp(Opacity.Get(1.f), 0.f, 1.f);
	const double T = Now();
	if (Cinematic.Get(false))
	{
		// sol yarı: kartların okunması için koyu degrade (sağ yarı sahneye açık); alt kenarda hafif kararma; birkaç sıcak ışık zerresi
		DrGradient(Out, L, G, FVector2D::ZeroVector, FVector2D(W * 0.66f, H), { { 0.f, Hex(0x050a16, 0.78f) }, { 0.55f, Hex(0x050a16, 0.52f) }, { 1.f, FLinearColor(0, 0, 0, 0) } }, false);
		DrGradient(Out, L, G, FVector2D(0, H * 0.78f), FVector2D(W, H * 0.22f), { { 0.f, FLinearColor(0, 0, 0, 0) }, { 1.f, FLinearColor(0, 0, 0, 0.45f) } }, true);
		for (int32 i = 0; i < 28; ++i)
		{
			const float x = W * (0.45f + 0.55f * SmvHash(i, 1));
			const float Sp = 4.f + SmvHash(i, 2) * 10.f;
			const float y = H - FMath::Fmod(SmvHash(i, 3) * H + (float)T * Sp, H);
			const float Tw = 0.3f + 0.7f * (0.5f + 0.5f * FMath::Sin((float)T * (0.7f + SmvHash(i, 4)) + i));
			const float R = 1.2f + SmvHash(i, 5) * 2.2f;
			DrBox(Out, L + 1, G, FVector2D(x, y), FVector2D(R, R), Hex(0xffd59a).CopyWithNewOpacity(0.35f * Tw), Circle());
		}
		return L + 2;
	}
	// zemin degradesi
	DrGradient(Out, L, G, FVector2D::ZeroVector, Sz, { { 0.f, Bg2().CopyWithNewOpacity(Op) }, { 0.55f, Bg().CopyWithNewOpacity(Op * 0.92f) }, { 1.f, Light() ? Hex(0xd8e2f3, Op) : Hex(0x04060c, Op) } }, true);
	// ışık lekeleri
	{
		const FVector2D A1(W * (0.25f + 0.08f * FMath::Sin(T * 0.11)), H * (0.30f + 0.06f * FMath::Cos(T * 0.13)));
		const FVector2D A2(W * (0.78f + 0.07f * FMath::Cos(T * 0.09)), H * (0.42f + 0.07f * FMath::Sin(T * 0.12)));
		DrGlow(Out, L + 1, G, A1, H * 0.55f, Accent().CopyWithNewOpacity(0.22f));
		DrGlow(Out, L + 1, G, A2, H * 0.45f, Accent2().CopyWithNewOpacity(0.16f));
		const FLinearColor Tc = Tint.Get();
		if (Tc.A > 0.f)
		{
			DrGlow(Out, L + 1, G, FVector2D(W * 0.5f, H * 0.15f), H * 0.5f, Tc.CopyWithNewOpacity(Tc.A * 0.3f));
			// bayram parçacıkları: süzülen konfeti / fener ışıkları (tema rengi + beyaz-altın)
			for (int32 k = 0; k < 46; ++k)
			{
				const float Sd = k * 12.9898f;
				const float Fx = FMath::Frac(FMath::Sin(Sd) * 43758.5453f), Fs = 0.4f + FMath::Frac(FMath::Sin(Sd * 1.7f) * 24634.63f);
				const float Y = FMath::Frac((float)(T * 0.035 * Fs) + Fx * 3.1f) * (H + 40.f) - 20.f;
				const float X = Fx * W + FMath::Sin((float)T * Fs + k) * 26.f;
				const float Ps = 3.f + 4.f * Fs;
				const FLinearColor Pc = (k % 3 == 0) ? FLinearColor(1.f, 0.85f, 0.4f, 0.55f) : Tc.CopyWithNewOpacity(0.6f);
				DrBox(Out, L + 2, G, FVector2D(X, Y), FVector2D(Ps, Ps * (k % 2 ? 1.f : 0.5f)), Pc, k % 2 ? Circle() : nullptr);
			}
		}
	}
	// parçacıklar (yukarı süzülür, göz kırpar)
	for (int32 i = 0; i < 70; ++i)
	{
		const float x = SmvHash(i, 1) * W;
		const float Sp = 6.f + SmvHash(i, 2) * 22.f;
		float y = H - FMath::Fmod(SmvHash(i, 3) * H + (float)T * Sp, H);
		const float Tw = 0.35f + 0.65f * (0.5f + 0.5f * FMath::Sin((float)T * (1.f + SmvHash(i, 4) * 2.f) + i));
		const float R = 1.f + SmvHash(i, 5) * 1.6f;
		DrBox(Out, L + 2, G, FVector2D(x, y), FVector2D(R, R), (i % 5 == 0 ? Accent2() : Hex(0xbfe9ff)).CopyWithNewOpacity(0.55f * Tw), Circle());
	}
	// perspektif ızgara (zemin)
	if (bGrid)
	{
		const float Y0 = H * 0.66f;
		const FVector2D Vp(W * 0.5f, Y0 - H * 0.05f);
		for (int32 i = -16; i <= 16; ++i)
		{
			const FVector2D Bot(W * 0.5f + i * W * 0.085f, H);
			const float k = (Y0 - (float)Vp.Y) / (H - (float)Vp.Y);
			const FVector2D Top = Vp + (Bot - Vp) * k;
			DrSeg(Out, L + 2, G, Top, Bot, Accent().CopyWithNewOpacity(0.10f), 1.f);
		}
		for (int32 k = 0; k < 14; ++k)
		{
			float u = (float)FMath::Fmod(T * 0.06 + k / 14.0, 1.0);
			u = FMath::Pow(u, 2.3f);
			const float y = Y0 + (H - Y0) * u;
			DrSeg(Out, L + 2, G, FVector2D(0, y), FVector2D(W, y), Accent().CopyWithNewOpacity(0.03f + 0.16f * u), 1.f);
		}
		// ufuk parıltısı
		DrGradient(Out, L + 2, G, FVector2D(0, Y0 - 40.f), FVector2D(W, 80.f), { { 0.f, FLinearColor(0, 0, 0, 0) }, { 0.5f, Accent().CopyWithNewOpacity(0.10f) }, { 1.f, FLinearColor(0, 0, 0, 0) } }, true);
	}
	// bölüme özel sahne
	const int32 Sc = Scene.Get(0);
	if (Sc > 0) PaintScene(Sc, Out, L + 2, G, T);
	// süzülen dron siluetleri
	if (bDrones && Sc != 1 && Sc != 3)
	{
		for (int32 d = 0; d < 3; ++d)
		{
			const float Depth = 0.45f + d * 0.3f;
			const double Per = 38.0 + d * 11.0;
			const double U = FMath::Fmod(T / Per + d * 0.37, 1.0);
			const float x = (float)(-0.15 + 1.3 * U) * W;
			const float y = H * (0.22f + 0.16f * d) + FMath::Sin((float)T * 0.6f + d) * H * 0.025f;
			const float Tilt = 0.12f * FMath::Sin((float)T * 0.4f + d);
			// içerik panellerinin arkasında dikkat dağıtmasın (yarı saydam satırlardan görünüyordu): sönük
			const float A = FMath::Clamp((float)FMath::Min(U, 1.0 - U) * 8.f, 0.f, 1.f) * (0.18f + 0.14f * Depth);
			SmvDroneSil(Out, L + 3, G, FVector2D(x, y), 14.f + 18.f * Depth, Tilt, A, T, d * 7);
			// iz
			DrGradient(Out, L + 3, G, FVector2D(x - 160.f * Depth, y - 1.f), FVector2D(140.f * Depth, 2.f),
				{ { 0.f, FLinearColor(0, 0, 0, 0) }, { 1.f, Accent().CopyWithNewOpacity(0.18f * A) } }, false);
		}
	}
	// tarama çizgisi
	{
		const float y = (float)FMath::Fmod(T * 40.0, (double)H + 200.0) - 100.f;
		DrGradient(Out, L + 4, G, FVector2D(0, y), FVector2D(W, 60.f), { { 0.f, FLinearColor(0, 0, 0, 0) }, { 0.5f, Accent().CopyWithNewOpacity(0.035f) }, { 1.f, FLinearColor(0, 0, 0, 0) } }, true);
	}
	// kenar kararması
	const float Vg = Light() ? 0.12f : 0.55f;
	DrGradient(Out, L + 5, G, FVector2D::ZeroVector, FVector2D(W * 0.22f, H), { { 0.f, FLinearColor(0, 0, 0, Vg) }, { 1.f, FLinearColor(0, 0, 0, 0) } }, false);
	DrGradient(Out, L + 5, G, FVector2D(W * 0.78f, 0), FVector2D(W * 0.22f, H), { { 0.f, FLinearColor(0, 0, 0, 0) }, { 1.f, FLinearColor(0, 0, 0, Vg) } }, false);
	DrGradient(Out, L + 5, G, FVector2D(0, H * 0.82f), FVector2D(W, H * 0.18f), { { 0.f, FLinearColor(0, 0, 0, 0) }, { 1.f, FLinearColor(0, 0, 0, Vg) } }, true);
	return L + 6;
}

void SSmvBackdrop::PaintScene(int32 S, FSlateWindowElementList& Out, int32 L, const FGeometry& G, double T) const
{
	const FVector2D Sz = FVector2D(G.GetLocalSize());
	const float W = (float)Sz.X, H = (float)Sz.Y;
	const FLinearColor Ac = Accent(), A2 = Accent2();
	if (S == 1)
	{
		// hangar içi: kapı açıklığından gece göğü, projektör huzmeleri, pist ışıkları, taksi yapan uçak siluetleri
		const FVector2D D0(W * 0.18f, H * 0.18f), D1(W * 0.82f, H * 0.62f);
		DrBox(Out, L, G, D0, D1 - D0, Hex(0x020611, 0.55f));
		for (int32 i = 0; i < 40; ++i) DrBox(Out, L, G, FVector2D(D0.X + SmvHash(i, 9) * (D1.X - D0.X), D0.Y + SmvHash(i, 8) * (D1.Y - D0.Y) * 0.6f), FVector2D(1.5f, 1.5f), FLinearColor(1, 1, 1, 0.5f + 0.5f * FMath::Sin((float)T * 2.f + i)));
		// kapı çerçevesi: arayüz arkasında sönük, ince (keskin mavi çizgiler karışıklık yaratıyordu)
		DrLine(Out, L + 1, G, { FVector2D(D0.X, D1.Y), D0, FVector2D(D1.X, D0.Y), FVector2D(D1.X, D1.Y) }, Edge().CopyWithNewOpacity(0.18f), 2.f);
		for (int32 k = 0; k < 2; ++k)
		{
			const float A = FMath::DegreesToRadians(-90.f + 28.f * FMath::Sin((float)T * 0.35f + k * 2.1f));
			const FVector2D O(W * (0.35f + 0.3f * k), D1.Y);
			for (int32 j = 0; j < 7; ++j) DrSeg(Out, L + 1, G, O, O + FVector2D(FMath::Cos(A + (j - 3) * 0.02f), FMath::Sin(A + (j - 3) * 0.02f)) * H * 0.55f, Ac.CopyWithNewOpacity(0.035f), 18.f);
		}
		for (int32 i = 0; i < 12; ++i) DrBox(Out, L + 1, G, FVector2D(W * 0.2f + i * W * 0.05f, D1.Y + 18.f), FVector2D(5, 5), Hex(0xfbbf24, 0.4f + 0.4f * FMath::Sin((float)T * 3.f + i * 0.6f)), Circle());
		const float Px = W * (float)FMath::Fmod(T * 0.04, 1.2) - W * 0.1f;
		DrIcon(Out, L + 2, G, ESmvIcon::Plane, FVector2D(Px, D1.Y - 18.f), 46.f, Hex(0xcfe7ff, 0.35f), 2.f);
	}
	else if (S == 2)
	{
		// brifing: büyük radar, rota çizgileri, lazer imleç
		const FVector2D C(W * 0.72f, H * 0.42f);
		const float R = H * 0.32f;
		for (int32 k = 1; k <= 4; ++k) DrArc(Out, L, G, C, R * k / 4.f, 0.f, 360.f, Accent3().CopyWithNewOpacity(0.12f), 1.f);
		const float Sw = (float)FMath::Fmod(T * 60.0, 360.0);
		for (int32 j = 0; j < 18; ++j) { const float A = FMath::DegreesToRadians(Sw - j * 2.f); DrSeg(Out, L + 1, G, C, C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R, Accent3().CopyWithNewOpacity(0.18f * (1.f - j / 18.f)), 2.f); }
		TArray<FVector2D> Rt;
		for (int32 i = 0; i < 6; ++i) Rt.Add(C + FVector2D(FMath::Cos(i * 1.7f + 0.4f), FMath::Sin(i * 1.3f)) * R * (0.3f + 0.1f * i));
		DrLine(Out, L + 1, G, Rt, Hex(0xf472b6, 0.35f), 2.f);
		const FVector2D Lz = Rt[(int32)FMath::Fmod(T * 0.5, 6.0)];
		DrGlow(Out, L + 2, G, Lz, 14.f, Hex(0xff3b3b, 0.7f));
	}
	else if (S == 3)
	{
		// tel kafes küre + yörüngede uçaklar
		const FVector2D C(W * 0.7f, H * 0.45f);
		const float R = H * 0.3f;
		const float Rot = (float)T * 0.25f;
		DrArc(Out, L, G, C, R, 0.f, 360.f, Accent3().CopyWithNewOpacity(0.35f), 1.5f);
		for (int32 k = 1; k < 6; ++k)
		{
			const float Lat = -75.f + k * 25.f, Ry = R * FMath::Cos(FMath::DegreesToRadians(Lat)), Yc = R * FMath::Sin(FMath::DegreesToRadians(Lat));
			TArray<FVector2D> P;
			for (int32 j = 0; j <= 36; ++j) { const float A = j * UE_TWO_PI / 36.f; P.Add(C + FVector2D(FMath::Cos(A) * Ry, Yc + FMath::Sin(A) * Ry * 0.18f)); }
			DrLine(Out, L, G, P, Accent3().CopyWithNewOpacity(0.15f), 1.f);
		}
		for (int32 k = 0; k < 8; ++k)
		{
			const float A = Rot + k * UE_PI / 8.f, Sx = FMath::Cos(A);
			TArray<FVector2D> P;
			for (int32 j = 0; j <= 36; ++j) { const float B = j * UE_TWO_PI / 36.f; P.Add(C + FVector2D(FMath::Sin(B) * R * Sx, -FMath::Cos(B) * R)); }
			DrLine(Out, L, G, P, Accent3().CopyWithNewOpacity(0.12f), 1.f);
		}
		for (int32 k = 0; k < 6; ++k)
		{
			const float A = (float)T * (0.4f + 0.07f * k) + k;
			const FVector2D Pp = C + FVector2D(FMath::Cos(A) * R * 1.25f, FMath::Sin(A) * R * (0.35f + 0.1f * k));
			DrGlow(Out, L + 1, G, Pp, 8.f, Ac.CopyWithNewOpacity(0.6f));
		}
	}
	else if (S == 4)
	{
		// alacakaranlıkta ilaçlama tarlası: perspektif sıralar + püskürtme parçacıkları
		const float Y0 = H * 0.55f;
		DrGradient(Out, L, G, FVector2D(0, Y0), FVector2D(W, H - Y0), { { 0.f, Hex(0x1d3a1a, 0.0f) }, { 1.f, Hex(0x2f5d22, 0.45f) } }, true);
		for (int32 i = -14; i <= 14; ++i) DrSeg(Out, L + 1, G, FVector2D(W * 0.5f + i * 8.f, Y0), FVector2D(W * 0.5f + i * W * 0.06f, H), Hex(0x6aa84f, 0.18f), 2.f);
		const float Dx = W * (float)FMath::Fmod(T * 0.05, 1.0);
		DrIcon(Out, L + 2, G, ESmvIcon::Drone, FVector2D(Dx, Y0 - 30.f), 40.f, Hex(0xcfe7ff, 0.5f), 2.f);
		for (int32 i = 0; i < 40; ++i)
		{
			const float a = SmvHash(i, 3), b = (float)FMath::Fmod(T * 0.9 + SmvHash(i, 4), 1.0);
			DrBox(Out, L + 2, G, FVector2D(Dx - 30.f + a * 60.f - b * 20.f, Y0 - 20.f + b * 60.f), FVector2D(2, 2), Hex(0xa7f3d0, 0.5f * (1.f - b)), Circle());
		}
	}
	else if (S == 5)
	{
		// gökyüzü paralaksı: bulut katmanları + uçak siluetleri
		for (int32 k = 0; k < 3; ++k)
			for (int32 i = 0; i < 6; ++i)
			{
				const float Sp = 8.f + k * 14.f;
				const float X = (float)FMath::Fmod(SmvHash(i, k) * W + T * Sp, (double)W + 400.0) - 200.f;
				DrGlow(Out, L, G, FVector2D(X, H * (0.25f + 0.18f * k) + SmvHash(i, k + 5) * 40.f), 80.f + k * 30.f, FLinearColor(1, 1, 1, 0.05f + 0.02f * k));
			}
		for (int32 i = 0; i < 3; ++i)
		{
			const float X = (float)FMath::Fmod(T * (40.0 + i * 25.0) + i * 500.0, (double)W + 200.0) - 100.f;
			DrIcon(Out, L + 1, G, ESmvIcon::Plane, FVector2D(X, H * (0.2f + 0.15f * i)), 28.f + i * 8.f, Hex(0xcfe7ff, 0.12f), 2.f);
		}
		(void)A2;
	}
}

// ---------------------------------------------------------------------------
// SSmvButton
// ---------------------------------------------------------------------------

void SSmvButton::Construct(const FArguments& A)
{
	Kind = A._Kind;
	IconId = A._Icon;
	Selected = A._Selected;
	AccentCol = A._Accent;
	OnClick = A._OnClick;
	BornAt = FPlatformTime::Seconds() + FMath::Max(0.f, A._IntroDelay);
	bSlideIn = A._IntroDelay > 0.f;
	const bool bTile = Kind == ESmvBtn::Tile || Kind == ESmvBtn::Hero;
	const int32 Fs = A._FontSize > 0 ? A._FontSize : (Kind == ESmvBtn::Hero ? 30 : bTile ? 17 : 13);
	SSmvButton* Self = this;
	const FLinearColor TextCol = Kind == ESmvBtn::Primary || Kind == ESmvBtn::Danger ? FLinearColor::White : Fg();
	TSharedRef<SWidget> IconW = SNullWidget::NullWidget;
	if (IconId != ESmvIcon::None)
	{
		const float Is = Kind == ESmvBtn::Hero ? 56.f : bTile ? 40.f : 18.f;
		TSharedRef<SSmvCanvas> Ic = SNew(SSmvCanvas).Size(FVector2D(Is, Is)).Visibility(EVisibility::HitTestInvisible);
		const ESmvIcon Id = IconId;
		const FLinearColor Ac = AccentCol;
		const bool bPrim = Kind == ESmvBtn::Primary || Kind == ESmvBtn::Danger;
		Ic->Painter = [Self, Id, Is, Ac, bPrim](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
		{
			const FLinearColor C = bPrim ? FLinearColor::White : FMath::Lerp(Ac, FLinearColor::White, Self->HoverA * 0.35f);
			const float Bob = Self->Kind == ESmvBtn::Hero ? 2.f * FMath::Sin((float)Now() * 2.f) : 0.f;
			DrIcon(Out, L, G, Id, FVector2D(G.GetLocalSize()) * 0.5 + FVector2D(0, Bob), Is * 0.86f, C, FMath::Max(1.6f, Is / 15.f));
		};
		IconW = Ic;
	}
	if (A._Extra.Widget != SNullWidget::NullWidget) IconW = A._Extra.Widget;

	TSharedRef<SWidget> Inner = SNullWidget::NullWidget;
	if (bTile)
	{
		TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
		V->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0, 0, 0, Kind == ESmvBtn::Hero ? 14.f : 10.f)[IconW];
		V->AddSlot().AutoHeight()
		[
			SNew(STextBlock).Text(A._Text).Font(Font(Fs, Kind == ESmvBtn::Hero ? "Black" : "Bold", Kind == ESmvBtn::Hero ? 220 : 60)).ColorAndOpacity(TextCol)
			.ShadowOffset(FVector2D(0, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.5f))
		];
		V->AddSlot().AutoHeight().Padding(0, 4, 0, 0)
		[
			SNew(STextBlock).Text(A._SubText).Font(Font(Kind == ESmvBtn::Hero ? 13 : 11)).ColorAndOpacity(Dim()).AutoWrapText(true)
			.Visibility_Lambda([Sub = A._SubText]() { return Sub.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
		];
		Inner = V;
	}
	else
	{
		TSharedRef<SHorizontalBox> Hb = SNew(SHorizontalBox);
		if (IconId != ESmvIcon::None || A._Extra.Widget != SNullWidget::NullWidget)
		{
			const bool bTextEmpty = A._Text.Get().IsEmpty() && !A._Text.IsBound();
			if (A._LeftAlign) Hb->AddSlot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0, 0, bTextEmpty ? 0.f : 10.f, 0)[IconW];
			else Hb->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, bTextEmpty ? 0.f : 10.f, 0)[IconW];
		}
		Hb->AddSlot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text(A._Text).Font(Font(Fs, "Bold", 40)).ColorAndOpacity(TextCol)
			.Visibility_Lambda([Tx = A._Text]() { return Tx.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
		];
		Inner = Hb;
	}
	const FMargin Pad = Kind == ESmvBtn::Hero ? FMargin(30.f, 26.f) : bTile ? FMargin(20.f, 18.f) : FMargin(18.f, 10.f);
	ChildSlot
	[
		SNew(SBox).MinDesiredWidth(A._MinWidth).MinDesiredHeight(A._MinHeight > 0 ? A._MinHeight : (bTile ? 0.f : 42.f)).Padding(Pad)
		.VAlign(bTile ? VAlign_Bottom : VAlign_Center).HAlign(bTile || A._LeftAlign ? HAlign_Fill : HAlign_Center)
		[
			Inner
		]
	];
	SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
}

void SSmvButton::Tick(const FGeometry& G, const double InNow, const float Dt)
{
	SCompoundWidget::Tick(G, InNow, Dt);
	const float K = FMath::Clamp(Dt * 12.f, 0.f, 1.f);
	HoverA = FMath::Lerp(HoverA, (bHover || (bNavFocus && (bCauseNav || PadActive()))) && IsEnabled() ? 1.f : 0.f, K);
	PressA = FMath::Lerp(PressA, bPressed ? 1.f : 0.f, FMath::Clamp(Dt * 20.f, 0.f, 1.f));
	FocusA = FMath::Lerp(FocusA, bNavFocus && (bCauseNav || PadActive()) ? 1.f : 0.f, K);
	const float Gain = Kind == ESmvBtn::Hero ? 0.035f : Kind == ESmvBtn::Tile ? 0.04f : 0.02f;
	const float Intro = ReducedMotion() ? 1.f : FMath::Clamp((float)(FPlatformTime::Seconds() - BornAt) * (bSlideIn ? 2.6f : 4.f), 0.f, 1.f);
	const float S = (1.f + Gain * HoverA - 0.03f * PressA) * FMath::Lerp(0.96f, 1.f, Intro);
	// sıralı giriş: aşağıdan kayma (ease-out)
	const float Slide = bSlideIn ? FMath::Pow(1.f - Intro, 3.f) * 26.f : 0.f;
	// kart 3B eğilme (web en çok ±9°): fareye göre kayma (shear) ile perspektif hissi
	// kapalı: fare konumu eğilmiş geometride ölçülüyordu → eğim fareyi, fare eğimi değiştirip kartlar titriyordu (kullanıcı, 2026-10-28)
	const bool bTiltKind = false;
	TiltN = FMath::Lerp(TiltN, bTiltKind && bHover ? MouseN : FVector2D::ZeroVector, (double)K);
	const float Tx = (float)TiltN.X * 0.035f, Ty = (float)TiltN.Y * 0.035f;
	if (FMath::Abs(S - 1.f) > 0.0005f || FMath::Abs(Tx) + FMath::Abs(Ty) > 0.0005f || Slide > 0.01f)
		SetRenderTransform(FSlateRenderTransform(FMatrix2x2f(S, -Ty * S, Tx * S, S), FVector2f(0.f, Slide)));
	else SetRenderTransform(TOptional<FSlateRenderTransform>());
	SetRenderOpacity(FMath::Lerp(0.f, 1.f, Intro));
}

int32 SSmvButton::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 L,
	const FWidgetStyle& Style, bool bParentEnabled) const
{
	const FVector2D Sz = FVector2D(G.GetLocalSize());
	const bool bEn = IsEnabled() && bParentEnabled;
	const bool bSel = Selected.Get(false);
	const bool bTile = Kind == ESmvBtn::Tile || Kind == ESmvBtn::Hero;
	const float R = bTile ? 18.f : 12.f;
	const double T = Now();
	FLinearColor Fill = FMath::Lerp(Card(), CardHover(), HoverA);
	FLinearColor Ring = FMath::Lerp(Edge(), AccentCol.CopyWithNewOpacity(0.75f), FMath::Max(HoverA, bSel ? 1.f : 0.f));
	switch (Kind)
	{
	case ESmvBtn::Primary: Fill = FMath::Lerp(AccentCol * 0.62f, AccentCol * 0.85f, HoverA).CopyWithNewOpacity(0.95f); Ring = AccentCol.CopyWithNewOpacity(0.9f); break;
	case ESmvBtn::Danger: Fill = FMath::Lerp(Danger() * 0.5f, Danger() * 0.72f, HoverA).CopyWithNewOpacity(0.95f); Ring = Danger(); break;
	case ESmvBtn::Ghost: Fill = FLinearColor(1, 1, 1, 0.03f + 0.06f * HoverA); break;
	case ESmvBtn::Hero: Fill = FMath::Lerp(Hex(0x0f2440, 0.9f), Hex(0x15345c, 0.95f), HoverA); Ring = AccentCol.CopyWithNewOpacity(0.55f + 0.4f * HoverA); break;
	default: break;
	}
	if (bSel && Kind != ESmvBtn::Primary) Fill = FMath::Lerp(Fill, AccentCol.CopyWithNewOpacity(0.28f), 0.6f);
	// parıltı
	const float GlowA = (Kind == ESmvBtn::Hero ? 0.35f + 0.15f * FMath::Sin((float)T * 2.2f) : 0.f) + HoverA * 0.45f + (bSel ? 0.25f : 0.f);
	if (GlowA > 0.01f && bEn)
	{
		for (int32 i = 3; i >= 1; --i)
		{
			const float E = i * 5.f;
			DrBox(Out, L, G, FVector2D(-E, -E), Sz + FVector2D(2 * E, 2 * E), AccentCol.CopyWithNewOpacity(GlowA * 0.07f), Round(R + E));
		}
	}
	DrBox(Out, L + 1, G, FVector2D::ZeroVector, Sz, Fill, Round(R));
	// üst ışık şeridi (cam)
	DrGradient(Out, L + 2, G, FVector2D(R * 0.6f, 1.f), FVector2D(Sz.X - R * 1.2f, FMath::Min(Sz.Y * 0.45f, 40.0)),
		{ { 0.f, FLinearColor(1, 1, 1, 0.07f + 0.05f * HoverA) }, { 1.f, FLinearColor(1, 1, 1, 0.f) } }, true);
	// parlak süpürme (Hero / Primary)
	if ((Kind == ESmvBtn::Hero || Kind == ESmvBtn::Primary || Kind == ESmvBtn::Tile) && bEn && !ReducedMotion())
	{
		// karolarda ekran konumuna göre faz kayması → süpürme menüde soldan sağa dalga gibi geçer
		const float Per = Kind == ESmvBtn::Hero ? 3.6f : Kind == ESmvBtn::Tile ? 7.f : 5.f;
		const double Ph = Kind == ESmvBtn::Tile ? G.GetAbsolutePosition().X / 2400.0 : 0.0;
		const float U = (float)FMath::Fmod(T / Per - Ph + 10.0, 1.0);
		const float X = -60.f + U * ((float)Sz.X + 120.f);
		Out.PushClip(FSlateClippingZone(G));
		DrSeg(Out, L + 2, G, FVector2D(X, -4.0), FVector2D(X - Sz.Y * 0.5f, Sz.Y + 4.0), FLinearColor(1, 1, 1, 0.10f), 18.f);
		Out.PopClip();
	}
	// fare parlaması (kart)
	if (bTile && HoverA > 0.02f && !ReducedMotion())
	{
		const FVector2D Gp((MouseN.X * 0.5 + 0.5) * Sz.X, (MouseN.Y * 0.5 + 0.5) * Sz.Y);
		Out.PushClip(FSlateClippingZone(G));
		DrGlow(Out, L + 2, G, Gp, (float)Sz.Y * 0.7f, FLinearColor(1, 1, 1, 0.10f * HoverA));
		Out.PopClip();
	}
	DrRoundRect(Out, L + 3, G, FVector2D(0.5f, 0.5f), Sz - FVector2D(1, 1), R, Ring, Kind == ESmvBtn::Hero ? 1.6f : 1.f);
	if (bSel && !bTile) DrBox(Out, L + 3, G, FVector2D(0, Sz.Y * 0.2f), FVector2D(3.f, Sz.Y * 0.6f), AccentCol, Round(1.5f));
	if (bTile && Kind == ESmvBtn::Tile) DrBox(Out, L + 3, G, FVector2D(R, Sz.Y - 3.f), FVector2D((Sz.X - 2 * R) * (0.25f + 0.75f * HoverA), 2.f), AccentCol.CopyWithNewOpacity(0.5f + 0.5f * HoverA), Round(1.f));
	// odak halkası (gezinme)
	if (FocusA > 0.02f)
	{
		const float Pulse = 0.75f + 0.25f * FMath::Sin((float)T * 6.f);
		DrRoundRect(Out, L + 3, G, FVector2D(-4.f, -4.f), Sz + FVector2D(8.f, 8.f), R + 4.f, Hex(0x7dd3fc, FocusA * Pulse), 2.5f);
	}
	int32 Max = SCompoundWidget::OnPaint(Args, G, Cull, Out, L + 4, Style, bEn);
	if (!bEn) DrBox(Out, Max + 1, G, FVector2D::ZeroVector, Sz, Bg().CopyWithNewOpacity(0.55f), Round(R));
	return FMath::Max(Max, L + 5) + 1;
}

FReply SSmvButton::OnFocusReceived(const FGeometry& G, const FFocusEvent& E)
{
	bNavFocus = E.GetCause() != EFocusCause::Mouse;
	bCauseNav = E.GetCause() == EFocusCause::Navigation;
	if (bNavFocus) FSmvUiSound::Play(ESmvSnd::Hover);
	return FReply::Handled();
}

void SSmvButton::OnFocusLost(const FFocusEvent& E) { bNavFocus = false; bPressed = false; }

void SSmvButton::OnMouseEnter(const FGeometry& G, const FPointerEvent& E)
{
	SCompoundWidget::OnMouseEnter(G, E);
	bHover = true;
	PadActive() = false;
	if (IsEnabled()) FSmvUiSound::Play(ESmvSnd::Hover);
}

FReply SSmvButton::OnMouseMove(const FGeometry& G, const FPointerEvent& E)
{
	const FVector2D Lp = FVector2D(G.AbsoluteToLocal(E.GetScreenSpacePosition()));
	const FVector2D Sz = FVector2D(G.GetLocalSize());
	MouseN = FVector2D(FMath::Clamp(Lp.X / FMath::Max(1.0, Sz.X) * 2.0 - 1.0, -1.0, 1.0), FMath::Clamp(Lp.Y / FMath::Max(1.0, Sz.Y) * 2.0 - 1.0, -1.0, 1.0));
	return FReply::Unhandled();
}

void SSmvButton::OnMouseLeave(const FPointerEvent& E)
{
	SCompoundWidget::OnMouseLeave(E);
	bHover = false;
	bPressed = false;
}

FReply SSmvButton::OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E)
{
	if (E.GetEffectingButton() != EKeys::LeftMouseButton || !IsEnabled()) return FReply::Unhandled();
	bPressed = true;
	return FReply::Handled();
}

FReply SSmvButton::OnMouseButtonUp(const FGeometry& G, const FPointerEvent& E)
{
	if (E.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	const bool bWas = bPressed;
	bPressed = false;
	if (bWas && IsHovered()) Click();
	return FReply::Handled();
}

FReply SSmvButton::OnKeyDown(const FGeometry& G, const FKeyEvent& E)
{
	if (E.GetKey().IsGamepadKey()) PadActive() = true;
	if (FSlateApplication::Get().GetNavigationActionFromKey(E) == EUINavigationAction::Accept && IsEnabled())
	{
		if (!E.IsRepeat()) { bPressed = true; Click(); }
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SSmvButton::OnKeyUp(const FGeometry& G, const FKeyEvent& E)
{
	bPressed = false;
	return FReply::Unhandled();
}

void SSmvButton::Click()
{
	if (!IsEnabled()) return;
	FSmvUiSound::Play(ESmvSnd::Click);
	PressA = 1.f;
	bPressed = false;
	if (OnClick) OnClick();
}

// ---------------------------------------------------------------------------
// SSmvToggle
// ---------------------------------------------------------------------------

void SSmvToggle::Tick(const FGeometry& G, const double InNow, const float Dt)
{
	const float Target = Value.Get(false) ? 1.f : 0.f;
	if (Knob < 0.f) Knob = Target;
	Knob = FMath::Lerp(Knob, Target, FMath::Clamp(Dt * 14.f, 0.f, 1.f));
}

int32 SSmvToggle::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 L,
	const FWidgetStyle& Style, bool bParentEnabled) const
{
	const FVector2D Sz = FVector2D(G.GetLocalSize());
	const float K = FMath::Max(0.f, Knob);
	const FVector2D P(0, (Sz.Y - 26.0) * 0.5), S(52, 26);
	DrBox(Out, L, G, P, S, FMath::Lerp(FLinearColor(1, 1, 1, 0.12f), Accent().CopyWithNewOpacity(0.85f), K), Circle());
	if (K > 0.5f) DrBox(Out, L, G, P - FVector2D(3, 3), S + FVector2D(6, 6), Accent().CopyWithNewOpacity(0.12f * K), Circle());
	const FVector2D Kc = P + FVector2D(13.f + 26.f * K, 13.f);
	DrBox(Out, L + 1, G, Kc - FVector2D(10, 10), FVector2D(20, 20), FLinearColor::White, Circle());
	if (bNavFocus) DrRoundRect(Out, L + 2, G, P - FVector2D(4, 4), S + FVector2D(8, 8), 17.f, Hex(0x7dd3fc), 2.f);
	return L + 3;
}

FReply SSmvToggle::OnFocusReceived(const FGeometry& G, const FFocusEvent& E)
{
	bNavFocus = E.GetCause() != EFocusCause::Mouse;
	if (bNavFocus) FSmvUiSound::Play(ESmvSnd::Hover);
	return FReply::Handled();
}

void SSmvToggle::Flip()
{
	FSmvUiSound::Play(ESmvSnd::Toggle);
	if (OnChanged) OnChanged(!Value.Get(false));
}

FReply SSmvToggle::OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E)
{
	if (E.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	Flip();
	return FReply::Handled();
}

FReply SSmvToggle::OnKeyDown(const FGeometry& G, const FKeyEvent& E)
{
	if (FSlateApplication::Get().GetNavigationActionFromKey(E) == EUINavigationAction::Accept && !E.IsRepeat()) { Flip(); return FReply::Handled(); }
	return FReply::Unhandled();
}

// ---------------------------------------------------------------------------
// SSmvSlider
// ---------------------------------------------------------------------------

void SSmvSlider::Construct(const FArguments& A)
{
	Value = A._Value; Min = A._Min; Max = FMath::Max(A._Max, A._Min + 1e-6); Step = A._Step > 0 ? A._Step : (Max - Min) / 20.0;
	Width = A._Width; Format = A._Format; OnChanged = A._OnChanged;
}

int32 SSmvSlider::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 L,
	const FWidgetStyle& Style, bool bParentEnabled) const
{
	const FVector2D Sz = FVector2D(G.GetLocalSize());
	const double V = Value.Get(Min);
	const float F = (float)FMath::Clamp((V - Min) / (Max - Min), 0.0, 1.0);
	const float Tw = (float)Sz.X - 70.f, Cy = (float)Sz.Y * 0.5f;
	DrBox(Out, L, G, FVector2D(0, Cy - 3.f), FVector2D(Tw, 6.f), FLinearColor(1, 1, 1, 0.12f), Round(3.f));
	DrBox(Out, L + 1, G, FVector2D(0, Cy - 3.f), FVector2D(Tw * F, 6.f), Accent(), Round(3.f));
	const FVector2D Kc(Tw * F, Cy);
	if (bNavFocus || HasMouseCapture()) DrBox(Out, L + 1, G, Kc - FVector2D(15, 15), FVector2D(30, 30), Accent().CopyWithNewOpacity(0.22f), Circle());
	DrBox(Out, L + 2, G, Kc - FVector2D(9, 9), FVector2D(18, 18), FLinearColor::White, Circle());
	const FString S = Format ? Format(V) : FString::Printf(TEXT("%.2f"), V);
	DrText(Out, L + 2, G, S, FVector2D(Sz.X, Cy), Font(12, "Bold"), bNavFocus ? Hex(0x7dd3fc) : Fg(), 1);
	if (bNavFocus) DrRoundRect(Out, L + 3, G, FVector2D(-6, Cy - 15.f), FVector2D(Tw + 12.f, 30.f), 12.f, Hex(0x7dd3fc, 0.8f), 1.5f);
	return L + 4;
}

void SSmvSlider::SetFromX(const FGeometry& G, float X)
{
	const float Tw = (float)G.GetLocalSize().X - 70.f;
	double V = Min + FMath::Clamp(X / FMath::Max(Tw, 1.f), 0.f, 1.f) * (Max - Min);
	V = FMath::GridSnap(V - Min, Step) + Min;
	V = FMath::Clamp(V, Min, Max);
	if (OnChanged) OnChanged(V);
}

void SSmvSlider::Nudge(double Steps)
{
	const double V = FMath::Clamp(Value.Get(Min) + Steps * Step, Min, Max);
	FSmvUiSound::Play(ESmvSnd::Hover);
	if (OnChanged) OnChanged(FMath::GridSnap(V - Min, Step) + Min);
}

FReply SSmvSlider::OnFocusReceived(const FGeometry& G, const FFocusEvent& E)
{
	bNavFocus = E.GetCause() != EFocusCause::Mouse;
	if (bNavFocus) FSmvUiSound::Play(ESmvSnd::Hover);
	return FReply::Handled();
}

FReply SSmvSlider::OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E)
{
	if (E.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	SetFromX(G, (float)G.AbsoluteToLocal(E.GetScreenSpacePosition()).X);
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SSmvSlider::OnMouseMove(const FGeometry& G, const FPointerEvent& E)
{
	if (!HasMouseCapture()) return FReply::Unhandled();
	SetFromX(G, (float)G.AbsoluteToLocal(E.GetScreenSpacePosition()).X);
	return FReply::Handled();
}

FReply SSmvSlider::OnMouseButtonUp(const FGeometry& G, const FPointerEvent& E)
{
	if (!HasMouseCapture()) return FReply::Unhandled();
	FSmvUiSound::Play(ESmvSnd::Toggle);
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SSmvSlider::OnMouseWheel(const FGeometry& G, const FPointerEvent& E)
{
	Nudge(E.GetWheelDelta() > 0 ? 1.0 : -1.0);
	return FReply::Handled();
}

FNavigationReply SSmvSlider::OnNavigation(const FGeometry& G, const FNavigationEvent& E)
{
	const EUINavigation N = E.GetNavigationType();
	if (N == EUINavigation::Left || N == EUINavigation::Right)
	{
		const double Mul = FSlateApplication::Get().GetModifierKeys().IsShiftDown() ? 10.0 : 1.0;
		Nudge((N == EUINavigation::Right ? 1.0 : -1.0) * Mul);
		return FNavigationReply::Stop();
	}
	return SLeafWidget::OnNavigation(G, E);
}

FReply SSmvSlider::OnKeyDown(const FGeometry& G, const FKeyEvent& E)
{
	if (E.GetKey() == EKeys::Home) { if (OnChanged) OnChanged(Min); return FReply::Handled(); }
	if (E.GetKey() == EKeys::End) { if (OnChanged) OnChanged(Max); return FReply::Handled(); }
	return FReply::Unhandled();
}

// ---------------------------------------------------------------------------
// SSmvCycler
// ---------------------------------------------------------------------------

int32 SSmvCycler::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 L,
	const FWidgetStyle& Style, bool bParentEnabled) const
{
	const FVector2D Sz = FVector2D(G.GetLocalSize());
	const int32 I = Index.Get(0);
	DrBox(Out, L, G, FVector2D::ZeroVector, Sz, FLinearColor(1, 1, 1, bNavFocus ? 0.09f : 0.05f), Round(10.f));
	DrRoundRect(Out, L + 1, G, FVector2D(0.5f, 0.5f), Sz - FVector2D(1, 1), 10.f, bNavFocus ? Hex(0x7dd3fc) : Edge(), bNavFocus ? 2.f : 1.f);
	const float Cy = (float)Sz.Y * 0.5f;
	const FLinearColor Ac = bNavFocus ? Hex(0x7dd3fc) : Dim();
	DrIcon(Out, L + 1, G, ESmvIcon::Prev, FVector2D(16.f, Cy), 14.f, Ac, 2.f);
	DrIcon(Out, L + 1, G, ESmvIcon::Next, FVector2D(Sz.X - 16.f, Cy), 14.f, Ac, 2.f);
	const FString S = Labels.IsValidIndex(I) ? Labels[I].ToString() : FString(TEXT("—"));
	DrText(Out, L + 2, G, S, FVector2D(Sz.X * 0.5f, Cy), Font(12, "Bold"), Fg(), 0);
	// konum noktaları
	const int32 N = FMath::Min(Labels.Num(), 12);
	if (N > 1)
	{
		const float Dx = 7.f, X0 = (float)Sz.X * 0.5f - (N - 1) * Dx * 0.5f;
		for (int32 k = 0; k < N; ++k) DrBox(Out, L + 2, G, FVector2D(X0 + k * Dx - 1.5f, Sz.Y - 6.f), FVector2D(3, 3), k == I ? Accent() : FLinearColor(1, 1, 1, 0.2f), Circle());
	}
	return L + 3;
}

void SSmvCycler::Step(int32 D)
{
	if (Labels.Num() == 0) return;
	const int32 I = (Index.Get(0) + D + Labels.Num()) % Labels.Num();
	FSmvUiSound::Play(ESmvSnd::Toggle);
	if (OnChanged) OnChanged(I);
}

FReply SSmvCycler::OnFocusReceived(const FGeometry& G, const FFocusEvent& E)
{
	bNavFocus = E.GetCause() != EFocusCause::Mouse;
	if (bNavFocus) FSmvUiSound::Play(ESmvSnd::Hover);
	return FReply::Handled();
}

FReply SSmvCycler::OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E)
{
	if (E.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	const float X = (float)G.AbsoluteToLocal(E.GetScreenSpacePosition()).X;
	Step(X < G.GetLocalSize().X * 0.5f ? -1 : 1);
	return FReply::Handled();
}

FReply SSmvCycler::OnMouseWheel(const FGeometry& G, const FPointerEvent& E)
{
	Step(E.GetWheelDelta() > 0 ? -1 : 1);
	return FReply::Handled();
}

FNavigationReply SSmvCycler::OnNavigation(const FGeometry& G, const FNavigationEvent& E)
{
	const EUINavigation N = E.GetNavigationType();
	if (N == EUINavigation::Left || N == EUINavigation::Right) { Step(N == EUINavigation::Right ? 1 : -1); return FNavigationReply::Stop(); }
	return SLeafWidget::OnNavigation(G, E);
}

FReply SSmvCycler::OnKeyDown(const FGeometry& G, const FKeyEvent& E)
{
	if (FSlateApplication::Get().GetNavigationActionFromKey(E) == EUINavigationAction::Accept && !E.IsRepeat()) { Step(1); return FReply::Handled(); }
	return FReply::Unhandled();
}

// ---------------------------------------------------------------------------
// SSmvKeyCapture
// ---------------------------------------------------------------------------

int32 SSmvKeyCapture::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 L,
	const FWidgetStyle& Style, bool bParentEnabled) const
{
	const FVector2D Sz = FVector2D(G.GetLocalSize());
	const bool bBlink = bListening && FMath::Fmod(FPlatformTime::Seconds(), 0.8) < 0.4;
	DrBox(Out, L, G, FVector2D::ZeroVector, Sz, bListening ? Accent().CopyWithNewOpacity(bBlink ? 0.35f : 0.2f) : FLinearColor(1, 1, 1, 0.06f), Round(8.f));
	DrRoundRect(Out, L + 1, G, FVector2D(0.5f, 0.5f), Sz - FVector2D(1, 1), 8.f, bNavFocus || bListening ? Hex(0x7dd3fc) : Edge(), bNavFocus ? 2.f : 1.f);
	FString S = bListening ? FSmvI18n::T(TEXT("bind.press_key")).ToString() : Current.Get(FString());
	// Joystix simgesi (gamepad sütununda gamepad ailesi, klavye sütununda klavye simgesi)
	if (!bListening && !S.IsEmpty())
	{
		if (const FSlateBrush* Gb = FSmvInputGlyphs::Brush(S, 24.f))
		{
			const FVector2D Isz = Gb->ImageSize;
			DrBox(Out, L + 2, G, FVector2D((Sz.X - Isz.X) * 0.5f, (Sz.Y - Isz.Y) * 0.5f), Isz, FLinearColor::White, Gb);
			return L + 3;
		}
	}
	if (S.StartsWith(TEXT("pad:"))) S = FString(TEXT("Pad ")) + S.Mid(4);
	DrText(Out, L + 2, G, S, FVector2D(Sz.X * 0.5f, Sz.Y * 0.5f), Font(11, "Bold"), bListening ? FLinearColor::White : Fg(), 0);
	return L + 3;
}

FReply SSmvKeyCapture::OnFocusReceived(const FGeometry& G, const FFocusEvent& E)
{
	bNavFocus = E.GetCause() != EFocusCause::Mouse;
	return FReply::Handled();
}

FReply SSmvKeyCapture::OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E)
{
	if (E.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	bListening = true;
	ListenAt = FPlatformTime::Seconds();
	FSmvUiSound::Play(ESmvSnd::Open);
	return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::SetDirectly);
}

FReply SSmvKeyCapture::OnKeyDown(const FGeometry& G, const FKeyEvent& E)
{
	const FKey K = E.GetKey();
	if (!bListening)
	{
		if (FSlateApplication::Get().GetNavigationActionFromKey(E) == EUINavigationAction::Accept && !E.IsRepeat())
		{
			bListening = true;
			ListenAt = FPlatformTime::Seconds();
			FSmvUiSound::Play(ESmvSnd::Open);
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}
	if (E.IsRepeat() || FPlatformTime::Seconds() - ListenAt < 0.15) return FReply::Handled();
	if (K == EKeys::Escape) { bListening = false; FSmvUiSound::Play(ESmvSnd::Back); return FReply::Handled(); }
	if (K.IsGamepadKey() != bPad) { FSmvUiSound::Play(ESmvSnd::Error); return FReply::Handled(); }
	if (K.IsMouseButton() || K.IsAxis1D() || K.IsAxis2D()) return FReply::Handled();
	bListening = false;
	FSmvUiSound::Play(ESmvSnd::Click);
	if (OnCaptured) OnCaptured(K);
	return FReply::Handled();
}

FNavigationReply SSmvKeyCapture::OnNavigation(const FGeometry& G, const FNavigationEvent& E)
{
	if (bListening) return FNavigationReply::Stop();
	return SLeafWidget::OnNavigation(G, E);
}

// ---------------------------------------------------------------------------
// SSmvRadial
// ---------------------------------------------------------------------------

void SSmvRadial::Construct(const FArguments& A)
{
	Items = A._Items; Current = A._Current; Radius = A._Radius; Stick = A._Stick; OnPick = A._OnPick; TitleText = A._Title;
}

int32 SSmvRadial::IndexAt(const FVector2D& D) const
{
	const int32 N = Items.Get().Num();
	if (N == 0) return -1;
	// 0 = üst, saat yönünde
	float A = FMath::RadiansToDegrees(FMath::Atan2((float)D.Y, (float)D.X)) + 90.f;
	A = FMath::Fmod(A + 360.f + 180.f / N, 360.f);
	return FMath::Clamp((int32)(A / (360.f / N)), 0, N - 1);
}

void SSmvRadial::Tick(const FGeometry& G, const double InNow, const float Dt)
{
	OpenA = FMath::Min(1.f, OpenA + Dt * (ReducedMotion() ? 100.f : 5.5f));
	if (Stick)
	{
		const FVector2D V = Stick();
		if (V.Size() > 0.5) Hi = IndexAt(FVector2D(V.X, -V.Y));
	}
	if (Hi != LastHi) { if (Hi >= 0) FSmvUiSound::Play(ESmvSnd::Hover); LastHi = Hi; }
}

int32 SSmvRadial::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 L,
	const FWidgetStyle& Style, bool bParentEnabled) const
{
	const TArray<FString> It = Items.Get();
	const int32 N = It.Num();
	const FVector2D C = FVector2D(G.GetLocalSize()) * 0.5;
	const float E = 1.f - FMath::Pow(1.f - OpenA, 3.f);
	const float R = Radius * (0.6f + 0.4f * E);
	const float Ri = R * 0.36f;
	DrGlow(Out, L, G, C, R * 1.25f, Accent().CopyWithNewOpacity(0.25f * E));
	DrBox(Out, L + 1, G, C - FVector2D(R, R), FVector2D(2 * R, 2 * R), Hex(0x0a1222, 0.88f * E), Circle());
	DrArc(Out, L + 2, G, C, R, 0.f, 360.f, Accent().CopyWithNewOpacity(0.55f * E), 2.f);
	DrArc(Out, L + 2, G, C, Ri, 0.f, 360.f, Edge().CopyWithNewOpacity(E), 1.f);
	if (N == 0) return L + 6;
	const float Step = 360.f / N;
	const FString Cur = Current.Get(FString());
	for (int32 i = 0; i < N; ++i)
	{
		const float Mid = -90.f + i * Step;
		const float A0 = Mid - Step * 0.5f;
		// ayırıcı
		const float Ra = FMath::DegreesToRadians(A0);
		DrSeg(Out, L + 2, G, C + FVector2D(FMath::Cos(Ra), FMath::Sin(Ra)) * Ri, C + FVector2D(FMath::Cos(Ra), FMath::Sin(Ra)) * R, Edge().CopyWithNewOpacity(E), 1.f);
		const bool bHi = i == Hi;
		const bool bCur = It[i] == Cur;
		if (bHi || bCur)
		{
			// dilim dolgusu: kalın yay
			const float Rm = (R + Ri) * 0.5f, Th = (R - Ri) - 4.f;
			DrArc(Out, L + 3, G, C, Rm, A0 + 1.5f, A0 + Step - 1.5f, (bHi ? Accent() : Accent3()).CopyWithNewOpacity((bHi ? 0.42f : 0.18f) * E), Th);
		}
		const float Rl = (R + Ri) * 0.5f;
		const float Rm = FMath::DegreesToRadians(Mid);
		const FVector2D Lp = C + FVector2D(FMath::Cos(Rm), FMath::Sin(Rm)) * Rl;
		const int32 Fs = N > 12 ? 9 : N > 8 ? 10 : 12;
		DrText(Out, L + 4, G, It[i], Lp, Font(Fs, "Bold"), (bHi ? FLinearColor::White : bCur ? Accent3() : Fg()).CopyWithNewOpacity(E), 0);
		if (bCur) DrBox(Out, L + 4, G, Lp + FVector2D(-3, 10), FVector2D(6, 6), Accent3().CopyWithNewOpacity(E), Circle());
	}
	// merkez
	DrBox(Out, L + 4, G, C - FVector2D(Ri - 3.f, Ri - 3.f), FVector2D(2 * Ri - 6.f, 2 * Ri - 6.f), Hex(0x060a14, 0.95f * E), Circle());
	DrText(Out, L + 5, G, TitleText.Get(FText::GetEmpty()).ToString(), C - FVector2D(0, 12), Font(9, "Bold", 120), Dim().CopyWithNewOpacity(E), 0);
	DrText(Out, L + 5, G, It.IsValidIndex(Hi) ? It[Hi] : Cur, C + FVector2D(0, 8), Font(13, "Bold"), (It.IsValidIndex(Hi) ? Accent() : Accent3()).CopyWithNewOpacity(E), 0);
	return L + 6;
}

FReply SSmvRadial::OnMouseMove(const FGeometry& G, const FPointerEvent& E)
{
	const FVector2D D = FVector2D(G.AbsoluteToLocal(E.GetScreenSpacePosition())) - FVector2D(G.GetLocalSize()) * 0.5;
	Hi = D.Size() > Radius * 0.3f && D.Size() < Radius * 1.3f ? IndexAt(D) : -1;
	return FReply::Handled();
}

FReply SSmvRadial::OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E)
{
	if (E.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	Confirm();
	return FReply::Handled();
}

bool SSmvRadial::Confirm()
{
	if (Hi < 0 || !Items.Get().IsValidIndex(Hi)) return false;
	FSmvUiSound::Play(ESmvSnd::Click);
	if (OnPick) OnPick(Hi);
	return true;
}

// ---------------------------------------------------------------------------
// SSmvTutorial
// ---------------------------------------------------------------------------

void SSmvTutorial::Construct(const FArguments& A)
{
	Pages = A._Pages;
	OnDone = A._OnDone;
	ChildSlot
	[
		SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FLinearColor(0.01f, 0.015f, 0.03f, 0.72f)).HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(720.f)
			[
				SAssignNew(Body, SBox)
			]
		]
	];
	Rebuild();
}

void SSmvTutorial::Rebuild()
{
	if (!Body.IsValid() || Pages.Num() == 0) return;
	const FPage& P = Pages[FMath::Clamp(Page, 0, Pages.Num() - 1)];
	const bool bLast = Page >= Pages.Num() - 1;
	TSharedRef<SHorizontalBox> Dots = SNew(SHorizontalBox);
	for (int32 i = 0; i < Pages.Num(); ++i)
	{
		Dots->AddSlot().AutoWidth().Padding(3, 0)
		[
			SNew(SBox).WidthOverride(i == Page ? 22.f : 8.f).HeightOverride(8.f)
			[
				SNew(SBorder).BorderImage(Round(4.f)).BorderBackgroundColor(i == Page ? Accent() : FLinearColor(1, 1, 1, 0.2f))
			]
		];
	}
	SSmvTutorial* Self = this;
	TSharedPtr<SWidget> Next;
	Body->SetContent(Glass(SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6, 0, 16)[Icon(P.Icon, 84.f, Accent())]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Title(P.Title, 24)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 0)
		[
			SNew(STextBlock).Text(P.Body).Font(Font(14)).ColorAndOpacity(Fg()).AutoWrapText(true).Justification(ETextJustify::Center)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 22, 0, 18)[Dots]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SSmvButton).Kind(ESmvBtn::Ghost).Text(FSmvI18n::T(TEXT("tut.skip"))).OnClick([Self]() { if (Self->OnDone) Self->OnDone(); })
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
			[
				SNew(SSmvButton).Text(FSmvI18n::T(TEXT("tut.prev"))).Icon(ESmvIcon::Prev).IsEnabled(Page > 0).OnClick([Self]() { Self->Go(-1); })
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SAssignNew(Next, SSmvButton).Kind(ESmvBtn::Primary).Text(FSmvI18n::T(bLast ? TEXT("tut.start") : TEXT("tut.next")))
				.Icon(bLast ? ESmvIcon::Check : ESmvIcon::Next).OnClick([Self]() { Self->Go(1); })
			]
		], FMargin(36.f, 30.f)));
	NextBtn = Next;
	if (NextBtn.IsValid()) FSlateApplication::Get().SetAllUserFocus(NextBtn, EFocusCause::Navigation);
}

void SSmvTutorial::Go(int32 D)
{
	if (Page + D >= Pages.Num()) { if (OnDone) OnDone(); return; }
	Page = FMath::Clamp(Page + D, 0, Pages.Num() - 1);
	FSmvUiSound::Play(ESmvSnd::Open);
	Rebuild();
}

FReply SSmvTutorial::OnKeyDown(const FGeometry& G, const FKeyEvent& E)
{
	const EUINavigationAction A = FSlateApplication::Get().GetNavigationActionFromKey(E);
	if (A == EUINavigationAction::Back) { if (OnDone) OnDone(); return FReply::Handled(); }
	if (E.GetKey() == EKeys::Gamepad_RightShoulder || E.GetKey() == EKeys::E) { Go(1); return FReply::Handled(); }
	if (E.GetKey() == EKeys::Gamepad_LeftShoulder || E.GetKey() == EKeys::Q) { Go(-1); return FReply::Handled(); }
	return FReply::Unhandled();
}

#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

UMaterialInterface* SmvSolidMat(UMaterialInterface* Fallback)
{
	static TWeakObjectPtr<UMaterialInterface> Pbr;
	static bool bTried = false;
	if (!bTried)
	{
		bTried = true;
		Pbr = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Simumav/Materials/M_SmvPbr.M_SmvPbr"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	return Pbr.IsValid() ? Pbr.Get() : Fallback;
}

void SmvSetSolidColor(UMaterialInstanceDynamic* M, const FLinearColor& C)
{
	if (!M) return;
	M->SetVectorParameterValue(TEXT("BaseColor"), C);
	M->SetVectorParameterValue(TEXT("Color"), C);
	M->SetScalarParameterValue(TEXT("Roughness"), 0.65f);
	M->SetScalarParameterValue(TEXT("Metallic"), 0.f);
}
