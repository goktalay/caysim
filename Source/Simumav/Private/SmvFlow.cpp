#include "SmvFlow.h"
#include "SmvRegion.h"
#include "SmvMapRegistry.h"
#include "SmvFabContent.h"
#include "SimumavModule.h"
#include "SimumavClient.h"
#include "SmvDataService.h"
#include "SmvLocalFlightLink.h"
#include "SmvCloudFlightLink.h"
#include "SmvMatchClient.h"
#include "SmvSteam.h"
#include "SmvI18n.h"
#include "SmvUiPrefs.h"
#include "Engine/GameInstance.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SmvProgress.h"
#include "SmvGameRules.h"

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

void USmvFlow::ResetTo(ESmvScreen S, ESmvScreen Mid)
{
	History.Reset();
	if (S != ESmvScreen::Menu) History.Add(ESmvScreen::Menu);
	if (Mid != ESmvScreen::Menu && Mid != S) History.Add(Mid);
	Screen = S;
	OnScreenChanged.Broadcast(S);
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

bool USmvFlow::NeedsRegion(const FSmvTrack& T)
{
	// Fab haritaları ve FPV parkurları kendi zeminini taşır; duman testinde -SmvNoRegionGate ile eski davranış
	static const bool bNoGate = FParse::Param(FCommandLine::Get(), TEXT("SmvNoRegionGate"));
	return !bNoGate && T.IsReal() && !FSmvFabContent::IsFabWorld(T.WorldMode) && !T.Id.StartsWith(TEXT("fpv:"));
}

FString USmvFlow::ValidateDraft() const
{
	if (Draft.Drone.Id.IsEmpty()) return FSmvI18n::T(TEXT("wizard.err_drone")).ToString();
	if (Draft.Track.Id.IsEmpty()) return FSmvI18n::T(TEXT("wizard.err_track")).ToString();
	// Fab haritaları yalnız bu bilgisayarın paketinde var; bulut SITL'i parkuru sunucudan bekler
	if (Draft.Backend == ESmvFlightBackend::Cloud && FSmvFabContent::IsFabWorld(Draft.Track.WorldMode)) return FSmvI18n::T(TEXT("wizard.err_fab_cloud")).ToString();
	if (Draft.Backend == ESmvFlightBackend::Cloud && !ClientObj->IsLoggedIn()) return FSmvI18n::T(TEXT("wizard.err_cloud_login")).ToString();
	if (Draft.Backend == ESmvFlightBackend::LocalSitl && !USmvLocalFlightLink::SitlAvailable()) return FSmvI18n::T(TEXT("wizard.err_no_sitl")).ToString();
	// gerçek dünya parkuru: bölge haritası (çevrimdışı paket) inmeden girilmez — Parkur › Gerçek Dünya › Bölge haritası
	if (NeedsRegion(Draft.Track) && !FSmvRegions::Get().FindPack(Draft.Track.Lat, Draft.Track.Lon, Draft.Track.RadiusM)) return FSmvI18n::T(TEXT("region.err_need")).ToString();
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
	// Fab haritası: SITL ev konumu haritanın gerçek dünya demiri (kayıt tablosu) — kayıtlı eski parkur kopyaları da aynı yere uçar
	if (FSmvFabContent::IsFabWorld(Draft.Track.WorldMode))
	{
		if (const FSmvMapDef* Md = FSmvMapRegistry::Find(Draft.Track.WorldMode.Mid(4)))
		{
			if (Md->Lat != 0.0) { Draft.Track.Lat = Md->Lat; Draft.Track.Lon = Md->Lon; Draft.Track.GroundMsl = Md->GroundMsl; }
		}
	}
	// bölge paketi: kalkış zemini (SITL ev irtifası) paketin arazisinden — Google / paket dünyasında ev noktası yüzeyde
	if (NeedsRegion(Draft.Track))
		if (const FSmvRegionInfo* Rg = FSmvRegions::Get().FindPack(Draft.Track.Lat, Draft.Track.Lon, Draft.Track.RadiusM))
		{
			FSmvRegionDem Dm;
			if (Dm.Load(*Rg)) Draft.Track.GroundMsl = FMath::Max(0.0, Dm.HeightMsl(Draft.Track.Lat, Draft.Track.Lon));
		}
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
	BindLink();
	LastPath.Reset();
	LastRequest = Draft;
	LoadingText = FSmvI18n::T(TEXT("loading.starting")).ToString();
	bLinkHazir = false;
	SetScreen(ESmvScreen::Loading);
	Link->Start(Draft);
}

void USmvFlow::BindLink()
{
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
			if (St == TEXT("HAZIR") && Self->Screen == ESmvScreen::Loading) Self->RequestFlight();
			if (St == TEXT("HATA") && Self->Screen == ESmvScreen::Loading) { Self->Toast(Self->LoadingText, 3); Self->SetScreen(ESmvScreen::Wizard); }
		}
	});
}

void USmvFlow::RequestFlight()
{
	bLinkHazir = true;
	if (Screen != ESmvScreen::Loading) return;
	if (!FlightGate || FlightGate()) SetScreen(ESmvScreen::Flight);
}

bool USmvFlow::StartReplay(const FString& File, float Speed)
{
	FSmvFlightRequest R;
	TArray<FSmvReplayFrame> Frames;
	if (!SmvReplay::Load(File, R, Frames)) { Toast(FSmvI18n::T(TEXT("replay.bad")).ToString(), 3); return false; }
	ReplaySavedDraft = Draft;
	ReplaySavedRace = Race;
	ReplaySavedFpv = Fpv;
	Draft = R;
	Draft.Backend = ESmvFlightBackend::LocalSitl;
	Race = FSmvLocalRace();
	Fpv = FSmvFpvCfg();
	bReplay = true;
	bEnding = false;
	LastSummary = FSmvFlightSummary();
	LastFpv = FFpvResult();
	USmvReplayFlightLink* L = NewObject<USmvReplayFlightLink>(this);
	L->Init(Frames, Speed);
	Link = L;
	BindLink();
	LoadingText = FSmvI18n::T(TEXT("replay.loading")).ToString();
	bLinkHazir = false;
	SetScreen(ESmvScreen::Loading);
	Link->Start(Draft);
	return true;
}

void USmvFlow::EndFlight(const FString& Reason)
{
	if (!Link || bEnding) return;
	bEnding = true;
	Link->Stop(Reason);
}

void USmvFlow::HandleSummary(const FSmvFlightSummary& In)
{
	if (bReplay)
	{
		// kayıttan oynatma bitti: kayıt yazılmaz, taslak geri yüklenir
		bReplay = false;
		Draft = ReplaySavedDraft;
		Race = ReplaySavedRace;
		Fpv = ReplaySavedFpv;
		SetScreen(ESmvScreen::Replay);
		return;
	}
	LastSummary = In;
	if (LastSummary.Label.IsEmpty()) LastSummary.Label = LastRequest.Drone.Name + (LastRequest.Track.Name.IsEmpty() ? FString() : TEXT(" · ") + LastRequest.Track.Name);
	if (LastSummary.When.IsEmpty()) LastSummary.When = FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M"));
	if (SummaryAugmenter) SummaryAugmenter(LastSummary);
	// deneyim (XP, manevralar, pilot) özet kaydından önce: kayıtta pilot / tür / XP alanları dolu olsun
	AwardXp();
	// Mission Planner'dan (ya da komutla) değiştirilen parametreler dron kaydına kalıcı yazılır (hangar "parametreler kalıcı");
	// SIM_* ve donanımın kendi parametreleri (eklenti aç/kapa, irtifa çiti — hangar donanımından gelir) hasat edilmez
	if (const USmvLocalFlightLink* LL = Cast<USmvLocalFlightLink>(Link))
	{
		const TMap<FString, double> Fixed = LastRequest.Drone.Equipment.SitlParams(LastRequest.Drone.Type);
		for (const FSmvDrone& D : DataObj->Drones())
		{
			if (D.Id != LastRequest.Drone.Id) continue;
			FSmvDrone C = D;
			int32 N = 0;
			for (const TPair<FString, float>& Kv : LL->GetSessionUserParams())
			{
				if (Kv.Key.StartsWith(TEXT("SIM_")) || Kv.Key == TEXT("SCR_ENABLE") || Fixed.Contains(Kv.Key)) continue;
				const double* Old = C.Params.Find(Kv.Key);
				if (Old && FMath::IsNearlyEqual(*Old, (double)Kv.Value, 1e-4)) continue;
				C.Params.Add(Kv.Key, (double)Kv.Value);
				++N;
			}
			if (N > 0)
			{
				DataObj->SaveDrone(C, FSmvDone());
				Toast(FSmvI18n::Format(TEXT("hangar.params_kept"), { { TEXT("n"), FString::FromInt(N) }, { TEXT("name"), C.Name } }).ToString(), 6);
			}
			break;
		}
	}
	DataObj->AddFlightRecord(LastSummary);
	LastReplayFile = SmvReplay::Save(LastRequest, LastSummary, LastPath);
	// ★ Önerilen: son uçuşta ölçülen tüketim (yakıt km/L, ortalama elektrik gücü W)
	{
		const double Kml = (LastSummary.FuelUsedL > 0.01 && LastSummary.DistM > 100) ? (LastSummary.DistM / 1000.0) / LastSummary.FuelUsedL : -1.0;
		const double Wt = (LastSummary.WhUsed > 0.01 && LastSummary.DurationS > 30) ? LastSummary.WhUsed / (LastSummary.DurationS / 3600.0) : -1.0;
		if (Kml > 0 || Wt > 0) FSmvUiPrefs::SetMeasured(LastRequest.Drone.Id, Kml, Wt);
	}
	EvaluateAchievements(LastSummary);
	// yol haritası: önce deneyim ekranı (XP, manevralar, seviye atlama), oradan özet
	SetScreen(LastXp.bValid || LastXp.bPending ? ESmvScreen::Progress : ESmvScreen::Summary);
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
			Self->bLinkHazir = false;
			Self->SetScreen(ESmvScreen::Loading);
			L->OnMessage.AddLambda([Weak](const TSharedPtr<FJsonObject>& Msg)
			{
				USmvFlow* S3 = Weak.Get();
				if (S3 && SmvJson::Str(Msg, TEXT("t")) == TEXT("hello") && S3->Screen == ESmvScreen::Loading) S3->RequestFlight();
			});
			L->AttachExisting(M.MyFlightId);
		}
		if (Self->bPendingInvites && !M.Id.IsEmpty()) Self->SendPendingInvites();
		Self->OnScreenChanged.Broadcast(Self->Screen);
	});
}

// ---------------------------------------------------------------------------
// yol haritası: oyun türü, uçuş akışı, ilerleme (XP), sosyal
// ---------------------------------------------------------------------------

FString USmvFlow::RequiredAddon(const FString& Mode)
{
	if (Mode == TEXT("cargo_drop")) return TEXT("gripper");
	if (Mode == TEXT("spraying")) return TEXT("sprayer");
	return FString();
}

bool USmvFlow::ModeAllowsType(const FString& Mode, const FString& Type, bool bOnline)
{
	const bool bFlying = Type != TEXT("rover");
	if (Mode == TEXT("free")) return true;
	if (Mode == TEXT("rings") || Mode == TEXT("dogfight")) return bFlying;
	if (Mode == TEXT("cargo_drop")) return bFlying && Type != TEXT("interceptor");
	if (Mode == TEXT("spraying")) return Type == TEXT("multirotor") || Type == TEXT("heli") || (!bOnline && Type == TEXT("fpv"));
	// savunma: sunucu yalnız INTERCEPTOR kabul eder; yerelde tüm uçan tipler (yeni oyuncunun hangarında yalnız FPV vardır)
	if (Mode == TEXT("facility_defense") || Mode == TEXT("ship_defense")) return bOnline ? Type == TEXT("interceptor") : bFlying;
	return true;
}

void USmvFlow::ApplyGameMode()
{
	const FString GM = Draft.GameMode.IsEmpty() ? FString(TEXT("free")) : Draft.GameMode;
	Draft.GameMode = GM;
	Fpv = FSmvFpvCfg();
	Race = FSmvLocalRace();
	Race.Kind = TEXT("none");
	Race.Level = AiLevel;
	if (GM == TEXT("rings"))
	{
		Race.Kind = TEXT("rings");
		// tek oyunculu: önerilen yapay zekâ sayısı (modes rings ai.recommended 2); çok oyunculuda sunucu salonu
		Race.Bots = FMath::Clamp(AiCount > 0 ? AiCount : 2, 1, 7);
		Race.RingCount = 10;
	}
	else if (GM == TEXT("dogfight"))
	{
		Race.Kind = TEXT("dogfight");
		Race.Bots = 1;
	}
	Draft.bHasMission = false;
	Draft.Targets.Reset();
	// savunma: yer kontrol istasyonu (sunucu requires.site = station) — sahnede kule, radar ekranı
	if (GM == TEXT("facility_defense") || GM == TEXT("ship_defense")) { Draft.Site.bStation = true; Draft.Site.bRadar = true; }
	// donanım: kamera türü + eklenti / irtifa çiti SITL parametreleri (kullanıcı parametresinin üstüne)
	Draft.Camera = Draft.Drone.Equipment.Camera.IsEmpty() ? FString(TEXT("day")) : Draft.Drone.Equipment.Camera;
	for (const TPair<FString, double>& Kv : Draft.Drone.Equipment.SitlParams(Draft.Drone.Type)) Draft.Drone.Params.Add(Kv.Key, Kv.Value);
	if (const FSmvPilot* P = DataObj->DefaultPilot()) if (Draft.Pilot.Id.IsEmpty()) Draft.Pilot = *P;
	LastGame = FGameResult();
	LastGame.Mode = GM;
}

void USmvFlow::StartGame()
{
	ApplyGameMode();
	if (bMulti && ModeIsMultiplayer(Draft.GameMode))
	{
		if (!ClientObj->IsLoggedIn()) { Toast(FSmvI18n::T(TEXT("fly.multi_login")).ToString(), 4); return; }
		EnsureMatchClient();
		bPendingInvites = true;
		const bool bDog = Draft.GameMode == TEXT("dogfight");
		const int32 Max = bDog ? 2 : FMath::Clamp(1 + InviteEmails.Num() + InviteFriends.Num() + AiCount, 2, 8);
		Match->Create(Draft, Draft.GameMode, FString(), Max, nullptr, nullptr);
		SetScreen(ESmvScreen::Lobby);
		return;
	}
	StartFlight();
}

void USmvFlow::SendPendingInvites()
{
	bPendingInvites = false;
	if (!Match || !Match->HasMatch()) return;
	const FString Mid = Match->Current().Id;
	if (InviteEmails.Num() || InviteFriends.Num())
	{
		TSharedRef<FJsonObject> B = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> E, Fr;
		for (const FString& X : InviteEmails) if (!X.TrimStartAndEnd().IsEmpty()) E.Add(MakeShared<FJsonValueString>(X.TrimStartAndEnd()));
		for (const int64 X : InviteFriends) Fr.Add(MakeShared<FJsonValueNumber>((double)X));
		B->SetArrayField(TEXT("emails"), E);
		B->SetArrayField(TEXT("friend_ids"), Fr);
		TWeakObjectPtr<USmvFlow> Weak(this);
		ClientObj->Post(FString::Printf(TEXT("/api/match/%s/invites"), *Mid), B, FSmvRestDone::CreateLambda([Weak](int32 S, const TSharedPtr<FJsonObject>& O, const FString& C, const FString& M)
		{
			USmvFlow* Self = Weak.Get();
			if (!Self) return;
			if (S >= 200 && S < 300)
			{
				const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
				const int32 N = O.IsValid() && O->TryGetArrayField(TEXT("invites"), A) && A ? A->Num() : 0;
				Self->Toast(FSmvI18n::Format(TEXT("fly.invites_sent"), { { TEXT("n"), FString::FromInt(N) } }).ToString(), 6);
			}
			else Self->Toast(FSmvI18n::T(TEXT("fly.invites_fail")).ToString() + TEXT(" ") + (M.IsEmpty() ? C : M), 4);
		}));
	}
	for (int32 i = 0; i < AiCount && i < 7; ++i) Match->AddBot(AiLevel);
}

void USmvFlow::AwardXp()
{
	LastXp = FXpResult();
	FSmvPilot* Pilot = nullptr;
	const FString Pid = LastRequest.Pilot.Id;
	for (const FSmvPilot& P : DataObj->Pilots()) if (P.Id == Pid) Pilot = const_cast<FSmvPilot*>(&P);
	if (!Pilot && DataObj->DefaultPilot()) Pilot = const_cast<FSmvPilot*>(DataObj->DefaultPilot());
	if (!Pilot) return;
	// ---- olgular (yerel) ----
	SmvCore::FFlightFacts F;
	F.Mode = TCHAR_TO_UTF8(*(LastRequest.GameMode.IsEmpty() ? FString(TEXT("free")) : LastRequest.GameMode));
	F.Result = TCHAR_TO_UTF8(*LastSummary.Result);
	F.DurationS = LastSummary.DurationS;
	F.AirS = LastSummary.ArmedS > 0 ? LastSummary.ArmedS : LastSummary.DurationS;
	F.DistM = LastSummary.DistM;
	F.AlertScore = LastSummary.AlertScore;
	F.TargetScore = LastSummary.TargetScore;
	F.RingsPassed = LastSummary.GatesPassed;
	F.MatchPlayers = 1 + (Race.Kind != TEXT("none") ? Race.Bots : 0);
	F.bBotsOnly = true;
	for (int32 i = 0; i < LastPodium.Num(); ++i) if (LastPodium[i].bMe) { F.RaceRank = i + 1; F.bRaceFinished = LastPodium[i].FinishT >= 0; F.bWon = i == 0 && F.bRaceFinished; }
	if (LastRaceKind == TEXT("dogfight")) F.bWon = LastSummary.Badges.Contains(TEXT("dogfight_win"));
	F.bReleased = LastGame.bReleased;
	F.DropBestM = LastGame.DropBestM;
	F.SprayCoverage = LastGame.SprayCoverage;
	F.DefenseScore = LastGame.DefenseScore;
	F.bDefenseOk = LastGame.bDefenseOk;
	std::vector<SmvCore::FTrackSample> Tr;
	Tr.reserve(LastPath.Num());
	bool bLandedSoft = false;
	for (const FSmvReplayFrame& X : LastPath)
	{
		SmvCore::FTrackSample S;
		S.T = X.T; S.AltRel = X.AltRel; S.Roll = X.Roll; S.Pitch = X.Pitch; S.Yaw = X.Yaw; S.Climb = X.Climb; S.Gs = X.Gs; S.bArmed = X.bArmed;
		Tr.push_back(S);
	}
	if (LastPath.Num()) bLandedSoft = !LastPath.Last().bArmed && FMath::Abs(LastPath.Last().AltRel) <= 2.0;
	F.bLandedSoft = bLandedSoft;
	std::vector<SmvCore::FXpPart> Parts;
	std::vector<SmvCore::FManeuver> Mans;
	const int32 Gain = SmvCore::FlightXp(F, Tr, Parts, Mans);
	LastXp.Xp = Gain;
	for (const SmvCore::FXpPart& P : Parts) LastXp.Parts.Add(TPair<FString, int32>(UTF8_TO_TCHAR(P.Key.c_str()), P.Xp));
	for (const SmvCore::FManeuver& M : Mans)
	{
		LastXp.Maneuvers.Add(TPair<FString, int32>(UTF8_TO_TCHAR(M.Key.c_str()), M.Count));
		LastSummary.Maneuvers.Add(FString::Printf(TEXT("%s:%d"), UTF8_TO_TCHAR(M.Key.c_str()), M.Count));
	}
	LastXp.Before = Pilot->Xp;
	LastXp.LevelBefore = SmvCore::LevelForXp(Pilot->Xp);
	LastSummary.Xp = Gain;
	LastSummary.PilotId = Pilot->Id;
	LastSummary.PilotName = Pilot->Name;
	LastSummary.GameMode = LastRequest.GameMode;
	LastSummary.GameScore = LastGame.Score;
	auto Medals = [](int32 L0, int32 L1, TArray<FString>& Out)
	{
		for (const SmvCore::FMedal& M : SmvCore::Medals()) if (M.Level > L0 && M.Level <= L1) Out.Add(UTF8_TO_TCHAR(M.Id));
	};
	const bool bOnline = ClientObj->IsLoggedIn() && DataObj->GetMode() == ESmvAccountMode::Online;
	const bool bCloud = LastRequest.Backend == ESmvFlightBackend::Cloud;
	if (bOnline && bCloud)
	{
		// bulut uçuşu: XP'yi sunucu hesaplar (flights.summary) — seviye yanıtı gelince ekran tazelenir
		LastXp.bServer = true;
		LastXp.bPending = true;
		FetchPilotLevel(Pilot->Id);
		return;
	}
	FSmvPilot P = *Pilot;
	FSmvXpEntry E;
	E.Xp = Gain; E.At = FDateTime::Now().ToIso8601(); E.Mode = LastRequest.GameMode; E.DurationS = (int32)LastSummary.DurationS;
	P.Xp += Gain;
	P.Level = SmvCore::LevelForXp(P.Xp);
	P.Flights += 1;
	P.Hours += LastSummary.DurationS / 3600.0;
	if (Gain > 0 && !bOnline) P.XpLog.Add(E);
	if (P.XpLog.Num() > 200) P.XpLog.RemoveAt(0, P.XpLog.Num() - 200);
	LastXp.After = P.Xp;
	LastXp.LevelAfter = P.Level;
	LastXp.bValid = true;
	Medals(LastXp.LevelBefore, LastXp.LevelAfter, LastXp.NewMedals);
	if (bOnline && Gain > 0 && SmvJson::IsServerId(P.Id))
	{
		// hesapta yerel SITL uçuşu: sunucuya doğrulanmamış XP olarak aktarılır (POST /api/crew/{id}/xp/import; toplam ≤ seviye 10)
		TSharedRef<FJsonObject> B = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
		J->SetNumberField(TEXT("xp"), FMath::Min(Gain, 1000));
		J->SetStringField(TEXT("at"), E.At.Left(25));
		if (!E.Mode.IsEmpty()) J->SetStringField(TEXT("mode"), E.Mode);
		J->SetNumberField(TEXT("duration_s"), FMath::Clamp(E.DurationS, 0, 36000));
		TArray<TSharedPtr<FJsonValue>> A; A.Add(MakeShared<FJsonValueObject>(J));
		B->SetArrayField(TEXT("entries"), A);
		const FString PidS = P.Id;
		TWeakObjectPtr<USmvFlow> Weak(this);
		ClientObj->Post(FString::Printf(TEXT("/api/crew/%s/xp/import"), *PidS), B, FSmvRestDone::CreateLambda([Weak, PidS](int32 S, const TSharedPtr<FJsonObject>& O, const FString&, const FString&)
		{
			USmvFlow* Self = Weak.Get();
			if (!Self || S < 200 || S >= 300 || !O.IsValid()) return;
			Self->LastXp.Note = FSmvI18n::T(TEXT("xp.import_note")).ToString();
			Self->FetchPilotLevel(PidS);
		}));
		return;
	}
	DataObj->SavePilot(P, FSmvDone());
}

void USmvFlow::FetchPilotLevel(const FString& PilotId)
{
	if (!ClientObj->IsLoggedIn() || !SmvJson::IsServerId(PilotId)) return;
	TWeakObjectPtr<USmvFlow> Weak(this);
	const FString Pid = PilotId;
	ClientObj->Get(FString::Printf(TEXT("/api/crew/%s/level"), *Pid), FSmvRestDone::CreateLambda([Weak, Pid](int32 S, const TSharedPtr<FJsonObject>& O, const FString&, const FString&)
	{
		USmvFlow* Self = Weak.Get();
		if (!Self || S != 200 || !O.IsValid()) return;
		const int64 Xp = (int64)SmvJson::Num(O, TEXT("xp"));
		const int32 Lv = FMath::Clamp((int32)SmvJson::Num(O, TEXT("level"), 1), 1, 100);
		for (const FSmvPilot& P : Self->DataObj->Pilots())
		{
			if (P.Id != Pid) continue;
			FSmvPilot& M = const_cast<FSmvPilot&>(P);
			M.Xp = Xp; M.Level = Lv; M.XpUnverified = (int64)SmvJson::Num(O, TEXT("xp_unverified"));
		}
		if (Self->LastXp.bPending)
		{
			Self->LastXp.bPending = false;
			Self->LastXp.bValid = true;
			Self->LastXp.After = Xp;
			Self->LastXp.LevelAfter = Lv;
			if (Self->LastXp.bServer)
			{
				// sunucu bu uçuş için yazdığı XP (son olay)
				const TArray<TSharedPtr<FJsonValue>>* Ev = nullptr;
				if (O->TryGetArrayField(TEXT("events"), Ev) && Ev && Ev->Num())
				{
					const TSharedPtr<FJsonObject> E0 = (*Ev)[0]->AsObject();
					Self->LastXp.Xp = (int32)SmvJson::Num(E0, TEXT("xp"));
					Self->LastXp.Parts.Reset();
					if (const TSharedPtr<FJsonObject> Pr = SmvJson::Obj(E0, TEXT("parts")))
						for (const auto& Kv : Pr->Values) Self->LastXp.Parts.Add(TPair<FString, int32>(Kv.Key, (int32)Kv.Value->AsNumber()));
				}
				Self->LastXp.Before = FMath::Max<int64>(0, Xp - Self->LastXp.Xp);
				Self->LastXp.LevelBefore = SmvCore::LevelForXp(Self->LastXp.Before);
			}
		}
		Self->OnScreenChanged.Broadcast(Self->Screen);
	}));
}

void USmvFlow::FetchPilotFlights(const FString& PilotId)
{
	PilotFlights.Reset();
	const bool bOnline = ClientObj->IsLoggedIn() && DataObj->GetMode() == ESmvAccountMode::Online;
	if (!bOnline || !SmvJson::IsServerId(PilotId))
	{
		// misafir: yerel kayıtlar (pilot kimliği; eski kayıtlarda kimlik yok → varsayılan pilota sayılır)
		const FSmvPilot* Def = DataObj->DefaultPilot();
		for (const FSmvFlightSummary& S : DataObj->Flights())
			if (S.PilotId == PilotId || (S.PilotId.IsEmpty() && Def && Def->Id == PilotId)) PilotFlights.Add(S);
		OnScreenChanged.Broadcast(Screen);
		return;
	}
	bPilotFlightsBusy = true;
	TWeakObjectPtr<USmvFlow> Weak(this);
	ClientObj->Get(FString::Printf(TEXT("/api/crew/%s/flights?limit=50"), *PilotId), FSmvRestDone::CreateLambda([Weak](int32 S, const TSharedPtr<FJsonObject>& O, const FString& C, const FString& M)
	{
		USmvFlow* Self = Weak.Get();
		if (!Self) return;
		Self->bPilotFlightsBusy = false;
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (S == 200 && O.IsValid() && O->TryGetArrayField(TEXT("flights"), A) && A)
		{
			for (const TSharedPtr<FJsonValue>& V : *A)
			{
				const TSharedPtr<FJsonObject> J = V->AsObject();
				FSmvFlightSummary X = FSmvFlightSummary::FromServerFlight(J);
				X.GameMode = SmvJson::Str(J, TEXT("mode"));
				X.Xp = (int32)SmvJson::Num(J, TEXT("xp"));
				X.When = SmvJson::Str(J, TEXT("started_at")).Left(16).Replace(TEXT("T"), TEXT(" "));
				X.Label = SmvJson::Str(J, TEXT("drone_name"), SmvJson::Str(J, TEXT("label")));
				Self->PilotFlights.Add(X);
			}
		}
		else if (S != 200) Self->Toast(M.IsEmpty() ? C : M, 4);
		Self->OnScreenChanged.Broadcast(Self->Screen);
	}));
}

void USmvFlow::SwitchPilot(const FString& PilotId)
{
	for (const FSmvPilot& P : DataObj->Pilots())
	{
		if (P.Id != PilotId) continue;
		Draft.Pilot = P;
		DataObj->SetDefault(TEXT("pilot"), PilotId);
		Toast(FSmvI18n::Format(TEXT("crew.switched"), { { TEXT("name"), P.Name } }).ToString(), 6);
		break;
	}
	OnScreenChanged.Broadcast(Screen);
}

void USmvFlow::FetchFriends()
{
	FriendsError.Empty();
	if (!ClientObj->IsLoggedIn()) { Friends.Reset(); OnScreenChanged.Broadcast(Screen); return; }
	bFriendsBusy = true;
	TWeakObjectPtr<USmvFlow> Weak(this);
	ClientObj->Get(TEXT("/api/friends"), FSmvRestDone::CreateLambda([Weak](int32 S, const TSharedPtr<FJsonObject>& O, const FString& C, const FString& M)
	{
		USmvFlow* Self = Weak.Get();
		if (!Self) return;
		Self->bFriendsBusy = false;
		Self->Friends.Reset();
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (S == 200 && O.IsValid() && O->TryGetArrayField(TEXT("friends"), A) && A)
		{
			for (const TSharedPtr<FJsonValue>& V : *A)
			{
				const TSharedPtr<FJsonObject> J = V->AsObject();
				FFriend F;
				F.Id = (int64)SmvJson::Num(J, TEXT("user_id"));
				F.Name = SmvJson::Str(J, TEXT("name"));
				F.Email = SmvJson::Str(J, TEXT("email"));
				F.Played = (int32)SmvJson::Num(J, TEXT("played_count"));
				F.Last = SmvJson::Str(J, TEXT("last_played_at"), SmvJson::Str(J, TEXT("added_at"))).Left(10);
				F.bAuto = SmvJson::Bool(J, TEXT("auto"));
				F.bFriend = SmvJson::Bool(J, TEXT("friend"));
				F.Avatar = FSmvAvatar::FromJson(SmvJson::Obj(J, TEXT("avatar")));
				Self->Friends.Add(F);
			}
		}
		else if (S == 404) Self->FriendsError = FSmvI18n::T(TEXT("friends.api_missing")).ToString();
		else if (S != 200) Self->FriendsError = M.IsEmpty() ? C : M;
		Self->OnScreenChanged.Broadcast(Self->Screen);
	}));
}

void USmvFlow::AddFriend(const FString& Email)
{
	if (!ClientObj->IsLoggedIn() || Email.TrimStartAndEnd().IsEmpty()) return;
	TSharedRef<FJsonObject> B = MakeShared<FJsonObject>();
	B->SetStringField(TEXT("email"), Email.TrimStartAndEnd());
	TWeakObjectPtr<USmvFlow> Weak(this);
	ClientObj->Post(TEXT("/api/friends"), B, FSmvRestDone::CreateLambda([Weak](int32 S, const TSharedPtr<FJsonObject>&, const FString& C, const FString& M)
	{
		USmvFlow* Self = Weak.Get();
		if (!Self) return;
		Self->Toast(S >= 200 && S < 300 ? FSmvI18n::T(TEXT("friends.added")).ToString() : (M.IsEmpty() ? C : M), S >= 200 && S < 300 ? 6 : 4);
		Self->FetchFriends();
	}));
}

void USmvFlow::RemoveFriend(int64 Id)
{
	if (!ClientObj->IsLoggedIn()) return;
	TWeakObjectPtr<USmvFlow> Weak(this);
	ClientObj->Delete(FString::Printf(TEXT("/api/friends/%lld"), (long long)Id), FSmvRestDone::CreateLambda([Weak](int32 S, const TSharedPtr<FJsonObject>&, const FString& C, const FString& M)
	{
		USmvFlow* Self = Weak.Get();
		if (!Self) return;
		if (S < 200 || S >= 300) Self->Toast(M.IsEmpty() ? C : M, 4);
		Self->FetchFriends();
	}));
}

void USmvFlow::FetchInvites()
{
	if (!ClientObj->IsLoggedIn()) { Invites.Reset(); return; }
	TWeakObjectPtr<USmvFlow> Weak(this);
	ClientObj->Get(TEXT("/api/invites"), FSmvRestDone::CreateLambda([Weak](int32 S, const TSharedPtr<FJsonObject>& O, const FString&, const FString&)
	{
		USmvFlow* Self = Weak.Get();
		if (!Self) return;
		Self->Invites.Reset();
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (S == 200 && O.IsValid() && O->TryGetArrayField(TEXT("invites"), A) && A)
			for (const TSharedPtr<FJsonValue>& V : *A) if (V.IsValid() && V->AsObject().IsValid()) Self->Invites.Add(V->AsObject());
		Self->OnScreenChanged.Broadcast(Self->Screen);
	}));
}

void USmvFlow::AnswerInvite(int64 Id, bool bAccept)
{
	if (!ClientObj->IsLoggedIn()) return;
	TWeakObjectPtr<USmvFlow> Weak(this);
	ClientObj->Post(FString::Printf(TEXT("/api/invites/%lld/%s"), (long long)Id, bAccept ? TEXT("accept") : TEXT("decline")), MakeShared<FJsonObject>(),
		FSmvRestDone::CreateLambda([Weak, bAccept](int32 S, const TSharedPtr<FJsonObject>& O, const FString& C, const FString& M)
	{
		USmvFlow* Self = Weak.Get();
		if (!Self) return;
		if (S < 200 || S >= 300) { Self->Toast(M.IsEmpty() ? C : M, 4); Self->FetchInvites(); return; }
		if (bAccept)
		{
			Self->EnsureMatchClient();
			Self->Match->Attach(O);
			Self->SetScreen(ESmvScreen::Lobby);
		}
		Self->FetchInvites();
	}));
}
