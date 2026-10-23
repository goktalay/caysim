// Küçük SVG simge çizici: web panel simgeleri (24×24 viewBox, stroke-width 1.9, yuvarlak uç) birebir çizilsin diye
// path (M L H V C S Q A Z; büyük/küçük harf), rect (rx), circle, line, polyline öğelerini çoklu çizgiye çevirir (önbellekli) ve Slate'te çizer.
#pragma once

#include "CoreMinimal.h"

class FSlateWindowElementList;
struct FGeometry;

namespace SmvSvg
{
	/** Body: <svg> içindeki öğeler (ör. '<path d="M4 16a8 8 0 0 1 16 0"/>'); Pos sol üst, Size kenar (px) */
	SIMUMAV_API void Draw(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FString& Body, const FVector2D& Pos, float Size, const FLinearColor& Color, float StrokeW = 1.9f);
	/** Ad → gövde (web flightsim_panel.js ICO tablosu) */
	SIMUMAV_API const FString& Icon(const FString& Name);
}
