#include "SmvMatchClient.h"
#include "SimumavModule.h"
#include "SimumavClient.h"
#include "SimumavSocket.h"

void USmvMatchClient::BeginDestroy()
{
	if (Ws.IsValid()) { Ws->Close(); Ws.Reset(); }
	Super::BeginDestroy();
}

TSharedRef<FJsonObject> USmvMatchClient::BaseBody(const FSmvFlightRequest& Draft, const FString& Kind, TSharedPtr<FJsonObject> Rings, TSharedPtr<FJsonObject> Dogfight) const
{
	TSharedRef<FJsonObject> B = MakeShared<FJsonObject>();
	const TSharedRef<FJsonObject> Start = Draft.ToStartJson();
	B->SetField(TEXT("drone_id"), Start->TryGetField(TEXT("drone_id")));
	B->SetField(TEXT("track_id"), Start->TryGetField(TEXT("track_id")));
	if (const TSharedPtr<FJsonObject> L = Draft.Launch.ToJson()) B->SetObjectField(TEXT("launch"), L);
	B->SetStringField(TEXT("camera"), Draft.Camera);
	B->SetStringField(TEXT("kind"), Kind);
	if (Rings.IsValid()) B->SetObjectField(TEXT("rings"), Rings);
	if (Dogfight.IsValid()) B->SetObjectField(TEXT("dogfight"), Dogfight);
	return B;
}

void USmvMatchClient::Create(const FSmvFlightRequest& Draft, const FString& Kind, const FString& Password, int32 MaxPlayers, TSharedPtr<FJsonObject> Rings, TSharedPtr<FJsonObject> Dogfight)
{
	TSharedRef<FJsonObject> B = BaseBody(Draft, Kind, Rings, Dogfight);
	B->SetStringField(TEXT("password"), Password);
	B->SetNumberField(TEXT("max_players"), FMath::Clamp(MaxPlayers, 2, 8));
	Post(TEXT("/api/match"), B);
}

void USmvMatchClient::CreateAi(const FSmvFlightRequest& Draft, const FString& Kind, const FString& Level, int32 Bots, TSharedPtr<FJsonObject> Rings, TSharedPtr<FJsonObject> Dogfight)
{
	TSharedRef<FJsonObject> B = BaseBody(Draft, Kind, Rings, Dogfight);
	B->SetStringField(TEXT("level"), Level);
	B->SetNumberField(TEXT("bots"), FMath::Clamp(Bots, 1, 7));
	Post(TEXT("/api/match/ai"), B);
}

void USmvMatchClient::Join(const FString& Code, const FString& Password)
{
	TSharedRef<FJsonObject> B = MakeShared<FJsonObject>();
	B->SetStringField(TEXT("code"), Code.TrimStartAndEnd());
	B->SetStringField(TEXT("password"), Password);
	Post(TEXT("/api/match/join"), B);
}

void USmvMatchClient::SetReady(bool bReady)
{
	if (!HasMatch()) return;
	TSharedRef<FJsonObject> B = MakeShared<FJsonObject>();
	B->SetBoolField(TEXT("ready"), bReady);
	Post(FString::Printf(TEXT("/api/match/%s/ready"), *Snapshot.Id), B);
}

void USmvMatchClient::Start() { if (HasMatch()) Post(FString::Printf(TEXT("/api/match/%s/start"), *Snapshot.Id), MakeShared<FJsonObject>()); }
void USmvMatchClient::Cancel() { if (HasMatch()) Post(FString::Printf(TEXT("/api/match/%s/cancel"), *Snapshot.Id), MakeShared<FJsonObject>()); }

void USmvMatchClient::AddBot(const FString& Level)
{
	if (!HasMatch()) return;
	TSharedRef<FJsonObject> B = MakeShared<FJsonObject>();
	B->SetStringField(TEXT("level"), Level);
	Post(FString::Printf(TEXT("/api/match/%s/bot"), *Snapshot.Id), B);
}

void USmvMatchClient::Leave()
{
	if (!HasMatch()) return;
	Post(FString::Printf(TEXT("/api/match/%s/leave"), *Snapshot.Id), MakeShared<FJsonObject>());
	if (Ws.IsValid()) Ws->Close();
	Snapshot = FSmvMatch();
	OnUpdated.Broadcast(Snapshot);
}

void USmvMatchClient::Refresh()
{
	if (!HasMatch()) return;
	TWeakObjectPtr<USmvMatchClient> Weak(this);
	Client->Get(FString::Printf(TEXT("/api/match/%s"), *Snapshot.Id), FSmvRestDone::CreateLambda([Weak](int32 S, const TSharedPtr<FJsonObject>& O, const FString&, const FString&)
	{
		if (USmvMatchClient* Self = Weak.Get()) if (S == 200) Self->Apply(O);
	}));
}

void USmvMatchClient::Post(const FString& Path, const TSharedRef<FJsonObject>& Body)
{
	TWeakObjectPtr<USmvMatchClient> Weak(this);
	Client->Post(Path, Body, FSmvRestDone::CreateLambda([Weak](int32 S, const TSharedPtr<FJsonObject>& O, const FString& C, const FString& M)
	{
		USmvMatchClient* Self = Weak.Get();
		if (!Self) return;
		if (S >= 200 && S < 300) { Self->LastError.Empty(); if (O.IsValid() && O->HasField(TEXT("id"))) Self->Apply(O); return; }
		Self->LastError = M.IsEmpty() ? C : M;
		Self->OnUpdated.Broadcast(Self->Snapshot);
	}));
}

void USmvMatchClient::Apply(const TSharedPtr<FJsonObject>& O)
{
	const bool bNewId = SmvJson::Id(O, TEXT("id")) != Snapshot.Id;
	Snapshot = FSmvMatch::FromJson(O);
	if (Snapshot.MyFlightId.IsEmpty())
	{
		// `players[]` içinde kendi kaydımız (me.id) → flight_id
		const int64 MeId = (int64)SmvJson::Num(SmvJson::Obj(O, TEXT("me")), TEXT("id"));
		for (const FSmvMatchPlayer& P : Snapshot.Players) if (P.Id == MeId && !P.FlightId.IsEmpty()) Snapshot.MyFlightId = P.FlightId;
	}
	if (bNewId || !Ws.IsValid()) OpenWs();
	OnUpdated.Broadcast(Snapshot);
}

void USmvMatchClient::OpenWs()
{
	if (Snapshot.Id.IsEmpty()) return;
	if (Ws.IsValid()) Ws->Close();
	Ws = Client->MakeSocket();
	TWeakObjectPtr<USmvMatchClient> Weak(this);
	Ws->OnJson.AddLambda([Weak](const TSharedPtr<FJsonObject>& O) { if (USmvMatchClient* S = Weak.Get()) S->HandleWs(O); });
	Ws->Open(Client->WsUrl(FString::Printf(TEXT("/ws/match/%s"), *Snapshot.Id)), Client->WsHeaders());
}

void USmvMatchClient::HandleWs(const TSharedPtr<FJsonObject>& O)
{
	const FString T = SmvJson::Str(O, TEXT("t"));
	if (T == TEXT("hello") || T == TEXT("match"))
	{
		const TSharedPtr<FJsonObject> M = SmvJson::Obj(O, TEXT("match"));
		if (M.IsValid())
		{
			// `me` WS hello'da ayrı alanda
			if (const TSharedPtr<FJsonObject> Me = SmvJson::Obj(O, TEXT("me"))) M->SetObjectField(TEXT("me"), Me);
			Apply(M);
		}
		return;
	}
	if (T == TEXT("pos"))
	{
		TArray<FSmvGhostPos> Out;
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (O->TryGetArrayField(TEXT("p"), A) && A)
		{
			const double Ts = SmvJson::Num(O, TEXT("ts"));
			for (const TSharedPtr<FJsonValue>& V : *A)
			{
				const TSharedPtr<FJsonObject> J = V->AsObject();
				FSmvGhostPos G;
				G.Id = (int64)SmvJson::Num(J, TEXT("id"));
				G.Lat = SmvJson::Num(J, TEXT("lat"));
				G.Lon = SmvJson::Num(J, TEXT("lon"));
				G.AltMsl = SmvJson::Num(J, TEXT("alt_msl"));
				G.Yaw = SmvJson::Num(J, TEXT("yaw"));
				G.Roll = SmvJson::Num(J, TEXT("roll"));
				G.Pitch = SmvJson::Num(J, TEXT("pitch"));
				G.Gs = SmvJson::Num(J, TEXT("gs"));
				G.Rings = (int32)SmvJson::Num(J, TEXT("rings"));
				G.Ts = Ts;
				Out.Add(G);
			}
		}
		OnPositions.Broadcast(Out);
		OnEvent.Broadcast(O);
		return;
	}
	if (T == TEXT("state") || T == TEXT("player") || T == TEXT("result") || T == TEXT("countdown")) Refresh();
	OnEvent.Broadcast(O);
}
