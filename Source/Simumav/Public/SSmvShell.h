// Simumav arayüz kabuğu — tüm menü ekranları KODLA kurulur (Slate; UMG/.uasset gerekmez). USmvFlow ekranına göre içerik yeniden kurulur;
// düzenleme alanları yerel tampon yapılara yazar (yazarken yeniden kurulmaz → odak kaybolmaz).
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "SimumavTypes.h"
#include "SmvFlow.h"

class ASmvGameMode;
class SBox;
class SVerticalBox;

class SIMUMAV_API SSmvShell : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSmvShell) {}
		SLATE_ARGUMENT(USmvFlow*, Flow)
		SLATE_ARGUMENT(ASmvGameMode*, GameMode)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	/** Ekran değişti / veri değişti: içerik yeniden kurulur. */
	void Refresh();
	void ShowToast(const FString& Text, int32 Severity);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	TWeakObjectPtr<USmvFlow> Flow;
	TWeakObjectPtr<ASmvGameMode> GameMode;
	TSharedPtr<SBox> Content;
	TSharedPtr<SBox> ToastBox;
	FString ToastText; int32 ToastSev = 6; double ToastUntil = 0;
	ESmvScreen Built = ESmvScreen::Welcome;
	bool bBuiltPause = false;
	bool bDirty = true;

	// düzenleme tamponları
	FString LoginEmail, LoginPassword;
	FSmvDrone EditDrone; FSmvPilot EditPilot; FSmvTrack EditTrack; FSmvMission EditMission;
	FSmvSettings EditSettings;
	FString MatchCode, MatchPassword, MatchKind = TEXT("rings"), MatchLevel = TEXT("normal");
	int32 MatchBots = 3, MatchMax = 4;
	int32 ServoCh = 9, ServoPwm = 1900, ServoDelay = 0; FString ServoCmp = TEXT("ge");
	int32 TargetCount = 0; FString TargetModel = TEXT("tgt:sw300"); float TargetAlt = 120, TargetDia = 800, TargetSpeed = 28;
	TArray<TSharedPtr<TArray<TSharedPtr<FString>>>> ComboStores;
	TArray<TSharedPtr<TArray<TSharedPtr<FString>>>> PrevStores;

	// ekranlar
	TSharedRef<SWidget> BuildScreen(ESmvScreen S);
	TSharedRef<SWidget> Welcome();
	TSharedRef<SWidget> Login();
	TSharedRef<SWidget> Menu();
	TSharedRef<SWidget> Hangar();
	TSharedRef<SWidget> Crew();
	TSharedRef<SWidget> Tracks();
	TSharedRef<SWidget> Missions();
	TSharedRef<SWidget> Wizard();
	TSharedRef<SWidget> Loading();
	TSharedRef<SWidget> FlightPause();
	TSharedRef<SWidget> Summary();
	TSharedRef<SWidget> Lobby();
	TSharedRef<SWidget> Leaderboard();
	TSharedRef<SWidget> Logbook();
	TSharedRef<SWidget> Settings();
	TSharedRef<SWidget> Account();

	// yardımcılar
	TSharedRef<SWidget> Page(const FText& Title, TSharedRef<SWidget> Body, bool bBack = true);
	TSharedRef<SWidget> Panel(TSharedRef<SWidget> Inner, float Padding = 16.f);
	TSharedRef<SWidget> Btn(const FText& Label, TFunction<void()> OnClick, bool bPrimary = false, TAttribute<bool> Enabled = true);
	TSharedRef<SWidget> Label(const FText& T, int32 Size = 12, bool bBold = false, const FLinearColor& Color = FLinearColor(0.85f, 0.9f, 0.95f));
	TSharedRef<SWidget> Field(const FText& Name, TSharedRef<SWidget> Editor);
	TSharedRef<SWidget> TextEdit(FString* Target, bool bPassword = false, const FText& Hint = FText::GetEmpty());
	TSharedRef<SWidget> NumEdit(double* Target, double Min, double Max, double Step = 1.0);
	TSharedRef<SWidget> IntEdit(int32* Target, int32 Min, int32 Max);
	TSharedRef<SWidget> Check(bool* Target);
	/** Seçenek kutusu: değerler + görünen etiketler; Target'a değer yazar, OnPick (isteğe bağlı) sonra çağrılır. */
	TSharedRef<SWidget> Combo(FString* Target, const TArray<FString>& Values, const TArray<FText>& Labels, TFunction<void()> OnPick = nullptr);
	TSharedRef<SWidget> ColorEdit(FString* Hex);
	void Toast(const FString& T, int32 Sev = 6);
	class USmvDataService* Data() const;
	class USimumavClient* Client() const;
	static FText Tx(const TCHAR* Key);
};
