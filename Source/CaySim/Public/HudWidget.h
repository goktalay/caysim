// Askeri tarz FPV HUD — UUserWidget üzerinde Slate çizimi (pitch merdiveni, heading şeridi, hız/irtifa bantları, uyarılar)
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CaySimTypes.h"
#include "HudWidget.generated.h"

class ACaySimDroneActor;

/** HUD rengi (Shift+H ile döner: yeşil fosfor → beyaz → amber). */
UENUM(BlueprintType)
enum class ECaySimHudColor : uint8
{
	Green, White, Amber
};

/** HUD'un dış dünyadan aldığı bağlam (web `ctx` karşılığı). */
USTRUCT(BlueprintType)
struct CAYSIM_API FCaySimHudContext
{
	GENERATED_BODY()
	/** Render edilen enterpolasyonlu yönelim (rad) */
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") float RollRad = 0.f;
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") float PitchRad = 0.f;
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") float YawRad = 0.f;
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") float GimbalDeg = 0.f;
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") float FovDeg = 78.f;
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") bool bCollision = false;
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") bool bReplay = false;
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") bool bSitlMode = true;
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") bool bGcsEverConnected = false;
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") float WindMs = 0.f;
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") float WindDir = 0.f;
	/** Oturum başlangıcı (Unix sn) → T+; kayıtta kayıt saati */
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") double T0 = 0.0;
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") double WallClock = 0.0;
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") float LatencyMs = 0.f;
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") float TelemetryHz = 0.f;
};

/**
 * HUD taslağı. Tüm çizim NativePaint içinde FSlateDrawElement ile yapılır (UMG tasarım dosyası gerekmez);
 * Blueprint alt sınıfı yalnız yazı tipi/kalınlık ayarlar. Her kare `Update(tele)` + `SetContext(ctx)` çağrılır.
 * Öğeler: suni ufuk + pitch merdiveni (±90°, 5°; roll ile döner; zenit/nadir), roll yayı, boresight, uçuş yolu vektörü,
 * heading şeridi (ev H, rüzgâr), sol hava hızı bandı, sağ irtifa bandı (AGL/MSL/R), dikey hız, sol alt (mod/ARM/G/THR/BAT/GPS),
 * sağ alt (UTC, T+, lat/lon, HOME, GMB), üst sağ (REC/CAM/FOV), köşe çerçeveleri, uyarılar.
 */
UCLASS(Blueprintable)
class CAYSIM_API UCaySimHudWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|HUD") void Update(const FCaySimTelemetry& Tele) { Latest = Tele; bHasTele = true; }
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|HUD") void SetContext(const FCaySimHudContext& InCtx) { Ctx = InCtx; }
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|HUD") void SetHudColor(ECaySimHudColor C) { Color = C; }
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|HUD") void CycleColor();
	UFUNCTION(BlueprintPure, Category = "CAYSIM|HUD") ECaySimHudColor GetHudColor() const { return Color; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|HUD") float LineThickness = 1.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|HUD") int32 FontSize = 13;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|HUD") bool bShowFlightPathVector = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|HUD") bool bShowCornerFrames = true;

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	FCaySimTelemetry Latest;
	FCaySimHudContext Ctx;
	ECaySimHudColor Color = ECaySimHudColor::Green;
	bool bHasTele = false;

	struct FPainter; // uygulama ayrıntısı (HudWidget.cpp)
	FLinearColor MainColor() const;
};
