#include "SmvCloudFlightLink.h"
#include "SimumavModule.h"
#include "SimumavClient.h"
#include "SimumavSocket.h"
#include "HepgoClient.h"

// ---------------------------------------------------------------------------

void USmvCloudFlightLink::BeginDestroy()
{
	if (TickHandle.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(TickHandle); TickHandle.Reset(); }
	if (Ws.IsValid()) { Ws->Close(); Ws.Reset(); }
	Super::BeginDestroy();
}

double USmvCloudFlightLink::Now() const { return UHepgoClient::LocalUnixNow() + ClockOffset; }

FString USmvCloudFlightLink::MissionPlannerInfo() const
{
	if (FlightInfo.MpPort <= 0) return FString();
	return FString::Printf(TEXT("Mission Planner: TCP %s:%d (ilk bağlanan IP kilitlenir)"), *FlightInfo.MpHost, FlightInfo.MpPort);
}

void USmvCloudFlightLink::Start(const FSmvFlightRequest& Request)
{
	Req = Request;
	if (!Client || !Client->IsLoggedIn())
	{
		EmitEvent(TEXT("Bulutta uçmak için simumav.com hesabıyla giriş yapın."), 3);
		OnStarted.Broadcast(false);
		return;
	}
	EmitState(TEXT("BASLIYOR"));
	TWeakObjectPtr<USmvCloudFlightLink> Weak(this);
	Client->Post(TEXT("/api/fly/start"), Req.ToStartJson(), FSmvRestDone::CreateLambda([Weak](int32 Status, const TSharedPtr<FJsonObject>& O, const FString& C, const FString& M)
	{
		USmvCloudFlightLink* Self = Weak.Get();
		if (!Self) return;
		if (Status == 409 && C == TEXT("flight_open"))
		{
			// önceki açık uçuş: sunucu kimliğini verir → ona bağlan (web ön yüzüyle aynı davranış)
			const TSharedPtr<FJsonObject> D = SmvJson::Obj(O, TEXT("detail"));
			Self->FlightInfo.FlightId = SmvJson::Id(D, TEXT("flight_id"));
			Self->FlightInfo.SimId = SmvJson::Id(D, TEXT("sim_id"));
			Self->FlightInfo.WsPath = FString::Printf(TEXT("/ws/fly/%s"), *Self->FlightInfo.SimId);
			Self->EmitEvent(TEXT("Açık bir uçuşunuz vardı; ona bağlanılıyor."), 4);
		}
		else if (Status != 201 && Status != 200)
		{
			Self->EmitEvent(FString::Printf(TEXT("Uçuş başlatılamadı: %s"), *M), 3);
			Self->EmitState(TEXT("HATA"));
			Self->OnStarted.Broadcast(false);
			return;
		}
		else
		{
			Self->FlightInfo = FSmvFlightInfo::FromJson(O);
		}
		Self->Connect();
		Self->OnStarted.Broadcast(true);
	}));
}

void USmvCloudFlightLink::AttachExisting(const FString& FlightId)
{
	TWeakObjectPtr<USmvCloudFlightLink> Weak(this);
	Client->Get(FString::Printf(TEXT("/api/fly/%s"), *FlightId), FSmvRestDone::CreateLambda([Weak](int32 Status, const TSharedPtr<FJsonObject>& O, const FString&, const FString& M)
	{
		USmvCloudFlightLink* Self = Weak.Get();
		if (!Self) return;
		if (Status != 200) { Self->EmitEvent(FString::Printf(TEXT("Karşılaşma uçuşu alınamadı: %s"), *M), 3); Self->OnStarted.Broadcast(false); return; }
		Self->FlightInfo.FlightId = SmvJson::Id(O, TEXT("flight_id"));
		Self->FlightInfo.SimId = SmvJson::Id(O, TEXT("sim_id"));
		Self->FlightInfo.WsPath = SmvJson::Str(O, TEXT("ws"), FString::Printf(TEXT("/ws/fly/%s"), *Self->FlightInfo.SimId));
		Self->Connect();
		Self->OnStarted.Broadcast(true);
	}));
}

void USmvCloudFlightLink::Connect()
{
	if (FlightInfo.WsPath.IsEmpty()) FlightInfo.WsPath = FString::Printf(TEXT("/ws/fly/%s"), *FlightInfo.SimId);
	Ws = Client->MakeSocket();
	TWeakObjectPtr<USmvCloudFlightLink> Weak(this);
	Ws->OnJson.AddLambda([Weak](const TSharedPtr<FJsonObject>& O) { if (USmvCloudFlightLink* S = Weak.Get()) S->HandleWs(O); });
	Ws->OnClosed.AddLambda([Weak](int32 Code, const FString&)
	{
		USmvCloudFlightLink* S = Weak.Get();
		if (!S) return;
		if (Code == 4404) { S->EmitEvent(TEXT("Uçuş oturumu sunucuda bulunamadı (bitmiş)."), 3); S->FetchSummary(TEXT("closed")); }
		if (Code == 4403) S->EmitEvent(TEXT("Oturum yetkisi düştü; yeniden giriş yapın."), 3);
	});
	Ws->Open(Client->WsUrl(FlightInfo.WsPath), Client->WsHeaders());
	if (!TickHandle.IsValid()) TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &USmvCloudFlightLink::OnTick), 0.f);
}

void USmvCloudFlightLink::HandleWs(const TSharedPtr<FJsonObject>& O)
{
	const FString T = SmvJson::Str(O, TEXT("t"));
	if (T == TEXT("tele"))
	{
		Tele = FCaySimTelemetry::FromJson(O);
		Extra = FSmvTeleExtra::FromJson(O);
		if (Tele.Ts > 0)
		{
			const double Sample = Tele.Ts - UHepgoClient::LocalUnixNow();
			if (!bClockSynced) { ClockOffset = Sample; bClockSynced = true; }
			else ClockOffset += (Sample - ClockOffset) * 0.02;
		}
		EmitTele();
		return;
	}
	if (T == TEXT("hello"))
	{
		bReady = true;
		const TSharedPtr<FJsonObject> S = SmvJson::Obj(O, TEXT("session"));
		const FString St = SmvJson::Str(S, TEXT("state"));
		if (!St.IsEmpty()) FlightInfo.State = St;
	}
	else if (T == TEXT("state"))
	{
		FlightInfo.State = SmvJson::Str(O, TEXT("state"));
		if (FlightInfo.State == TEXT("KAPANDI")) FetchSummary(TEXT("closed"));
	}
	else if (T == TEXT("end"))
	{
		FetchSummary(SmvJson::Str(O, TEXT("reason"), TEXT("end")));
	}
	EmitMessage(O.ToSharedRef());
}

bool USmvCloudFlightLink::OnTick(float)
{
	// RC kısma: istemci 20 Hz (sunucu tavanı ~40 Hz; fazlası atılır)
	const double NowW = FPlatformTime::Seconds();
	if (bRcPending && Ws.IsValid() && Ws->IsOpen() && NowW - LastRcAt >= 0.05)
	{
		bRcPending = false;
		LastRcAt = NowW;
		TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetStringField(TEXT("t"), TEXT("rc"));
		TArray<TSharedPtr<FJsonValue>> Ch;
		for (int32 V : PendingRc) Ch.Add(MakeShared<FJsonValueNumber>(V));
		R->SetArrayField(TEXT("ch"), Ch);
		Ws->Send(R);
	}
	return true;
}

void USmvCloudFlightLink::SendRc(const TArray<int32>& Channels)
{
	PendingRc = Channels;
	PendingRc.SetNum(8);
	for (int32& V : PendingRc) if (V != 0) V = FMath::Clamp(V, 1000, 2000);
	bool bAllZero = true;
	for (int32 V : PendingRc) if (V != 0) { bAllZero = false; break; }
	if (bAllZero) LastRcAt = 0;   // serbest bırakma kısılmaz
	bRcPending = true;
}

FString USmvCloudFlightLink::Command(const FString& Name, const TSharedPtr<FJsonObject>& P)
{
	const FString Rid = NextRid();
	if (!Ws.IsValid() || !Ws->IsOpen()) { EmitResult(Rid, Name, false, TEXT("bağlı değil")); return Rid; }
	TSharedRef<FJsonObject> C = MakeShared<FJsonObject>();
	C->SetStringField(TEXT("t"), TEXT("cmd"));
	C->SetStringField(TEXT("rid"), Rid);
	C->SetStringField(TEXT("name"), Name);
	C->SetObjectField(TEXT("params"), P.IsValid() ? P : MakeShared<FJsonObject>());
	Ws->Send(C);
	return Rid;
}

void USmvCloudFlightLink::Stop(const FString& Reason)
{
	if (bStopping || FlightInfo.FlightId.IsEmpty()) { FetchSummary(Reason); return; }
	bStopping = true;
	TSharedRef<FJsonObject> B = MakeShared<FJsonObject>();
	B->SetStringField(TEXT("reason"), Reason.Left(20));
	if (GatesPassed || GatesMissed || Laps)
	{
		TSharedRef<FJsonObject> G = MakeShared<FJsonObject>();
		G->SetNumberField(TEXT("passed"), GatesPassed);
		G->SetNumberField(TEXT("missed"), GatesMissed);
		G->SetNumberField(TEXT("laps"), FMath::Clamp(Laps, 0, 10));
		if (BestLapS > 0) G->SetNumberField(TEXT("best_lap_s"), BestLapS);
		B->SetObjectField(TEXT("gates"), G);
	}
	TWeakObjectPtr<USmvCloudFlightLink> Weak(this);
	Client->Post(FString::Printf(TEXT("/api/fly/%s/stop"), *FlightInfo.FlightId), B, FSmvRestDone::CreateLambda(
		[Weak, Reason](int32 Status, const TSharedPtr<FJsonObject>& O, const FString&, const FString&)
	{
		USmvCloudFlightLink* Self = Weak.Get();
		if (!Self) return;
		if (Self->Ws.IsValid()) { Self->Ws->Close(); }
		FSmvFlightSummary S = Status == 200 ? FSmvFlightSummary::FromServerFlight(SmvJson::Obj(O, TEXT("flight"))) : FSmvFlightSummary();
		S.EndReason = Reason;
		if (S.FlightId.IsEmpty()) S.FlightId = Self->FlightInfo.FlightId;
		Self->OnSummary.Broadcast(S);
	}));
}

void USmvCloudFlightLink::FetchSummary(const FString& Reason)
{
	if (FlightInfo.FlightId.IsEmpty()) { FSmvFlightSummary S; S.EndReason = Reason; OnSummary.Broadcast(S); return; }
	TWeakObjectPtr<USmvCloudFlightLink> Weak(this);
	Client->Get(FString::Printf(TEXT("/api/ops/%s"), *FlightInfo.FlightId), FSmvRestDone::CreateLambda([Weak, Reason](int32 Status, const TSharedPtr<FJsonObject>& O, const FString&, const FString&)
	{
		USmvCloudFlightLink* Self = Weak.Get();
		if (!Self) return;
		FSmvFlightSummary S = Status == 200 ? FSmvFlightSummary::FromServerFlight(O) : FSmvFlightSummary();
		S.EndReason = Reason;
		Self->OnSummary.Broadcast(S);
	}));
}
