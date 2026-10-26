// SIMUMAV HUD katmanı — CAYSIM askeri HUD'unun (UCaySimHudWidget) üstünde, NativePaint ile kodla çizilir (UMG tasarım dosyası yok):
// uçuş uyarıları (MASTER WARNING / CAUTION şeridi), yarış/it dalaşı paneli, hedef kilit nişangâhı, PiP (gimbal kamerası), FPV OSD teması,
// çarpışma / geri sayım / duraklatma yazıları. Bilgi kartı, dron paneli, güç kadranları, ileti akışı ve sanal kumanda: SSmvFlightUi.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "SmvHudOverlay.generated.h"

class UTextureRenderTarget2D;

/** Her kare GameMode'un doldurduğu çizim durumu. */
struct FSmvHudState
{
	FString AccountLine;       // "Misafir — Ali" / "simumav.com — ali@…"
	FString BackendLine;       // "Yerel SITL · 2×" / "Bulut · 1×"
	FString MpLine;
	FString ModeLine;          // "LOITER · ARMED"
	TArray<TPair<FString, bool>> Alerts;   // (metin, warning?) öncelik sırasıyla
	bool bAlertUnacked = false;
	bool bFuel = false; float FuelPct = 0; FString FuelText;
	bool bElec = false; float ElecPct = 0; FString ElecText;
	// yarış
	bool bRace = false; FString RaceTitle; FString RaceLine1; FString RaceLine2; TArray<FString> Standings;
	bool bDogfight = false; float HoldFrac = 0; bool bInCone = false; FString DfLine;
	FString Countdown;         // "3", "2", "1", "BAŞLA!"
	// fırlatma (web launchHud / handHud): büyük geri sayım ("3", "2", "1", "AT!") ve yöntem satırı (rampa açısı, araç hızı / bırakma hızı …)
	FString LaunchBig; FString LaunchLine;
	// hedef kilidi
	bool bTargetOnScreen = false; FVector2D TargetPos = FVector2D::ZeroVector; bool bLocked = false; FString TargetLabel;
	// olay şeridi (en yeni sonda)
	TArray<TPair<FString, int32>> Events;
	// kumanda
	TArray<int32> Rc;
	bool bShowSticks = true;
	// PiP
	UTextureRenderTarget2D* Pip = nullptr;
	// FPV OSD teması (CAYSIM HUD yerine)
	bool bOsd = false; FString OsdLeft, OsdRight, OsdCenter;
	FString Toast; int32 ToastSeverity = 6;
	bool bCrashed = false;
	// fizik yakınlık uyarısı (yalnız gösterge): "ENGEL 3,2 m · SAĞ ÖN"; 1 dikkat (amber), 2 uyarı (kırmızı)
	FString ProxLine; int32 ProxLevel = 0;
	FString Paused;            // boş değilse duraklatma ipucu
};

UCLASS()
class SIMUMAV_API USmvHudOverlay : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetState(const FSmvHudState& S);

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 Layer,
		const FWidgetStyle& Style, bool bParentEnabled) const override;

private:
	FSmvHudState State;
	FSlateBrush PipBrush;
	FSlateBrush White;
	double BlinkT = 0;
};
