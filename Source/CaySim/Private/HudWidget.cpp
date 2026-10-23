#include "HudWidget.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/DateTime.h"
#include "CaySimGeo.h"

void UCaySimHudWidget::CycleColor()
{
	Color = (Color == ECaySimHudColor::Green) ? ECaySimHudColor::White : (Color == ECaySimHudColor::White ? ECaySimHudColor::Amber : ECaySimHudColor::Green);
}

FLinearColor UCaySimHudWidget::MainColor() const
{
	switch (Color)
	{
	case ECaySimHudColor::White: return FLinearColor(0.95f, 0.95f, 0.95f);
	case ECaySimHudColor::Amber: return FLinearColor(1.0f, 0.72f, 0.2f);
	default:                     return FLinearColor(0.61f, 1.0f, 0.42f); // #9cff6a
	}
}

/** Çizim yardımcısı: siyah dış çizgi + renkli çizgi/metin; koordinatlar piksel (yerel). */
struct UCaySimHudWidget::FPainter
{
	FSlateWindowElementList& Out;
	const FGeometry& Geo;
	int32 Layer;
	FLinearColor C;
	FSlateFontInfo Font;
	FSlateFontInfo FontBig;
	float Thick;
	TSharedRef<FSlateFontMeasure> Measure;
	FVector2D Size;

	FPainter(FSlateWindowElementList& InOut, const FGeometry& InGeo, int32 InLayer, const FLinearColor& InC, int32 FontSize, float InThick)
		: Out(InOut), Geo(InGeo), Layer(InLayer), C(InC), Thick(InThick), Measure(FSlateApplication::Get().GetRenderer()->GetFontMeasureService())
	{
		Font = FCoreStyle::GetDefaultFontStyle("Mono", FontSize);
		FontBig = FCoreStyle::GetDefaultFontStyle("Mono", FontSize + 5);
		Size = Geo.GetLocalSize();
	}

	void Lines(const TArray<FVector2D>& Pts, bool bDashed = false, float Alpha = 1.f) const
	{
		if (Pts.Num() < 2) return;
		if (bDashed)
		{
			for (int32 i = 0; i + 1 < Pts.Num(); ++i)
			{
				const FVector2D A = Pts[i], B = Pts[i + 1];
				const float L = (float)FVector2D::Distance(A, B);
				const int32 N = FMath::Max(1, (int32)(L / 8.f));
				for (int32 k = 0; k < N; k += 2)
				{
					TArray<FVector2D> Seg = { FMath::Lerp(A, B, (float)k / N), FMath::Lerp(A, B, FMath::Min(1.f, (float)(k + 1) / N)) };
					Stroke(Seg, Alpha);
				}
			}
			return;
		}
		Stroke(Pts, Alpha);
	}

	void Stroke(const TArray<FVector2D>& PtsD, float Alpha) const
	{
		// MakeLines: UE 5.x'te FVector2f aşırı yükü (FVector2D sürümü kaldırılabilir)
		TArray<FVector2f> Pts;
		Pts.Reserve(PtsD.Num());
		for (const FVector2D& V : PtsD) Pts.Add(FVector2f((float)V.X, (float)V.Y));
		// dış çizgi
		FSlateDrawElement::MakeLines(Out, Layer, Geo.ToPaintGeometry(), Pts, ESlateDrawEffect::None, FLinearColor(0, 0, 0, 0.6f * Alpha), true, Thick + 2.f);
		FSlateDrawElement::MakeLines(Out, Layer + 1, Geo.ToPaintGeometry(), Pts, ESlateDrawEffect::None, C.CopyWithNewOpacity(Alpha), true, Thick);
	}

	void Text(const FString& S, FVector2D Pos, int32 Align = -1 /* -1 sol, 0 orta, 1 sağ */, bool bBig = false, FLinearColor Override = FLinearColor::Transparent) const
	{
		const FSlateFontInfo& F = bBig ? FontBig : Font;
		const FVector2D Sz = Measure->Measure(S, F);
		if (Align == 0) Pos.X -= Sz.X * 0.5;
		else if (Align > 0) Pos.X -= Sz.X;
		const FLinearColor Col = (Override.A > 0.f) ? Override : C;
		for (const FVector2D& O : { FVector2D(1, 1), FVector2D(-1, 1), FVector2D(1, -1), FVector2D(-1, -1) })
		{
			FSlateDrawElement::MakeText(Out, Layer, Geo.ToPaintGeometry(Sz, FSlateLayoutTransform(Pos + O)), S, F, ESlateDrawEffect::None, FLinearColor(0, 0, 0, 0.8f));
		}
		FSlateDrawElement::MakeText(Out, Layer + 1, Geo.ToPaintGeometry(Sz, FSlateLayoutTransform(Pos)), S, F, ESlateDrawEffect::None, Col);
	}

	void Box(FVector2D Min, FVector2D Max, float Alpha = 1.f) const
	{
		Lines({ Min, FVector2D(Max.X, Min.Y), Max, FVector2D(Min.X, Max.Y), Min }, false, Alpha);
	}

	void FilledBox(FVector2D Min, FVector2D Max, FLinearColor Fill) const
	{
		FSlateDrawElement::MakeBox(Out, Layer, Geo.ToPaintGeometry(Max - Min, FSlateLayoutTransform(Min)), FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, Fill);
	}

	static FVector2D Rot(FVector2D P, float Ang) { const float c = FMath::Cos(Ang), s = FMath::Sin(Ang); return FVector2D(P.X * c - P.Y * s, P.X * s + P.Y * c); }
};

int32 UCaySimHudWidget::NativePaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const
{
	LayerId = Super::NativePaint(Args, G, Cull, Out, LayerId, Style, bParentEnabled);
	if (!bHasTele) return LayerId;

	FPainter P(Out, G, LayerId + 1, MainColor(), FontSize, LineThickness);
	const float W = (float)P.Size.X, H = (float)P.Size.Y;
	const FVector2D Ctr(W * 0.5f, H * 0.5f);
	const float Roll = Ctx.RollRad, Pitch = Ctx.PitchRad;
	const float Ppd = H / FMath::Max(10.f, Ctx.FovDeg); // piksel / derece
	const float CamPitchDeg = FMath::RadiansToDegrees(Pitch) + Ctx.GimbalDeg;
	const FCaySimTelemetry& T = Latest;

	// ---- Suni ufuk + pitch merdiveni (roll ile döner) ----
	{
		const float LadderHalfW = W * 0.11f;
		for (int32 Deg = -90; Deg <= 90; Deg += 5)
		{
			const float Dy = (CamPitchDeg - Deg) * Ppd; // yukarı pozitif pitch → çizgi aşağı kayar
			if (FMath::Abs(Dy) > H * 0.42f) continue;
			const float HalfW = (Deg == 0) ? W * 0.22f : (Deg % 10 == 0 ? LadderHalfW : LadderHalfW * 0.55f);
			const float Gap = (Deg == 0) ? W * 0.06f : W * 0.035f;
			auto Seg = [&](float x0, float x1)
			{
				TArray<FVector2D> Pts = { Ctr + FPainter::Rot(FVector2D(x0, Dy), -Roll), Ctr + FPainter::Rot(FVector2D(x1, Dy), -Roll) };
				P.Lines(Pts, Deg < 0);
			};
			Seg(-HalfW, -Gap);
			Seg(Gap, HalfW);
			if (Deg != 0 && Deg % 10 == 0)
			{
				const FString Lbl = FString::Printf(TEXT("%d"), FMath::Abs(Deg));
				P.Text(Lbl, Ctr + FPainter::Rot(FVector2D(HalfW + 6, Dy - 8), -Roll), -1);
				P.Text(Lbl, Ctr + FPainter::Rot(FVector2D(-HalfW - 6, Dy - 8), -Roll), 1);
			}
		}
		// zenit / nadir
		const float DyZ = (CamPitchDeg - 90.f) * Ppd, DyN = (CamPitchDeg + 90.f) * Ppd;
		if (FMath::Abs(DyZ) < H * 0.45f) P.Text(TEXT("●"), Ctr + FPainter::Rot(FVector2D(0, DyZ - 8), -Roll), 0);
		if (FMath::Abs(DyN) < H * 0.45f) P.Text(TEXT("✕"), Ctr + FPainter::Rot(FVector2D(0, DyN - 8), -Roll), 0);
	}

	// ---- Roll yayı (üst) ----
	{
		const float R = H * 0.36f;
		TArray<FVector2D> Arc;
		for (int32 A = -60; A <= 60; A += 5) Arc.Add(Ctr + FVector2D(FMath::Sin(FMath::DegreesToRadians((float)A)) * R, -FMath::Cos(FMath::DegreesToRadians((float)A)) * R));
		P.Lines(Arc, false, 0.8f);
		for (int32 A : { -60, -45, -30, -20, -10, 0, 10, 20, 30, 45, 60 })
		{
			const float a = FMath::DegreesToRadians((float)A);
			const float Len = (A % 30 == 0) ? 12.f : 7.f;
			P.Lines({ Ctr + FVector2D(FMath::Sin(a) * R, -FMath::Cos(a) * R), Ctr + FVector2D(FMath::Sin(a) * (R + Len), -FMath::Cos(a) * (R + Len)) });
		}
		// gökyüzü işaretçisi (roll) üçgen
		const float ra = -Roll;
		const FVector2D Tip = Ctr + FVector2D(FMath::Sin(ra) * (R - 2), -FMath::Cos(ra) * (R - 2));
		const FVector2D L = Ctr + FVector2D(FMath::Sin(ra - 0.035f) * (R - 14), -FMath::Cos(ra - 0.035f) * (R - 14));
		const FVector2D Rr = Ctr + FVector2D(FMath::Sin(ra + 0.035f) * (R - 14), -FMath::Cos(ra + 0.035f) * (R - 14));
		P.Lines({ Tip, L, Rr, Tip });
		P.Text(FString::Printf(TEXT("R %d°"), FMath::RoundToInt(FMath::RadiansToDegrees(Roll))), Ctr + FVector2D(0, -R - 34), 0);
	}

	// ---- Boresight (W) ----
	{
		const float s = 14.f;
		P.Lines({ Ctr + FVector2D(-s * 2, 0), Ctr + FVector2D(-s, 0), Ctr + FVector2D(-s * 0.5f, s * 0.6f), Ctr, Ctr + FVector2D(s * 0.5f, s * 0.6f), Ctr + FVector2D(s, 0), Ctr + FVector2D(s * 2, 0) });
	}

	// ---- Uçuş yolu vektörü ----
	if (bShowFlightPathVector)
	{
		const float Spd = FVector(T.Vn, T.Ve, T.Vd).Size();
		if (Spd >= 1.f)
		{
			const float Track = FMath::RadiansToDegrees(FMath::Atan2(T.Ve, T.Vn));
			float DriftDeg = Track - T.Heading;
			while (DriftDeg > 180.f) DriftDeg -= 360.f;
			while (DriftDeg < -180.f) DriftDeg += 360.f;
			const float Fpa = FMath::RadiansToDegrees(FMath::Atan2(-T.Vd, FMath::Max(0.1f, FVector2D(T.Vn, T.Ve).Size())));
			FVector2D Pos = Ctr + FPainter::Rot(FVector2D(DriftDeg * Ppd, (CamPitchDeg - Fpa) * Ppd), -Roll);
			const bool bClipped = FMath::Abs(Pos.X - Ctr.X) > W * 0.45f || FMath::Abs(Pos.Y - Ctr.Y) > H * 0.45f;
			Pos.X = FMath::Clamp(Pos.X, W * 0.05f, W * 0.95f);
			Pos.Y = FMath::Clamp(Pos.Y, H * 0.05f, H * 0.95f);
			TArray<FVector2D> Circ;
			for (int32 i = 0; i <= 24; ++i) { const float a = i / 24.f * 2 * PI; Circ.Add(Pos + FVector2D(FMath::Cos(a) * 9, FMath::Sin(a) * 9)); }
			P.Lines(Circ, bClipped);
			P.Lines({ Pos + FVector2D(-9, 0), Pos + FVector2D(-22, 0) });
			P.Lines({ Pos + FVector2D(9, 0), Pos + FVector2D(22, 0) });
			P.Lines({ Pos + FVector2D(0, -9), Pos + FVector2D(0, -18) });
		}
	}

	// ---- Heading şeridi (üst) ----
	{
		const float Y = 34.f, HalfSpan = 35.f, PxPerDeg = (W * 0.5f) / HalfSpan * 0.9f;
		const float Hdg = FMath::Fmod(T.Heading + 360.f, 360.f);
		for (int32 d = -40; d <= 40; d += 5)
		{
			const int32 Deg = (FMath::RoundToInt(Hdg / 5.f) * 5 + d + 720) % 360;
			const float X = Ctr.X + ((float)(FMath::RoundToInt(Hdg / 5.f) * 5 + d) - Hdg) * PxPerDeg;
			if (FMath::Abs(X - Ctr.X) > HalfSpan * PxPerDeg) continue;
			const bool bTen = Deg % 10 == 0;
			P.Lines({ FVector2D(X, Y), FVector2D(X, Y + (bTen ? 10 : 5)) });
			if (bTen)
			{
				FString Lbl;
				switch (Deg) { case 0: Lbl = TEXT("N"); break; case 90: Lbl = TEXT("E"); break; case 180: Lbl = TEXT("S"); break; case 270: Lbl = TEXT("W"); break; default: Lbl = FString::Printf(TEXT("%02d"), Deg / 10); }
				P.Text(Lbl, FVector2D(X, Y - 18), 0);
			}
		}
		// merkez üçgen + kutu
		P.Lines({ FVector2D(Ctr.X, Y + 14), FVector2D(Ctr.X - 6, Y + 22), FVector2D(Ctr.X + 6, Y + 22), FVector2D(Ctr.X, Y + 14) });
		P.Box(FVector2D(Ctr.X - 24, Y + 24), FVector2D(Ctr.X + 24, Y + 44));
		P.Text(FString::Printf(TEXT("%03d"), FMath::RoundToInt(Hdg) % 360), FVector2D(Ctr.X, Y + 26), 0);
		// ev yönü H
		if (T.bHasHome)
		{
			float Rel = (float)CaySimGeo::BearingDeg(T.Lat, T.Lon, T.HomeLat, T.HomeLon) - Hdg;
			while (Rel > 180) Rel -= 360; while (Rel < -180) Rel += 360;
			if (FMath::Abs(Rel) <= HalfSpan) P.Text(TEXT("H"), FVector2D(Ctr.X + Rel * PxPerDeg, Y + 46), 0);
		}
		// rüzgâr oku
		if (Ctx.WindMs > 0.1f)
		{
			float Rel = Ctx.WindDir - Hdg;
			while (Rel > 180) Rel -= 360; while (Rel < -180) Rel += 360;
			const FVector2D O(W * 0.5f + W * 0.3f, Y + 60);
			const float a = FMath::DegreesToRadians(Rel + 180.f);
			P.Lines({ O, O + FVector2D(FMath::Sin(a) * 18, -FMath::Cos(a) * 18) });
			P.Text(FString::Printf(TEXT("%.0f m/s %03.0f"), Ctx.WindMs, Ctx.WindDir), O + FVector2D(24, -8), -1);
		}
	}

	// ---- Sol: hava hızı bandı ----
	{
		const float X = W * 0.14f, Half = H * 0.26f, PxPerMs = Half / 20.f;
		const float V = T.Airspeed > 0.1f ? T.Airspeed : T.Groundspeed;
		P.Lines({ FVector2D(X, Ctr.Y - Half), FVector2D(X, Ctr.Y + Half) });
		for (int32 v = FMath::Max(0, FMath::FloorToInt(V) - 20); v <= FMath::FloorToInt(V) + 20; ++v)
		{
			const float Y = Ctr.Y - (v - V) * PxPerMs;
			if (FMath::Abs(Y - Ctr.Y) > Half) continue;
			const bool bFive = v % 5 == 0;
			P.Lines({ FVector2D(X, Y), FVector2D(X - (bFive ? 12 : 6), Y) });
			if (bFive) P.Text(FString::Printf(TEXT("%d"), v), FVector2D(X - 16, Y - 8), 1);
		}
		P.Box(FVector2D(X + 4, Ctr.Y - 12), FVector2D(X + 64, Ctr.Y + 12));
		P.Text(FString::Printf(TEXT("%.1f"), V), FVector2D(X + 34, Ctr.Y - 9), 0);
		P.Text(FString::Printf(TEXT("%.0f km/h"), V * 3.6f), FVector2D(X + 34, Ctr.Y + 14), 0);
		P.Text(TEXT("IAS m/s"), FVector2D(X, Ctr.Y - Half - 22), 0);
		P.Text(FString::Printf(TEXT("GS %.1f"), T.Groundspeed), FVector2D(X, Ctr.Y + Half + 6), 0);
	}

	// ---- Sağ: irtifa bandı (AGL) + MSL + R ----
	{
		const float X = W * 0.86f, Half = H * 0.26f, PxPerM = Half / 60.f;
		const float A = (float)T.AltRel;
		P.Lines({ FVector2D(X, Ctr.Y - Half), FVector2D(X, Ctr.Y + Half) });
		for (int32 m = FMath::FloorToInt((A - 60) / 10) * 10; m <= A + 60; m += 10)
		{
			const float Y = Ctr.Y - (m - A) * PxPerM;
			if (FMath::Abs(Y - Ctr.Y) > Half) continue;
			const bool bFifty = m % 50 == 0;
			P.Lines({ FVector2D(X, Y), FVector2D(X + (bFifty ? 12 : 6), Y) });
			if (bFifty) P.Text(FString::Printf(TEXT("%d"), m), FVector2D(X + 16, Y - 8), -1);
		}
		P.Box(FVector2D(X - 70, Ctr.Y - 12), FVector2D(X - 4, Ctr.Y + 12));
		P.Text(FString::Printf(TEXT("%.0f"), A), FVector2D(X - 37, Ctr.Y - 9), 0);
		P.Text(FString::Printf(TEXT("MSL %.0f"), T.AltMsl), FVector2D(X - 37, Ctr.Y + 14), 0);
		const bool bLow = T.bArmed && A < 60.f && T.Groundspeed > 3.f;
		P.Text(FString::Printf(TEXT("R %.0f"), A), FVector2D(X - 37, Ctr.Y + 30), 0, false, bLow ? FLinearColor::Red : FLinearColor::Transparent);
		P.Text(TEXT("AGL m"), FVector2D(X, Ctr.Y - Half - 22), 0);
		// dikey hız (karekök ölçek ±10)
		const float Vs = FMath::Clamp(T.Climb, -10.f, 10.f);
		const float Vy = -FMath::Sign(Vs) * FMath::Sqrt(FMath::Abs(Vs) / 10.f) * Half * 0.8f;
		const float Xv = X + 50;
		P.Lines({ FVector2D(Xv, Ctr.Y - Half * 0.8f), FVector2D(Xv, Ctr.Y + Half * 0.8f) }, false, 0.7f);
		P.Lines({ FVector2D(Xv - 5, Ctr.Y + Vy), FVector2D(Xv + 5, Ctr.Y + Vy) });
		P.Text(FString::Printf(TEXT("%+.1f"), T.Climb), FVector2D(Xv + 8, Ctr.Y + Vy - 8), -1);
	}

	// ---- Sol alt: mod / ARM / G / THR / BAT / GPS ----
	{
		float Y = H - 150.f;
		const float X = 24.f;
		P.Text(T.Mode.IsEmpty() ? TEXT("---") : T.Mode, FVector2D(X, Y), -1, true); Y += 26;
		P.Text(T.bArmed ? TEXT("ARM") : TEXT("SAFE"), FVector2D(X, Y), -1, false, T.bArmed ? FLinearColor::Transparent : FLinearColor(1, 0.6f, 0.2f)); Y += 20;
		const float G = (T.bArmed && T.Airspeed > 5.f) ? 1.f / FMath::Max(0.2f, FMath::Cos(Roll)) : 1.f;
		P.Text(FString::Printf(TEXT("G %.1f"), G), FVector2D(X, Y), -1); Y += 20;
		P.Text(FString::Printf(TEXT("THR %3.0f%%"), T.Throttle), FVector2D(X, Y), -1);
		P.Box(FVector2D(X + 90, Y + 4), FVector2D(X + 190, Y + 14));
		P.FilledBox(FVector2D(X + 90, Y + 4), FVector2D(X + 90 + FMath::Clamp(T.Throttle, 0.f, 100.f), Y + 14), P.C.CopyWithNewOpacity(0.6f)); Y += 20;
		if (T.BattV >= 0.f) P.Text(FString::Printf(TEXT("BAT %.1fV %s"), T.BattV, T.BattPct >= 0.f ? *FString::Printf(TEXT("%.0f%%"), T.BattPct) : TEXT("")), FVector2D(X, Y), -1, false, (T.BattPct >= 0.f && T.BattPct < 25.f) ? FLinearColor::Red : FLinearColor::Transparent); Y += 20;
		P.Text(FString::Printf(TEXT("GPS %d  SAT %s"), T.GpsFix, T.Sats >= 0 ? *FString::Printf(TEXT("%d"), T.Sats) : TEXT("--")), FVector2D(X, Y), -1);
	}

	// ---- Sağ alt: UTC / T+ / konum / HOME / GMB / gecikme ----
	{
		float Y = H - 150.f;
		const float X = W - 24.f;
		const FDateTime Utc = Ctx.bReplay && Ctx.WallClock > 0 ? FDateTime::FromUnixTimestamp((int64)Ctx.WallClock) : FDateTime::UtcNow();
		P.Text(Utc.ToString(TEXT("%H:%M:%S Z")), FVector2D(X, Y), 1); Y += 20;
		const double Tp = Ctx.bReplay ? Ctx.WallClock - Ctx.T0 : FDateTime::UtcNow().ToUnixTimestamp() - Ctx.T0;
		if (Ctx.T0 > 0) { P.Text(FString::Printf(TEXT("T+ %02d:%02d:%02d"), (int32)Tp / 3600, ((int32)Tp / 60) % 60, (int32)Tp % 60), FVector2D(X, Y), 1); Y += 20; }
		P.Text(FString::Printf(TEXT("%.5f %.5f"), T.Lat, T.Lon), FVector2D(X, Y), 1); Y += 20;
		if (T.bHasHome)
		{
			const double D = CaySimGeo::HaversineM(T.Lat, T.Lon, T.HomeLat, T.HomeLon);
			const double B = CaySimGeo::BearingDeg(T.Lat, T.Lon, T.HomeLat, T.HomeLon);
			P.Text(FString::Printf(TEXT("HOME %.0f m %03.0f°"), D, B), FVector2D(X, Y), 1); Y += 20;
		}
		P.Text(FString::Printf(TEXT("GMB %+.0f°"), Ctx.GimbalDeg), FVector2D(X, Y), 1); Y += 20;
		if (!Ctx.bReplay) P.Text(FString::Printf(TEXT("LNK %.0f ms  %.0f Hz"), Ctx.LatencyMs, Ctx.TelemetryHz), FVector2D(X, Y), 1);
	}

	// ---- Üst sağ: REC / CAM / FOV ----
	{
		const float X = W - 24.f;
		const bool bBlink = FMath::Fmod(FPlatformTime::Seconds(), 1.0) < 0.6;
		if (Ctx.bReplay) P.Text(TEXT("REC ▸ PLAY"), FVector2D(X, 20), 1);
		else if (bBlink) P.Text(TEXT("● REC"), FVector2D(X, 20), 1, false, FLinearColor::Red);
		P.Text(TEXT("CAM 1 · EO"), FVector2D(X, 42), 1);
		P.Text(FString::Printf(TEXT("FOV %.0f°"), Ctx.FovDeg), FVector2D(X, 62), 1);
	}

	// ---- Köşe çerçeveleri + merkez hedef kutusu ----
	if (bShowCornerFrames)
	{
		const float m = 14.f, l = 40.f;
		P.Lines({ FVector2D(m, m + l), FVector2D(m, m), FVector2D(m + l, m) }, false, 0.7f);
		P.Lines({ FVector2D(W - m - l, m), FVector2D(W - m, m), FVector2D(W - m, m + l) }, false, 0.7f);
		P.Lines({ FVector2D(m, H - m - l), FVector2D(m, H - m), FVector2D(m + l, H - m) }, false, 0.7f);
		P.Lines({ FVector2D(W - m - l, H - m), FVector2D(W - m, H - m), FVector2D(W - m, H - m - l) }, false, 0.7f);
		const float b = 60.f, c = 14.f;
		P.Lines({ Ctr + FVector2D(-b, -b + c), Ctr + FVector2D(-b, -b), Ctr + FVector2D(-b + c, -b) }, false, 0.6f);
		P.Lines({ Ctr + FVector2D(b - c, -b), Ctr + FVector2D(b, -b), Ctr + FVector2D(b, -b + c) }, false, 0.6f);
		P.Lines({ Ctr + FVector2D(-b, b - c), Ctr + FVector2D(-b, b), Ctr + FVector2D(-b + c, b) }, false, 0.6f);
		P.Lines({ Ctr + FVector2D(b - c, b), Ctr + FVector2D(b, b), Ctr + FVector2D(b, b - c) }, false, 0.6f);
	}

	// ---- Uyarılar (üst orta, kırmızı kutular) ----
	{
		TArray<FString> Warns;
		if (Ctx.bCollision) Warns.Add(TEXT("ÇARPIŞMA"));
		bool bRcEmpty = true;
		for (int32 V : T.Rc) if (V > 0) { bRcEmpty = false; break; }
		if (Ctx.bSitlMode && T.bArmed && bRcEmpty) Warns.Add(TEXT("RC YOK"));
		if (T.BattPct >= 0.f && T.BattPct < 25.f) Warns.Add(TEXT("BATARYA"));
		if (Ctx.bSitlMode && Ctx.bGcsEverConnected && !T.bGcsConnected) Warns.Add(TEXT("GCS KESİLDİ"));
		if (T.bArmed && T.AltRel < 60.0 && T.Groundspeed > 3.f) Warns.Add(TEXT("ALÇAK"));
		float Y = H * 0.2f;
		const bool bBlink = FMath::Fmod(FPlatformTime::Seconds(), 0.8) < 0.4;
		for (const FString& Wn : Warns)
		{
			const FVector2D Sz = P.Measure->Measure(Wn, P.FontBig);
			const FVector2D Min(Ctr.X - Sz.X * 0.5f - 12, Y), Max(Ctr.X + Sz.X * 0.5f + 12, Y + Sz.Y + 8);
			if (bBlink) P.FilledBox(Min, Max, FLinearColor(0.8f, 0, 0, 0.55f));
			const TArray<FVector2f> Frame = { FVector2f(Min), FVector2f((float)Max.X, (float)Min.Y), FVector2f(Max), FVector2f((float)Min.X, (float)Max.Y), FVector2f(Min) };
			FSlateDrawElement::MakeLines(Out, P.Layer + 1, G.ToPaintGeometry(), Frame, ESlateDrawEffect::None, FLinearColor::Red, true, 2.f);
			P.Text(Wn, FVector2D(Ctr.X, Y + 4), 0, true, FLinearColor::White);
			Y += Sz.Y + 16;
		}
	}

	return LayerId + 3;
}
