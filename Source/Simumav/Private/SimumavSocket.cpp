#include "SimumavSocket.h"
#include "SimumavModule.h"
#include "SimumavTypes.h"
#include "IWebSocket.h"
#include "WebSocketsModule.h"
#include "Modules/ModuleManager.h"

static const double GSmvBackoff[] = { 0.5, 1.0, 2.0, 4.0, 8.0, 10.0 };

FSmvSocket::FSmvSocket()
{
	Tick = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FSmvSocket::OnTick), 0.f);
}

FSmvSocket::~FSmvSocket()
{
	if (Tick.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(Tick);
	bWant = false;
	Drop();
}

void FSmvSocket::SetState(EState S)
{
	if (State == S) return;
	State = S;
	OnState.Broadcast(S);
}

void FSmvSocket::Open(const FString& Url, const TMap<FString, FString>& Headers)
{
	CurUrl = Url;
	CurHeaders = Headers;
	bWant = true;
	Backoff = 0;
	Connect();
}

void FSmvSocket::Close()
{
	bWant = false;
	Drop();
	SetState(EState::Idle);
}

bool FSmvSocket::IsOpen() const { return Ws.IsValid() && Ws->IsConnected(); }

double FSmvSocket::SecondsSinceRx() const { return LastRx > 0 ? FPlatformTime::Seconds() - LastRx : 1e9; }

void FSmvSocket::Drop()
{
	if (Ws.IsValid())
	{
		Ws->OnConnected().Clear();
		Ws->OnConnectionError().Clear();
		Ws->OnClosed().Clear();
		Ws->OnMessage().Clear();
		if (Ws->IsConnected()) Ws->Close();
		Ws.Reset();
	}
}

void FSmvSocket::Schedule(double DelayS)
{
	if (!bWant) { SetState(EState::Idle); return; }
	const int32 N = UE_ARRAY_COUNT(GSmvBackoff);
	const double D = DelayS > 0 ? DelayS : GSmvBackoff[FMath::Min(Backoff, N - 1)];
	Backoff = FMath::Min(Backoff + 1, N - 1);
	NextTry = FPlatformTime::Seconds() + D;
	SetState(EState::Reconnecting);
}

void FSmvSocket::Connect()
{
	Drop();
	FWebSocketsModule& Mod = FModuleManager::LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));
	Ws = Mod.CreateWebSocket(CurUrl, FString(), CurHeaders);
	if (!Ws.IsValid()) { Schedule(); return; }
	SetState(Backoff > 0 ? EState::Reconnecting : EState::Connecting);
	TWeakPtr<FSmvSocket> Weak = AsShared();
	Ws->OnConnected().AddLambda([Weak]()
	{
		if (TSharedPtr<FSmvSocket> S = Weak.Pin())
		{
			S->Backoff = 0;
			S->LastRx = S->LastPing = FPlatformTime::Seconds();
			S->SetState(EState::Open);
		}
	});
	Ws->OnConnectionError().AddLambda([Weak](const FString& Err)
	{
		if (TSharedPtr<FSmvSocket> S = Weak.Pin())
		{
			UE_LOG(LogSimumav, Warning, TEXT("WS bağlantı hatası: %s"), *Err);
			S->Schedule();
		}
	});
	Ws->OnClosed().AddLambda([Weak](int32 Code, const FString& Reason, bool /*bClean*/)
	{
		if (TSharedPtr<FSmvSocket> S = Weak.Pin())
		{
			UE_LOG(LogSimumav, Log, TEXT("WS kapandı: %d %s"), Code, *Reason);
			S->OnClosed.Broadcast(Code, Reason);
			if (Code == 4403 || Code == 4404) { S->bWant = false; S->SetState(EState::Failed); return; }
			if (Code == 4429) { S->Schedule(5.0); return; }
			S->Schedule();
		}
	});
	Ws->OnMessage().AddLambda([Weak](const FString& Msg)
	{
		if (TSharedPtr<FSmvSocket> S = Weak.Pin())
		{
			S->LastRx = FPlatformTime::Seconds();
			const TSharedPtr<FJsonObject> O = SmvJson::Parse(Msg);
			if (!O.IsValid()) return;
			if (SmvJson::Str(O, TEXT("t")) == TEXT("pong")) S->RttMs = (float)((S->LastRx - S->PingSent) * 1000.0);
			S->OnJson.Broadcast(O);
		}
	});
	Ws->Connect();
}

void FSmvSocket::Send(const TSharedRef<FJsonObject>& Obj) { SendText(SmvJson::ToString(Obj)); }

void FSmvSocket::SendText(const FString& Text)
{
	if (IsOpen()) Ws->Send(Text);
}

bool FSmvSocket::OnTick(float /*Dt*/)
{
	const double Now = FPlatformTime::Seconds();
	if (bWant && State == EState::Reconnecting && NextTry > 0 && Now >= NextTry)
	{
		NextTry = 0;
		Connect();
	}
	if (State == EState::Open && IsOpen())
	{
		if (Now - LastPing >= PingIntervalS)
		{
			LastPing = PingSent = Now;
			TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
			P->SetStringField(TEXT("t"), TEXT("ping"));
			Send(P);
		}
		if (Now - LastRx > DeadAfterS)
		{
			UE_LOG(LogSimumav, Warning, TEXT("WS: %.0f sn veri yok, yeniden bağlanılıyor"), Now - LastRx);
			Drop();
			Schedule();
		}
	}
	return true;
}
