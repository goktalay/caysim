// Giriş cihazı algılama + tuş/düğme simgeleri (Fab "Joystix – Controllers & Button Overlays Pack").
//   - Son kullanılan cihaz kazanır: klavye/fare ↔ gamepad (Xbox / PlayStation / genel). Gamepad bağlanınca o aileye,
//     ayrılınca klavyeye anında geçilir (IPlatformInputDeviceMapper). Gamepad ailesi UInputDeviceSubsystem donanım
//     kimliğinden (XInput → Xbox, DualSense/DualShock → PlayStation, diğer → genel).
//   - Simge eşleme tablosu: SmvInputGlyphs.cpp JOYSTIX-ESLEME bloğu (mantıksal tuş → paketteki doku adı, aileye göre).
//     Paket yoksa ya da tuşun simgesi yoksa SmvUi::KeyCap metin rozeti kalır.
#pragma once

#include "CoreMinimal.h"

struct FSlateBrush;
class UTexture2D;

enum class ESmvGlyphStyle : uint8
{
	Keyboard,
	Xbox,
	PlayStation,
	Generic,
};

struct SIMUMAV_API FSmvInputGlyphs
{
	/** Girdi ön işlemcisini ve cihaz bağlantı olayını kaydeder (birden çok çağrı güvenli). */
	static void Init();
	/** Şu anki simge ailesi (gamepad etkin değilse Keyboard) */
	static ESmvGlyphStyle Style();
	/** Son kullanılan gamepad ailesi */
	static ESmvGlyphStyle PadStyle();
	/** Paket pakette var mı (en az bir simge çözüldü) */
	static bool HasPack();
	/** Tuş metni ("Enter", "F1", "Space", "W", "↑", "pad:A", "pad:LB", "pad:LS" …) için simge — "pad:" tuşları gamepad ailesine,
	 *  diğerleri klavye/fare simgelerine çözülür. Yoksa nullptr. */
	static const FSlateBrush* Brush(const FString& Key, float Height = 26.f);
	/** Gamepad ailesinin kumanda görseli (Mode 1–4 çubuk şeması için; yoksa nullptr) ve çubukların görsel üzerindeki konumu (0..1) */
	static const FSlateBrush* ControllerBrush(FVector2D& OutLeftStick, FVector2D& OutRightStick);
	/** Ad (günlük / ayarlar ekranı için) */
	static const TCHAR* StyleName(ESmvGlyphStyle S);
};
