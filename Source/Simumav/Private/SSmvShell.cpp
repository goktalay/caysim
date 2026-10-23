#include "SSmvShell.h"
#include "SimumavModule.h"
#include "SimumavClient.h"
#include "SmvDataService.h"
#include "SmvFlightLink.h"
#include "SmvLocalFlightLink.h"
#include "SmvMatchClient.h"
#include "SmvGameMode.h"
#include "SmvGameInstance.h"
#include "SmvSteam.h"
#include "SmvI18n.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/GameInstance.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "SmvShell"

namespace
{
	const FLinearColor GSmvBg(0.02f, 0.035f, 0.06f, 0.92f);
	const FLinearColor GSmvPanel(0.06f, 0.09f, 0.14f, 0.95f);
	const FLinearColor GSmvAccent(0.98f, 0.55f, 0.1f, 1.f);
	const FLinearColor GSmvText(0.88f, 0.92f, 0.96f, 1.f);
	const FLinearColor GSmvDim(0.6f, 0.66f, 0.74f, 1.f);
}

FText SSmvShell::Tx(const TCHAR* Key) { return FSmvI18n::T(Key); }

USmvDataService* SSmvShell::Data() const { return Flow.IsValid() ? Flow->Data() : nullptr; }
USimumavClient* SSmvShell::Client() const { return Flow.IsValid() ? Flow->Client() : nullptr; }

void SSmvShell::Construct(const FArguments& InArgs)
{
	Flow = InArgs._Flow;
	GameMode = InArgs._GameMode;
	ToastBrush = FSlateRoundedBoxBrush(FLinearColor::White, 8.f);
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SAssignNew(Content, SBox)
		]
		// bildirim üst ortada: uçuşta alt bant / kadranlarla çakışmasın
		+ SOverlay::Slot().VAlign(VAlign_Top).HAlign(HAlign_Center).Padding(0, 150, 0, 0)
		[
			SAssignNew(ToastBox, SBox).Visibility(EVisibility::HitTestInvisible)
		]
	];
	// kabuk tüm ekranı kaplar: kendisi tıklamayı yutmasın (alttaki uçuş arayüzü düğmeleri çalışsın)
	SetVisibility(EVisibility::SelfHitTestInvisible);
	if (Data()) EditSettings = Data()->Settings();
	Refresh();
}

void SSmvShell::Refresh() { bDirty = true; }

void SSmvShell::ShowToast(const FString& Text, int32 Severity)
{
	ToastText = Text; ToastSev = Severity; ToastUntil = FPlatformTime::Seconds() + 5.0;
	if (!ToastBox.IsValid()) return;
	const FLinearColor C = Severity <= 3 ? FLinearColor(1.f, 0.3f, 0.25f) : Severity == 4 ? FLinearColor(1.f, 0.8f, 0.2f) : GSmvText;
	ToastBox->SetContent(
		SNew(SBorder).BorderImage(&ToastBrush).BorderBackgroundColor(FLinearColor(0.02f, 0.03f, 0.04f, 0.88f)).Padding(FMargin(18, 10))
		[
			SNew(STextBlock).Text(FText::FromString(Text)).ColorAndOpacity(C).Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
		]);
}

void SSmvShell::Toast(const FString& T, int32 Sev) { ShowToast(T, Sev); }

void SSmvShell::Tick(const FGeometry& G, const double Now, const float Dt)
{
	SCompoundWidget::Tick(G, Now, Dt);
	if (ToastUntil > 0 && FPlatformTime::Seconds() > ToastUntil && ToastBox.IsValid()) { ToastBox->SetContent(SNullWidget::NullWidget); ToastUntil = 0; }
	if (!Flow.IsValid()) return;
	const bool bPause = GameMode.IsValid() && GameMode->IsPausedMenu();
	if (!bDirty && Built == Flow->GetScreen() && bBuiltPause == bPause) return;
	const bool bScreenChanged = Built != Flow->GetScreen();
	bDirty = false;
	Built = Flow->GetScreen();
	bBuiltPause = bPause;
	// ekrana girerken düzenleme tamponlarını tazele
	if (bScreenChanged && Data())
	{
		if (Built == ESmvScreen::Settings) EditSettings = Data()->Settings();
		if (Built == ESmvScreen::Hangar && Data()->Drones().Num()) EditDrone = Data()->Drones()[0];
		if (Built == ESmvScreen::Crew && Data()->Pilots().Num()) EditPilot = Data()->Pilots()[0];
		if (Built == ESmvScreen::Tracks && Data()->Tracks().Num()) EditTrack = Data()->Tracks()[0];
		if (Built == ESmvScreen::Leaderboard) Flow->FetchLeaderboard();
	}
	PrevStores = MoveTemp(ComboStores);   // eski seçenek kutuları bir kuşak daha yaşar (Slate geç bırakabilir)
	ComboStores.Reset();
	Content->SetContent(BuildScreen(Built));
}

// ---------------------------------------------------------------------------
// yardımcı bileşenler
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SSmvShell::Label(const FText& T, int32 Size, bool bBold, const FLinearColor& Color)
{
	return SNew(STextBlock).Text(T).Font(FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size)).ColorAndOpacity(Color).AutoWrapText(true);
}

TSharedRef<SWidget> SSmvShell::Panel(TSharedRef<SWidget> Inner, float Pad)
{
	return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(GSmvPanel).Padding(Pad)[Inner];
}

TSharedRef<SWidget> SSmvShell::Btn(const FText& L, TFunction<void()> OnClick, bool bPrimary, TAttribute<bool> Enabled)
{
	return SNew(SBox).MinDesiredWidth(200).Padding(FMargin(0, 3))
	[
		SNew(SButton)
		.ButtonColorAndOpacity(bPrimary ? GSmvAccent : FLinearColor(0.2f, 0.26f, 0.34f))
		.IsEnabled(Enabled)
		.ContentPadding(FMargin(14, 8))
		.OnClicked_Lambda([OnClick]() { if (OnClick) OnClick(); return FReply::Handled(); })
		[
			SNew(STextBlock).Text(L).Font(FCoreStyle::GetDefaultFontStyle(bPrimary ? "Bold" : "Regular", 13)).ColorAndOpacity(FLinearColor::White)
		]
	];
}

TSharedRef<SWidget> SSmvShell::Field(const FText& Name, TSharedRef<SWidget> Editor)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(0.4f).VAlign(VAlign_Center).Padding(0, 3, 10, 3)[Label(Name, 12, false, GSmvDim)]
		+ SHorizontalBox::Slot().FillWidth(0.6f).VAlign(VAlign_Center).Padding(0, 3)[Editor];
}

TSharedRef<SWidget> SSmvShell::TextEdit(FString* Target, bool bPassword, const FText& Hint)
{
	return SNew(SEditableTextBox)
		.Text(FText::FromString(*Target))
		.HintText(Hint)
		.IsPassword(bPassword)
		.OnTextChanged_Lambda([Target](const FText& T) { *Target = T.ToString(); });
}

TSharedRef<SWidget> SSmvShell::NumEdit(double* Target, double Min, double Max, double Step)
{
	return SNew(SSpinBox<double>)
		.MinValue(Min).MaxValue(Max).Delta(Step)
		.Value_Lambda([Target]() { return *Target; })
		.OnValueChanged_Lambda([Target](double V) { *Target = V; });
}

TSharedRef<SWidget> SSmvShell::IntEdit(int32* Target, int32 Min, int32 Max)
{
	return SNew(SSpinBox<int32>)
		.MinValue(Min).MaxValue(Max).Delta(1)
		.Value_Lambda([Target]() { return *Target; })
		.OnValueChanged_Lambda([Target](int32 V) { *Target = V; });
}

TSharedRef<SWidget> SSmvShell::Check(bool* Target)
{
	return SNew(SCheckBox)
		.IsChecked_Lambda([Target]() { return *Target ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
		.OnCheckStateChanged_Lambda([Target](ECheckBoxState S) { *Target = S == ECheckBoxState::Checked; });
}

TSharedRef<SWidget> SSmvShell::Combo(FString* Target, const TArray<FString>& Values, const TArray<FText>& Labels, TFunction<void()> OnPick)
{
	TSharedPtr<TArray<TSharedPtr<FString>>> Store = MakeShared<TArray<TSharedPtr<FString>>>();
	TSharedPtr<FString> Initial;
	for (const FString& V : Values)
	{
		TSharedPtr<FString> P = MakeShared<FString>(V);
		Store->Add(P);
		if (V == *Target) Initial = P;
	}
	ComboStores.Add(Store);
	TMap<FString, FText> Names;
	for (int32 i = 0; i < Values.Num(); ++i) Names.Add(Values[i], Labels.IsValidIndex(i) ? Labels[i] : FText::FromString(Values[i]));
	return SNew(SComboBox<TSharedPtr<FString>>)
		.OptionsSource(Store.Get())
		.InitiallySelectedItem(Initial)
		.OnGenerateWidget_Lambda([Names](TSharedPtr<FString> V) -> TSharedRef<SWidget>
		{
			const FText* T = V.IsValid() ? Names.Find(*V) : nullptr;
			return SNew(STextBlock).Text(T ? *T : FText::FromString(V.IsValid() ? *V : FString()));
		})
		.OnSelectionChanged_Lambda([Target, OnPick](TSharedPtr<FString> V, ESelectInfo::Type Info)
		{
			if (!V.IsValid() || Info == ESelectInfo::Direct) return;
			*Target = *V;
			if (OnPick) OnPick();
		})
		[
			SNew(STextBlock).Text_Lambda([Target, Names]()
			{
				const FText* T = Names.Find(*Target);
				return T ? *T : FText::FromString(*Target);
			})
		];
}

TSharedRef<SWidget> SSmvShell::ColorEdit(FString* Hex)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
		[
			SNew(SBox).WidthOverride(28).HeightOverride(20)
			[
				SNew(SColorBlock).Color_Lambda([Hex]() { return Hex->Len() == 7 ? FLinearColor::FromSRGBColor(FColor::FromHex(*Hex)) : FLinearColor::Gray; })
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)[TextEdit(Hex, false, FText::FromString(TEXT("#rrggbb")))];
}

TSharedRef<SWidget> SSmvShell::Page(const FText& Title, TSharedRef<SWidget> Body, bool bBack)
{
	TWeakObjectPtr<USmvFlow> F = Flow;
	return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(GSmvBg).Padding(FMargin(48, 32))
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 18, 0)
			[
				bBack ? Btn(Tx(TEXT("common.back")), [F]() { if (F.IsValid()) F->Back(); }) : SNullWidget::NullWidget
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[Label(Title, 26, true, GSmvAccent)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				Label(FText::FromString(Client() && Client()->IsLoggedIn() ? FString::Printf(TEXT("simumav.com · %s"), *Client()->GetUser().DisplayName) : Tx(TEXT("account.guest")).ToString()), 11, false, GSmvDim)
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.f)[SNew(SScrollBox) + SScrollBox::Slot()[Body]]
	];
}

TSharedRef<SWidget> SSmvShell::BuildScreen(ESmvScreen S)
{
	switch (S)
	{
	case ESmvScreen::Welcome: return Welcome();
	case ESmvScreen::Login: return Login();
	case ESmvScreen::Menu: return Menu();
	case ESmvScreen::Hangar: return Hangar();
	case ESmvScreen::Crew: return Crew();
	case ESmvScreen::Tracks: return Tracks();
	case ESmvScreen::Missions: return Missions();
	case ESmvScreen::Wizard: return Wizard();
	case ESmvScreen::Loading: return Loading();
	case ESmvScreen::Flight: return FlightPause();
	case ESmvScreen::Summary: return Summary();
	case ESmvScreen::Lobby: return Lobby();
	case ESmvScreen::Leaderboard: return Leaderboard();
	case ESmvScreen::Logbook: return Logbook();
	case ESmvScreen::Settings: return Settings();
	case ESmvScreen::Account: return Account();
	}
	return SNullWidget::NullWidget;
}

// ---------------------------------------------------------------------------
// hoş geldin / giriş / hesap
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SSmvShell::Welcome()
{
	TWeakObjectPtr<USmvFlow> F = Flow;
	return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(GSmvBg)
	[
		SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(560)
			[
				Panel(SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Label(FText::FromString(TEXT("SIMUMAV")), 44, true, GSmvAccent)]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 4, 0, 24)[Label(Tx(TEXT("welcome.tagline")), 14, false, GSmvDim)]
					+ SVerticalBox::Slot().AutoHeight()[Btn(Tx(TEXT("welcome.guest")), [F]() { if (F.IsValid()) F->ContinueAsGuest(); }, true)]
					+ SVerticalBox::Slot().AutoHeight()[Btn(Tx(TEXT("welcome.login")), [F]() { if (F.IsValid()) F->SetScreen(ESmvScreen::Login); })]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 18, 0, 0)[Label(Tx(TEXT("welcome.note")), 11, false, GSmvDim)], 32.f)
			]
		]
	];
}

TSharedRef<SWidget> SSmvShell::Login()
{
	TWeakObjectPtr<USmvFlow> F = Flow;
	SSmvShell* Self = this;
	USmvGameInstance* GI = GameMode.IsValid() ? USmvGameInstance::GetSmv(GameMode.Get()) : nullptr;
	return Page(Tx(TEXT("login.title")), SNew(SBox).MaxDesiredWidth(620).HAlign(HAlign_Left)
	[
		Panel(SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[Label(Tx(TEXT("login.intro")), 12, false, GSmvDim)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)[Field(Tx(TEXT("login.email")), TextEdit(&LoginEmail))]
			+ SVerticalBox::Slot().AutoHeight()[Field(Tx(TEXT("login.password")), TextEdit(&LoginPassword, true))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8)
			[
				SNew(STextBlock).Text_Lambda([F]() { return FText::FromString(F.IsValid() ? (F->bLoginBusy ? FSmvI18n::T(TEXT("login.busy")).ToString() : F->LoginError) : FString()); })
				.ColorAndOpacity(FLinearColor(1.f, 0.45f, 0.35f)).AutoWrapText(true)
			]
			+ SVerticalBox::Slot().AutoHeight()[Btn(Tx(TEXT("login.submit")), [F, Self]() { if (F.IsValid()) { F->Login(Self->LoginEmail, Self->LoginPassword); Self->LoginPassword.Empty(); } }, true,
				TAttribute<bool>::CreateLambda([F]() { return F.IsValid() && !F->bLoginBusy; }))]
			+ SVerticalBox::Slot().AutoHeight()[Btn(Tx(TEXT("login.register")), [GI, Self]()
			{
				if (GI) if (USmvSteamSubsystem* St = GI->GetSubsystem<USmvSteamSubsystem>()) St->OpenUrl(Self->Client()->RegisterUrl());
			})]
			+ SVerticalBox::Slot().AutoHeight()[Btn(Tx(TEXT("welcome.guest")), [F]() { if (F.IsValid()) F->ContinueAsGuest(); })]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)[Label(Tx(TEXT("login.privacy")), 10, false, GSmvDim)])
	]);
}

TSharedRef<SWidget> SSmvShell::Account()
{
	TWeakObjectPtr<USmvFlow> F = Flow;
	USimumavClient* C = Client();
	const bool bIn = C && C->IsLoggedIn();
	TSharedRef<SVerticalBox> B = SNew(SVerticalBox);
	B->AddSlot().AutoHeight()[Label(bIn ? FSmvI18n::Format(TEXT("account.logged_in"), { { TEXT("name"), C->GetUser().DisplayName }, { TEXT("email"), C->GetUser().Email } }) : Tx(TEXT("account.guest_info")), 13)];
	if (bIn && F.IsValid() && F->bOfferImport)
	{
		B->AddSlot().AutoHeight().Padding(0, 14, 0, 0)[Label(Tx(TEXT("account.import_offer")), 12, true, GSmvAccent)];
		B->AddSlot().AutoHeight()[Btn(Tx(TEXT("account.import")), [F]() { if (F.IsValid()) F->ImportGuest(); }, true)];
		B->AddSlot().AutoHeight()[Btn(Tx(TEXT("account.import_skip")), [F]() { if (F.IsValid()) { F->bOfferImport = false; F->SetScreen(ESmvScreen::Menu); } })];
	}
	else if (bIn && Data() && Data()->HasGuestData())
	{
		B->AddSlot().AutoHeight().Padding(0, 14, 0, 0)[Btn(Tx(TEXT("account.import")), [F]() { if (F.IsValid()) F->ImportGuest(); })];
	}
	B->AddSlot().AutoHeight().Padding(0, 14, 0, 0)[bIn ? Btn(Tx(TEXT("account.logout")), [F]() { if (F.IsValid()) F->Logout(); })
		: Btn(Tx(TEXT("welcome.login")), [F]() { if (F.IsValid()) F->SetScreen(ESmvScreen::Login); }, true)];
	B->AddSlot().AutoHeight().Padding(0, 14, 0, 0)[Label(Tx(TEXT("account.features")), 11, false, GSmvDim)];
	return Page(Tx(TEXT("menu.account")), SNew(SBox).MaxDesiredWidth(720).HAlign(HAlign_Left)[Panel(B)]);
}

// ---------------------------------------------------------------------------
// ana menü
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SSmvShell::Menu()
{
	TWeakObjectPtr<USmvFlow> F = Flow;
	auto Go = [F](ESmvScreen S) { return [F, S]() { if (F.IsValid()) F->SetScreen(S); }; };
	const bool bSitl = USmvLocalFlightLink::SitlAvailable();
	USmvGameInstance* GI = GameMode.IsValid() ? USmvGameInstance::GetSmv(GameMode.Get()) : nullptr;
	const USmvSteamSubsystem* St = GI ? GI->GetSubsystem<USmvSteamSubsystem>() : nullptr;
	return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(GSmvBg).Padding(FMargin(56, 40))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 40, 0)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 20)[Label(FText::FromString(TEXT("SIMUMAV")), 40, true, GSmvAccent)]
			+ SVerticalBox::Slot().AutoHeight()[Btn(Tx(TEXT("menu.fly")), [F]() { if (F.IsValid()) { F->NewDraft(); F->SetScreen(ESmvScreen::Wizard); } }, true)]
			+ SVerticalBox::Slot().AutoHeight()[Btn(Tx(TEXT("menu.hangar")), Go(ESmvScreen::Hangar))]
			+ SVerticalBox::Slot().AutoHeight()[Btn(Tx(TEXT("menu.crew")), Go(ESmvScreen::Crew))]
			+ SVerticalBox::Slot().AutoHeight()[Btn(Tx(TEXT("menu.tracks")), Go(ESmvScreen::Tracks))]
			+ SVerticalBox::Slot().AutoHeight()[Btn(Tx(TEXT("menu.missions")), Go(ESmvScreen::Missions))]
			+ SVerticalBox::Slot().AutoHeight()[Btn(Tx(TEXT("menu.match")), Go(ESmvScreen::Lobby))]
			+ SVerticalBox::Slot().AutoHeight()[Btn(Tx(TEXT("menu.top10")), Go(ESmvScreen::Leaderboard))]
			+ SVerticalBox::Slot().AutoHeight()[Btn(Tx(TEXT("menu.logbook")), Go(ESmvScreen::Logbook))]
			+ SVerticalBox::Slot().AutoHeight()[Btn(Tx(TEXT("menu.settings")), Go(ESmvScreen::Settings))]
			+ SVerticalBox::Slot().AutoHeight()[Btn(Tx(TEXT("menu.account")), Go(ESmvScreen::Account))]
			+ SVerticalBox::Slot().AutoHeight()[Btn(Tx(TEXT("menu.quit")), [G = GameMode]()
			{
				if (G.IsValid()) UKismetSystemLibrary::QuitGame(G.Get(), nullptr, EQuitPreference::Quit, false);
			})]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Top)
		[
			Panel(SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[Label(Client() && Client()->IsLoggedIn()
					? FSmvI18n::Format(TEXT("menu.status_online"), { { TEXT("name"), Client()->GetUser().DisplayName } })
					: FSmvI18n::Format(TEXT("menu.status_guest"), { { TEXT("name"), St ? St->GetPersonaName() : FString(TEXT("Pilot")) } }), 14, true)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)[Label(bSitl ? Tx(TEXT("menu.sitl_ok")) : Tx(TEXT("menu.sitl_missing")), 12, false, bSitl ? GSmvDim : FLinearColor(1.f, 0.7f, 0.2f))]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)[Label(St && St->IsSteamAvailable() ? FSmvI18n::Format(TEXT("menu.steam_ok"), { { TEXT("name"), St->GetPersonaName() } }) : Tx(TEXT("menu.steam_off")), 11, false, GSmvDim)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 16, 0, 0)[Label(Tx(TEXT("menu.hint")), 11, false, GSmvDim)])
		]
	];
}

// ---------------------------------------------------------------------------
// hangar
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SSmvShell::Hangar()
{
	USmvDataService* D = Data();
	if (!D) return SNullWidget::NullWidget;
	SSmvShell* Self = this;
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	for (const FSmvDrone& X : D->Drones())
	{
		const FSmvDrone Copy = X;
		List->AddSlot().AutoHeight()[Btn(FText::FromString(FString::Printf(TEXT("%s%s  ·  %s"), X.bIsDefault ? TEXT("* ") : TEXT(""), *X.Name, *X.Type)),
			[Self, Copy]() { Self->EditDrone = Copy; Self->Refresh(); }, X.Id == EditDrone.Id)];
	}
	List->AddSlot().AutoHeight().Padding(0, 10, 0, 0)[Btn(Tx(TEXT("hangar.new")), [Self]()
	{
		FSmvDrone N; N.Name = FSmvI18n::T(TEXT("hangar.new_name")).ToString(); N.ModelKey = USmvDataService::DefaultModelFor(N.Type);
		Self->EditDrone = N; Self->Refresh();
	})];
	TArray<FString> Types = { TEXT("multirotor"), TEXT("fpv"), TEXT("plane"), TEXT("vtol"), TEXT("interceptor"), TEXT("heli"), TEXT("rover") };
	TArray<FText> TypeL;
	for (const FString& T : Types) TypeL.Add(FSmvI18n::T(FString(TEXT("type.")) + T));
	TArray<FString> Models; TArray<FText> ModelL;
	for (const FSmvCatalogModel& M : USmvDataService::Catalog()) if (M.Type == EditDrone.Type) { Models.Add(M.Key); ModelL.Add(FText::FromString(M.Label)); }
	const FSmvCatalogModel* Cm = USmvDataService::FindModel(EditDrone.ModelKey);
	const bool bNew = EditDrone.Id.IsEmpty();
	TSharedRef<SVerticalBox> Ed = SNew(SVerticalBox);
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("hangar.name")), TextEdit(&EditDrone.Name))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("hangar.type")), bNew ? Combo(&EditDrone.Type, Types, TypeL, [Self]()
	{
		Self->EditDrone.ModelKey = USmvDataService::DefaultModelFor(Self->EditDrone.Type);
		if (const FSmvCatalogModel* M = USmvDataService::FindModel(Self->EditDrone.ModelKey)) { Self->EditDrone.Params = M->Defaults; Self->EditDrone.Drive = M->Drive; if (!M->Airframe.IsEmpty()) Self->EditDrone.Airframe = M->Airframe; }
		Self->Refresh();
	}) : Label(FSmvI18n::T(FString(TEXT("type.")) + EditDrone.Type)))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("hangar.model")), Combo(&EditDrone.ModelKey, Models, ModelL, [Self]()
	{
		if (const FSmvCatalogModel* M = USmvDataService::FindModel(Self->EditDrone.ModelKey)) { if (!M->Airframe.IsEmpty()) Self->EditDrone.Airframe = M->Airframe; Self->EditDrone.Drive = M->Drive; }
		Self->Refresh();
	}))];
	if (Cm) Ed->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[Label(FText::FromString(Cm->Desc), 11, false, GSmvDim)];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("hangar.color")), ColorEdit(&EditDrone.Color))];
	if (EditDrone.Type == TEXT("plane"))
		Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("hangar.airframe")), Combo(&EditDrone.Airframe, { TEXT("classic"), TEXT("elevon"), TEXT("vtail") },
			{ Tx(TEXT("airframe.classic")), Tx(TEXT("airframe.elevon")), Tx(TEXT("airframe.vtail")) }))];
	if (EditDrone.Type == TEXT("rover"))
		Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("hangar.drive")), Combo(&EditDrone.Drive, { TEXT("ackermann"), TEXT("skid") }, { Tx(TEXT("drive.ackermann")), Tx(TEXT("drive.skid")) }))];
	Ed->AddSlot().AutoHeight().Padding(0, 10, 0, 4)[Label(Tx(TEXT("hangar.power")), 13, true)];
	if (EditDrone.Type == TEXT("plane") || EditDrone.Type == TEXT("vtol") || EditDrone.Type == TEXT("interceptor"))
		Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("power.max_speed")), NumEdit(&EditDrone.Power.MaxSpeedMs, -1, 150, 1))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("power.fuel_l")), NumEdit(&EditDrone.Power.FuelL, -1, 500, 0.5))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("power.km_per_l")), NumEdit(&EditDrone.Power.FuelKmPerL, -1, 500, 0.5))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("power.cells")), IntEdit(&EditDrone.Power.ElecCells, -1, 16))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("power.mah")), NumEdit(&EditDrone.Power.ElecMah, -1, 200000, 100))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("power.w")), NumEdit(&EditDrone.Power.ElecW, -1, 2000, 1))];
	Ed->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[Label(Tx(TEXT("power.hint")), 10, false, GSmvDim)];
	Ed->AddSlot().AutoHeight().Padding(0, 12, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Btn(Tx(TEXT("common.save")), [Self]()
		{
			Self->Data()->SaveDrone(Self->EditDrone, FSmvDone::CreateLambda([Self](bool bOk, const FString& M) { Self->Toast(bOk ? FSmvI18n::T(TEXT("common.saved")).ToString() : M, bOk ? 6 : 3); Self->Refresh(); }));
		}, true)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Btn(Tx(TEXT("common.make_default")), [Self]() { Self->Data()->SetDefault(TEXT("drone"), Self->EditDrone.Id); Self->Refresh(); }, false,
			TAttribute<bool>(!bNew))]
		+ SHorizontalBox::Slot().AutoWidth()[Btn(Tx(TEXT("common.delete")), [Self]()
		{
			Self->Data()->DeleteDrone(Self->EditDrone.Id, FSmvDone::CreateLambda([Self](bool bOk, const FString& M) { Self->Toast(bOk ? FSmvI18n::T(TEXT("common.deleted")).ToString() : M, bOk ? 6 : 3); if (bOk && Self->Data()->Drones().Num()) Self->EditDrone = Self->Data()->Drones()[0]; Self->Refresh(); }));
		}, false, TAttribute<bool>(!bNew))]
	];
	return Page(Tx(TEXT("menu.hangar")), SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 24, 0)[Panel(List)]
		+ SHorizontalBox::Slot().FillWidth(1.f)[Panel(Ed)]);
}

// ---------------------------------------------------------------------------
// mürettebat (pilot + avatar)
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SSmvShell::Crew()
{
	USmvDataService* D = Data();
	if (!D) return SNullWidget::NullWidget;
	SSmvShell* Self = this;
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	for (const FSmvPilot& X : D->Pilots())
	{
		const FSmvPilot Copy = X;
		List->AddSlot().AutoHeight()[Btn(FText::FromString(FString::Printf(TEXT("%s%s"), X.bIsDefault ? TEXT("* ") : TEXT(""), *X.Name)), [Self, Copy]() { Self->EditPilot = Copy; Self->Refresh(); }, X.Id == EditPilot.Id)];
	}
	List->AddSlot().AutoHeight().Padding(0, 10, 0, 0)[Btn(Tx(TEXT("crew.new")), [Self]() { FSmvPilot P; P.Name = FSmvI18n::T(TEXT("crew.new_name")).ToString(); Self->EditPilot = P; Self->Refresh(); })];
	TArray<FString> Chars; TArray<FText> CharL;
	for (int32 i = 1; i <= 10; ++i) { Chars.Add(FString::Printf(TEXT("p%02d"), i)); CharL.Add(FText::FromString(FString::Printf(TEXT("P%02d"), i))); }
	TArray<FString> Skins; TArray<FText> SkinL;
	for (int32 i = 1; i <= 8; ++i) { Skins.Add(FString::Printf(TEXT("s%d"), i)); SkinL.Add(FText::FromString(FString::Printf(TEXT("%d"), i))); }
	const TArray<FString> Patches = { TEXT("none"), TEXT("star"), TEXT("wing"), TEXT("bolt"), TEXT("chevron"), TEXT("target"), TEXT("shield"), TEXT("cross") };
	const TArray<FString> Acc = { TEXT("none"), TEXT("mic"), TEXT("crest"), TEXT("antenna"), TEXT("stripe") };
	TArray<FText> PatchL, AccL;
	for (const FString& P : Patches) PatchL.Add(FSmvI18n::T(FString(TEXT("avatar.patch_")) + P));
	for (const FString& A : Acc) AccL.Add(FSmvI18n::T(FString(TEXT("avatar.acc_")) + A));
	const bool bNew = EditPilot.Id.IsEmpty();
	TSharedRef<SVerticalBox> Ed = SNew(SVerticalBox);
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("crew.name")), TextEdit(&EditPilot.Name))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("crew.character")), Combo(&EditPilot.Character, Chars, CharL))];
	Ed->AddSlot().AutoHeight().Padding(0, 10, 0, 4)[Label(Tx(TEXT("crew.avatar")), 13, true)];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("avatar.helmet")), ColorEdit(&EditPilot.Avatar.Helmet))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("avatar.visor")), ColorEdit(&EditPilot.Avatar.Visor))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("avatar.mirror")), Check(&EditPilot.Avatar.bMirror))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("avatar.suit")), ColorEdit(&EditPilot.Avatar.Suit))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("avatar.skin")), Combo(&EditPilot.Avatar.Skin, Skins, SkinL))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("avatar.patch")), Combo(&EditPilot.Avatar.Patch, Patches, PatchL))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("avatar.patch_color")), ColorEdit(&EditPilot.Avatar.PatchColor))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("avatar.accessory")), Combo(&EditPilot.Avatar.Accessory, Acc, AccL))];
	Ed->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[Label(Tx(TEXT("crew.avatar_note")), 10, false, GSmvDim)];
	if (!bNew) Ed->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[Label(FText::FromString(FString::Printf(TEXT("%d uçuş · %.1f sa"), EditPilot.Flights, EditPilot.Hours)), 11, false, GSmvDim)];
	Ed->AddSlot().AutoHeight().Padding(0, 12, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Btn(Tx(TEXT("common.save")), [Self]()
		{
			Self->Data()->SavePilot(Self->EditPilot, FSmvDone::CreateLambda([Self](bool bOk, const FString& M) { Self->Toast(bOk ? FSmvI18n::T(TEXT("common.saved")).ToString() : M, bOk ? 6 : 3); Self->Refresh(); }));
		}, true)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Btn(Tx(TEXT("common.make_default")), [Self]() { Self->Data()->SetDefault(TEXT("pilot"), Self->EditPilot.Id); Self->Refresh(); }, false, TAttribute<bool>(!bNew))]
		+ SHorizontalBox::Slot().AutoWidth()[Btn(Tx(TEXT("common.delete")), [Self]()
		{
			Self->Data()->DeletePilot(Self->EditPilot.Id, FSmvDone::CreateLambda([Self](bool bOk, const FString& M) { Self->Toast(bOk ? FSmvI18n::T(TEXT("common.deleted")).ToString() : M, bOk ? 6 : 3); if (bOk && Self->Data()->Pilots().Num()) Self->EditPilot = Self->Data()->Pilots()[0]; Self->Refresh(); }));
		}, false, TAttribute<bool>(!bNew))]
	];
	return Page(Tx(TEXT("menu.crew")), SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 24, 0)[Panel(List)]
		+ SHorizontalBox::Slot().FillWidth(1.f)[Panel(Ed)]);
}

// ---------------------------------------------------------------------------
// parkurlar
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SSmvShell::Tracks()
{
	USmvDataService* D = Data();
	if (!D) return SNullWidget::NullWidget;
	SSmvShell* Self = this;
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	for (const FSmvTrack& X : D->Tracks())
	{
		const FSmvTrack Copy = X;
		List->AddSlot().AutoHeight()[Btn(FText::FromString(FString::Printf(TEXT("%s%s"), X.bIsDefault ? TEXT("* ") : TEXT(""), *X.Name)), [Self, Copy]() { Self->EditTrack = Copy; Self->Refresh(); }, X.Id == EditTrack.Id)];
	}
	List->AddSlot().AutoHeight().Padding(0, 10, 0, 0)[Btn(Tx(TEXT("tracks.new")), [Self]() { FSmvTrack T; T.Name = FSmvI18n::T(TEXT("tracks.new_name")).ToString(); Self->EditTrack = T; Self->Refresh(); })];
	const bool bNew = EditTrack.Id.IsEmpty();
	TSharedRef<SVerticalBox> Ed = SNew(SVerticalBox);
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("tracks.name")), TextEdit(&EditTrack.Name))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("tracks.lat")), NumEdit(&EditTrack.Lat, -85, 85, 0.0001))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("tracks.lon")), NumEdit(&EditTrack.Lon, -180, 180, 0.0001))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("tracks.ground")), NumEdit(&EditTrack.GroundMsl, -10000, 8000, 1))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("tracks.radius")), NumEdit(&EditTrack.RadiusM, 200, 5000, 50))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("tracks.world")), Combo(&EditTrack.WorldMode, { TEXT("procedural"), TEXT("real"), TEXT("google3d") },
		{ Tx(TEXT("world.procedural")), Tx(TEXT("world.real")), Tx(TEXT("world.google3d")) }))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("tracks.time")), Combo(&EditTrack.TimeOfDay, { TEXT("day"), TEXT("dusk"), TEXT("night") }, { Tx(TEXT("time.day")), Tx(TEXT("time.dusk")), Tx(TEXT("time.night")) }))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("tracks.weather")), Combo(&EditTrack.Weather, { TEXT("clear"), TEXT("cloudy"), TEXT("rain"), TEXT("fog"), TEXT("snow") },
		{ Tx(TEXT("weather.clear")), Tx(TEXT("weather.cloudy")), Tx(TEXT("weather.rain")), Tx(TEXT("weather.fog")), Tx(TEXT("weather.snow")) }))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("tracks.wind")), NumEdit(&EditTrack.WindMs, 0, 30, 0.5))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("tracks.wind_dir")), NumEdit(&EditTrack.WindDir, 0, 359.99, 5))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("tracks.gust")), NumEdit(&EditTrack.WindGust, 0, 20, 0.5))];
	Ed->AddSlot().AutoHeight().Padding(0, 10, 0, 4)[Label(Tx(TEXT("tracks.proc")), 13, true)];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("tracks.seed")), IntEdit(&EditTrack.World.Seed, 1, 999999))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("tracks.size")), NumEdit(&EditTrack.World.SizeM, 1000, 12000, 500))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("tracks.mountains")), IntEdit(&EditTrack.World.MountainCount, 0, 12))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("tracks.mountain_h")), NumEdit(&EditTrack.World.MountainHeight, 0, 1500, 10))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("tracks.trees")), NumEdit(&EditTrack.World.TreeDensity, 0, 1, 0.05))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("tracks.buildings")), IntEdit(&EditTrack.World.BuildingCount, 0, 400))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("tracks.runway")), Check(&EditTrack.World.bRunway))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("tracks.runway_hdg")), NumEdit(&EditTrack.World.RunwayHeading, 0, 359, 5))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("tracks.water")), NumEdit(&EditTrack.World.Water, 0, 1, 0.05))];
	// kapılar: merkez çevresine eşit aralıklı halka ekle / temizle
	Ed->AddSlot().AutoHeight().Padding(0, 10, 0, 4)[Label(FSmvI18n::Format(TEXT("tracks.gates_n"), { { TEXT("n"), FString::FromInt(EditTrack.Gates.Num()) } }), 13, true)];
	Ed->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Btn(Tx(TEXT("tracks.gates_ring")), [Self]()
		{
			FSmvTrack& T = Self->EditTrack;
			T.Gates.Reset();
			for (int32 i = 0; i < 8; ++i)
			{
				const double A = 2.0 * UE_DOUBLE_PI * i / 8, R = FMath::Min(T.RadiusM * 0.4, 400.0);
				FSmvGate G; G.I = i;
				G.Lat = T.Lat + FMath::RadiansToDegrees(R * FMath::Cos(A) / 6378137.0);
				G.Lon = T.Lon + FMath::RadiansToDegrees(R * FMath::Sin(A) / (6378137.0 * FMath::Cos(FMath::DegreesToRadians(T.Lat))));
				G.AglM = 15 + 5 * (i % 3); G.YawDeg = FMath::Fmod(FMath::RadiansToDegrees(A) + 90.0, 360.0); G.RadiusM = 6;
				T.Gates.Add(G);
			}
			Self->Refresh();
		})]
		+ SHorizontalBox::Slot().AutoWidth()[Btn(Tx(TEXT("tracks.gates_clear")), [Self]() { Self->EditTrack.Gates.Reset(); Self->Refresh(); })]
	];
	Ed->AddSlot().AutoHeight().Padding(0, 12, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Btn(Tx(TEXT("common.save")), [Self]()
		{
			Self->Data()->SaveTrack(Self->EditTrack, FSmvDone::CreateLambda([Self](bool bOk, const FString& M) { Self->Toast(bOk ? FSmvI18n::T(TEXT("common.saved")).ToString() : M, bOk ? 6 : 3); Self->Refresh(); }));
		}, true)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Btn(Tx(TEXT("common.make_default")), [Self]() { Self->Data()->SetDefault(TEXT("track"), Self->EditTrack.Id); Self->Refresh(); }, false, TAttribute<bool>(!bNew))]
		+ SHorizontalBox::Slot().AutoWidth()[Btn(Tx(TEXT("common.delete")), [Self]()
		{
			Self->Data()->DeleteTrack(Self->EditTrack.Id, FSmvDone::CreateLambda([Self](bool bOk, const FString& M) { Self->Toast(bOk ? FSmvI18n::T(TEXT("common.deleted")).ToString() : M, bOk ? 6 : 3); if (bOk && Self->Data()->Tracks().Num()) Self->EditTrack = Self->Data()->Tracks()[0]; Self->Refresh(); }));
		}, false, TAttribute<bool>(!bNew))]
	];
	return Page(Tx(TEXT("menu.tracks")), SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 24, 0)[Panel(List)]
		+ SHorizontalBox::Slot().FillWidth(1.f)[Panel(Ed)]);
}

// ---------------------------------------------------------------------------
// görevler (plan sunucu biçiminde JSON — ileri düzey)
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SSmvShell::Missions()
{
	USmvDataService* D = Data();
	if (!D) return SNullWidget::NullWidget;
	SSmvShell* Self = this;
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	for (const FSmvMission& X : D->Missions())
	{
		const FSmvMission Copy = X;
		List->AddSlot().AutoHeight()[Btn(FText::FromString(FString::Printf(TEXT("%s · %s"), *X.Name, *X.Kind)), [Self, Copy]() { Self->EditMission = Copy; Self->Refresh(); }, X.Id == EditMission.Id)];
	}
	List->AddSlot().AutoHeight().Padding(0, 10, 0, 0)[Btn(Tx(TEXT("missions.new")), [Self]()
	{
		FSmvMission M; M.Name = FSmvI18n::T(TEXT("missions.new_name")).ToString();
		if (const FSmvTrack* T = Self->Data()->DefaultTrack()) M.TrackId = T->Id;
		Self->EditMission = M; Self->Refresh();
	})];
	TArray<FString> TrackIds; TArray<FText> TrackL;
	for (const FSmvTrack& T : D->Tracks()) { TrackIds.Add(T.Id); TrackL.Add(FText::FromString(T.Name)); }
	const TArray<FString> Kinds = { TEXT("free"), TEXT("corridor"), TEXT("spray"), TEXT("survey"), TEXT("fpv_flags"), TEXT("fpv_gates"), TEXT("defend") };
	TArray<FText> KindL; for (const FString& K : Kinds) KindL.Add(FSmvI18n::T(FString(TEXT("mission.")) + K));
	TSharedRef<SVerticalBox> Ed = SNew(SVerticalBox);
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("missions.name")), TextEdit(&EditMission.Name))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("missions.track")), Combo(&EditMission.TrackId, TrackIds, TrackL))];
	Ed->AddSlot().AutoHeight()[Field(Tx(TEXT("missions.kind")), Combo(&EditMission.Kind, Kinds, KindL))];
	Ed->AddSlot().AutoHeight().Padding(0, 8, 0, 4)[Label(Tx(TEXT("missions.plan")), 12, true)];
	Ed->AddSlot().AutoHeight()
	[
		SNew(SBox).HeightOverride(220)
		[
			SNew(SMultiLineEditableTextBox).Text(FText::FromString(EditMission.PlanJson))
			.OnTextChanged_Lambda([Self](const FText& T) { Self->EditMission.PlanJson = T.ToString(); })
		]
	];
	Ed->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[Label(Tx(TEXT("missions.plan_hint")), 10, false, GSmvDim)];
	Ed->AddSlot().AutoHeight().Padding(0, 12, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Btn(Tx(TEXT("common.save")), [Self]()
		{
			Self->Data()->SaveMission(Self->EditMission, FSmvDone::CreateLambda([Self](bool bOk, const FString& M) { Self->Toast(bOk ? FSmvI18n::T(TEXT("common.saved")).ToString() : M, bOk ? 6 : 3); Self->Refresh(); }));
		}, true)]
		+ SHorizontalBox::Slot().AutoWidth()[Btn(Tx(TEXT("common.delete")), [Self]()
		{
			Self->Data()->DeleteMission(Self->EditMission.Id, FSmvDone::CreateLambda([Self](bool bOk, const FString& M) { Self->Toast(bOk ? FSmvI18n::T(TEXT("common.deleted")).ToString() : M, bOk ? 6 : 3); Self->EditMission = FSmvMission(); Self->Refresh(); }));
		}, false, TAttribute<bool>(!EditMission.Id.IsEmpty()))]
	];
	return Page(Tx(TEXT("menu.missions")), SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 24, 0)[Panel(List)]
		+ SHorizontalBox::Slot().FillWidth(1.f)[Panel(Ed)]);
}

// ---------------------------------------------------------------------------
// uç sihirbazı
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SSmvShell::Wizard()
{
	USmvDataService* D = Data();
	if (!D || !Flow.IsValid()) return SNullWidget::NullWidget;
	SSmvShell* Self = this;
	USmvFlow* F = Flow.Get();
	FSmvFlightRequest& R = F->Draft;
	// seçim kutuları için kimlik tamponları
	static FString DroneId, PilotId, TrackId, MissionId, Backend, Speed;
	DroneId = R.Drone.Id; PilotId = R.Pilot.Id; TrackId = R.Track.Id; MissionId = R.bHasMission ? R.Mission.Id : FString(TEXT("-"));
	Backend = R.Backend == ESmvFlightBackend::Cloud ? TEXT("cloud") : TEXT("local");
	Speed = FString::SanitizeFloat(R.Speedup);
	TArray<FString> Ids; TArray<FText> Ls;
	for (const FSmvDrone& X : D->Drones()) { Ids.Add(X.Id); Ls.Add(FText::FromString(FString::Printf(TEXT("%s (%s)"), *X.Name, *FSmvI18n::T(FString(TEXT("type.")) + X.Type).ToString()))); }
	TSharedRef<SVerticalBox> B = SNew(SVerticalBox);
	B->AddSlot().AutoHeight()[Label(Tx(TEXT("wizard.vehicle")), 14, true, GSmvAccent)];
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("wizard.drone")), Combo(&DroneId, Ids, Ls, [Self, F]()
	{
		for (const FSmvDrone& X : Self->Data()->Drones()) if (X.Id == DroneId) F->Draft.Drone = X;
		F->Draft.Launch = FSmvLaunch();
		if (F->Draft.Drone.Type == TEXT("plane")) F->Draft.Launch.Method = TEXT("wheels");
		Self->Refresh();
	}))];
	Ids.Reset(); Ls.Reset();
	for (const FSmvPilot& X : D->Pilots()) { Ids.Add(X.Id); Ls.Add(FText::FromString(X.Name)); }
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("wizard.pilot")), Combo(&PilotId, Ids, Ls, [Self, F]() { for (const FSmvPilot& X : Self->Data()->Pilots()) if (X.Id == PilotId) F->Draft.Pilot = X; }))];
	Ids.Reset(); Ls.Reset();
	for (const FSmvTrack& X : D->Tracks()) { Ids.Add(X.Id); Ls.Add(FText::FromString(FString::Printf(TEXT("%s · %s"), *X.Name, *FSmvI18n::T(FString(TEXT("world.")) + X.WorldMode).ToString()))); }
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("wizard.track")), Combo(&TrackId, Ids, Ls, [Self, F]() { for (const FSmvTrack& X : Self->Data()->Tracks()) if (X.Id == TrackId) F->Draft.Track = X; F->Draft.bHasMission = false; Self->Refresh(); }))];
	Ids = { TEXT("-") }; Ls = { Tx(TEXT("wizard.no_mission")) };
	for (const FSmvMission& X : D->Missions()) if (X.TrackId == R.Track.Id) { Ids.Add(X.Id); Ls.Add(FText::FromString(X.Name)); }
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("wizard.mission")), Combo(&MissionId, Ids, Ls, [Self, F]()
	{
		F->Draft.bHasMission = MissionId != TEXT("-");
		for (const FSmvMission& X : Self->Data()->Missions()) if (X.Id == MissionId) F->Draft.Mission = X;
	}))];

	// kalkış
	const FString& Ty = R.Drone.Type;
	if (Ty == TEXT("plane"))
	{
		B->AddSlot().AutoHeight().Padding(0, 12, 0, 0)[Label(Tx(TEXT("wizard.launch")), 14, true, GSmvAccent)];
		B->AddSlot().AutoHeight()[Field(Tx(TEXT("wizard.launch_method")), Combo(&R.Launch.Method, { TEXT("wheels"), TEXT("ramp"), TEXT("vehicle"), TEXT("hand") },
			{ Tx(TEXT("launch.wheels")), Tx(TEXT("launch.ramp")), Tx(TEXT("launch.vehicle")), Tx(TEXT("launch.hand")) }, [Self]() { Self->Refresh(); }))];
		if (R.Launch.Method == TEXT("ramp")) B->AddSlot().AutoHeight()[Field(Tx(TEXT("launch.angle")), NumEdit(&R.Launch.AngleDeg, 10, 30, 1))];
		if (R.Launch.Method == TEXT("ramp") || R.Launch.Method == TEXT("vehicle")) B->AddSlot().AutoHeight()[Field(Tx(TEXT("launch.release")), NumEdit(&R.Launch.ReleaseKmh, 40, 110, 1))];
		if (R.Launch.Method == TEXT("hand")) B->AddSlot().AutoHeight()[Label(Tx(TEXT("launch.hand_note")), 10, false, GSmvDim)];
	}
	else if (Ty == TEXT("interceptor"))
	{
		B->AddSlot().AutoHeight().Padding(0, 12, 0, 0)[Label(Tx(TEXT("wizard.launch")), 14, true, GSmvAccent)];
		B->AddSlot().AutoHeight()[Field(Tx(TEXT("wizard.launch_method")), Combo(&R.Launch.Method, { TEXT(""), TEXT("pod4"), TEXT("pod8"), TEXT("tube"), TEXT("handheld") },
			{ Tx(TEXT("launch.vertical")), Tx(TEXT("launch.pod4")), Tx(TEXT("launch.pod8")), Tx(TEXT("launch.tube")), Tx(TEXT("launch.handheld")) }, [Self]() { Self->Refresh(); }))];
		if (!R.Launch.Method.IsEmpty())
		{
			B->AddSlot().AutoHeight()[Field(Tx(TEXT("launch.aim_az")), NumEdit(&R.Launch.AimAzDeg, 0, 360, 5))];
			B->AddSlot().AutoHeight()[Field(Tx(TEXT("launch.aim_el")), NumEdit(&R.Launch.AimElDeg, 10, 85, 1))];
		}
	}

	// güç
	B->AddSlot().AutoHeight().Padding(0, 12, 0, 0)[Label(Tx(TEXT("hangar.power")), 14, true, GSmvAccent)];
	if (Ty == TEXT("plane") || Ty == TEXT("vtol") || Ty == TEXT("interceptor")) B->AddSlot().AutoHeight()[Field(Tx(TEXT("power.max_speed")), NumEdit(&R.Power.MaxSpeedMs, -1, 150, 1))];
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("power.fuel_l")), NumEdit(&R.Power.FuelL, -1, 500, 0.5))];
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("power.mah")), NumEdit(&R.Power.ElecMah, -1, 200000, 100))];
	B->AddSlot().AutoHeight()[Label(Tx(TEXT("power.wizard_hint")), 10, false, GSmvDim)];

	// hedefler
	B->AddSlot().AutoHeight().Padding(0, 12, 0, 0)[Label(Tx(TEXT("wizard.targets")), 14, true, GSmvAccent)];
	TargetCount = R.Targets.Num();
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("targets.count")), IntEdit(&TargetCount, 0, 8))];
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("targets.model")), Combo(&TargetModel, { TEXT("tgt:sw300"), TEXT("tgt:lancet"), TEXT("tgt:altius"), TEXT("tgt:octopus") },
		{ FText::FromString(TEXT("Switchblade 300")), FText::FromString(TEXT("ZALA Lancet-3")), FText::FromString(TEXT("Altius-600M")), FText::FromString(TEXT("Octopus-100")) }))];
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("targets.alt")), SNew(SSpinBox<float>).MinValue(10).MaxValue(3000).Value_Lambda([Self]() { return Self->TargetAlt; }).OnValueChanged_Lambda([Self](float V) { Self->TargetAlt = V; }))];
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("targets.diameter")), SNew(SSpinBox<float>).MinValue(50).MaxValue(5000).Value_Lambda([Self]() { return Self->TargetDia; }).OnValueChanged_Lambda([Self](float V) { Self->TargetDia = V; }))];
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("targets.speed")), SNew(SSpinBox<float>).MinValue(1).MaxValue(110).Value_Lambda([Self]() { return Self->TargetSpeed; }).OnValueChanged_Lambda([Self](float V) { Self->TargetSpeed = V; }))];

	// yarış (yerel yapay zekâ)
	B->AddSlot().AutoHeight().Padding(0, 12, 0, 0)[Label(Tx(TEXT("wizard.race")), 14, true, GSmvAccent)];
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("race.kind")), Combo(&F->Race.Kind, { TEXT("none"), TEXT("rings"), TEXT("dogfight") },
		{ Tx(TEXT("race.none")), Tx(TEXT("race.rings")), Tx(TEXT("race.dogfight")) }, [Self, F]() { if (F->Race.Kind == TEXT("dogfight")) F->Race.Bots = 1; Self->Refresh(); }))];
	if (F->Race.Kind != TEXT("none"))
	{
		if (F->Race.Kind == TEXT("rings"))
		{
			B->AddSlot().AutoHeight()[Field(Tx(TEXT("race.bots")), IntEdit(&F->Race.Bots, 1, 7))];
			B->AddSlot().AutoHeight()[Field(Tx(TEXT("race.ring_count")), IntEdit(&F->Race.RingCount, 1, 30))];
		}
		else B->AddSlot().AutoHeight()[Field(Tx(TEXT("race.roles")), Combo(&F->Race.Roles, { TEXT("random"), TEXT("host_chaser"), TEXT("host_evader") },
			{ Tx(TEXT("race.role_random")), Tx(TEXT("race.role_chaser")), Tx(TEXT("race.role_evader")) }))];
		B->AddSlot().AutoHeight()[Field(Tx(TEXT("race.level")), Combo(&F->Race.Level, { TEXT("easy"), TEXT("normal"), TEXT("hard") },
			{ Tx(TEXT("level.easy")), Tx(TEXT("level.normal")), Tx(TEXT("level.hard")) }))];
		B->AddSlot().AutoHeight()[Field(Tx(TEXT("race.seed")), IntEdit(&F->Race.Seed, 0, 2147483646))];
		B->AddSlot().AutoHeight()[Label(Tx(TEXT("race.local_note")), 10, false, GSmvDim)];
	}

	// görüntü / hız / fizik
	B->AddSlot().AutoHeight().Padding(0, 12, 0, 0)[Label(Tx(TEXT("wizard.sim")), 14, true, GSmvAccent)];
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("wizard.camera")), Combo(&R.Camera, { TEXT("day"), TEXT("thermal"), TEXT("night"), TEXT("none") },
		{ Tx(TEXT("cam.day")), Tx(TEXT("cam.thermal")), Tx(TEXT("cam.night")), Tx(TEXT("cam.none")) }))];
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("wizard.speedup")), Combo(&Speed, { TEXT("0.25"), TEXT("0.5"), TEXT("1.0"), TEXT("2.0"), TEXT("4.0"), TEXT("8.0") },
		{ FText::FromString(TEXT("0,25×")), FText::FromString(TEXT("0,5×")), FText::FromString(TEXT("1×")), FText::FromString(TEXT("2×")), FText::FromString(TEXT("4×")), FText::FromString(TEXT("8×")) },
		[F]() { F->Draft.Speedup = FCString::Atod(*Speed); }))];
	const bool bIn = Client() && Client()->IsLoggedIn();
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("wizard.backend")), bIn
		? Combo(&Backend, { TEXT("local"), TEXT("cloud") }, { Tx(TEXT("backend.local")), Tx(TEXT("backend.cloud")) }, [F]() { F->Draft.Backend = Backend == TEXT("cloud") ? ESmvFlightBackend::Cloud : ESmvFlightBackend::LocalSitl; })
		: Label(Tx(TEXT("backend.local_guest"))))];
	B->AddSlot().AutoHeight()[Label(USmvLocalFlightLink::SitlAvailable() ? Tx(TEXT("wizard.sitl_note")) : Tx(TEXT("menu.sitl_missing")), 10, false, GSmvDim)];
	B->AddSlot().AutoHeight().Padding(0, 16, 0, 0)[Btn(Tx(TEXT("wizard.start")), [Self, F]()
	{
		F->Draft.Targets.Reset();
		for (int32 i = 0; i < Self->TargetCount; ++i)
		{
			FSmvTarget T; T.Model = Self->TargetModel; T.AltM = Self->TargetAlt; T.DiameterM = Self->TargetDia; T.SpeedMs = Self->TargetSpeed; T.bCw = (i % 2) == 0;
			T.EastM = (i % 3) * 150.0; T.NorthM = (i / 3) * 150.0;
			F->Draft.Targets.Add(T);
		}
		F->StartFlight();
	}, true)];
	return Page(Tx(TEXT("wizard.title")), SNew(SBox).MaxDesiredWidth(860).HAlign(HAlign_Left)[Panel(B)]);
}

// ---------------------------------------------------------------------------
// yükleniyor / uçuş (duraklatma) / özet
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SSmvShell::Loading()
{
	TWeakObjectPtr<USmvFlow> F = Flow;
	return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(GSmvBg)
	[
		SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(640)
			[
				Panel(SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[Label(Tx(TEXT("loading.title")), 22, true, GSmvAccent)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 12)[SNew(STextBlock).Text_Lambda([F]() { return FText::FromString(F.IsValid() ? F->LoadingText : FString()); })
						.ColorAndOpacity(GSmvText).AutoWrapText(true)]
					+ SVerticalBox::Slot().AutoHeight()[Label(Tx(TEXT("loading.tips")), 11, false, GSmvDim)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 16, 0, 0)[Btn(Tx(TEXT("common.cancel")), [F]() { if (F.IsValid()) F->EndFlight(TEXT("abort")); })], 28.f)
			]
		]
	];
}

TSharedRef<SWidget> SSmvShell::FlightPause()
{
	if (!GameMode.IsValid() || !GameMode->IsPausedMenu()) return SNullWidget::NullWidget;
	TWeakObjectPtr<ASmvGameMode> G = GameMode;
	TWeakObjectPtr<USmvFlow> F = Flow;
	SSmvShell* Self = this;
	return SNew(SBox).HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(FMargin(48, 0))
	[
		SNew(SBox).WidthOverride(460)
		[
			Panel(SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[Label(Tx(TEXT("pause.title")), 20, true, GSmvAccent)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 10)[Label(Tx(TEXT("pause.note")), 10, false, GSmvDim)]
				+ SVerticalBox::Slot().AutoHeight()[Btn(Tx(TEXT("pause.resume")), [G]() { if (G.IsValid()) G->TogglePause(); }, true)]
				+ SVerticalBox::Slot().AutoHeight()[Btn(Tx(TEXT("pause.reset")), [G]() { if (G.IsValid()) { G->Command(TEXT("reset")); G->TogglePause(); } })]
				+ SVerticalBox::Slot().AutoHeight()[Btn(Tx(TEXT("pause.rtl")), [G]() { if (G.IsValid()) { G->Command(TEXT("rtl")); G->TogglePause(); } })]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 4)[Label(Tx(TEXT("servo.title")), 13, true)]
				+ SVerticalBox::Slot().AutoHeight()[Field(Tx(TEXT("servo.ch")), IntEdit(&ServoCh, 1, 16))]
				+ SVerticalBox::Slot().AutoHeight()[Field(Tx(TEXT("servo.pwm")), IntEdit(&ServoPwm, 800, 2200))]
				+ SVerticalBox::Slot().AutoHeight()[Field(Tx(TEXT("servo.cmp")), Combo(&ServoCmp, { TEXT("ge"), TEXT("le"), TEXT("eq") }, { FText::FromString(TEXT("≥")), FText::FromString(TEXT("≤")), FText::FromString(TEXT("=")) }))]
				+ SVerticalBox::Slot().AutoHeight()[Field(Tx(TEXT("servo.delay")), IntEdit(&ServoDelay, 0, 60000))]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Btn(Tx(TEXT("servo.listen")), [G, Self]()
					{
						TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
						P->SetBoolField(TEXT("on"), true); P->SetNumberField(TEXT("ch"), Self->ServoCh); P->SetNumberField(TEXT("pwm"), Self->ServoPwm);
						P->SetStringField(TEXT("cmp"), Self->ServoCmp); P->SetNumberField(TEXT("tol"), 50); P->SetNumberField(TEXT("delay_ms"), Self->ServoDelay);
						if (G.IsValid()) G->Command(TEXT("servo_trigger"), P);
					})]
					+ SHorizontalBox::Slot().AutoWidth()[Btn(Tx(TEXT("servo.off")), [G]()
					{
						TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>(); P->SetBoolField(TEXT("on"), false);
						if (G.IsValid()) G->Command(TEXT("servo_trigger"), P);
					})]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)[SNew(STextBlock).Text_Lambda([G]() { return FText::FromString(G.IsValid() ? G->MissionPlannerLine() : FString()); })
					.ColorAndOpacity(GSmvDim).AutoWrapText(true)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)[Label(Tx(TEXT("pause.keys")), 10, false, GSmvDim)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)[Btn(Tx(TEXT("pause.end")), [F, G]() { if (G.IsValid() && G->IsPausedMenu()) G->TogglePause(); if (F.IsValid()) F->EndFlight(TEXT("user")); })])
		]
	];
}

TSharedRef<SWidget> SSmvShell::Summary()
{
	TWeakObjectPtr<USmvFlow> F = Flow;
	const FSmvFlightSummary& S = Flow->LastSummary;
	TSharedRef<SVerticalBox> B = SNew(SVerticalBox);
	auto Row = [this, &B](const FText& K, const FString& V) { B->AddSlot().AutoHeight()[Field(K, Label(FText::FromString(V), 13, true))]; };
	Row(Tx(TEXT("summary.result")), FSmvI18n::T(FString(TEXT("result.")) + S.Result).ToString() + (S.EndReason.IsEmpty() ? FString() : FString::Printf(TEXT(" (%s)"), *S.EndReason)));
	Row(Tx(TEXT("summary.duration")), FString::Printf(TEXT("%02d:%02d"), (int32)S.DurationS / 60, (int32)S.DurationS % 60));
	Row(Tx(TEXT("summary.distance")), FString::Printf(TEXT("%.2f km"), S.DistM / 1000.0));
	Row(Tx(TEXT("summary.max_speed")), FString::Printf(TEXT("%.0f km/h"), S.MaxSpeedMs * 3.6));
	if (S.AlertScore >= 0) Row(Tx(TEXT("summary.discipline")), FString::Printf(TEXT("%d / 100  (%s)  ·  %d WARNING  %d CAUTION"), S.AlertScore, *S.AlertGrade, S.Warnings, S.Cautions));
	if (S.GatesPassed || S.GatesMissed) Row(Tx(TEXT("summary.gates")), FString::Printf(TEXT("%d / %d"), S.GatesPassed, S.GatesPassed + S.GatesMissed));
	if (S.FuelUsedL > 0) Row(Tx(TEXT("summary.fuel")), FString::Printf(TEXT("%.2f L"), S.FuelUsedL));
	if (S.WhUsed > 0) Row(Tx(TEXT("summary.energy")), FString::Printf(TEXT("%.1f Wh"), S.WhUsed));
	if (S.TargetScore > 0) Row(Tx(TEXT("summary.targets")), FString::Printf(TEXT("%d"), S.TargetScore));
	if (S.Badges.Num()) Row(Tx(TEXT("summary.badges")), FString::Join(S.Badges, TEXT(", ")));
	if (S.bServoTriggerFired) Row(Tx(TEXT("summary.servo")), Tx(TEXT("summary.servo_fired")).ToString());
	B->AddSlot().AutoHeight().Padding(0, 10, 0, 0)[Label(Client() && Client()->IsLoggedIn() && F->Draft.Backend == ESmvFlightBackend::Cloud ? Tx(TEXT("summary.cloud_saved")) : Tx(TEXT("summary.local_saved")), 10, false, GSmvDim)];
	B->AddSlot().AutoHeight().Padding(0, 16, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Btn(Tx(TEXT("summary.again")), [F]() { if (F.IsValid()) F->StartFlight(); }, true)]
		+ SHorizontalBox::Slot().AutoWidth()[Btn(Tx(TEXT("summary.menu")), [F]() { if (F.IsValid()) F->SetScreen(ESmvScreen::Menu); })]
	];
	return Page(Tx(TEXT("summary.title")), SNew(SBox).MaxDesiredWidth(760).HAlign(HAlign_Left)[Panel(B)], false);
}

// ---------------------------------------------------------------------------
// karşılaşma salonu
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SSmvShell::Lobby()
{
	TWeakObjectPtr<USmvFlow> F = Flow;
	SSmvShell* Self = this;
	TSharedRef<SVerticalBox> B = SNew(SVerticalBox);
	if (!(Client() && Client()->IsLoggedIn()))
	{
		B->AddSlot().AutoHeight()[Label(Tx(TEXT("lobby.login_needed")), 13)];
		B->AddSlot().AutoHeight().Padding(0, 10, 0, 0)[Btn(Tx(TEXT("welcome.login")), [F]() { if (F.IsValid()) F->SetScreen(ESmvScreen::Login); }, true)];
		B->AddSlot().AutoHeight().Padding(0, 10, 0, 0)[Btn(Tx(TEXT("lobby.local_race")), [F]() { if (F.IsValid()) { F->NewDraft(); F->Race.Kind = TEXT("rings"); F->SetScreen(ESmvScreen::Wizard); } })];
		return Page(Tx(TEXT("menu.match")), SNew(SBox).MaxDesiredWidth(720).HAlign(HAlign_Left)[Panel(B)]);
	}
	F->EnsureMatchClient();
	USmvMatchClient* M = F->GetMatch();
	if (M && M->HasMatch())
	{
		const FSmvMatch& X = M->Current();
		B->AddSlot().AutoHeight()[Label(FText::FromString(FString::Printf(TEXT("%s · %s · %s"), *X.Code, *FSmvI18n::T(FString(TEXT("race.")) + X.Kind).ToString(), *X.State)), 16, true, GSmvAccent)];
		for (const FSmvMatchPlayer& P : X.Players)
			B->AddSlot().AutoHeight()[Label(FText::FromString(FString::Printf(TEXT("%s%s%s  ·  %s%s"), P.bHost ? TEXT("* ") : TEXT(""), *P.Name, P.bBot ? TEXT(" (YZ)") : TEXT(""),
				P.bReady ? *FSmvI18n::T(TEXT("lobby.ready")).ToString() : *FSmvI18n::T(TEXT("lobby.not_ready")).ToString(), P.Rings ? *FString::Printf(TEXT("  %d/%d"), P.Rings, P.Total) : TEXT(""))), 12)];
		B->AddSlot().AutoHeight().Padding(0, 10, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Btn(Tx(TEXT("lobby.ready_btn")), [F]() { if (F.IsValid() && F->GetMatch()) F->GetMatch()->SetReady(true); }, true)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Btn(Tx(TEXT("lobby.start")), [F]() { if (F.IsValid() && F->GetMatch()) F->GetMatch()->Start(); }, false, TAttribute<bool>(X.bCanStart))]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Btn(Tx(TEXT("lobby.add_bot")), [F, Self]() { if (F.IsValid() && F->GetMatch()) F->GetMatch()->AddBot(Self->MatchLevel); })]
			+ SHorizontalBox::Slot().AutoWidth()[Btn(Tx(TEXT("lobby.leave")), [F]() { if (F.IsValid() && F->GetMatch()) F->GetMatch()->Leave(); })]
		];
		B->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[Label(FSmvI18n::Format(TEXT("lobby.invite"), { { TEXT("code"), X.Code } }), 11, false, GSmvDim)];
	}
	else
	{
		B->AddSlot().AutoHeight()[Label(Tx(TEXT("lobby.create")), 14, true, GSmvAccent)];
		B->AddSlot().AutoHeight()[Field(Tx(TEXT("race.kind")), Combo(&MatchKind, { TEXT("rings"), TEXT("dogfight") }, { Tx(TEXT("race.rings")), Tx(TEXT("race.dogfight")) }))];
		B->AddSlot().AutoHeight()[Field(Tx(TEXT("lobby.password")), TextEdit(&MatchPassword, true))];
		B->AddSlot().AutoHeight()[Field(Tx(TEXT("lobby.max")), IntEdit(&MatchMax, 2, 8))];
		B->AddSlot().AutoHeight()[Btn(Tx(TEXT("lobby.create_btn")), [F, Self]()
		{
			if (F.IsValid() && F->GetMatch()) F->GetMatch()->Create(F->Draft, Self->MatchKind, Self->MatchPassword, Self->MatchKind == TEXT("dogfight") ? 2 : Self->MatchMax, nullptr, nullptr);
		}, true)];
		B->AddSlot().AutoHeight().Padding(0, 14, 0, 0)[Label(Tx(TEXT("lobby.ai")), 14, true, GSmvAccent)];
		B->AddSlot().AutoHeight()[Field(Tx(TEXT("race.level")), Combo(&MatchLevel, { TEXT("easy"), TEXT("normal"), TEXT("hard") }, { Tx(TEXT("level.easy")), Tx(TEXT("level.normal")), Tx(TEXT("level.hard")) }))];
		B->AddSlot().AutoHeight()[Field(Tx(TEXT("race.bots")), IntEdit(&MatchBots, 1, 7))];
		B->AddSlot().AutoHeight()[Btn(Tx(TEXT("lobby.ai_btn")), [F, Self]()
		{
			if (F.IsValid() && F->GetMatch()) F->GetMatch()->CreateAi(F->Draft, Self->MatchKind, Self->MatchLevel, Self->MatchKind == TEXT("dogfight") ? 1 : Self->MatchBots, nullptr, nullptr);
		})];
		B->AddSlot().AutoHeight().Padding(0, 14, 0, 0)[Label(Tx(TEXT("lobby.join")), 14, true, GSmvAccent)];
		B->AddSlot().AutoHeight()[Field(Tx(TEXT("lobby.code")), TextEdit(&MatchCode))];
		B->AddSlot().AutoHeight()[Field(Tx(TEXT("lobby.password")), TextEdit(&MatchPassword, true))];
		B->AddSlot().AutoHeight()[Btn(Tx(TEXT("lobby.join_btn")), [F, Self]() { if (F.IsValid() && F->GetMatch()) F->GetMatch()->Join(Self->MatchCode, Self->MatchPassword); })];
		B->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[Label(Tx(TEXT("lobby.uses_draft")), 10, false, GSmvDim)];
	}
	if (M && !M->LastError.IsEmpty()) B->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[Label(FText::FromString(M->LastError), 11, false, FLinearColor(1.f, 0.45f, 0.35f))];
	return Page(Tx(TEXT("menu.match")), SNew(SBox).MaxDesiredWidth(760).HAlign(HAlign_Left)[Panel(B)]);
}

// ---------------------------------------------------------------------------
// TOP 10 / kayıtlar
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SSmvShell::Leaderboard()
{
	TWeakObjectPtr<USmvFlow> F = Flow;
	TSharedRef<SVerticalBox> B = SNew(SVerticalBox);
	B->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Btn(Tx(TEXT("top10.30d")), [F]() { if (F.IsValid()) { F->LeaderboardPeriod = TEXT("30d"); F->FetchLeaderboard(); } }, F->LeaderboardPeriod == TEXT("30d"))]
		+ SHorizontalBox::Slot().AutoWidth()[Btn(Tx(TEXT("top10.all")), [F]() { if (F.IsValid()) { F->LeaderboardPeriod = TEXT("all"); F->FetchLeaderboard(); } }, F->LeaderboardPeriod == TEXT("all"))]
	];
	for (const FSmvLeaderRow& R : Flow->Leaderboard)
	{
		B->AddSlot().AutoHeight().Padding(0, 4)[Label(FText::FromString(FString::Printf(TEXT("%2d.  %s   %d   (%d galibiyet · %d karşılaşma · %d uçuş)%s"), R.Rank, *R.Name, R.Score, R.Wins, R.Matches, R.Flights,
			R.bDemo ? *FSmvI18n::T(TEXT("top10.demo")).ToString() : TEXT(""))), 13, R.Rank <= 3)];
	}
	B->AddSlot().AutoHeight().Padding(0, 12, 0, 0)[Label(Tx(TEXT("top10.note")), 10, false, GSmvDim)];
	return Page(Tx(TEXT("menu.top10")), SNew(SBox).MaxDesiredWidth(900).HAlign(HAlign_Left)[Panel(B)]);
}

TSharedRef<SWidget> SSmvShell::Logbook()
{
	TSharedRef<SVerticalBox> B = SNew(SVerticalBox);
	if (Data())
	{
		for (const FSmvFlightSummary& S : Data()->Flights())
		{
			B->AddSlot().AutoHeight().Padding(0, 3)[Label(FText::FromString(FString::Printf(TEXT("%s  ·  %s  ·  %02d:%02d  ·  %.2f km%s%s"), *S.FlightId, *FSmvI18n::T(FString(TEXT("result.")) + S.Result).ToString(),
				(int32)S.DurationS / 60, (int32)S.DurationS % 60, S.DistM / 1000.0, S.AlertScore >= 0 ? *FString::Printf(TEXT("  ·  %s %d"), *S.AlertGrade, S.AlertScore) : TEXT(""),
				S.GatesPassed ? *FString::Printf(TEXT("  ·  %d halka"), S.GatesPassed) : TEXT(""))), 12)];
		}
		if (Data()->Flights().Num() == 0) B->AddSlot().AutoHeight()[Label(Tx(TEXT("logbook.empty")), 12, false, GSmvDim)];
	}
	B->AddSlot().AutoHeight().Padding(0, 12, 0, 0)[Label(FText::FromString(FString::Printf(TEXT("%s: %s"), *FSmvI18n::T(TEXT("logbook.bin")).ToString(),
		*FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Simumav"), TEXT("Logs"))))), 10, false, GSmvDim)];
	return Page(Tx(TEXT("menu.logbook")), SNew(SBox).MaxDesiredWidth(900).HAlign(HAlign_Left)[Panel(B)]);
}

// ---------------------------------------------------------------------------
// ayarlar
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SSmvShell::Settings()
{
	SSmvShell* Self = this;
	FSmvSettings& S = EditSettings;
	while (S.RcAxisMap.Num() < 5) S.RcAxisMap.Add(S.RcAxisMap.Num() + 1);
	while (S.RcInvert.Num() < 5) S.RcInvert.Add(false);
	static TArray<FString> AxisStore; AxisStore.SetNum(5);
	static bool InvStore[5];
	for (int32 i = 0; i < 5; ++i) { AxisStore[i] = FString::FromInt(S.RcAxisMap[i]); InvStore[i] = S.RcInvert[i]; }
	TArray<FString> Langs = FSmvI18n::Languages();
	TArray<FText> LangL = { FText::FromString(TEXT("Türkçe")), FText::FromString(TEXT("English")), FText::FromString(TEXT("Deutsch")), FText::FromString(TEXT("Français")),
		FText::FromString(TEXT("Español")), FText::FromString(TEXT("Русский")), FText::FromString(TEXT("العربية")) };
	TArray<FString> Axes = { TEXT("0"), TEXT("1"), TEXT("2"), TEXT("3"), TEXT("4"), TEXT("5"), TEXT("6"), TEXT("7"), TEXT("8") };
	TArray<FText> AxesL = { Tx(TEXT("settings.axis_none")) };
	for (int32 i = 1; i <= 8; ++i) AxesL.Add(FText::FromString(FString::Printf(TEXT("Axis %d"), i)));
	TSharedRef<SVerticalBox> B = SNew(SVerticalBox);
	B->AddSlot().AutoHeight()[Label(Tx(TEXT("settings.general")), 14, true, GSmvAccent)];
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("settings.lang")), Combo(&S.Lang, Langs, LangL))];
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("settings.units")), Combo(&S.Units, { TEXT("metric"), TEXT("imperial") }, { Tx(TEXT("units.metric")), Tx(TEXT("units.imperial")) }))];
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("settings.graphics")), IntEdit(&S.GraphicsQuality, 0, 4))];
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("settings.cesium")), TextEdit(&S.CesiumToken, true, Tx(TEXT("settings.cesium_hint"))))];
	B->AddSlot().AutoHeight().Padding(0, 10, 0, 0)[Label(Tx(TEXT("settings.audio")), 14, true, GSmvAccent)];
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("settings.sound")), Check(&S.bSound))];
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("settings.music")), Check(&S.bMusic))];
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("settings.volume")), NumEdit(&S.Volume, 0, 1, 0.05))];
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("settings.alert_tones")), Check(&S.bAlertTones))];
	B->AddSlot().AutoHeight().Padding(0, 10, 0, 0)[Label(Tx(TEXT("gpws.title")), 14, true, GSmvAccent)];
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("gpws.mode")), Combo(&S.GpwsMode, { TEXT("off"), TEXT("visual"), TEXT("full") }, { Tx(TEXT("gpws.mode_off")), Tx(TEXT("gpws.mode_visual")), Tx(TEXT("gpws.mode_full")) }))];
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("gpws.sens")), Combo(&S.GpwsSens, { TEXT("low"), TEXT("normal"), TEXT("high") }, { Tx(TEXT("gpws.sens_low")), Tx(TEXT("gpws.sens_normal")), Tx(TEXT("gpws.sens_high")) }))];
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("gpws.lang")), Combo(&S.GpwsLang, { TEXT("aviation"), TEXT("ui") }, { Tx(TEXT("gpws.lang_aviation")), Tx(TEXT("gpws.lang_ui")) }))];
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("gpws.bank_copter")), IntEdit(&S.BankCopter, 20, 60))];
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("gpws.bank_plane")), IntEdit(&S.BankPlane, 30, 70))];
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("settings.gauge")), Combo(&S.GaugeTheme, { TEXT("military"), TEXT("fpv_osd"), TEXT("minimal") },
		{ Tx(TEXT("gauge.military")), Tx(TEXT("gauge.fpv_osd")), Tx(TEXT("gauge.minimal")) }))];
	B->AddSlot().AutoHeight().Padding(0, 10, 0, 0)[Label(Tx(TEXT("settings.input")), 14, true, GSmvAccent)];
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("settings.device")), Combo(&S.InputDevice, { TEXT("keyboard"), TEXT("gamepad"), TEXT("rc") },
		{ Tx(TEXT("device.keyboard")), Tx(TEXT("device.gamepad")), Tx(TEXT("device.rc")) }, [Self]() { Self->Refresh(); }))];
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("settings.mode2")), Check(&S.bStickMode2))];
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("settings.expo")), NumEdit(&S.StickExpo, 0, 1, 0.05))];
	if (S.InputDevice == TEXT("rc"))
	{
		const TCHAR* Slots[] = { TEXT("rc.roll"), TEXT("rc.pitch"), TEXT("rc.throttle"), TEXT("rc.yaw"), TEXT("rc.mode") };
		for (int32 i = 0; i < 5; ++i)
		{
			B->AddSlot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)[Field(Tx(Slots[i]), Combo(&AxisStore[i], Axes, AxesL, [Self, i]() { Self->EditSettings.RcAxisMap[i] = FCString::Atoi(*AxisStore[i]); }))]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0)[Label(Tx(TEXT("settings.invert")), 11, false, GSmvDim)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SCheckBox)
					.IsChecked_Lambda([Self, i]() { return Self->EditSettings.RcInvert[i] ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([Self, i](ECheckBoxState St) { Self->EditSettings.RcInvert[i] = St == ECheckBoxState::Checked; })]
			];
		}
		B->AddSlot().AutoHeight()[Label(Tx(TEXT("settings.rc_note")), 10, false, GSmvDim)];
	}
	B->AddSlot().AutoHeight().Padding(0, 10, 0, 0)[Label(Tx(TEXT("settings.mp")), 14, true, GSmvAccent)];
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("settings.mp_out")), Check(&S.bMpUdpOut))];
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("settings.mp_host")), TextEdit(&S.MpUdpHost))];
	B->AddSlot().AutoHeight()[Field(Tx(TEXT("settings.mp_port")), IntEdit(&S.MpUdpPort, 1024, 65535))];
	B->AddSlot().AutoHeight()[Label(Tx(TEXT("settings.mp_note")), 10, false, GSmvDim)];
	B->AddSlot().AutoHeight().Padding(0, 16, 0, 0)[Btn(Tx(TEXT("common.save")), [Self]()
	{
		USmvDataService* D = Self->Data();
		if (!D) return;
		D->MutableSettings() = Self->EditSettings;
		FSmvI18n::SetLanguage(Self->EditSettings.Lang);
		if (Self->GameMode.IsValid()) if (USmvGameInstance* GI = USmvGameInstance::GetSmv(Self->GameMode.Get())) GI->ApplyGraphicsQuality(Self->EditSettings.GraphicsQuality);
		D->SaveSettings(FSmvDone::CreateLambda([Self](bool bOk, const FString& M) { Self->Toast(bOk ? FSmvI18n::T(TEXT("common.saved")).ToString() : M, bOk ? 6 : 3); Self->Refresh(); }));
	}, true)];
	return Page(Tx(TEXT("menu.settings")), SNew(SBox).MaxDesiredWidth(820).HAlign(HAlign_Left)[Panel(B)]);
}

#undef LOCTEXT_NAMESPACE
