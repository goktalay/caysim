#include "SmvSvgIcon.h"
#include "Rendering/DrawElements.h"
#include "Layout/Geometry.h"

namespace
{
	using FPoly = TArray<FVector2D>;

	struct FTok
	{
		const TCHAR* P;
		explicit FTok(const TCHAR* S) : P(S) {}
		void Skip() { while (*P && (FChar::IsWhitespace(*P) || *P == TEXT(','))) ++P; }
		bool IsNum() { Skip(); return *P && (FChar::IsDigit(*P) || *P == TEXT('-') || *P == TEXT('+') || *P == TEXT('.')); }
		double Num()
		{
			Skip();
			const TCHAR* S = P;
			if (*P == TEXT('-') || *P == TEXT('+')) ++P;
			bool bDot = false;
			while (*P && (FChar::IsDigit(*P) || (*P == TEXT('.') && !bDot))) { if (*P == TEXT('.')) bDot = true; ++P; }
			if (*P == TEXT('e') || *P == TEXT('E')) { ++P; if (*P == TEXT('-') || *P == TEXT('+')) ++P; while (FChar::IsDigit(*P)) ++P; }
			return FCString::Atod(*FString(P - S, S));
		}
		bool Flag() { Skip(); const bool B = *P == TEXT('1'); if (*P) ++P; return B; }
	};

	void Arc(FPoly& Out, FVector2D P0, double Rx, double Ry, double PhiDeg, bool bLarge, bool bSweep, FVector2D P1)
	{
		// SVG uç nokta parametrelemesi → merkez (W3C F.6.5)
		if (Rx == 0 || Ry == 0) { Out.Add(P1); return; }
		Rx = FMath::Abs(Rx); Ry = FMath::Abs(Ry);
		const double Phi = FMath::DegreesToRadians(PhiDeg), Cp = FMath::Cos(Phi), Sp = FMath::Sin(Phi);
		const double Dx = (P0.X - P1.X) / 2, Dy = (P0.Y - P1.Y) / 2;
		const double X1 = Cp * Dx + Sp * Dy, Y1 = -Sp * Dx + Cp * Dy;
		double L = X1 * X1 / (Rx * Rx) + Y1 * Y1 / (Ry * Ry);
		if (L > 1) { Rx *= FMath::Sqrt(L); Ry *= FMath::Sqrt(L); }
		double Num = Rx * Rx * Ry * Ry - Rx * Rx * Y1 * Y1 - Ry * Ry * X1 * X1, Den = Rx * Rx * Y1 * Y1 + Ry * Ry * X1 * X1;
		double Co = Den > 0 ? FMath::Sqrt(FMath::Max(0.0, Num / Den)) : 0;
		if (bLarge == bSweep) Co = -Co;
		const double Cx1 = Co * Rx * Y1 / Ry, Cy1 = -Co * Ry * X1 / Rx;
		const double Cx = Cp * Cx1 - Sp * Cy1 + (P0.X + P1.X) / 2, Cy = Sp * Cx1 + Cp * Cy1 + (P0.Y + P1.Y) / 2;
		auto Ang = [](double Ux, double Uy, double Vx, double Vy) { return FMath::Atan2(Ux * Vy - Uy * Vx, Ux * Vx + Uy * Vy); };
		const double T1 = Ang(1, 0, (X1 - Cx1) / Rx, (Y1 - Cy1) / Ry);
		double Dt = Ang((X1 - Cx1) / Rx, (Y1 - Cy1) / Ry, (-X1 - Cx1) / Rx, (-Y1 - Cy1) / Ry);
		if (!bSweep && Dt > 0) Dt -= 2 * PI; else if (bSweep && Dt < 0) Dt += 2 * PI;
		const int32 N = FMath::Clamp((int32)(FMath::Abs(Dt) / 0.2) + 2, 3, 48);
		for (int32 i = 1; i <= N; ++i)
		{
			const double T = T1 + Dt * i / N;
			Out.Add(FVector2D(Cx + Rx * FMath::Cos(T) * Cp - Ry * FMath::Sin(T) * Sp, Cy + Rx * FMath::Cos(T) * Sp + Ry * FMath::Sin(T) * Cp));
		}
	}

	void ParsePath(const FString& D, TArray<FPoly>& Out)
	{
		FTok T(*D);
		FVector2D Cur(0, 0), Start(0, 0), LastCtl(0, 0);
		TCHAR Cmd = 0;
		FPoly Poly;
		auto Flush = [&]() { if (Poly.Num() > 1) Out.Add(Poly); Poly.Reset(); };
		while (true)
		{
			T.Skip();
			if (!*T.P) break;
			if (FChar::IsAlpha(*T.P)) { Cmd = *T.P; ++T.P; }
			else if (!Cmd) break;
			const bool bRel = FChar::IsLower(Cmd);
			const TCHAR C = FChar::ToUpper(Cmd);
			const FVector2D Base = bRel ? Cur : FVector2D::ZeroVector;
			if (C == TEXT('Z')) { Poly.Add(Start); Cur = Start; Flush(); Cmd = 0; continue; }
			if (!T.IsNum()) { Cmd = 0; continue; }
			if (C == TEXT('M')) { Flush(); Cur = Base + FVector2D(T.Num(), T.Num()); Start = Cur; Poly.Add(Cur); Cmd = bRel ? TEXT('l') : TEXT('L'); }
			else if (C == TEXT('L')) { Cur = Base + FVector2D(T.Num(), T.Num()); Poly.Add(Cur); }
			else if (C == TEXT('H')) { Cur.X = (bRel ? Cur.X : 0) + T.Num(); Poly.Add(Cur); }
			else if (C == TEXT('V')) { Cur.Y = (bRel ? Cur.Y : 0) + T.Num(); Poly.Add(Cur); }
			else if (C == TEXT('C') || C == TEXT('S') || C == TEXT('Q'))
			{
				FVector2D C1, C2, E;
				if (C == TEXT('C')) { C1 = Base + FVector2D(T.Num(), T.Num()); C2 = Base + FVector2D(T.Num(), T.Num()); E = Base + FVector2D(T.Num(), T.Num()); }
				else if (C == TEXT('S')) { C1 = Cur * 2 - LastCtl; C2 = Base + FVector2D(T.Num(), T.Num()); E = Base + FVector2D(T.Num(), T.Num()); }
				else { const FVector2D Q = Base + FVector2D(T.Num(), T.Num()); E = Base + FVector2D(T.Num(), T.Num()); C1 = Cur + (Q - Cur) * (2.0 / 3.0); C2 = E + (Q - E) * (2.0 / 3.0); }
				if (Poly.Num() == 0) Poly.Add(Cur);
				for (int32 i = 1; i <= 12; ++i)
				{
					const double U = i / 12.0, V = 1 - U;
					Poly.Add(Cur * (V * V * V) + C1 * (3 * V * V * U) + C2 * (3 * V * U * U) + E * (U * U * U));
				}
				LastCtl = C2; Cur = E;
			}
			else if (C == TEXT('A'))
			{
				const double Rx = T.Num(), Ry = T.Num(), Ph = T.Num();
				const bool bL = T.Flag(), bS = T.Flag();
				const FVector2D E = Base + FVector2D(T.Num(), T.Num());
				if (Poly.Num() == 0) Poly.Add(Cur);
				Arc(Poly, Cur, Rx, Ry, Ph, bL, bS, E);
				Cur = E;
			}
			else { T.Num(); }
		}
		Flush();
	}

	double Attr(const FString& El, const TCHAR* Name, double Def = 0)
	{
		const FString Key = FString(TEXT(" ")) + Name + TEXT("=\"");
		const int32 I = El.Find(Key);
		if (I == INDEX_NONE) return Def;
		return FCString::Atod(*El.Mid(I + Key.Len()));
	}
	FString AttrS(const FString& El, const TCHAR* Name)
	{
		const FString Key = FString(TEXT(" ")) + Name + TEXT("=\"");
		const int32 I = El.Find(Key);
		if (I == INDEX_NONE) return FString();
		const int32 S = I + Key.Len(), E = El.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, S);
		return E > S ? El.Mid(S, E - S) : FString();
	}

	void Circle(TArray<FPoly>& Out, double Cx, double Cy, double R)
	{
		FPoly P;
		const int32 N = FMath::Clamp((int32)(R * 4) + 10, 12, 40);
		for (int32 i = 0; i <= N; ++i) { const double A = 2 * PI * i / N; P.Add(FVector2D(Cx + R * FMath::Cos(A), Cy + R * FMath::Sin(A))); }
		Out.Add(P);
	}

	void RoundRect(TArray<FPoly>& Out, double X, double Y, double W, double H, double Rr)
	{
		Rr = FMath::Min(Rr, FMath::Min(W, H) * 0.5);
		FPoly P;
		auto Corner = [&P, Rr](double Cx, double Cy, double A0)
		{
			for (int32 i = 0; i <= 4; ++i) { const double A = A0 + (PI / 2) * i / 4; P.Add(FVector2D(Cx + Rr * FMath::Cos(A), Cy + Rr * FMath::Sin(A))); }
		};
		if (Rr <= 0.01) { P = { FVector2D(X, Y), FVector2D(X + W, Y), FVector2D(X + W, Y + H), FVector2D(X, Y + H), FVector2D(X, Y) }; Out.Add(P); return; }
		Corner(X + W - Rr, Y + Rr, -PI / 2); Corner(X + W - Rr, Y + H - Rr, 0); Corner(X + Rr, Y + H - Rr, PI / 2); Corner(X + Rr, Y + Rr, PI);
		P.Add(P[0]);
		Out.Add(P);
	}

	struct FParsed { TArray<FPoly> Lines; TArray<bool> Fill; };

	const FParsed& Parse(const FString& Body)
	{
		static TMap<FString, FParsed> Cache;
		if (const FParsed* F = Cache.Find(Body)) return *F;
		FParsed R;
		int32 I = 0;
		while ((I = Body.Find(TEXT("<"), ESearchCase::CaseSensitive, ESearchDir::FromStart, I)) != INDEX_NONE)
		{
			const int32 E = Body.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, I);
			if (E == INDEX_NONE) break;
			const FString El = Body.Mid(I, E - I + 1);
			I = E + 1;
			const int32 Before = R.Lines.Num();
			if (El.StartsWith(TEXT("<path"))) ParsePath(AttrS(El, TEXT("d")), R.Lines);
			else if (El.StartsWith(TEXT("<circle"))) Circle(R.Lines, Attr(El, TEXT("cx")), Attr(El, TEXT("cy")), Attr(El, TEXT("r")));
			else if (El.StartsWith(TEXT("<rect"))) RoundRect(R.Lines, Attr(El, TEXT("x")), Attr(El, TEXT("y")), Attr(El, TEXT("width")), Attr(El, TEXT("height")), Attr(El, TEXT("rx")));
			else if (El.StartsWith(TEXT("<line"))) R.Lines.Add({ FVector2D(Attr(El, TEXT("x1")), Attr(El, TEXT("y1"))), FVector2D(Attr(El, TEXT("x2")), Attr(El, TEXT("y2"))) });
			const bool bFill = El.Contains(TEXT("fill=\"currentColor\"")) || El.Contains(TEXT("class=\"fill\""));
			for (int32 k = Before; k < R.Lines.Num(); ++k) R.Fill.Add(bFill);
		}
		return Cache.Add(Body, R);
	}
}

namespace SmvSvg
{
	void Draw(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const FString& Body, const FVector2D& Pos, float Size, const FLinearColor& Color, float StrokeW)
	{
		const FParsed& P = Parse(Body);
		const float K = Size / 24.f;
		for (int32 i = 0; i < P.Lines.Num(); ++i)
		{
			TArray<FVector2f> Pts;
			Pts.Reserve(P.Lines[i].Num());
			for (const FVector2D& V : P.Lines[i]) Pts.Add(FVector2f(Pos + V * K));
			// dolgu: kalın çizgiyle yaklaşık (küçük boyutta fark edilmez)
			FSlateDrawElement::MakeLines(Out, L, G.ToPaintGeometry(), Pts, ESlateDrawEffect::None, Color, true, (P.Fill.IsValidIndex(i) && P.Fill[i] ? 3.4f : StrokeW) * K * 1.05f);
		}
	}

	const FString& Icon(const FString& Name)
	{
		static TMap<FString, FString> M = {
		{ TEXT("alt"), TEXT("<path d=\"M8 4v16M8 4 5 7M8 4l3 3M16 20V4M16 20l-3-3M16 20l3-3\"/>") },
		{ TEXT("spd"), TEXT("<path d=\"M4 16a8 8 0 0 1 16 0\"/><path d=\"M12 16l4-6\"/><circle cx=\"12\" cy=\"16\" r=\"1.6\"/><path d=\"M4 16h2M18 16h2\"/>") },
		{ TEXT("bat"), TEXT("<rect x=\"2.5\" y=\"8\" width=\"16\" height=\"8\" rx=\"1.6\"/><path d=\"M21 11v2\"/><rect class=\"fill\" x=\"4.5\" y=\"10\" width=\"12\" height=\"4\" fill=\"currentColor\" stroke=\"none\"/>") },
		{ TEXT("fuel"), TEXT("<path d=\"M5 21V5a2 2 0 0 1 2-2h6a2 2 0 0 1 2 2v16\"/><path d=\"M4 21h12\"/><path d=\"M7 7h6v4H7z\"/><path d=\"M15 9h2a2 2 0 0 1 2 2v5a1.5 1.5 0 0 0 3 0V9l-2-2\"/>") },
		{ TEXT("gps"), TEXT("<path d=\"M4 11a9 9 0 0 1 9 9\"/><path d=\"M4 7a13 13 0 0 1 13 13\"/><path d=\"M4 20l5-5\"/><circle cx=\"10\" cy=\"14\" r=\"1.6\"/>") },
		{ TEXT("lock"), TEXT("<rect x=\"5\" y=\"11\" width=\"14\" height=\"9\" rx=\"1.6\"/><path d=\"M8 11V8a4 4 0 0 1 8 0v3\"/>") },
		{ TEXT("unlock"), TEXT("<rect x=\"5\" y=\"11\" width=\"14\" height=\"9\" rx=\"1.6\"/><path d=\"M8 11V8a4 4 0 0 1 7.5-2\"/>") },
		{ TEXT("mode"), TEXT("<rect x=\"7\" y=\"7\" width=\"10\" height=\"10\" rx=\"1.6\"/><path d=\"M10 3v4M14 3v4M10 17v4M14 17v4M3 10h4M3 14h4M17 10h4M17 14h4\"/>") },
		{ TEXT("fan"), TEXT("<circle cx=\"12\" cy=\"12\" r=\"2\"/><path d=\"M12 10c0-4 2.5-6.5 5-5.5s.5 5.5-5 5.5M14 12c4 0 6.5 2.5 5.5 5s-5.5.5-5.5-5M12 14c0 4-2.5 6.5-5 5.5s-.5-5.5 5-5.5M10 12c-4 0-6.5-2.5-5.5-5s5.5-.5 5.5 5\"/>") },
		{ TEXT("takeoff"), TEXT("<path d=\"M3 20h18\"/><path d=\"M3.5 14.5l3.5-.8 3.8 1.9 7.6-4.5c1.2-.7 2.7-.2 2.6 1.2l-.1.5-9.3 5.3-8.1-3.6z\"/><path d=\"M8 9l3 1.5\"/>") },
		{ TEXT("land"), TEXT("<path d=\"M3 20h18\"/><path d=\"M3.5 9.5l3.6 1 2.4 3.6 8.6 2.1c1.4.3 2.6-.7 2.1-1.9l-.3-.5-10.4-2.5-6-1.8z\"/><path d=\"M9 6l1 3.5\"/>") },
		{ TEXT("rtl"), TEXT("<path d=\"M4 11l8-7 8 7\"/><path d=\"M6 10v10h12V10\"/><path d=\"M12 20v-4\"/><path d=\"M9.5 14.5 12 12l2.5 2.5\"/>") },
		{ TEXT("home"), TEXT("<path d=\"M4 11l8-7 8 7\"/><path d=\"M6 10v10h12V10\"/><path d=\"M10 20v-5h4v5\"/>") },
		{ TEXT("goto"), TEXT("<circle cx=\"12\" cy=\"12\" r=\"6\"/><path d=\"M12 2v4M12 18v4M2 12h4M18 12h4\"/><circle cx=\"12\" cy=\"12\" r=\"1.5\" fill=\"currentColor\"/>") },
		{ TEXT("upload"), TEXT("<path d=\"M12 16V5M12 5 8 9M12 5l4 4\"/><path d=\"M4 15v3a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-3\"/>") },
		{ TEXT("play"), TEXT("<path d=\"M7 5v14l11-7z\" fill=\"currentColor\"/>") },
		{ TEXT("pause"), TEXT("<rect x=\"6\" y=\"5\" width=\"4\" height=\"14\" rx=\"1\" fill=\"currentColor\"/><rect x=\"14\" y=\"5\" width=\"4\" height=\"14\" rx=\"1\" fill=\"currentColor\"/>") },
		{ TEXT("resume"), TEXT("<path d=\"M5 5v14l8-7z\" fill=\"currentColor\"/><path d=\"M15 5v14\" stroke-width=\"2.4\"/>") },
		{ TEXT("cancel"), TEXT("<path d=\"M6 6l12 12M18 6 6 18\"/>") },
		{ TEXT("reset"), TEXT("<path d=\"M4 12a8 8 0 1 0 2.3-5.7\"/><path d=\"M4 4v5h5\"/>") },
		{ TEXT("layout"), TEXT("<rect x=\"3\" y=\"4\" width=\"18\" height=\"16\" rx=\"1.6\"/><path d=\"M3 10h18M9 10v10M15 10v10\"/>") },
		{ TEXT("up"), TEXT("<path d=\"M6 15l6-6 6 6\"/>") },
		{ TEXT("down"), TEXT("<path d=\"M6 9l6 6 6-6\"/>") },
		{ TEXT("chat"), TEXT("<path d=\"M4 5h16v10H9l-5 4z\"/>") },
		};
		static const FString Empty;
		const FString* S = M.Find(Name);
		return S ? *S : Empty;
	}
}
