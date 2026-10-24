// Uydu haritası bileşeni (web gmap.js / satmap.js eşi): Esri World Imagery XYZ karoları (Web Mercator, 256 px), sürükle-kaydır, tekerlek
// yakınlaştırma (3–19), tıkla → konum seç; halka düzenleyici kipinde tıkla → halka ekle, halkayı sürükle → taşı, sağ tık → sil.
// Ev konumu (camgöbeği), parkur yarıçapı ve numaralı halkalar (pembe; yarıçap ölçekli) çizilir. Karolar bellekte önbelleklenir (en çok 300).
// Atıf (Esri, Maxar, Earthstar Geographics) sağ altta yazılıdır.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "SimumavTypes.h"

class SIMUMAV_API SSmvTileMap : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSmvTileMap) : _Size(FVector2D(460, 300)), _Lat(nullptr), _Lon(nullptr), _Gates(nullptr), _RadiusM(nullptr), _RingMode(false) {}
		SLATE_ARGUMENT(FVector2D, Size)
		SLATE_ARGUMENT(double*, Lat)
		SLATE_ARGUMENT(double*, Lon)
		SLATE_ARGUMENT(TArray<FSmvGate>*, Gates)
		SLATE_ARGUMENT(double*, RadiusM)
		SLATE_ATTRIBUTE(bool, RingMode)
		SLATE_ARGUMENT(TFunction<void()>, OnChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& A);
	/** Haritayı bir noktaya ortala (arama sonucu) */
	void CenterOn(double Lat, double Lon, int32 Zoom = -1);
	/** Ad / adres araması (OpenStreetMap Nominatim) → ilk sonuç; Done(bOk, lat, lon, ad) */
	static void Search(const FString& Query, TFunction<void(bool, double, double, const FString&)> Done);
	/** Başka çizimlerin altına uydu karoları: ToScr (enlem, boylam) → yerel piksel; MetersPerPx ölçeği (görev haritası vb.) */
	static void PaintImagery(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, double CenterLat, double CenterLon, double MetersPerPx, TFunction<FVector2D(double, double)> ToScr, float Alpha = 0.85f);

	virtual FVector2D ComputeDesiredSize(float) const override { return Size; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E) override;
	virtual FReply OnMouseMove(const FGeometry& G, const FPointerEvent& E) override;
	virtual FReply OnMouseButtonUp(const FGeometry& G, const FPointerEvent& E) override;
	virtual FReply OnMouseWheel(const FGeometry& G, const FPointerEvent& E) override;
	virtual bool SupportsKeyboardFocus() const override { return false; }

private:
	FVector2D Size;
	double* Lat = nullptr;
	double* Lon = nullptr;
	TArray<FSmvGate>* Gates = nullptr;
	double* RadiusM = nullptr;
	TAttribute<bool> RingMode;
	TFunction<void()> OnChanged;
	// görünüm: merkez (dünya pikseli, z = Zoom) ve yakınlaştırma
	double Cx = 0, Cy = 0;
	int32 Zoom = 15;
	bool bInit = false;
	// sürükleme
	int32 DragGate = -1;
	bool bPan = false, bMoved = false;
	/** Konum işaretini sürükleme (yol haritası "Gerçek Dünya › ikonu kaydır") */
	bool bDragPin = false;
	FVector2D DownAt = FVector2D::ZeroVector, LastAt = FVector2D::ZeroVector;

	static FVector2D Project(double Lat, double Lon, int32 Z);
	static void Unproject(double X, double Y, int32 Z, double& Lat, double& Lon);
public:
	static FVector2D ProjectPub(double Lat, double Lon, int32 Z) { return Project(Lat, Lon, Z); }
private:
	FVector2D ToLocal(const FGeometry& G, double La, double Lo) const;
	void ToGeo(const FGeometry& G, const FVector2D& P, double& La, double& Lo) const;
	int32 HitGate(const FGeometry& G, const FVector2D& P) const;
	void EnsureInit();
};
