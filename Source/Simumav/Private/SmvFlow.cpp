#include "SmvFlow.h"
#include "SmvFabContent.h"
#include "SimumavModule.h"
#include "SimumavClient.h"
#include "SmvDataService.h"
#include "SmvLocalFlightLink.h"
#include "SmvCloudFlightLink.h"
#include "SmvMatchClient.h"
#include "SmvSteam.h"
#include "SmvI18n.h"
#include "Engine/GameInstance.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

static FString SmvWelcomeFlag() { return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Simumav"), TEXT("welcome.done")); }

void USmvFlow::Init(USimumavClient* InClient, USmvDataService* InData)
{
	ClientObj = InClient;
	DataObj = InData;
	bFirstRun = !FPaths::FileExists(SmvWelcomeFlag());
	Screen = bFirstRun ? ESmvScreen::Welcome : ESmvScreen::Menu;
	NewDraft();
}

void USmvFlow::MarkWelcomeDone()
{
	bFirstRun = false;
	FFileHelper::SaveStringToFile(TEXT("1"), *SmvWelcomeFlag());
}

void USmvFlow::SetScreen(ESmvScreen S)
{
	if (S == Screen) { OnScreenChanged.Broadcast(S); return; }
	if (Screen != ESmvScreen::Loading && Screen != ESmvScreen::Flight) History.Add(Screen);
	if (History.Num() > 16) History.RemoveAt(0);
	Screen = S;
	OnScreenChanged.Broadcast(S);
}

void USmvFlow::Back()
{
	if (Screen == ESmvScreen::Flight || Screen == ESmvScreen::Loading) return;   // uçuştan çıkış EndFlight ile
	ESmvScreen Prev = ESmvScreen::Menu;
	while (History.Num())
	{
		Prev = History.Pop();
		if (Prev != Screen && Prev != ESmvScreen::Summary) break;
	}
	Screen = Prev;
	OnScreenChanged.Broadcast(Screen);
}

// ---------------------------------------------------------------------------
// hesap
// ---------------------------------------------------------------------------

void USmvFlow::ContinueAsGuest()
{
	MarkWelcomeDone();
	DataObj->LoadLocal();
	NewDraft();
	SetScreen(ESmvScreen::Menu);
}

void USmvFlow::Login(const FString& Email, const FString& Password)
{
	if (bLoginBusy) return;
	bLoginBusy = true;
	LoginError.Empty();
	OnScreenChanged.Broadcast(Screen);
	TWeakObjectPtr<USmvFlow> Weak(this);
	ClientObj->Login(Email, Password, [Weak](bool bOk, const FString& Code, const FString& Msg)
	{
		USmvFlow* Self = Weak.Get();
		if (!Self) return;
		Self->bLoginBusy = false;
		if (!bOk)
		{
			if (Code == TEXT("unverified")) Self->LoginError = FSmvI18n::T(TEXT("login.err_unverified")).ToString();
			else if (Code == TEXT("bad_credentials")) Self->LoginError = FSmvI18n::T(TEXT("login.err_credentials")).ToString();
			else if (Code == TEXT("rate_limited")) Self->LoginError = FSmvI18n::T(TEXT("login.err_rate")).ToString();
			else if (Code == TEXT("network")) Self->LoginError = FSmvI18n::T(TEXT("login.err_network")).ToString();
			else Self->LoginError = Msg.IsEmpty() ? Code : Msg;
			Self->OnScreenChanged.Broadcast(Self->Screen);
			return;
		}
		Self->MarkWelcomeDone();
		Self->bOfferImport = Self->DataObj->HasGuestData();
		Self->DataObj->RefreshOnline(FSmvDone::CreateLambda([Weak](bool, const FString&)
		{
			if (USmvFlow* S2 = Weak.Get()) { S2->NewDraft(); S2->SetScreen(S2->bOfferImport ? ESmvScreen::Account : ESmvScreen::Menu); }
		}));
		Self->Toast(FSmvI18n::Format(TEXT("login.welcome"), { { TEXT("name"), Self->ClientObj->GetUser().DisplayName } }).ToString(), 6);
	});
}

void USmvFlow::Logout()
{
	TWeakObjectPtr<USmvFlow> Weak(this);
	ClientObj->Logout([Weak]()
	{
		if (USmvFlow* Self = Weak.Get())
		{
			Self->DataObj->LoadLocal();
			Self->NewDraft();
			Self->SetScreen(ESmvScreen::Menu);
		}
	});
}

void USmvFlow::ImportGuest()
{
	TWeakObjectPtr<USmvFlow> Weak(this);
	DataObj->ImportGuestToAccount(FSmvDone::CreateLambda([Weak](bool bOk, const FString& Msg)
	{
		if (USmvFlow* Self = Weak.Get())
		{
			Self->bOfferImport = false;
			Self->Toast(Msg, bOk ? 6 : 4);
			Self->SetScreen(ESmvScreen::Menu);
		}
	}));
}

// ---------------------------------------------------------------------------
// sihirbaz
// ---------------------------------------------------------------------------

void USmvFlow::NewDraft()
{
	Draft = FSmvFlightRequest();
	Fpv = FSmvFpvCfg();
	if (const FSmvDrone* D = DataObj->DefaultDrone()) Draft.Drone = *D;
	if (const FSmvPilot* P = DataObj->DefaultPilot()) Draft.Pilot = *P;
	if (const FSmvTrack* T = DataObj->DefaultTrack()) Draft.Track = *T;
	Draft.Backend = USmvLocalFlightLink::SitlAvailable() || !ClientObj->IsLoggedIn() ? ESmvFlightBackend::LocalSitl : ESmvFlightBackend::Cloud;
	if (Draft.Drone.Type == TEXT("plane")) Draft.Launch.Method = TEXT("wheels");
}

FString USmvFlow::ValidateDraft() const
{
	if (Draft.Drone.Id.IsEmpty()) return FSmvI18n::T(TEXT("wizard.err_drone")).ToString();
	if (Draft.Track.Id.IsEmpty()) return FSmvI18n::T(TEXT("wizard.err_track")).ToString();
	// Fab haritaları yalnız bu bilgisayarın paketinde var; bulut SITL'i parkuru sunucudan bekler
	if (Draft.Backend == ESmvFlightBackend::Cloud && FSmvFabContent::IsFabWorld(Draft.Track.WorldMode)) return FSmvI18n::T(TEXT("wizard.err_fab_cloud")).ToString();
	if (Draft.Backend == ESmvFlightBackend::Cloud && !ClientObj->IsLoggedIn()) return FSmvI18n::T(TEXT("wizard.err_cloud_login")).ToString();
	if (Draft.Backend == ESmvFlightBackend::LocalSitl && !USmvLocalFlightLink::SitlAvailable()) return FSmvI18n::T(TEXT("wizard.err_no_sitl")).ToString();
	if (Race.Kind != TEXT("none") && (Draft.Drone.Type == TEXT("rover"))) return FSmvI18n::T(TEXT("wizard.err_race_rover")).ToString();
	if (Race.Kind == TEXT("dogfight") && Race.Bots != 1) return FSmvI18n::T(TEXT("wizard.err_dogfight_bots")).ToString();
	if (Fpv.IsActive())
	{
		const FString& Ty = Draft.Drone.Type;
		if (Ty != TEXT("multirotor") && Ty != TEXT("fpv") && Ty != TEXT("heli")) return FSmvI18n::T(TEXT("fpv.type_warn")).ToString();
		if (Draft.Backend == ESmvFlightBackend::Cloud) return FSmvI18n::T(TEXT("fpv.local_only")).ToString();
	}
	return FString();
}

FSmvTrack USmvFlow::MakeFpvTrack(const FSmvFpvCourse& C, const FSmvTrack& Base)
{
	FSmvTrack T;
	T.Id = FString(TEXT("fpv:")) + C.Id;
	T.Name = C.Name().ToString();
	T.Lat = Base.Lat; T.Lon = Base.Lon; T.GroundMsl = Base.GroundMsl;
	T.RadiusM = 1000;
	const bool bIndoor = C.EnvSky == TEXT("indoor");
	T.WindMs = bIndoor ? 0.0 : 1.5; T.WindGust = 0; T.WindDir = 270;
	T.TimeOfDay = C.EnvTime == TEXT("night") ? TEXT("night") : C.EnvTime == TEXT("dusk") ? TEXT("dusk") : TEXT("day");
	T.Weather = TEXT("clear");
	T.WorldMode = TEXT("procedural");
	T.World.Seed = 4000 + C.Difficulty * 7 + C.Id.Len();
	T.World.SizeM = 4000;
	T.World.TreeDensity = 0; T.World.BuildingCount = 0; T.World.bRunway = false; T.World.Water = 0;
	T.World.MountainCount = bIndoor ? 0 : (C.bCanyon || C.bHill ? 8 : 3);
	T.World.MountainHeight = C.bCanyon ? 600 : 300;
	T.World.Fog = C.EnvFog;
	T.World.RunwayHeading = C.StartYaw;
	return T;
}

void USmvFlow::StartFlight()
{
	const FString Err = ValidateDraft();
	if (!Err.IsEmpty()) { Toast(Err, 3); return; }
	bEnding = false;
	LastSummary = FSmvFlightSummary();
	LastFpv = FFpvResult();
	if (Draft.Backend == ESmvFlightBackend::Cloud)
	{
		USmvCloudFlightLink* L = NewObject<USmvCloudFlightLink>(this);
		L->Init(ClientObj);
		Link = L;
	}
	else
	{
		USmvLocalFlightLink* L = NewObject<USmvLocalFlightLink>(this);
		const FSmvSettings& S = DataObj->Settings();
		L->SetMissionPlannerOut(S.bMpUdpOut, S.MpUdpHost, S.MpUdpPort);
		Link = L;
	}
	TWeakObjectPtr<USmvFlow> Weak(this);
	Link->OnSummary.AddLambda([Weak](const FSmvFlightSummary& S) { if (USmvFlow* Self = Weak.Get()) Self->HandleSummary(S); });
	Link->OnMessage.AddLambda([Weak](const TSharedPtr<FJsonObject>& M)
	{
		USmvFlow* Self = Weak.Get();
		if (!Self) return;
		const FString T = SmvJson::Str(M, TEXT("t"));
		if (T == TEXT("event")) { Self->LoadingText = SmvJson::Str(M, TEXT("text")); if (Self->Screen == ESmvScreen::Loading) Self->OnScreenChanged.Broadcast(Self->Screen); }
		if (T == TEXT("state"))
		{
			const FString St = SmvJson::Str(M, TEXT("state"));
			if (St == TEXT("HAZIR") && Self->Screen == ESmvScreen::Loading) Self->SetScreen(ESmvScreen::Flight);
			if (St == TEXT("HATA") && Self->Screen == ESmvScreen::Loading) { Self->Toast(Self->LoadingText, 3); Self->SetScreen(ESmvScreen::Wizard); }
		}
	});
	LoadingText = FSmvI18n::T(TEXT("loading.starting")).ToString();
	SetScreen(ESmvScreen::Loading);
	Link->Start(Draft);
}

void USmvFlow::EndFlight(const FString& Reason)
{
	if (!Link || bEnding) return;
	bEnding = true;
	Link->Stop(Reason);
}

void USmvFlow::HandleSummary(const FSmvFlightSummary& In)
{
	LastSummary = In;
	if (SummaryAugmenter) SummaryAugmenter(LastSummary);
	DataObj->AddFlightRecord(LastSummary);
	EvaluateAchievements(LastSummary);
	SetScreen(ESmvScreen::Summary);
}

void USmvFlow::EvaluateAchievements(const FSmvFlightSummary& S)
{
	const UGameInstance* GI = Cast<UGameInstance>(GetOuter());
	USmvSteamSubsystem* Steam = GI ? GI->GetSubsystem<USmvSteamSubsystem>() : nullptr;
	if (!Steam) return;
	if (S.ArmedS >= 10) Steam->Unlock(SmvAch::FirstFlight);
	if (S.Result == TEXT("ok") && S.ArmedS >= 60) Steam->Unlock(SmvAch::FirstLanding);
	if (S.AlertGrade == TEXT("A") && S.ArmedS >= 60) Steam->Unlock(SmvAch::DisciplineA);
	if (S.GatesPassed > 0 && S.GatesMissed == 0 && S.GatesPassed == Draft.Track.Gates.Num()) Steam->Unlock(SmvAch::AllRings);
	if (S.TargetScore > 0) Steam->Unlock(SmvAch::Interceptor);
	if (S.Badges.Contains(TEXT("ace"))) Steam->Unlock(SmvAch::Ace);
	if (S.Badges.Contains(TEXT("race_win"))) Steam->Unlock(SmvAch::RingWin);
	if (S.Badges.Contains(TEXT("dogfight_win"))) Steam->Unlock(SmvAch::DogfightWin);
	if (Draft.Launch.Method == TEXT("ramp") && S.ArmedS > 5) Steam->Unlock(SmvAch::Ramp);
	if (Draft.Drone.Type == TEXT("rover") && S.DistM > 100) Steam->Unlock(SmvAch::Rover);
	if (Draft.Backend == ESmvFlightBackend::Cloud && S.ArmedS > 10) Steam->Unlock(SmvAch::CloudFlight);
	double Total = 0;
	for (const FSmvFlightSummary& F : DataObj->Flights()) Total += F.DurationS;
	if (Total >= 10 * 3600) Steam->Unlock(SmvAch::Hours10);
}

// ---------------------------------------------------------------------------

void USmvFlow::FetchLeaderboard()
{
	TWeakObjectPtr<USmvFlow> Weak(this);
	ClientObj->Request(TEXT("GET"), FString::Printf(TEXT("/api/leaderboard?period=%s"), *LeaderboardPeriod), nullptr,
		FSmvRestDone::CreateLambda([Weak](int32 S, const TSharedPtr<FJsonObject>& O, const FString& C, const FString& M)
	{
		USmvFlow* Self = Weak.Get();
		if (!Self) return;
		Self->Leaderboard.Reset();
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (S == 200 && O->TryGetArrayField(TEXT("pilots"), A) && A)
		{
			for (const TSharedPtr<FJsonValue>& V : *A) Self->Leaderboard.Add(FSmvLeaderRow::FromJson(V->AsObject()));
		}
		else Self->Toast(M.IsEmpty() ? C : M, 4);
		Self->OnScreenChanged.Broadcast(Self->Screen);
	}), false);
}

void USmvFlow::EnsureMatchClient()
{
	if (Match) return;
	Match = NewObject<USmvMatchClient>(this);
	Match->Init(ClientObj);
	TWeakObjectPtr<USmvFlow> Weak(this);
	Match->OnUpdated.AddLambda([Weak](const FSmvMatch& M)
	{
		USmvFlow* Self = Weak.Get();
		if (!Self) return;
		// sunucu uçuşu açtı (HAZIRLIK) → kendi uçuşuna bağlan
		if ((M.State == TEXT("HAZIRLIK") || M.State == TEXT("GERI_SAYIM") || M.State == TEXT("YARIS")) && !M.MyFlightId.IsEmpty()
			&& (!Self->Link || Self->Link->Info().FlightId != M.MyFlightId) && Self->Screen == ESmvScreen::Lobby)
		{
			USmvCloudFlightLink* L = NewObject<USmvCloudFlightLink>(Self);
			L->Init(Self->ClientObj);
			Self->Link = L;
			Self->bEnding = false;
			L->OnSummary.AddLambda([Weak](const FSmvFlightSummary& S) { if (USmvFlow* S2 = Weak.Get()) S2->HandleSummary(S); });
			Self->LoadingText = FSmvI18n::T(TEXT("loading.match")).ToString();
			Self->SetScreen(ESmvScreen::Loading);
			L->OnMessage.AddLambda([Weak](const TSharedPtr<FJsonObject>& Msg)
			{
				USmvFlow* S3 = Weak.Get();
				if (S3 && SmvJson::Str(Msg, TEXT("t")) == TEXT("hello") && S3->Screen == ESmvScreen::Loading) S3->SetScreen(ESmvScreen::Flight);
			});
			L->AttachExisting(M.MyFlightId);
		}
		Self->OnScreenChanged.Broadcast(Self->Screen);
	});
}
