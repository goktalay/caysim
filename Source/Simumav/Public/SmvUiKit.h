// Simumav arayüz takımı — tüm menü / uçuş ekranlarının ortak görsel dili (SIMUMAV web theme.css: koyu cam kartlar, neon mavi/turkuaz
// vurgular, yumuşak animasyonlar). KODLA çizilir (Slate; UMG/.uasset yok). Bileşenler fare, klavye ve gamepad ile tam gezinilir:
// odak halkası yalnız klavye/gamepad gezinmesinde görünür; A/Enter = seç, B/Esc = geri; kaydırıcı ve seçicilerde sol/sağ değeri değiştirir.
// Ses geri bildirimi: FSmvUiSound (GameMode prosedürel ton sentezine bağlar — ses varlığı gerekmez).
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Styling/SlateBrush.h"
#include "Fonts/SlateFontInfo.h"
#include "Input/Reply.h"
#include "Input/NavigationReply.h"
#include "InputCoreTypes.h"

class FSlateWindowElementList;
struct FGeometry;

/** Vektör simgeler (çizgi sanatı; yazı tipine bağlı değil). */
enum class ESmvIcon : uint8
{
	None, Play, Drone, Pilot, Route, Mission, Match, Trophy, Book, Gear, User, Power, Back, Help, Globe, Plus, Save, Trash, Star,
	Plane, Heli, Rover, Map, Sun, Moon, Cloud, Rain, Snow, Fog, Wind, Gamepad, Keyboard, Check, Next, Prev, Pause, Home, Flag,
	Camera, Volume, Monitor, Info, Bolt, Shield, Fpv, Vtol, Rocket, Sunset, Stick, Replay, Ghost
};

enum class ESmvBtn : uint8
{
	Normal,   // cam düğme
	Primary,  // vurgu degradesi + parıltı
	Ghost,    // saydam, yalnız kenar
	Tile,     // büyük kart: simge üstte, başlık + alt yazı
	Danger,
	Hero      // ana menü "UÇ" kartı: büyük, animasyonlu parıltı
};

/** Arayüz sesleri (FSmvUiSound). */
enum class ESmvSnd : uint8 { Hover = 5, Click = 6, Back = 7, Open = 8, Toggle = 9, Error = 10 };

struct SIMUMAV_API FSmvUiSound
{
	static void Play(ESmvSnd S);
	static void SetSink(TFunction<void(int32)> InSink);
};

/** Degrade durağı (konum 0..1, renk) */
struct FSmvStop { float P = 0.f; FLinearColor C = FLinearColor::Transparent; };
/** İpucu çubuğu öğesi (tuş adı, etiket); gamepad tuşları "pad:A" biçiminde */
struct FSmvHint { FString Key; FText Label; };

namespace SmvUi
{
	/** sRGB onaltılık → doğrusal renk */
	SIMUMAV_API FLinearColor Hex(uint32 Rgb, float A = 1.f);
	SIMUMAV_API const FLinearColor& Bg();
	SIMUMAV_API const FLinearColor& Bg2();
	SIMUMAV_API const FLinearColor& Card();
	SIMUMAV_API const FLinearColor& CardHover();
	SIMUMAV_API const FLinearColor& Edge();
	SIMUMAV_API const FLinearColor& Fg();
	SIMUMAV_API const FLinearColor& Dim();
	SIMUMAV_API const FLinearColor& Mute();
	SIMUMAV_API const FLinearColor& Accent();    // #38bdf8 neon mavi
	SIMUMAV_API const FLinearColor& Accent2();   // #a78bfa mor
	SIMUMAV_API const FLinearColor& Accent3();   // #34d399 turkuaz/yeşil
	SIMUMAV_API const FLinearColor& Warn();
	SIMUMAV_API const FLinearColor& Danger();

	/** Yazı tipi: Weight "Regular" | "Bold" | "Light" | "Black" | "BoldCondensed" | "Mono"; Spacing harf aralığı (1/1000 em) */
	SIMUMAV_API FSlateFontInfo Font(int32 Size, const ANSICHAR* Weight = "Regular", int32 Spacing = 0);
	/** Ortak fırçalar (ömür boyu yaşar) */
	SIMUMAV_API const FSlateBrush* White();
	SIMUMAV_API const FSlateBrush* Round(float Radius);          // beyaz dolgulu yuvarlak köşe (renk = tint)
	SIMUMAV_API const FSlateBrush* RoundOutline(float Radius, float Width);   // yalnız kenar
	SIMUMAV_API const FSlateBrush* Circle();
	SIMUMAV_API const FSlateBrush* GlassCard();                  // cam kart (dolgu + ince kenar)

	/** Hareket azaltma (Ayarlar → Erişilebilirlik): animasyonlar durur */
	SIMUMAV_API bool& ReducedMotion();
	/** Açık tema (Ayarlar → Genel → Tema: koyu / açık / otomatik — otomatik: 07–19 arası açık) */
	SIMUMAV_API bool& Light();
	/** Tema adını uygula: dark / light / auto (yerel saat 07–19 açık) */
	SIMUMAV_API void ApplyTheme(const FString& Theme);
	/** Son girdi gamepad mi (ipucu çubuğu gamepad simgeleri gösterir) */
	SIMUMAV_API bool& PadActive();
	/** Zaman (sn; hareket azaltmada sabit) */
	SIMUMAV_API double Now();
	/** Giriş animasyonu (soluk başlama / kayma) yalnız ekran değişince: aynı ekranda yeniden kurulumda (seçim → Refresh) düğmeler
	 *  animasyonu baştan oynatıp ekranı titretiyordu (kullanıcı, 2026-10-29). Kabuk ekran değişince true, aksi hâlde false yapar. */
	SIMUMAV_API bool& IntroOn();

	// ---- çizim yardımcıları (OnPaint) ----
	SIMUMAV_API void DrBox(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const FVector2D& P, const FVector2D& S, const FLinearColor& C, const FSlateBrush* B = nullptr);
	SIMUMAV_API void DrLine(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const TArray<FVector2D>& Pts, const FLinearColor& C, float Thick);
	SIMUMAV_API void DrSeg(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const FVector2D& A, const FVector2D& B, const FLinearColor& C, float Thick);
	SIMUMAV_API void DrArc(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const FVector2D& C, float R, float A0Deg, float A1Deg, const FLinearColor& Col, float Thick);
	SIMUMAV_API void DrText(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const FString& S, FVector2D Pos, const FSlateFontInfo& F, const FLinearColor& C, int32 Align = -1);
	/** Yumuşak parıltı (iç içe saydam daireler) */
	SIMUMAV_API void DrGlow(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const FVector2D& C, float R, const FLinearColor& Col);
	/** Dikey (bVertical) ya da yatay degrade; konumlar 0..1 */
	SIMUMAV_API void DrGradient(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const FVector2D& P, const FVector2D& S, const TArray<FSmvStop>& Stops, bool bVertical);
	/** Simge (kare alan: merkez C, kenar Sz) */
	SIMUMAV_API void DrIcon(FSlateWindowElementList& Out, int32 L, const FGeometry& G, ESmvIcon I, const FVector2D& C, float Sz, const FLinearColor& Col, float Thick = 2.f);
	/** Araç tipi → simge */
	SIMUMAV_API ESmvIcon IconForType(const FString& Type);

	/** Başlık: büyük harf, harf aralıklı */
	/** Dile duyarlı büyük harf (Türkçede i → İ, ı → I; ğ ş ö ü ç da çevrilir) — FString::ToUpper yalnız ASCII'yi çevirir */
	SIMUMAV_API FString SmvUpper(const FString& In);
	SIMUMAV_API TSharedRef<SWidget> Title(const FText& T, int32 Size = 28, const FLinearColor& Color = FLinearColor::White);
	SIMUMAV_API TSharedRef<SWidget> Text(const TAttribute<FText>& T, int32 Size = 12, const ANSICHAR* Weight = "Regular", const FLinearColor& Color = FLinearColor(0.88f, 0.92f, 0.97f), bool bWrap = true);
	/** Bölüm başlığı: vurgu çizgisi + küçük büyük harf metin */
	SIMUMAV_API TSharedRef<SWidget> Section(const FText& T, const FLinearColor& Color = FLinearColor(0.22f, 0.74f, 0.97f));
	/** Cam kart */
	SIMUMAV_API TSharedRef<SWidget> Glass(TSharedRef<SWidget> Inner, FMargin Pad = FMargin(18.f), float Alpha = 1.f);
	/** Ayar satırı: etiket (+ ipucu) solda, düzenleyici sağda */
	SIMUMAV_API TSharedRef<SWidget> Row(const FText& Label, TSharedRef<SWidget> Editor, const FText& Hint = FText::GetEmpty());
	/** Simge bileşeni */
	SIMUMAV_API TSharedRef<SWidget> Icon(ESmvIcon I, float Size, const FLinearColor& Color);
	/** Tuş başlığı (klavye tuşu ya da gamepad düğmesi) */
	SIMUMAV_API TSharedRef<SWidget> KeyCap(const FString& Key);
	/** İpucu çubuğu: (tuş, etiket) çiftleri; gamepad etkinse PadKeys kullanılır */
	SIMUMAV_API TSharedRef<SWidget> HintBar(const TArray<FSmvHint>& KbKeys, const TArray<FSmvHint>& PadKeys);
	/** FKey → kısa görünen ad ("Space", "F1", "A" …) */
	SIMUMAV_API FString KeyLabel(const struct FKey& K);

	/** Kısayol yardımı (F1): klavye + gamepad, uçuşta ek bölümler; StickMode etiketleri */
	SIMUMAV_API TSharedRef<SWidget> HelpOverlay(TFunction<void()> OnClose, bool bFlight, int32 StickMode);
	/** Kumanda modu (1–4) eşlemesi: [solX, solY, sağX, sağY] eksen adları (yaw|thr|pitch|roll) */
	SIMUMAV_API void StickMap(int32 Mode, FString Out[4]);
	/** Kumanda modu eksen ipucu, tuş/çubuk simgeleriyle (klavye: W/S A/D ↑↓ ←→; gamepad: LS / RS) — etkin cihaza göre anında değişir */
	SIMUMAV_API TSharedRef<SWidget> StickKeyHintWidget(int32 Mode, int32 Size, const FLinearColor& Color);
	/** Eksen adı → dil anahtarı metni */
	SIMUMAV_API FText AxisName(const FString& Axis);
	/** Mod için klavye ipucu: "W/S gaz · A/D yaw · ↑↓ pitch · ←→ roll" */
	SIMUMAV_API FString StickKeyHint(int32 Mode);
	/** Kumanda modu çizimi (iki çubuk + eksen okları) */
	SIMUMAV_API void DrStickMode(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const FVector2D& P, const FVector2D& S, int32 Mode, const FLinearColor& Col);
}

/** Serbest çizim yaprak bileşeni: her kare Painter çağrılır; isteğe bağlı fare sürükleme / üzerinde gezinme. */
class SIMUMAV_API SSmvCanvas : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSmvCanvas) : _Size(FVector2D(100, 100)) {}
		SLATE_ARGUMENT(FVector2D, Size)
	SLATE_END_ARGS()

	TFunction<void(const FGeometry&, FSlateWindowElementList&, int32)> Painter;
	/** Faz 0 bas, 1 sürükle, 2 bırak */
	TFunction<void(const FGeometry&, const FVector2D&, int32)> OnDrag;
	TFunction<void(const FVector2D&)> OnHover;
	int32 LayersUsed = 24;

	void Construct(const FArguments& A) { Size = A._Size; }
	void SetSize(const FVector2D& S) { Size = S; }
	virtual FVector2D ComputeDesiredSize(float) const override { return Size; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 Layer,
		const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E) override;
	virtual FReply OnMouseMove(const FGeometry& G, const FPointerEvent& E) override;
	virtual FReply OnMouseButtonUp(const FGeometry& G, const FPointerEvent& E) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& E) override;

private:
	FVector2D Size = FVector2D(100, 100);
};

/** Animasyonlu menü arka planı: degrade, yumuşak ışık lekeleri, kayan perspektif ızgara, parçacıklar, süzülen dron siluetleri,
 *  tarama çizgisi, kenar kararması. Opacity < 1 → arkadaki 3B menü sahnesi görünür. */
class SIMUMAV_API SSmvBackdrop : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSmvBackdrop) : _Opacity(1.f), _Drones(true), _Grid(true), _Tint(FLinearColor::Transparent), _Scene(0), _Cinematic(false) {}
		SLATE_ATTRIBUTE(float, Opacity)
		/** Sinematik kip (ana menü + canlı 3B sahne): yalnız sol yarıda koyu degrade + birkaç ışık zerresi; sahne net görünür */
		SLATE_ATTRIBUTE(bool, Cinematic)
		/** Bölüme özel sahne: 0 genel, 1 hangar, 2 brifing (mürettebat), 3 küre (parkur), 4 tarla (görev), 5 gökyüzü (uç) */
		SLATE_ATTRIBUTE(int32, Scene)
		SLATE_ARGUMENT(bool, Drones)
		SLATE_ARGUMENT(bool, Grid)
		/** Mevsim / bayram vurgu rengi (alfa 0 = yok) */
		SLATE_ATTRIBUTE(FLinearColor, Tint)
	SLATE_END_ARGS()

	void Construct(const FArguments& A);
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1920, 1080); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 Layer,
		const FWidgetStyle& Style, bool bParentEnabled) const override;

private:
	TAttribute<float> Opacity;
	TAttribute<int32> Scene;
	bool bDrones = true, bGrid = true;
	void PaintScene(int32 S, FSlateWindowElementList& Out, int32 L, const FGeometry& G, double T) const;
		TAttribute<FLinearColor> Tint;
		TAttribute<bool> Cinematic;
	};

/** Düğme / kart: fare üzerindeyken parlar ve hafifçe büyür, basınca içe çöker; odak halkası (gezinme), ses geri bildirimi. */
class SIMUMAV_API SSmvButton : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSmvButton) : _Kind(ESmvBtn::Normal), _Icon(ESmvIcon::None), _MinWidth(0.f), _MinHeight(0.f), _Selected(false),
		_Accent(FLinearColor(0.22f, 0.74f, 0.97f)), _FontSize(0), _LeftAlign(false), _IntroDelay(0.f) {}
		SLATE_ATTRIBUTE(FText, Text)
		SLATE_ATTRIBUTE(FText, SubText)
		SLATE_ARGUMENT(ESmvBtn, Kind)
		SLATE_ARGUMENT(ESmvIcon, Icon)
		SLATE_ARGUMENT(float, MinWidth)
		SLATE_ARGUMENT(float, MinHeight)
		SLATE_ATTRIBUTE(bool, Selected)
		SLATE_ARGUMENT(FLinearColor, Accent)
		SLATE_ARGUMENT(int32, FontSize)
		/** İçerik sola yaslı ve genişliği doldurur (liste öğeleri) */
		SLATE_ARGUMENT(bool, LeftAlign)
		/** Giriş animasyonu gecikmesi (sn): kartlar sırayla belirir ve aşağıdan kayarak yerine oturur */
		SLATE_ARGUMENT(float, IntroDelay)
		SLATE_ARGUMENT(TFunction<void()>, OnClick)
		/** Kart içine özel içerik (Tile'da simgenin yerine; ör. 3B önizleme) */
		SLATE_NAMED_SLOT(FArguments, Extra)
	SLATE_END_ARGS()

	void Construct(const FArguments& A);
	virtual void Tick(const FGeometry& G, const double Now, const float Dt) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 Layer,
		const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnFocusReceived(const FGeometry& G, const FFocusEvent& E) override;
	virtual void OnFocusLost(const FFocusEvent& E) override;
	virtual void OnMouseEnter(const FGeometry& G, const FPointerEvent& E) override;
	virtual void OnMouseLeave(const FPointerEvent& E) override;
	virtual FReply OnMouseMove(const FGeometry& G, const FPointerEvent& E) override;
	virtual FReply OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E) override;
	virtual FReply OnMouseButtonUp(const FGeometry& G, const FPointerEvent& E) override;
	virtual FReply OnKeyDown(const FGeometry& G, const FKeyEvent& E) override;
	virtual FReply OnKeyUp(const FGeometry& G, const FKeyEvent& E) override;
	void Click();

private:
	ESmvBtn Kind = ESmvBtn::Normal;
	ESmvIcon IconId = ESmvIcon::None;
	TAttribute<bool> Selected;
	FLinearColor AccentCol;
	TFunction<void()> OnClick;
	bool bHover = false, bPressed = false, bNavFocus = false, bCauseNav = false;
	float HoverA = 0.f, PressA = 0.f, FocusA = 0.f;
	double BornAt = 0;
	bool bSlideIn = false;
	/** Fare konumu (kart içinde −1..1) — 3B eğilme ve parlama */
	FVector2D MouseN = FVector2D::ZeroVector, TiltN = FVector2D::ZeroVector;
};

/** Aç/kapa anahtarı */
class SIMUMAV_API SSmvToggle : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSmvToggle) {}
		SLATE_ATTRIBUTE(bool, Value)
		SLATE_ARGUMENT(TFunction<void(bool)>, OnChanged)
	SLATE_END_ARGS()
	void Construct(const FArguments& A) { Value = A._Value; OnChanged = A._OnChanged; }
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(56, 30); }
	virtual void Tick(const FGeometry& G, const double Now, const float Dt) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 Layer,
		const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnFocusReceived(const FGeometry& G, const FFocusEvent& E) override;
	virtual void OnFocusLost(const FFocusEvent& E) override { bNavFocus = false; }
	virtual FReply OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E) override;
	virtual FReply OnKeyDown(const FGeometry& G, const FKeyEvent& E) override;
private:
	TAttribute<bool> Value;
	TFunction<void(bool)> OnChanged;
	float Knob = -1.f;
	bool bNavFocus = false;
	void Flip();
};

/** Kaydırıcı: sürükle, tekerlek; odaktayken sol/sağ adım (Shift ×10) */
class SIMUMAV_API SSmvSlider : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSmvSlider) : _Min(0.0), _Max(1.0), _Step(0.05), _Width(260.f) {}
		SLATE_ATTRIBUTE(double, Value)
		SLATE_ARGUMENT(double, Min)
		SLATE_ARGUMENT(double, Max)
		SLATE_ARGUMENT(double, Step)
		SLATE_ARGUMENT(float, Width)
		/** Değer metni (yoksa %.2f) */
		SLATE_ARGUMENT(TFunction<FString(double)>, Format)
		SLATE_ARGUMENT(TFunction<void(double)>, OnChanged)
	SLATE_END_ARGS()
	void Construct(const FArguments& A);
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Width, 30); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 Layer,
		const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnFocusReceived(const FGeometry& G, const FFocusEvent& E) override;
	virtual void OnFocusLost(const FFocusEvent& E) override { bNavFocus = false; }
	virtual FReply OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E) override;
	virtual FReply OnMouseMove(const FGeometry& G, const FPointerEvent& E) override;
	virtual FReply OnMouseButtonUp(const FGeometry& G, const FPointerEvent& E) override;
	virtual FReply OnMouseWheel(const FGeometry& G, const FPointerEvent& E) override;
	virtual FNavigationReply OnNavigation(const FGeometry& G, const FNavigationEvent& E) override;
	virtual FReply OnKeyDown(const FGeometry& G, const FKeyEvent& E) override;
private:
	TAttribute<double> Value;
	double Min = 0, Max = 1, Step = 0.05;
	float Width = 260.f;
	TFunction<FString(double)> Format;
	TFunction<void(double)> OnChanged;
	bool bNavFocus = false;
	void SetFromX(const FGeometry& G, float X);
	void Nudge(double Steps);
};

/** Seçici: ‹ değer › — tıkla ya da odaktayken sol/sağ */
class SIMUMAV_API SSmvCycler : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSmvCycler) : _Width(260.f) {}
		SLATE_ARGUMENT(TArray<FText>, Labels)
		SLATE_ATTRIBUTE(int32, Index)
		SLATE_ARGUMENT(float, Width)
		SLATE_ARGUMENT(TFunction<void(int32)>, OnChanged)
	SLATE_END_ARGS()
	void Construct(const FArguments& A) { Labels = A._Labels; Index = A._Index; Width = A._Width; OnChanged = A._OnChanged; }
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Width, 34); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 Layer,
		const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnFocusReceived(const FGeometry& G, const FFocusEvent& E) override;
	virtual void OnFocusLost(const FFocusEvent& E) override { bNavFocus = false; }
	virtual FReply OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E) override;
	virtual FReply OnMouseWheel(const FGeometry& G, const FPointerEvent& E) override;
	virtual FNavigationReply OnNavigation(const FGeometry& G, const FNavigationEvent& E) override;
	virtual FReply OnKeyDown(const FGeometry& G, const FKeyEvent& E) override;
private:
	TArray<FText> Labels;
	TAttribute<int32> Index;
	float Width = 260.f;
	TFunction<void(int32)> OnChanged;
	bool bNavFocus = false;
	void Step(int32 D);
};

/** Tuş yakalayıcı (tuş atama): tıkla/A → "bir tuşa basın…"; Esc iptal, Backspace temizlemez (atanabilir tuş) */
class SIMUMAV_API SSmvKeyCapture : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSmvKeyCapture) : _bPad(false) {}
		SLATE_ATTRIBUTE(FString, Current)
		SLATE_ARGUMENT(bool, bPad)
		SLATE_ARGUMENT(TFunction<void(const struct FKey&)>, OnCaptured)
	SLATE_END_ARGS()
	void Construct(const FArguments& A) { Current = A._Current; bPad = A._bPad; OnCaptured = A._OnCaptured; }
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(150, 32); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 Layer,
		const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnFocusReceived(const FGeometry& G, const FFocusEvent& E) override;
	virtual void OnFocusLost(const FFocusEvent& E) override { bNavFocus = false; bListening = false; }
	virtual FReply OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E) override;
	virtual FReply OnKeyDown(const FGeometry& G, const FKeyEvent& E) override;
	virtual FNavigationReply OnNavigation(const FGeometry& G, const FNavigationEvent& E) override;
private:
	TAttribute<FString> Current;
	bool bPad = false, bListening = false, bNavFocus = false;
	double ListenAt = 0;
	TFunction<void(const struct FKey&)> OnCaptured;
};

/** Radyal tekerlek menüsü (uçuş modu seçimi): fare açısı ya da gamepad sağ çubuk vurgular; tık / A / F seçer. */
class SIMUMAV_API SSmvRadial : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSmvRadial) : _Radius(210.f) {}
		SLATE_ATTRIBUTE(TArray<FString>, Items)
		SLATE_ATTRIBUTE(FString, Current)
		SLATE_ARGUMENT(float, Radius)
		SLATE_ARGUMENT(TFunction<FVector2D()>, Stick)
		SLATE_ARGUMENT(TFunction<void(int32)>, OnPick)
		SLATE_ATTRIBUTE(FText, Title)
	SLATE_END_ARGS()
	void Construct(const FArguments& A);
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Radius * 2.f + 140.f, Radius * 2.f + 120.f); }
	virtual void Tick(const FGeometry& G, const double Now, const float Dt) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 Layer,
		const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FReply OnMouseMove(const FGeometry& G, const FPointerEvent& E) override;
	virtual FReply OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E) override;
	int32 GetHighlighted() const { return Hi; }
	/** Vurgulananı seç (A / F); vurgu yoksa false */
	bool Confirm();
	void ResetAnim() { OpenA = 0.f; Hi = -1; }
private:
	TAttribute<TArray<FString>> Items;
	TAttribute<FString> Current;
	TAttribute<FText> TitleText;
	float Radius = 210.f;
	TFunction<FVector2D()> Stick;
	TFunction<void(int32)> OnPick;
	int32 Hi = -1, LastHi = -1;
	float OpenA = 0.f;
	int32 IndexAt(const FVector2D& FromCenter) const;
};

/** İlk açılış tanıtımı: sayfalı kart dizisi (simge, başlık, metin), ileri / geri / atla; bitince OnDone. */
class SIMUMAV_API SSmvTutorial : public SCompoundWidget
{
public:
	struct FPage { ESmvIcon Icon = ESmvIcon::Info; FText Title, Body; };
	SLATE_BEGIN_ARGS(SSmvTutorial) {}
		SLATE_ARGUMENT(TArray<FPage>, Pages)
		SLATE_ARGUMENT(TFunction<void()>, OnDone)
	SLATE_END_ARGS()
	void Construct(const FArguments& A);
	virtual FReply OnKeyDown(const FGeometry& G, const FKeyEvent& E) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	void Go(int32 D);
	TSharedPtr<SWidget> FocusTarget() const { return NextBtn; }
private:
	TArray<FPage> Pages;
	int32 Page = 0;
	TFunction<void()> OnDone;
	TSharedPtr<class SBox> Body;
	TSharedPtr<SWidget> NextBtn;
	void Rebuild();
};

/** Düz renkli parça malzemesi: /Game/Simumav/Materials/M_SmvPbr (BaseColor; editör betiği üretir) varsa o, yoksa verilen yedek
 *  (BasicShapeMaterial'da "Color" parametresi yok → beyaz kalıyordu). Rengi her iki parametreye de yazın: SmvSetSolidColor. */
class UMaterialInterface;
class UMaterialInstanceDynamic;
SIMUMAV_API UMaterialInterface* SmvSolidMat(UMaterialInterface* Fallback);
SIMUMAV_API void SmvSetSolidColor(UMaterialInstanceDynamic* M, const FLinearColor& C);
