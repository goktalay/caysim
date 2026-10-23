#include "SessionWizard.h"
#include "CaySim.h"
#include "HepgoClient.h"
#include "CaySimSaveGame.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Kismet/GameplayStatics.h"

void UCaySimSessionWizard::Init(UHepgoClient* Client)
{
	UnbindClient();
	Hepgo = Client;
	BindClient();
	SetStep(ECaySimWizardStep::ClientCode);
}

void UCaySimSessionWizard::BindClient()
{
	if (!Hepgo) return;
	Hepgo->OnHello.AddDynamic(this, &UCaySimSessionWizard::HandleHello);
	Hepgo->OnError.AddDynamic(this, &UCaySimSessionWizard::HandleError);
	Hepgo->OnCenters.AddDynamic(this, &UCaySimSessionWizard::HandleCenters);
	Hepgo->OnTypes.AddDynamic(this, &UCaySimSessionWizard::HandleTypes);
	Hepgo->OnModels.AddDynamic(this, &UCaySimSessionWizard::HandleModels);
	Hepgo->OnLogs.AddDynamic(this, &UCaySimSessionWizard::HandleLogs);
	Hepgo->OnWorldDefaults.AddDynamic(this, &UCaySimSessionWizard::HandleWorldDefaults);
	Hepgo->OnSessions.AddDynamic(this, &UCaySimSessionWizard::HandleSessions);
	Hepgo->OnSessionCreated.AddDynamic(this, &UCaySimSessionWizard::HandleSessionCreated);
	Hepgo->OnSessionFetched.AddDynamic(this, &UCaySimSessionWizard::HandleSessionFetched);
	Hepgo->OnSessionState.AddDynamic(this, &UCaySimSessionWizard::HandleSessionState);
	Hepgo->OnWsHello.AddDynamic(this, &UCaySimSessionWizard::HandleSessionState);
	Hepgo->OnEnd.AddDynamic(this, &UCaySimSessionWizard::HandleEnd);
}

void UCaySimSessionWizard::UnbindClient()
{
	if (!Hepgo) return;
	Hepgo->OnHello.RemoveAll(this);
	Hepgo->OnError.RemoveAll(this);
	Hepgo->OnCenters.RemoveAll(this);
	Hepgo->OnTypes.RemoveAll(this);
	Hepgo->OnModels.RemoveAll(this);
	Hepgo->OnLogs.RemoveAll(this);
	Hepgo->OnWorldDefaults.RemoveAll(this);
	Hepgo->OnSessions.RemoveAll(this);
	Hepgo->OnSessionCreated.RemoveAll(this);
	Hepgo->OnSessionFetched.RemoveAll(this);
	Hepgo->OnSessionState.RemoveAll(this);
	Hepgo->OnWsHello.RemoveAll(this);
	Hepgo->OnEnd.RemoveAll(this);
}

void UCaySimSessionWizard::SetStep(ECaySimWizardStep S)
{
	Step = S;
	OnStepChanged.Broadcast(S);
}

// ---- istemci kodu ----------------------------------------------------------

void UCaySimSessionWizard::SubmitClientCode(const FString& Text)
{
	if (!Hepgo) { OnError.Broadcast(TEXT("İstemci nesnesi yok.")); return; }
	FString Err;
	if (!Hepgo->SetClientCode(Text, Err))
	{
		OnError.Broadcast(Err);
		return;
	}
	UCaySimSaveGame::SaveClientCode(Text.TrimStartAndEnd());
	SetStep(ECaySimWizardStep::Connecting);
	Status(FString::Printf(TEXT("%s adresine bağlanılıyor…"), *Hepgo->GetClientCode().Domain));
	Hepgo->Hello();
}

bool UCaySimSessionWizard::TrySavedClientCode()
{
	const FString Saved = UCaySimSaveGame::LoadClientCode();
	if (Saved.IsEmpty()) return false;
	SubmitClientCode(Saved);
	return true;
}

void UCaySimSessionWizard::ForgetClientCode()
{
	UCaySimSaveGame::SaveClientCode(FString());
	if (Hepgo) Hepgo->Disconnect();
	Logo = nullptr;
	Thumbs.Reset();
	SetStep(ECaySimWizardStep::ClientCode);
}

void UCaySimSessionWizard::HandleHello(bool bOk, const FCaySimHello& InHello)
{
	if (!bOk)
	{
		SetStep(ECaySimWizardStep::ClientCode);
		return;
	}
	Hello = InHello;
	Status(FString::Printf(TEXT("%s · %s"), *Hello.Company.Name, *Hello.ClientName));
	// Logo: her zaman PNG (SVG sunucuda çevrilir) — `/brand/logo?format=png&size=640` (§2)
	LoadRemoteImage(TEXT("/brand/logo?format=png&size=640"), [this](UTexture2D* Tex) { Logo = Tex; OnLogoReady.Broadcast(Tex); });
	AfterHello();
	SetStep(ECaySimWizardStep::Welcome);
}

void UCaySimSessionWizard::AfterHello()
{
	// Tüm listeleri peşin yükle (sihirbaz adımları beklemesin)
	Hepgo->FetchCenters();
	Hepgo->FetchTypes();
	Hepgo->FetchWorldDefaults();
	Hepgo->FetchSessions();
	if (Hello.HasCap(TEXT("replay"))) Hepgo->FetchLogs(FString());
}

void UCaySimSessionWizard::HandleError(const FString& Code, const FString& Msg)
{
	OnError.Broadcast(Msg);
	if (Code == TEXT("auth") || Code == TEXT("client_inactive"))
	{
		// Kod geçersiz/yenilenmiş ya da istemci pasif: kayıtlı kodu koru ama kod adımına dön
		if (Hepgo) Hepgo->Disconnect();
		SetStep(ECaySimWizardStep::ClientCode);
		return;
	}
	if (Step == ECaySimWizardStep::Connecting) SetStep(ECaySimWizardStep::ClientCode);
	else if (Step == ECaySimWizardStep::Starting) SetStep(ECaySimWizardStep::Summary);
}

void UCaySimSessionWizard::LoadRemoteImage(const FString& Url, TFunction<void(UTexture2D*)> Done)
{
	if (!Hepgo) { Done(nullptr); return; }
	TWeakObjectPtr<UCaySimSessionWizard> WeakThis(this);
	Hepgo->FetchBytes(Url, FHepgoBytesNative::CreateLambda([WeakThis, Done](bool bOk, const TArray<uint8>& Bytes)
	{
		if (!WeakThis.IsValid()) return;
		UTexture2D* Tex = nullptr;
		if (bOk && Bytes.Num() > 0)
		{
			Tex = FImageUtils::ImportBufferAsTexture2D(Bytes); // PNG/JPEG/BMP; SVG desteklenmez
			if (!Tex) UE_LOG(LogCaySim, Warning, TEXT("Görsel çözümlenemedi (%d bayt)"), Bytes.Num());
		}
		Done(Tex);
	}));
}

// ---- listeler -------------------------------------------------------------

void UCaySimSessionWizard::HandleCenters(const TArray<FCaySimCenter>& In)
{
	Centers = In;
	if (CenterId == 0 && Centers.Num()) CenterId = Centers[0].Id;
	OnDataChanged.Broadcast(ECaySimWizardStep::Center);
}

void UCaySimSessionWizard::HandleTypes(const TArray<FCaySimDroneTypeInfo>& In)
{
	Types = In;
	if (TypeKey.IsEmpty() && Types.Num())
	{
		TypeKey = Types[0].Key;
		Hepgo->FetchModels(TypeKey);
	}
	OnDataChanged.Broadcast(ECaySimWizardStep::DroneType);
}

void UCaySimSessionWizard::HandleModels(const TArray<FCaySimModelInfo>& In)
{
	Models = In;
	// Varsayılan model: tipe ait ilk hazır dahili
	bool bKeep = false;
	for (const FCaySimModelInfo& M : Models) if (M.Id == ModelId) { bKeep = true; break; }
	if (!bKeep)
	{
		// Varsayılan: `GET /models` yanıtındaki defaults[type]; yoksa ilk hazır dahili
		ModelId = Hepgo ? Hepgo->GetDefaultModelFor(TypeKey) : FString();
		if (ModelId.IsEmpty()) for (const FCaySimModelInfo& M : Models) if (M.Source == TEXT("builtin") && M.bReady) { ModelId = M.Id; break; }
		if (ModelId.IsEmpty()) for (const FCaySimModelInfo& M : Models) if (M.Source == TEXT("builtin")) { ModelId = M.Id; break; }
		if (ModelId.IsEmpty() && Models.Num()) ModelId = Models[0].Id;
	}
	// Küçük resimleri indir
	for (const FCaySimModelInfo& M : Models)
	{
		if (M.ThumbUrl.IsEmpty() || Thumbs.Contains(M.Id)) continue;
		const FString Id = M.Id;
		LoadRemoteImage(M.ThumbUrl, [this, Id](UTexture2D* Tex) { if (Tex) { Thumbs.Add(Id, Tex); OnThumbReady.Broadcast(Id); } });
	}
	OnDataChanged.Broadcast(ECaySimWizardStep::Model);
}

void UCaySimSessionWizard::HandleLogs(const TArray<FCaySimLogInfo>& In)
{
	Logs = In;
	OnDataChanged.Broadcast(ECaySimWizardStep::Log);
}

void UCaySimSessionWizard::HandleWorldDefaults(const FCaySimWorld& In)
{
	World = In;
	if (World.Mode == TEXT("procedural")) World.Mode = TEXT("real"); // CAYSIM'de prosedürel dünya yok
	WorldSource = Hello.bGoogle3dEnabled ? ECaySimWorldSource::GooglePhotoreal : ECaySimWorldSource::CesiumIon;
	OnDataChanged.Broadcast(ECaySimWizardStep::World);
}

void UCaySimSessionWizard::HandleSessions(const TArray<FCaySimSession>& In)
{
	OpenSessions = In;
	OnDataChanged.Broadcast(ECaySimWizardStep::Welcome);
}

UTexture2D* UCaySimSessionWizard::GetModelThumb(const FString& InModelId) const
{
	const TObjectPtr<UTexture2D>* T = Thumbs.Find(InModelId);
	return T ? T->Get() : nullptr;
}

TArray<ECaySimLaunchMethod> UCaySimSessionWizard::AvailableLaunchMethods() const
{
	const ECaySimDroneType T = CaySimTypes::DroneTypeFromKey(TypeKey);
	if (T == ECaySimDroneType::FixedWing) return { ECaySimLaunchMethod::Runway, ECaySimLaunchMethod::Ramp, ECaySimLaunchMethod::Vehicle };
	if (T == ECaySimDroneType::Vtol) return { ECaySimLaunchMethod::Vertical, ECaySimLaunchMethod::Runway, ECaySimLaunchMethod::Ramp };
	return { ECaySimLaunchMethod::Vertical };
}

// ---- seçimler -------------------------------------------------------------

void UCaySimSessionWizard::ChooseMode(ECaySimSessionMode InMode)
{
	Mode = InMode;
	SetStep(Mode == ECaySimSessionMode::Replay ? ECaySimWizardStep::Log : ECaySimWizardStep::Center);
}

void UCaySimSessionWizard::ChooseCenter(int32 InCenterId)
{
	CenterId = InCenterId;
	SetStep(ECaySimWizardStep::DroneType);
}

void UCaySimSessionWizard::SearchLogs(const FString& Query)
{
	if (Hepgo) Hepgo->FetchLogs(Query);
}

void UCaySimSessionWizard::ChooseLog(int32 InLogId)
{
	LogId = InLogId;
	for (const FCaySimLogInfo& L : Logs)
	{
		if (L.Id == LogId && !L.TypeKey.IsEmpty())
		{
			TypeKey = L.TypeKey; // kayıtta tip logdan gelir; model yine seçilir
			Hepgo->FetchModels(TypeKey);
		}
	}
	SetStep(ECaySimWizardStep::Model);
}

void UCaySimSessionWizard::ChooseType(const FString& InTypeKey)
{
	if (TypeKey != InTypeKey)
	{
		TypeKey = InTypeKey;
		ModelId.Empty();
		Hepgo->FetchModels(TypeKey);
	}
	// Kalkış varsayılanı
	const TArray<ECaySimLaunchMethod> Avail = AvailableLaunchMethods();
	if (!Avail.Contains(Launch.Method) && Avail.Num()) Launch.Method = Avail[0];
	SetStep(ECaySimWizardStep::Model);
}

void UCaySimSessionWizard::ChooseModel(const FString& InModelId)
{
	ModelId = InModelId;
	bOrientOverride = false;
	SetStep(ECaySimWizardStep::World);
}

void UCaySimSessionWizard::SetModelOrientOverride(const FCaySimModelOrient& Orient, bool bEnabled)
{
	OrientOverride = Orient;
	bOrientOverride = bEnabled;
}

void UCaySimSessionWizard::SetWorld(const FCaySimWorld& InWorld, ECaySimWorldSource Source)
{
	World = InWorld;
	WorldSource = Source;
	World.Mode = (Source == ECaySimWorldSource::GooglePhotoreal && Hello.bGoogle3dEnabled) ? TEXT("google3d") : TEXT("real");
	if (Mode == ECaySimSessionMode::Replay) SetStep(ECaySimWizardStep::Summary);
	else SetStep(AvailableLaunchMethods().Num() > 1 ? ECaySimWizardStep::Launch : ECaySimWizardStep::Summary);
}

void UCaySimSessionWizard::SetLaunch(const FCaySimLaunchConfig& InLaunch)
{
	Launch = InLaunch;
	if (World.bRunway && Launch.Method == ECaySimLaunchMethod::Runway) Launch.HeadingDeg = World.RunwayHeading;
	// Sunucuya `world.launch` (yalnız sabit kanat; dikey kalkışta gönderilmez) — HEPGO §13
	const bool bFixedWing = CaySimTypes::DroneTypeFromKey(TypeKey) == ECaySimDroneType::FixedWing;
	World.bHasLaunch = bFixedWing && Launch.Method != ECaySimLaunchMethod::Vertical;
	World.LaunchMethod = CaySimTypes::LaunchMethodToString(Launch.Method);
	World.LaunchAngleDeg = Launch.RampAngleDeg;
	World.LaunchReleaseKmh = Launch.VehicleSpeedMs * 3.6f; // araç bırakma hızı (km/h)
	SetStep(ECaySimWizardStep::Summary);
}

void UCaySimSessionWizard::GoTo(ECaySimWizardStep S) { SetStep(S); }

void UCaySimSessionWizard::Back()
{
	switch (Step)
	{
	case ECaySimWizardStep::Mode:      SetStep(ECaySimWizardStep::Welcome); break;
	case ECaySimWizardStep::Center:
	case ECaySimWizardStep::Log:       SetStep(ECaySimWizardStep::Mode); break;
	case ECaySimWizardStep::DroneType: SetStep(ECaySimWizardStep::Center); break;
	case ECaySimWizardStep::Model:     SetStep(Mode == ECaySimSessionMode::Replay ? ECaySimWizardStep::Log : ECaySimWizardStep::DroneType); break;
	case ECaySimWizardStep::World:     SetStep(ECaySimWizardStep::Model); break;
	case ECaySimWizardStep::Launch:    SetStep(ECaySimWizardStep::World); break;
	case ECaySimWizardStep::Summary:   SetStep((Mode == ECaySimSessionMode::Sitl && AvailableLaunchMethods().Num() > 1) ? ECaySimWizardStep::Launch : ECaySimWizardStep::World); break;
	default: break;
	}
}

void UCaySimSessionWizard::Next()
{
	switch (Step)
	{
	case ECaySimWizardStep::Welcome:   SetStep(ECaySimWizardStep::Mode); break;
	case ECaySimWizardStep::Mode:      ChooseMode(Mode); break;
	case ECaySimWizardStep::Center:    ChooseCenter(CenterId); break;
	case ECaySimWizardStep::Log:       if (LogId) ChooseLog(LogId); break;
	case ECaySimWizardStep::DroneType: ChooseType(TypeKey); break;
	case ECaySimWizardStep::Model:     ChooseModel(ModelId); break;
	case ECaySimWizardStep::World:     SetWorld(World, WorldSource); break;
	case ECaySimWizardStep::Launch:    SetLaunch(Launch); break;
	case ECaySimWizardStep::Summary:   Start(); break;
	default: break;
	}
}

// ---- oturum ---------------------------------------------------------------

void UCaySimSessionWizard::Start()
{
	if (!Hepgo) return;
	SetStep(ECaySimWizardStep::Starting);
	if (Mode == ECaySimSessionMode::Replay)
	{
		if (LogId <= 0) { OnError.Broadcast(TEXT("Uçuş logu seçilmedi.")); SetStep(ECaySimWizardStep::Log); return; }
		Status(TEXT("Kayıt oturumu açılıyor…"));
		Hepgo->CreateReplaySession(LogId, World, ModelId);
	}
	else
	{
		if (CenterId <= 0) { OnError.Broadcast(TEXT("Merkez seçilmedi.")); SetStep(ECaySimWizardStep::Center); return; }
		Status(TEXT("SITL başlatılıyor…"));
		Hepgo->CreateSitlSession(TypeKey, CenterId, World, ModelId, OrientOverride, bOrientOverride);
	}
}

void UCaySimSessionWizard::JoinSession(const FString& SessionId)
{
	if (!Hepgo) return;
	SetStep(ECaySimWizardStep::Starting);
	Status(TEXT("Oturuma katılınıyor…"));
	Hepgo->FetchSession(SessionId);
}

void UCaySimSessionWizard::HandleSessionCreated(bool bOk, const FCaySimSession& InSession)
{
	if (!bOk) { SetStep(ECaySimWizardStep::Summary); return; }
	Session = InSession;
	Hepgo->ConnectSession(Session.Id);
	if (Session.State == ECaySimSessionState::Ready) SetStep(ECaySimWizardStep::Running);
	else Status(TEXT("SITL hazırlanıyor…"));
}

void UCaySimSessionWizard::HandleSessionFetched(bool bOk, const FCaySimSession& InSession)
{
	if (Step != ECaySimWizardStep::Starting) return;
	HandleSessionCreated(bOk, InSession);
}

void UCaySimSessionWizard::HandleSessionState(const FCaySimSession& InSession)
{
	if (InSession.Id.IsEmpty() || InSession.Id != Session.Id)
	{
		if (!InSession.Id.IsEmpty() && Step == ECaySimWizardStep::Starting) Session = InSession;
		else return;
	}
	if (!InSession.Center.Name.IsEmpty()) Session = InSession;
	else { Session.State = InSession.State; if (InSession.Replay.Frames > 0 || InSession.Replay.Progress > 0) Session.Replay = InSession.Replay; }
	if (InSession.bCanCommand) Session.bCanCommand = true;

	switch (Session.State)
	{
	case ECaySimSessionState::Ready:
		if (Step == ECaySimWizardStep::Starting) SetStep(ECaySimWizardStep::Running);
		break;
	case ECaySimSessionState::Error:
		OnError.Broadcast(Session.SitlError.IsEmpty() ? TEXT("Oturum hata verdi.") : Session.SitlError);
		SetStep(ECaySimWizardStep::Summary);
		break;
	case ECaySimSessionState::Closed:
		if (Step == ECaySimWizardStep::Running || Step == ECaySimWizardStep::Starting) SetStep(ECaySimWizardStep::Summary);
		break;
	default:
		if (Session.Mode == ECaySimSessionMode::Replay && Session.Replay.Progress > 0) Status(FString::Printf(TEXT("Kayıt ayrıştırılıyor… %%%.0f"), Session.Replay.Progress));
		break;
	}
}

void UCaySimSessionWizard::HandleEnd(const FString& Reason)
{
	Status(FString::Printf(TEXT("Oturum sona erdi (%s)"), *Reason));
	if (Step == ECaySimWizardStep::Running) SetStep(ECaySimWizardStep::Summary);
}

void UCaySimSessionWizard::EndSession()
{
	if (Hepgo && !Session.Id.IsEmpty())
	{
		// Oturum sahibi kapatır; izleyici yalnız ayrılır
		Hepgo->DeleteSession(Session.Id);
	}
	Session = FCaySimSession();
	SetStep(ECaySimWizardStep::Summary);
}
