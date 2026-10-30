#include "SmvInputGlyphs.h"
#include "SimumavModule.h"
#include "SmvUiKit.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/IInputProcessor.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "GameFramework/InputDeviceSubsystem.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Texture2D.h"
#include "Styling/SlateBrush.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	ESmvGlyphStyle GPadStyle = ESmvGlyphStyle::Xbox;
	bool GMouseUp = false;

	ESmvGlyphStyle SmvStyleOfDevice(FInputDeviceId Id)
	{
		FString S;
		if (UInputDeviceSubsystem* Sub = UInputDeviceSubsystem::Get())
		{
			const FHardwareDeviceIdentifier Hw = Sub->GetInputDeviceHardwareIdentifier(Id);
			S = Hw.InputClassName.ToString() + TEXT("|") + Hw.HardwareDeviceIdentifier.ToString();
		}
		const FString L = S.ToLower();
		if (L.Contains(TEXT("dualsense")) || L.Contains(TEXT("dualshock")) || L.Contains(TEXT("playstation")) || L.Contains(TEXT("ps4")) || L.Contains(TEXT("ps5")) || L.Contains(TEXT("sony")))
			return ESmvGlyphStyle::PlayStation;
		if (L.IsEmpty() || L.Contains(TEXT("xinput")) || L.Contains(TEXT("xbox")) || L.Contains(TEXT("gameinput")) || L == TEXT("|") || L.Contains(TEXT("none")))
			return ESmvGlyphStyle::Xbox;
		return ESmvGlyphStyle::Generic;
	}

	void SmvNoteDevice(bool bPad, FInputDeviceId Id)
	{
		if (bPad)
		{
			const ESmvGlyphStyle S = SmvStyleOfDevice(Id);
			if (S != GPadStyle) UE_LOG(LogSimumav, Log, TEXT("Giriş cihazı: %s"), FSmvInputGlyphs::StyleName(S));
			GPadStyle = S;
		}
		SmvUi::PadActive() = bPad;
	}

	/** Son kullanılan cihaz kazanır: Slate'e gelen tüm olayları görür, hiçbirini tüketmez */
	class FSmvGlyphInputProc : public IInputProcessor
	{
		TMap<FName, float> LastAxis;
	public:
		virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}
		virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& E) override { SmvNoteDevice(E.GetKey().IsGamepadKey(), E.GetInputDeviceId()); return false; }
		virtual bool HandleAnalogInputEvent(FSlateApplication&, const FAnalogInputEvent& E) override
		{
			// mutlak değer değil DEĞİŞİM: RC kumanda / gaz ekseni sabit uçta dururken sürekli olay gönderir → "gamepad etkin" fare ile
			// sürekli yer değiştirip odak halkası yanıp sönüyordu (hover titremesi + mavi çerçeve, 2026-10-30)
			if (!E.GetKey().IsGamepadKey()) return false;
			const float V = E.GetAnalogValue();
			float& Last = LastAxis.FindOrAdd(E.GetKey().GetFName(), V);
			if (FMath::Abs(V - Last) > 0.35f) { Last = V; SmvNoteDevice(true, E.GetInputDeviceId()); }
			return false;
		}
		virtual bool HandleMouseMoveEvent(FSlateApplication&, const FPointerEvent& E) override
		{
			if (E.GetCursorDelta().SizeSquared() > 9.0) SmvUi::PadActive() = false;
			return false;
		}
		virtual bool HandleMouseButtonDownEvent(FSlateApplication&, const FPointerEvent&) override { SmvUi::PadActive() = false; return false; }
		virtual bool HandleMouseButtonUpEvent(FSlateApplication&, const FPointerEvent&) override { GMouseUp = true; return false; }
		virtual const TCHAR* GetDebugName() const override { return TEXT("SmvGlyphInput"); }
	};
}

const TCHAR* FSmvInputGlyphs::StyleName(ESmvGlyphStyle S)
{
	switch (S)
	{
	case ESmvGlyphStyle::Keyboard: return TEXT("klavye/fare");
	case ESmvGlyphStyle::Xbox: return TEXT("Xbox");
	case ESmvGlyphStyle::PlayStation: return TEXT("PlayStation");
	default: return TEXT("genel gamepad");
	}
}

void FSmvInputGlyphs::Init()
{
	static bool bDone = false;
	if (bDone || !FSlateApplication::IsInitialized()) return;
	bDone = true;
	FSlateApplication::Get().RegisterInputPreProcessor(MakeShared<FSmvGlyphInputProc>());
	IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange().AddLambda([](EInputDeviceConnectionState State, FPlatformUserId, FInputDeviceId Dev)
	{
		// bağlanınca o kumandanın ailesine, ayrılınca klavyeye hemen geç
		if (State == EInputDeviceConnectionState::Connected) SmvNoteDevice(true, Dev);
		else if (State == EInputDeviceConnectionState::Disconnected) SmvUi::PadActive() = false;
		UE_LOG(LogSimumav, Log, TEXT("Kumanda %s"), State == EInputDeviceConnectionState::Connected ? TEXT("bağlandı") : TEXT("ayrıldı"));
	});
}

bool FSmvInputGlyphs::ConsumeMouseUp()
{
	const bool b = GMouseUp;
	GMouseUp = false;
	return b;
}

ESmvGlyphStyle FSmvInputGlyphs::Style()
{
	return SmvUi::PadActive() ? GPadStyle : ESmvGlyphStyle::Keyboard;
}

ESmvGlyphStyle FSmvInputGlyphs::PadStyle()
{
	return GPadStyle;
}

// ---------------------------------------------------------------------------
// simge eşleme
// ---------------------------------------------------------------------------

namespace
{
	struct FGlyphTex { FString Name; TArray<FString> Tokens; FString Path; };

	// JOYSTIX-ESLEME: aile belirteçleri (dosya / klasör adında geçen) ve mantıksal tuş → ad belirteçleri (ilk eşleşen kazanır).
	// Paket indikten sonra günlükteki "Joystix simgeleri" satırlarına göre gerekirse buradan düzeltilir.
	// <JOYSTIX-ESLEME>
	const TCHAR* const GFamXbox[] = { TEXT("xbox"), TEXT("xb"), TEXT("xsx"), TEXT("xone"), TEXT("x360") };
	const TCHAR* const GFamPs[] = { TEXT("ps5"), TEXT("ps4"), TEXT("ps"), TEXT("playstation"), TEXT("dualsense"), TEXT("dualshock") };
	const TCHAR* const GFamGen[] = { TEXT("generic"), TEXT("gamepad"), TEXT("controller"), TEXT("steam"), TEXT("switch") };
	const TCHAR* const GFamKb[] = { TEXT("keyboard"), TEXT("kb"), TEXT("key"), TEXT("keys"), TEXT("mouse"), TEXT("pc") };
	struct FPadKey { const TCHAR* Key; const TCHAR* Xbox; const TCHAR* Ps; const TCHAR* Gen; };
	const FPadKey GPadKeys[] =
	{
		{ TEXT("A"), TEXT("a"), TEXT("cross|x"), TEXT("a|south|bottom") },
		{ TEXT("B"), TEXT("b"), TEXT("circle|o"), TEXT("b|east|right") },
		{ TEXT("X"), TEXT("x"), TEXT("square"), TEXT("x|west|left") },
		{ TEXT("Y"), TEXT("y"), TEXT("triangle"), TEXT("y|north|top") },
		{ TEXT("LB"), TEXT("lb|leftbumper"), TEXT("l1"), TEXT("lb|l1") },
		{ TEXT("RB"), TEXT("rb|rightbumper"), TEXT("r1"), TEXT("rb|r1") },
		{ TEXT("LT"), TEXT("lt|lefttrigger"), TEXT("l2"), TEXT("lt|l2") },
		{ TEXT("RT"), TEXT("rt|righttrigger"), TEXT("r2"), TEXT("rt|r2") },
		{ TEXT("LS"), TEXT("ls|leftstick|l3|lstick"), TEXT("l3|leftstick|ls"), TEXT("ls|leftstick|l3") },
		{ TEXT("RS"), TEXT("rs|rightstick|r3|rstick"), TEXT("r3|rightstick|rs"), TEXT("rs|rightstick|r3") },
		{ TEXT("D↑"), TEXT("dpadup|up"), TEXT("dpadup|up"), TEXT("dpadup|up") },
		{ TEXT("D↓"), TEXT("dpaddown|down"), TEXT("dpaddown|down"), TEXT("dpaddown|down") },
		{ TEXT("D←"), TEXT("dpadleft|left"), TEXT("dpadleft|left"), TEXT("dpadleft|left") },
		{ TEXT("D→"), TEXT("dpadright|right"), TEXT("dpadright|right"), TEXT("dpadright|right") },
		{ TEXT("Start"), TEXT("menu|start"), TEXT("options|start"), TEXT("start|menu|plus") },
		{ TEXT("Back"), TEXT("view|back|select"), TEXT("share|create|select"), TEXT("select|back|minus") },
	};
	struct FKbKey { const TCHAR* Key; const TCHAR* Tok; };
	const FKbKey GKbKeys[] =
	{
		{ TEXT("Enter"), TEXT("enter|return") }, { TEXT("Esc"), TEXT("esc|escape") }, { TEXT("Space"), TEXT("space|spacebar") },
		{ TEXT("Tab"), TEXT("tab") }, { TEXT("Shift"), TEXT("shift") }, { TEXT("Ctrl"), TEXT("ctrl|control") }, { TEXT("Alt"), TEXT("alt") },
		{ TEXT("Backspace"), TEXT("backspace") }, { TEXT("↑"), TEXT("arrowup|up") }, { TEXT("↓"), TEXT("arrowdown|down") },
		{ TEXT("←"), TEXT("arrowleft|left") }, { TEXT("→"), TEXT("arrowright|right") },
		{ TEXT("LMB"), TEXT("mouseleft|lmb|leftclick") }, { TEXT("RMB"), TEXT("mouseright|rmb|rightclick") }, { TEXT("Wheel"), TEXT("mousewheel|scroll|wheel") },
	};
	// kumanda görseli (Mode 1–4 şeması): ad belirteci ve çubukların görsel üzerindeki konumu (0..1)
	const TCHAR* const GCtrlTok[] = { TEXT("controller"), TEXT("gamepad"), TEXT("pad") };
	// </JOYSTIX-ESLEME>

	TArray<FGlyphTex>& GlyphIndex()
	{
		static TArray<FGlyphTex> Idx;
		static bool bDone = false;
		if (bDone) return Idx;
		bDone = true;
		TArray<FAssetData> A;
		for (const TCHAR* Root : { TEXT("/Game/Fab/Joystix"), TEXT("/Game/Joystix") })
			IAssetRegistry::GetChecked().GetAssetsByPath(FName(Root), A, true);
		for (const FAssetData& D : A)
		{
			if (D.AssetClassPath != UTexture2D::StaticClass()->GetClassPathName()) continue;
			FGlyphTex G;
			G.Name = D.AssetName.ToString();
			G.Path = D.GetObjectPathString();
			// belirteçler: klasör + ad, harf/rakam dışı ayraç; CamelCase sınırları da ayrılır
			FString Src = D.PackagePath.ToString() + TEXT("/") + G.Name, Cur;
			for (int32 i = 0; i < Src.Len(); ++i)
			{
				const TCHAR C = Src[i];
				const bool bSplit = !FChar::IsAlnum(C) || (i > 0 && FChar::IsUpper(C) && FChar::IsLower(Src[i - 1]));
				if (bSplit && !Cur.IsEmpty()) { G.Tokens.Add(Cur.ToLower()); Cur.Reset(); }
				if (FChar::IsAlnum(C)) Cur.AppendChar(C);
			}
			if (!Cur.IsEmpty()) G.Tokens.Add(Cur.ToLower());
			Idx.Add(G);
		}
		UE_LOG(LogSimumav, Log, TEXT("Joystix simgeleri: %d doku"), Idx.Num());
		for (int32 i = 0; i < Idx.Num() && i < 40; ++i) UE_LOG(LogSimumav, Log, TEXT("Joystix simgesi: %s"), *Idx[i].Path);
		return Idx;
	}

	template <SIZE_T N>
	bool HasAny(const FGlyphTex& G, const TCHAR* const (&Fam)[N]) { for (const TCHAR* F : Fam) if (G.Tokens.Contains(F)) return true; return false; }

	bool FamilyOk(const FGlyphTex& G, ESmvGlyphStyle S)
	{
		switch (S)
		{
		case ESmvGlyphStyle::Xbox: return HasAny(G, GFamXbox);
		case ESmvGlyphStyle::PlayStation: return HasAny(G, GFamPs);
		case ESmvGlyphStyle::Generic: return HasAny(G, GFamGen) && !HasAny(G, GFamXbox) && !HasAny(G, GFamPs);
		default: return HasAny(G, GFamKb) && !HasAny(G, GFamXbox) && !HasAny(G, GFamPs);
		}
	}

	/** Ad belirteçleri ("a|south") — dokunun son belirteçlerinden biri tam eşleşmeli (birleşik "dpadup" için ardışık iki belirteç de denenir) */
	const FGlyphTex* Find(ESmvGlyphStyle S, const FString& Alts)
	{
		TArray<FString> Want;
		Alts.ParseIntoArray(Want, TEXT("|"));
		const FGlyphTex* Best = nullptr;
		for (const FString& W : Want)
		{
			for (const FGlyphTex& G : GlyphIndex())
			{
				if (!FamilyOk(G, S)) continue;
				bool bHit = G.Tokens.Contains(W);
				for (int32 i = 0; !bHit && i + 1 < G.Tokens.Num(); ++i) bHit = G.Tokens[i] + G.Tokens[i + 1] == W;
				if (!bHit) continue;
				if (G.Tokens.Contains(TEXT("pressed")) || G.Tokens.Contains(TEXT("outline")) || G.Tokens.Contains(TEXT("disabled"))) continue;
				if (!Best || G.Name.Len() < Best->Name.Len()) Best = &G;
			}
			if (Best) return Best;
		}
		return Best;
	}

	const FSlateBrush* BrushFor(const FGlyphTex* G, float Height)
	{
		if (!G) return nullptr;
		static TMap<FString, TSharedPtr<FSlateBrush>> Cache;
		const FString K = G->Path + FString::Printf(TEXT("@%.0f"), Height);
		if (TSharedPtr<FSlateBrush>* B = Cache.Find(K)) return B->Get();
		UTexture2D* T = LoadObject<UTexture2D>(nullptr, *G->Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!T) { Cache.Add(K, nullptr); return nullptr; }
		T->AddToRoot();
		TSharedPtr<FSlateBrush> B = MakeShared<FSlateBrush>();
		B->SetResourceObject(T);
		const float Asp = T->GetSizeY() > 0 ? (float)T->GetSizeX() / (float)T->GetSizeY() : 1.f;
		B->ImageSize = FVector2D(Height * Asp, Height);
		B->DrawAs = ESlateBrushDrawType::Image;
		Cache.Add(K, B);
		return B.Get();
	}
}

bool FSmvInputGlyphs::HasPack()
{
	return GlyphIndex().Num() > 0;
}

const FSlateBrush* FSmvInputGlyphs::Brush(const FString& Key, float Height)
{
	if (!HasPack()) return nullptr;
	static TMap<FString, const FGlyphTex*> Memo;
	const bool bPad = Key.StartsWith(TEXT("pad:"));
	// klavye tuşları metin rozetiyle çizilir: Joystix "Filled" klavye simgeleri küçük boyda harfsiz beyaz kutu olarak görünüyordu
	// (duman testi ekranlarında "Seç / Geri / Yardım" ipuçları ve Ayarlar › Tuşlar tablosu). Kumanda simgeleri kalır.
	bool bKbIcons = false;
	if (GConfig) GConfig->GetBool(TEXT("/Script/Simumav.SmvUi"), TEXT("KeyboardGlyphIcons"), bKbIcons, GGameIni);
	if (!bPad && !bKbIcons) return nullptr;
	const ESmvGlyphStyle S = bPad ? GPadStyle : ESmvGlyphStyle::Keyboard;
	const FString MemoKey = FString::FromInt((int32)S) + TEXT("|") + Key;
	if (const FGlyphTex* const* M = Memo.Find(MemoKey)) return BrushFor(*M, Height);
	const FGlyphTex* G = nullptr;
	if (bPad)
	{
		const FString N = Key.Mid(4);
		for (const FPadKey& P : GPadKeys)
			if (N == P.Key) { G = Find(S, S == ESmvGlyphStyle::PlayStation ? P.Ps : S == ESmvGlyphStyle::Generic ? P.Gen : P.Xbox); if (!G && S == ESmvGlyphStyle::Generic) G = Find(ESmvGlyphStyle::Xbox, P.Xbox); break; }
	}
	else
	{
		for (const FKbKey& K : GKbKeys) if (Key == K.Key) { G = Find(S, K.Tok); break; }
		// tek harf / rakam / F tuşları: ad belirteci tuşun kendisi
		if (!G && (Key.Len() == 1 || (Key.Len() <= 3 && Key.StartsWith(TEXT("F")) && FChar::IsDigit(Key[Key.Len() - 1])))) G = Find(S, Key.ToLower());
	}
	Memo.Add(MemoKey, G);
	return BrushFor(G, Height);
}

const FSlateBrush* FSmvInputGlyphs::ControllerBrush(FVector2D& OutL, FVector2D& OutR)
{
	const bool bPs = GPadStyle == ESmvGlyphStyle::PlayStation;
	// konumlar: Xbox düzeni sol çubuk üstte-solda, sağ çubuk altta-sağda; PlayStation iki çubuk altta (ayar ile değiştirilebilir)
	OutL = bPs ? FVector2D(0.36, 0.62) : FVector2D(0.29, 0.40);
	OutR = bPs ? FVector2D(0.64, 0.62) : FVector2D(0.62, 0.60);
	if (GConfig)
	{
		FVector2D V;
		if (GConfig->GetVector2D(TEXT("/Script/Simumav.SmvInput"), bPs ? TEXT("PsLeftStick") : TEXT("XboxLeftStick"), V, GGameIni)) OutL = V;
		if (GConfig->GetVector2D(TEXT("/Script/Simumav.SmvInput"), bPs ? TEXT("PsRightStick") : TEXT("XboxRightStick"), V, GGameIni)) OutR = V;
	}
	if (!HasPack()) return nullptr;
	const FGlyphTex* G = nullptr;
	for (const TCHAR* T : GCtrlTok)
	{
		for (const FGlyphTex& X : GlyphIndex())
		{
			if (!FamilyOk(X, GPadStyle == ESmvGlyphStyle::Keyboard ? ESmvGlyphStyle::Xbox : GPadStyle) || !X.Tokens.Contains(T)) continue;
			bool bKey = false;
			for (const FPadKey& P : GPadKeys) if (X.Tokens.Contains(FString(P.Key).ToLower())) bKey = true;
			if (bKey) continue;
			if (!G || X.Name.Len() < G->Name.Len()) G = &X;
		}
		if (G) break;
	}
	return BrushFor(G, 200.f);
}
