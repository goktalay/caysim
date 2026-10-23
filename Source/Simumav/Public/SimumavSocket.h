// Yeniden bağlanan WebSocket sarmalayıcısı (simumav.com /ws/fly/{sim_id} ve /ws/match/{id}).
// Kimlik: `Cookie: smsid=…` başlığı (sunucu çerezden okur; sorgu belirteci yoktur). Geri çekilme 0,5 → 1 → 2 → 4 → 8 → 10 sn.
// Kapanış kodları 4403 (yetki) / 4404 (yok) kalıcıdır — yeniden denenmez; 4429 (hız sınırı) 5 sn sonra denenir.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"

class IWebSocket;

class SIMUMAV_API FSmvSocket : public TSharedFromThis<FSmvSocket>
{
public:
	enum class EState : uint8 { Idle, Connecting, Open, Reconnecting, Failed };

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnJson, const TSharedPtr<FJsonObject>&);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnState, EState);
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnClosed, int32 /*Code*/, const FString& /*Reason*/);

	FSmvSocket();
	~FSmvSocket();

	/** Url: wss://simumav.com/ws/fly/12 ; Headers: Cookie, Origin, User-Agent. */
	void Open(const FString& Url, const TMap<FString, FString>& Headers);
	void Close();
	bool IsOpen() const;
	EState GetState() const { return State; }
	void Send(const TSharedRef<FJsonObject>& Obj);
	void SendText(const FString& Text);
	/** Son karenin üzerinden geçen süre (sn). */
	double SecondsSinceRx() const;
	float GetRttMs() const { return RttMs; }

	FOnJson OnJson;
	FOnState OnState;
	FOnClosed OnClosed;

	float PingIntervalS = 10.f;
	/** Bu süre boyunca hiç çerçeve gelmezse bağlantı ölü sayılır. */
	float DeadAfterS = 25.f;

private:
	TSharedPtr<IWebSocket> Ws;
	FString CurUrl;
	TMap<FString, FString> CurHeaders;
	EState State = EState::Idle;
	bool bWant = false;
	int32 Backoff = 0;
	double NextTry = 0, LastRx = 0, LastPing = 0, PingSent = 0;
	float RttMs = 0.f;
	FTSTicker::FDelegateHandle Tick;
	void SetState(EState S);
	void Connect();
	void Drop();
	void Schedule(double DelayS = -1.0);
	bool OnTick(float Dt);
};
