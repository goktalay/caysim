#include "HepgoClient.h"
#include "CaySim.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "IWebSocket.h"
#include "WebSocketsModule.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Misc/DateTime.h"
#include "HAL/PlatformProcess.h"
#include "Misc/App.h"

// Geri çekilme dizisi (§3.3)
static const float GBackoffS[] = { 0.5f, 1.f, 2.f, 4.f, 8.f, 10.f };

// ---------------------------------------------------------------------------
// FHepgoClientCode
// ---------------------------------------------------------------------------

bool FHepgoClientCode::Parse(const FString& Text, FHepgoClientCode& Out, FString& Error)
{
	FString S = Text.TrimStartAndEnd();
	Out = FHepgoClientCode();
	if (S.IsEmpty()) { Error = TEXT("İstemci kodu boş."); return false; }

	bool bForcedScheme = false;
	if (S.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase)) { S.RightChopInline(8); Out.bSecure = true; bForcedScheme = true; }
	else if (S.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase)) { S.RightChopInline(7); Out.bSecure = false; bForcedScheme = true; }

	// İlk '/' konumundan ikiye böl (§0.2)
	int32 Slash;
	if (!S.FindChar(TEXT('/'), Slash) || Slash == 0 || Slash == S.Len() - 1)
	{
		Error = TEXT("Biçim: <alan adı>/<anahtar> — ör. caymaz.app/hV3k…");
		return false;
	}
	Out.Domain = S.Left(Slash).TrimStartAndEnd().ToLower();
	Out.Key = S.Mid(Slash + 1).TrimStartAndEnd();

	for (TCHAR C : Out.Domain)
	{
		if (!(FChar::IsAlnum(C) || C == TEXT('.') || C == TEXT('-') || C == TEXT(':')))
		{
			Error = TEXT("Alan adında geçersiz karakter.");
			return false;
		}
	}
	// Anahtar [A-Za-z0-9_-]{43}; ileriye uyum için 32–128 karakter kabul edilir
	if (Out.Key.Len() < 32 || Out.Key.Len() > 128)
	{
		Error = TEXT("Anahtar uzunluğu geçersiz (43 karakter bekleniyor).");
		return false;
	}
	for (TCHAR C : Out.Key)
	{
		if (!(FChar::IsAlnum(C) || C == TEXT('_') || C == TEXT('-')))
		{
			Error = TEXT("Anahtarda geçersiz karakter (yalnız A–Z, a–z, 0–9, _ ve -).");
			return false;
		}
	}
	if (!bForcedScheme)
	{
		const bool bLocal = Out.Domain.StartsWith(TEXT("localhost")) || Out.Domain.StartsWith(TEXT("127.")) ||
			Out.Domain.StartsWith(TEXT("10.")) || Out.Domain.StartsWith(TEXT("192.168.")) || Out.Domain.Contains(TEXT(":"));
		Out.bSecure = !bLocal;
	}
	return true;
}

FString FHepgoClientCode::Origin() const
{
	return FString::Printf(TEXT("%s://%s"), bSecure ? TEXT("https") : TEXT("http"), *Domain);
}

FString FHepgoClientCode::HttpBase() const { return Origin() + TEXT("/api/caysim/v1"); }

FString FHepgoClientCode::WsBase() const
{
	return FString::Printf(TEXT("%s://%s/ws/caysim/v1"), bSecure ? TEXT("wss") : TEXT("ws"), *Domain);
}

// ---------------------------------------------------------------------------
// UHepgoClient — yaşam döngüsü
// ---------------------------------------------------------------------------

UHepgoClient::UHepgoClient()
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UHepgoClient::Tick), 0.f);
	}
}

void UHepgoClient::BeginDestroy()
{
	bWantConnected = false;
	CloseWs();
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
	Super::BeginDestroy();
}

bool UHepgoClient::SetClientCode(const FString& Text, FString& Error)
{
	FHepgoClientCode NewCode;
	if (!FHepgoClientCode::Parse(Text, NewCode, Error)) return false;
	Disconnect();
	Code = NewCode;
	bClockSynced = false;
	LastHello = FCaySimHello();
	ModelDefaults.Reset();
	// Anahtar günlüğe yazılmaz (§0.2)
	UE_LOG(LogCaySim, Log, TEXT("İstemci kodu ayarlandı: %s (anahtar %d karakter)"), *Code.Domain, Code.Key.Len());
	return true;
}

FString UHepgoClient::ResolveUrl(const FString& PathOrUrl) const
{
	if (PathOrUrl.StartsWith(TEXT("http://")) || PathOrUrl.StartsWith(TEXT("https://"))) return PathOrUrl;
	if (PathOrUrl.StartsWith(TEXT("/api/")) || PathOrUrl.StartsWith(TEXT("/ws/"))) return Code.Origin() + PathOrUrl; // sunucu mutlak yol
	const FString Rel = PathOrUrl.StartsWith(TEXT("/")) ? PathOrUrl : TEXT("/") + PathOrUrl;
	return Code.HttpBase() + Rel;
}

double UHepgoClient::LocalUnixNow()
{
	const FDateTime Now = FDateTime::UtcNow();
	return (double)Now.ToUnixTimestamp() + Now.GetMillisecond() / 1000.0;
}

double UHepgoClient::NowServer() const { return LocalUnixNow() + ServerClockOffset; }

FString UHepgoClient::PlatformName()
{
#if PLATFORM_WINDOWS
	return TEXT("windows");
#elif PLATFORM_LINUX
	return TEXT("linux");
#elif PLATFORM_MAC
	return TEXT("macos");
#else
	return TEXT("other");
#endif
}

// ---------------------------------------------------------------------------
// HTTP
// ---------------------------------------------------------------------------

TSharedRef<IHttpRequest, ESPMode::ThreadSafe> UHepgoClient::MakeRequest(const FString& Verb, const FString& Url) const
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(Url);
	Req->SetVerb(Verb);
	Req->SetTimeout(HttpTimeoutS);
	Req->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Code.Key);
	Req->SetHeader(TEXT("Accept"), TEXT("application/json, */*"));
	Req->SetHeader(TEXT("User-Agent"), FString::Printf(TEXT("CAYSIM/%s (Unreal Engine)"), ClientVersion()));
	// §2.1: kayda yazılan istemci bilgileri (her istekte zararsız)
	Req->SetHeader(TEXT("X-Caysim-Host"), FPlatformProcess::ComputerName());
	Req->SetHeader(TEXT("X-Caysim-Ver"), ClientVersion());
	Req->SetHeader(TEXT("X-Caysim-Platform"), PlatformName());
	return Req;
}

bool UHepgoClient::ParseJson(const FString& Text, TSharedPtr<FJsonObject>& Out)
{
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	return FJsonSerializer::Deserialize(Reader, Out) && Out.IsValid();
}

FString UHepgoClient::JsonToString(const TSharedRef<FJsonObject>& Obj)
{
	FString Out;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Obj, Writer);
	return Out;
}

TSharedPtr<FJsonObject> UHepgoClient::ParamsFromString(const FString& ParamsJson)
{
	TSharedPtr<FJsonObject> P;
	if (ParamsJson.IsEmpty() || !ParseJson(ParamsJson, P)) P = MakeShared<FJsonObject>();
	return P;
}

void UHepgoClient::ReportError(const FString& ErrCode, const FString& Msg)
{
	UE_LOG(LogCaySim, Warning, TEXT("HEPGO [%s]: %s"), *ErrCode, *Msg);
	OnError.Broadcast(ErrCode, Msg);
}

void UHepgoClient::ParseError(int32 Status, const FString& Body, FString& OutCode, FString& OutMsg) const
{
	// §1.1: {"detail": {"error": "<kod>", "message": "<metin>"}}; 422'de detail listedir
	OutCode.Empty(); OutMsg.Empty();
	TSharedPtr<FJsonObject> O;
	if (ParseJson(Body, O))
	{
		const TSharedPtr<FJsonObject>* D = nullptr;
		if (O->TryGetObjectField(TEXT("detail"), D) && D && D->IsValid())
		{
			(*D)->TryGetStringField(TEXT("error"), OutCode);
			(*D)->TryGetStringField(TEXT("message"), OutMsg);
		}
		else if (O->HasTypedField<EJson::Array>(TEXT("detail")))
		{
			OutCode = TEXT("invalid");
			OutMsg = TEXT("İstek doğrulanamadı (422).");
		}
		else
		{
			O->TryGetStringField(TEXT("error"), OutCode);
		}
	}
	if (OutCode.IsEmpty())
	{
		switch (Status)
		{
		case 400: OutCode = TEXT("invalid"); break;
		case 401: OutCode = TEXT("auth"); break;
		case 403: OutCode = TEXT("forbidden"); break;
		case 404: OutCode = TEXT("not_found"); break;
		case 409: OutCode = TEXT("limit"); break;
		case 413: OutCode = TEXT("too_large"); break;
		case 429: OutCode = TEXT("rate_limited"); break;
		case 503: OutCode = TEXT("sitl"); break;
		case 0:   OutCode = TEXT("network"); break;
		default:  OutCode = FString::Printf(TEXT("http_%d"), Status); break;
		}
	}
	if (OutMsg.IsEmpty())
	{
		if (OutCode == TEXT("auth")) OutMsg = TEXT("İstemci kodu geçersiz ya da yenilenmiş (401).");
		else if (OutCode == TEXT("client_inactive")) OutMsg = TEXT("Bu istemci HEPGO'da pasif (403).");
		else if (OutCode == TEXT("forbidden")) OutMsg = TEXT("İstemci sahibinin bu kayda yetkisi yok (403).");
		else if (OutCode == TEXT("not_found")) OutMsg = TEXT("Kayıt bulunamadı (404).");
		else if (OutCode == TEXT("limit")) OutMsg = TEXT("Eşzamanlı oturum sınırı doldu ya da kayıt hazır değil (409).");
		else if (OutCode == TEXT("rate_limited")) OutMsg = TEXT("Çok fazla başarısız kimlik denemesi; 60 sn bekleyin (429).");
		else if (OutCode == TEXT("sitl")) OutMsg = TEXT("SITL ya da arazi kaynağı kullanılamıyor (503).");
		else if (OutCode == TEXT("network")) OutMsg = TEXT("Sunucuya ulaşılamadı (ağ / DNS / TLS).");
		else OutMsg = FString::Printf(TEXT("HTTP %d"), Status);
	}
}

void UHepgoClient::RequestJson(const FString& Verb, const FString& Path, const FString& Body, FHepgoJsonNative Done, bool bSilent)
{
	if (!Code.IsValid())
	{
		if (!bSilent) ReportError(TEXT("no_code"), TEXT("İstemci kodu ayarlanmadı."));
		Done.ExecuteIfBound(false, nullptr);
		return;
	}
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = MakeRequest(Verb, ResolveUrl(Path));
	if (!Body.IsEmpty())
	{
		Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Req->SetContentAsString(Body);
	}
	TWeakObjectPtr<UHepgoClient> WeakThis(this);
	Req->OnProcessRequestComplete().BindLambda([WeakThis, Done, Path, bSilent](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		UHepgoClient* Self = WeakThis.Get();
		if (!Self) return;
		const int32 Status = (bOk && Resp.IsValid()) ? Resp->GetResponseCode() : 0;
		const FString Text = (bOk && Resp.IsValid()) ? Resp->GetContentAsString() : FString();
		if (Status < 200 || Status >= 300)
		{
			FString C, M;
			Self->ParseError(Status, Text, C, M);
			if (!bSilent) Self->ReportError(C, FString::Printf(TEXT("%s → %s"), *Path, *M));
			Done.ExecuteIfBound(false, nullptr);
			return;
		}
		TSharedPtr<FJsonObject> Obj;
		if (!Text.IsEmpty() && !Self->ParseJson(Text, Obj))
		{
			if (!bSilent) Self->ReportError(TEXT("invalid_json"), FString::Printf(TEXT("%s → geçersiz JSON"), *Path));
			Done.ExecuteIfBound(false, nullptr);
			return;
		}
		Done.ExecuteIfBound(true, Obj);
	});
	Req->ProcessRequest();
}

void UHepgoClient::RequestList(const FString& Path, const FString& Key, TFunction<void(bool, const TArray<TSharedPtr<FJsonValue>>&, const TSharedPtr<FJsonObject>&)> Done)
{
	GetJson(Path, FHepgoJsonNative::CreateWeakLambda(this, [Done, Key, Path](bool bOk, const TSharedPtr<FJsonObject>& O)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (bOk && O.IsValid() && O->TryGetArrayField(Key, Arr) && Arr)
		{
			Done(true, *Arr, O);
			return;
		}
		if (bOk) UE_LOG(LogCaySim, Warning, TEXT("%s → '%s' dizisi yok"), *Path, *Key);
		Done(false, {}, O);
	}));
}

void UHepgoClient::GetJson(const FString& Path, FHepgoJsonNative Done, bool bSilent)
{
	RequestJson(TEXT("GET"), Path, FString(), Done, bSilent);
}

void UHepgoClient::FetchBytes(const FString& PathOrUrl, FHepgoBytesNative Done, bool bSilent)
{
	if (!Code.IsValid())
	{
		Done.ExecuteIfBound(false, {});
		return;
	}
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = MakeRequest(TEXT("GET"), ResolveUrl(PathOrUrl));
	Req->SetHeader(TEXT("Accept"), TEXT("*/*"));
	Req->SetTimeout(FMath::Max(HttpTimeoutS, 120.f)); // büyük GLB / replay dosyaları
	TWeakObjectPtr<UHepgoClient> WeakThis(this);
	Req->OnProcessRequestComplete().BindLambda([WeakThis, Done, PathOrUrl, bSilent](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		UHepgoClient* Self = WeakThis.Get();
		if (!Self) return;
		const int32 Status = (bOk && Resp.IsValid()) ? Resp->GetResponseCode() : 0;
		if (Status < 200 || Status >= 300)
		{
			FString C, M;
			Self->ParseError(Status, (bOk && Resp.IsValid()) ? Resp->GetContentAsString() : FString(), C, M);
			if (!bSilent) Self->ReportError(C, FString::Printf(TEXT("%s → %s"), *PathOrUrl, *M));
			Done.ExecuteIfBound(false, {});
			return;
		}
		Done.ExecuteIfBound(true, Resp->GetContent());
	});
	Req->ProcessRequest();
}

// ---- yüksek düzey REST (§2) ---------------------------------------------

void UHepgoClient::Hello()
{
	GetJson(TEXT("/hello"), FHepgoJsonNative::CreateWeakLambda(this, [this](bool bOk, const TSharedPtr<FJsonObject>& O)
	{
		FCaySimHello H;
		if (bOk && O.IsValid())
		{
			double N;
			if (O->TryGetNumberField(TEXT("v"), N)) H.Version = (int32)N;
			O->TryGetStringField(TEXT("server_time"), H.ServerTime);
			const TSharedPtr<FJsonObject>* P = nullptr;
			if (O->TryGetObjectField(TEXT("client"), P) && P && P->IsValid())
			{
				if ((*P)->TryGetNumberField(TEXT("id"), N)) H.ClientId = (int32)N;
				(*P)->TryGetStringField(TEXT("name"), H.ClientName);
				const TSharedPtr<FJsonObject>* Ow = nullptr;
				if ((*P)->TryGetObjectField(TEXT("owner"), Ow) && Ow && Ow->IsValid()) (*Ow)->TryGetStringField(TEXT("name"), H.ClientOwnerName);
			}
			if (O->TryGetObjectField(TEXT("company"), P) && P && P->IsValid())
			{
				(*P)->TryGetStringField(TEXT("name"), H.Company.Name);
				(*P)->TryGetStringField(TEXT("legal_name"), H.Company.LegalName);
				(*P)->TryGetStringField(TEXT("logo_url"), H.Company.LogoUrl);
				(*P)->TryGetStringField(TEXT("primary_color"), H.Company.PrimaryColor);
			}
			const TArray<TSharedPtr<FJsonValue>>* Caps = nullptr;
			if (O->TryGetArrayField(TEXT("caps"), Caps) && Caps)
			{
				for (const auto& V : *Caps) H.Caps.Add(V->AsString());
			}
			if (O->TryGetObjectField(TEXT("limits"), P) && P && P->IsValid())
			{
				if ((*P)->TryGetNumberField(TEXT("max_sessions"), N)) H.MaxSessions = (int32)N;
				if ((*P)->TryGetNumberField(TEXT("max_minutes"), N)) H.MaxMinutes = (int32)N;
				if ((*P)->TryGetNumberField(TEXT("rc_hz"), N)) H.RcHz = FMath::Max(1, (int32)N);
				if ((*P)->TryGetNumberField(TEXT("tele_hz"), N)) H.TeleHz = (int32)N;
			}
			O->TryGetStringField(TEXT("host"), H.Host);
			if (O->TryGetObjectField(TEXT("google3d"), P) && P && P->IsValid()) (*P)->TryGetBoolField(TEXT("enabled"), H.bGoogle3dEnabled);
			if (O->TryGetObjectField(TEXT("sitl"), P) && P && P->IsValid()) (*P)->TryGetBoolField(TEXT("enabled"), H.bSitlEnabled);
			O->TryGetStringField(TEXT("ws_url"), H.WsUrl);
			if (H.Version != 1) UE_LOG(LogCaySim, Warning, TEXT("HEPGO CAYSIM API sürümü %d (beklenen 1) — uyumsuzluk olabilir"), H.Version);
			LastHello = H;
		}
		OnHello.Broadcast(bOk, H);
	}));
}

void UHepgoClient::FetchCenters()
{
	RequestList(TEXT("/centers"), TEXT("centers"), [this](bool bOk, const TArray<TSharedPtr<FJsonValue>>& Arr, const TSharedPtr<FJsonObject>&)
	{
		TArray<FCaySimCenter> Out;
		for (const auto& V : Arr)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			if (!O.IsValid()) continue;
			FCaySimCenter C;
			double N;
			if (O->TryGetNumberField(TEXT("id"), N)) C.Id = (int32)N;
			O->TryGetStringField(TEXT("name"), C.Name);
			O->TryGetNumberField(TEXT("lat"), C.Lat);
			O->TryGetNumberField(TEXT("lon"), C.Lon);
			C.bHasAlt = O->TryGetNumberField(TEXT("alt"), C.Alt); // null → false
			O->TryGetStringField(TEXT("location_name"), C.LocationName);
			Out.Add(C);
		}
		OnCenters.Broadcast(Out);
	});
}

void UHepgoClient::FetchTypes()
{
	RequestList(TEXT("/types"), TEXT("types"), [this](bool bOk, const TArray<TSharedPtr<FJsonValue>>& Arr, const TSharedPtr<FJsonObject>&)
	{
		TArray<FCaySimDroneTypeInfo> Out;
		for (const auto& V : Arr)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			if (!O.IsValid()) continue;
			FCaySimDroneTypeInfo T;
			O->TryGetStringField(TEXT("key"), T.Key);
			O->TryGetStringField(TEXT("name"), T.Name);
			O->TryGetBoolField(TEXT("binary"), T.bBinaryAvailable);
			T.Type = CaySimTypes::DroneTypeFromKey(T.Key);
			Out.Add(T);
		}
		OnTypes.Broadcast(Out);
	});
}

void UHepgoClient::FetchModels(const FString& TypeKey)
{
	const FString Path = TypeKey.IsEmpty() ? TEXT("/models") : FString::Printf(TEXT("/models?type=%s"), *FGenericPlatformHttp::UrlEncode(TypeKey));
	RequestList(Path, TEXT("models"), [this](bool bOk, const TArray<TSharedPtr<FJsonValue>>& Arr, const TSharedPtr<FJsonObject>& Root)
	{
		TArray<FCaySimModelInfo> Out;
		for (const auto& V : Arr)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			if (!O.IsValid()) continue;
			TSharedRef<FJsonObject> Wrap = MakeShared<FJsonObject>();
			Wrap->SetObjectField(TEXT("model"), O);
			Out.Add(FCaySimSession::FromJson(Wrap).Model);
		}
		// defaults:{type: builtin_id}
		const TSharedPtr<FJsonObject>* D = nullptr;
		if (Root.IsValid() && Root->TryGetObjectField(TEXT("defaults"), D) && D && D->IsValid())
		{
			for (const auto& KV : (*D)->Values)
			{
				if (KV.Value.IsValid() && KV.Value->Type == EJson::String) ModelDefaults.Add(FString(*KV.Key), KV.Value->AsString());
			}
		}
		OnModels.Broadcast(Out);
	});
}

FString UHepgoClient::GetDefaultModelFor(const FString& TypeKey) const
{
	const FString* P = ModelDefaults.Find(TypeKey);
	return P ? *P : FString();
}

void UHepgoClient::FetchWorldDefaults()
{
	GetJson(TEXT("/world-defaults"), FHepgoJsonNative::CreateWeakLambda(this, [this](bool bOk, const TSharedPtr<FJsonObject>& O)
	{
		FCaySimWorld W;
		if (bOk && O.IsValid())
		{
			const TSharedPtr<FJsonObject>* WD = nullptr;
			if (O->TryGetObjectField(TEXT("world_defaults"), WD) && WD) W = FCaySimWorld::FromJson(*WD);
			else W = FCaySimWorld::FromJson(O);
		}
		OnWorldDefaults.Broadcast(W);
	}));
}

void UHepgoClient::FetchLogs(const FString& Query)
{
	const FString Path = Query.IsEmpty() ? TEXT("/logs") : FString::Printf(TEXT("/logs?q=%s"), *FGenericPlatformHttp::UrlEncode(Query));
	RequestList(Path, TEXT("logs"), [this](bool bOk, const TArray<TSharedPtr<FJsonValue>>& Arr, const TSharedPtr<FJsonObject>&)
	{
		TArray<FCaySimLogInfo> Out;
		for (const auto& V : Arr)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			if (!O.IsValid()) continue;
			FCaySimLogInfo L;
			double N;
			if (O->TryGetNumberField(TEXT("id"), N)) L.Id = (int32)N;
			O->TryGetStringField(TEXT("file_name"), L.FileName);
			O->TryGetStringField(TEXT("ext"), L.Ext);
			if (O->TryGetNumberField(TEXT("size"), N)) L.Size = (int64)N;
			O->TryGetStringField(TEXT("uploaded_at"), L.UploadedAt);
			O->TryGetStringField(TEXT("aircraft"), L.Aircraft);
			O->TryGetStringField(TEXT("pilot"), L.Pilot);
			O->TryGetStringField(TEXT("location"), L.Location);
			O->TryGetStringField(TEXT("task_code"), L.TaskCode);
			O->TryGetStringField(TEXT("vehicle"), L.Vehicle);
			O->TryGetStringField(TEXT("type"), L.TypeKey);
			if (O->TryGetNumberField(TEXT("duration_s"), N)) L.DurationS = (float)N;
			O->TryGetNumberField(TEXT("home_lat"), L.HomeLat);
			O->TryGetNumberField(TEXT("home_lon"), L.HomeLon);
			O->TryGetBoolField(TEXT("cached"), L.bCached);
			Out.Add(L);
		}
		OnLogs.Broadcast(Out);
	});
}

void UHepgoClient::FetchSessions()
{
	RequestList(TEXT("/sessions"), TEXT("sessions"), [this](bool bOk, const TArray<TSharedPtr<FJsonValue>>& Arr, const TSharedPtr<FJsonObject>&)
	{
		TArray<FCaySimSession> Out;
		for (const auto& V : Arr) Out.Add(FCaySimSession::FromJson(V->AsObject()));
		OnSessions.Broadcast(Out);
	});
}

void UHepgoClient::FetchSession(const FString& SessionId)
{
	GetJson(FString::Printf(TEXT("/sessions/%s"), *SessionId), FHepgoJsonNative::CreateWeakLambda(this, [this](bool bOk, const TSharedPtr<FJsonObject>& O)
	{
		OnSessionFetched.Broadcast(bOk, bOk ? FCaySimSession::FromJson(O) : FCaySimSession());
	}));
}

void UHepgoClient::FetchMavlinkInfo(const FString& SessionId)
{
	GetJson(FString::Printf(TEXT("/sessions/%s/mavlink"), *SessionId), FHepgoJsonNative::CreateWeakLambda(this, [this](bool bOk, const TSharedPtr<FJsonObject>& O)
	{
		FCaySimMavlinkInfo I;
		if (bOk && O.IsValid())
		{
			double N;
			O->TryGetStringField(TEXT("host"), I.Host);
			if (O->TryGetNumberField(TEXT("udp_port"), N)) I.UdpPort = (int32)N;
			O->TryGetBoolField(TEXT("gcs_locked"), I.bGcsLocked);
			O->TryGetStringField(TEXT("gcs_addr"), I.GcsAddr);
			O->TryGetStringField(TEXT("note"), I.Note);
		}
		OnMavlinkInfo.Broadcast(bOk, I);
	}));
}

void UHepgoClient::CreateSitlSession(const FString& TypeKey, int32 CenterId, const FCaySimWorld& World, const FString& ModelId, const FCaySimModelOrient& Orient, bool bSendOrient)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("mode"), TEXT("sitl"));
	Body->SetStringField(TEXT("type"), TypeKey);
	Body->SetNumberField(TEXT("center_id"), CenterId);
	Body->SetObjectField(TEXT("world"), World.ToJson());
	if (!ModelId.IsEmpty())
	{
		TSharedRef<FJsonObject> M = MakeShared<FJsonObject>();
		M->SetStringField(TEXT("id"), ModelId);
		if (bSendOrient) M->SetObjectField(TEXT("orient"), Orient.ToJson());
		else M->SetField(TEXT("orient"), MakeShared<FJsonValueNull>());
		Body->SetObjectField(TEXT("model"), M);
	}
	RequestJson(TEXT("POST"), TEXT("/sessions"), JsonToString(Body), FHepgoJsonNative::CreateWeakLambda(this, [this](bool bOk, const TSharedPtr<FJsonObject>& O)
	{
		OnSessionCreated.Broadcast(bOk, bOk ? FCaySimSession::FromJson(O) : FCaySimSession());
	}));
}

void UHepgoClient::CreateReplaySession(int32 LogId, const FCaySimWorld& World, const FString& ModelId)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("mode"), TEXT("replay"));
	Body->SetNumberField(TEXT("log_id"), LogId);
	Body->SetObjectField(TEXT("world"), World.ToJson());
	if (!ModelId.IsEmpty())
	{
		TSharedRef<FJsonObject> M = MakeShared<FJsonObject>();
		M->SetStringField(TEXT("id"), ModelId);
		Body->SetObjectField(TEXT("model"), M);
	}
	RequestJson(TEXT("POST"), TEXT("/sessions"), JsonToString(Body), FHepgoJsonNative::CreateWeakLambda(this, [this](bool bOk, const TSharedPtr<FJsonObject>& O)
	{
		OnSessionCreated.Broadcast(bOk, bOk ? FCaySimSession::FromJson(O) : FCaySimSession());
	}));
}

void UHepgoClient::DeleteSession(const FString& SessionId)
{
	if (WsSessionId == SessionId) Disconnect();
	RequestJson(TEXT("DELETE"), FString::Printf(TEXT("/sessions/%s"), *SessionId), FString(), FHepgoJsonNative::CreateWeakLambda(this, [](bool, const TSharedPtr<FJsonObject>&) {}));
}

void UHepgoClient::SendCommandRest(const FString& SessionId, const FString& Name, const FString& ParamsJson)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("name"), Name);
	Body->SetObjectField(TEXT("params"), ParamsFromString(ParamsJson));
	RequestJson(TEXT("POST"), FString::Printf(TEXT("/sessions/%s/cmd"), *SessionId), JsonToString(Body),
		FHepgoJsonNative::CreateWeakLambda(this, [this, Name](bool bOk, const TSharedPtr<FJsonObject>&)
	{
		OnCommandResult.Broadcast(Name, FString(), bOk, bOk ? FString() : TEXT("rest"));
	}));
}

void UHepgoClient::FetchGoogle3dKey(TFunction<void(bool, const FString&)> Done)
{
	// Kapalıyken 404 → sessiz
	GetJson(TEXT("/g3d-key"), FHepgoJsonNative::CreateWeakLambda(this, [Done](bool bOk, const TSharedPtr<FJsonObject>& O)
	{
		FString Key;
		if (bOk && O.IsValid()) O->TryGetStringField(TEXT("key"), Key);
		Done(bOk && !Key.IsEmpty(), Key);
	}), /*bSilent*/ true);
}

void UHepgoClient::FetchTerrain(double Lat, double Lon, float SizeM, FHepgoJsonNative Done)
{
	GetJson(FString::Printf(TEXT("/terrain?lat=%.6f&lon=%.6f&size_m=%.0f"), Lat, Lon, SizeM), Done);
}

void UHepgoClient::FetchOsm(double Lat, double Lon, float SizeM, FHepgoJsonNative Done)
{
	GetJson(FString::Printf(TEXT("/osm?lat=%.6f&lon=%.6f&size_m=%.0f"), Lat, Lon, SizeM), Done);
}

// ---------------------------------------------------------------------------
// WebSocket (§3)
// ---------------------------------------------------------------------------

void UHepgoClient::SetWsState(EHepgoWsState S)
{
	if (WsState == S) return;
	WsState = S;
	OnWsStateChanged.Broadcast(S);
}

void UHepgoClient::ConnectSession(const FString& SessionId)
{
	if (!Code.IsValid())
	{
		ReportError(TEXT("no_code"), TEXT("İstemci kodu ayarlanmadı."));
		return;
	}
	if (WsSessionId != SessionId) CloseWs();
	WsSessionId = SessionId;
	bWantConnected = true;
	BackoffIndex = 0;
	OpenWs();
}

void UHepgoClient::Disconnect()
{
	bWantConnected = false;
	NextReconnectAt = 0.0;
	CloseWs();
	WsSessionId.Empty();
	SetWsState(EHepgoWsState::Disconnected);
}

void UHepgoClient::OpenWs()
{
	CloseWs();
	FWebSocketsModule& Mod = FModuleManager::LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));
	// Kimlik: `?key=` sorgusu (§3); ayrıca ilk çerçeve `auth` da gönderilir (vekil sorguyu düşürürse)
	const FString Url = FString::Printf(TEXT("%s/sessions/%s?key=%s"), *Code.WsBase(), *WsSessionId, *FGenericPlatformHttp::UrlEncode(Code.Key));
	TMap<FString, FString> Headers;
	Headers.Add(TEXT("User-Agent"), FString::Printf(TEXT("CAYSIM/%s (Unreal Engine)"), ClientVersion()));
	Headers.Add(TEXT("Authorization"), TEXT("Bearer ") + Code.Key); // sunucu yok sayar; vekiller için zararsız
	Ws = Mod.CreateWebSocket(Url, TEXT(""), Headers);
	if (!Ws.IsValid())
	{
		ReportError(TEXT("ws"), TEXT("WebSocket oluşturulamadı."));
		ScheduleReconnect();
		return;
	}
	SetWsState(BackoffIndex > 0 ? EHepgoWsState::Reconnecting : EHepgoWsState::Connecting);

	TWeakObjectPtr<UHepgoClient> WeakThis(this);
	Ws->OnConnected().AddLambda([WeakThis]()
	{
		if (UHepgoClient* Self = WeakThis.Get())
		{
			Self->BackoffIndex = 0;
			Self->LastPingAt = Self->LastRxAt = FPlatformTime::Seconds();
			// Yedek kimlik çerçevesi (sorgu zaten var; sunucu ikisini de kabul eder)
			TSharedRef<FJsonObject> A = MakeShared<FJsonObject>();
			A->SetStringField(TEXT("t"), TEXT("auth"));
			A->SetStringField(TEXT("key"), Self->Code.Key);
			Self->SendWsJson(A);
			Self->SetWsState(EHepgoWsState::Connected);
			UE_LOG(LogCaySim, Log, TEXT("WS bağlandı: oturum %s"), *Self->WsSessionId);
		}
	});
	Ws->OnConnectionError().AddLambda([WeakThis](const FString& Err)
	{
		if (UHepgoClient* Self = WeakThis.Get())
		{
			UE_LOG(LogCaySim, Warning, TEXT("WS bağlantı hatası: %s"), *Err);
			Self->ScheduleReconnect();
		}
	});
	Ws->OnClosed().AddLambda([WeakThis](int32 Status, const FString& Reason, bool bWasClean)
	{
		if (UHepgoClient* Self = WeakThis.Get())
		{
			UE_LOG(LogCaySim, Log, TEXT("WS kapandı: %d %s"), Status, *Reason);
			switch (Status)
			{
			case 4400:
				Self->ReportError(TEXT("auth"), TEXT("WebSocket kimlik zaman aşımı (4400)."));
				Self->ScheduleReconnect();
				return;
			case 4403:
				Self->ReportError(TEXT("auth"), TEXT("WebSocket: anahtar geçersiz, istemci pasif ya da kod yenilenmiş (4403)."));
				Self->bWantConnected = false;
				Self->SetWsState(EHepgoWsState::Failed);
				return;
			case 4404:
				Self->ReportError(TEXT("not_found"), TEXT("Oturum bulunamadı (4404)."));
				Self->bWantConnected = false;
				Self->SetWsState(EHepgoWsState::Failed);
				Self->OnEnd.Broadcast(TEXT("not_found"));
				return;
			case 4429:
				Self->ReportError(TEXT("rate_limited"), TEXT("Çok fazla başarısız kimlik denemesi (4429); 60 sn bekleniyor."));
				Self->ScheduleReconnect(60.0);
				return;
			case 1000:
				// `end` sonrası normal kapanış
				if (Self->bWantConnected) Self->ScheduleReconnect();
				else Self->SetWsState(EHepgoWsState::Disconnected);
				return;
			default:
				if (Self->bWantConnected) Self->ScheduleReconnect();
				else Self->SetWsState(EHepgoWsState::Disconnected);
				return;
			}
		}
	});
	Ws->OnMessage().AddLambda([WeakThis](const FString& Msg)
	{
		if (UHepgoClient* Self = WeakThis.Get()) Self->HandleWsMessage(Msg);
	});
	Ws->Connect();
}

void UHepgoClient::CloseWs()
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

void UHepgoClient::ScheduleReconnect(double DelayOverrideS)
{
	if (!bWantConnected)
	{
		SetWsState(EHepgoWsState::Disconnected);
		return;
	}
	SetWsState(EHepgoWsState::Reconnecting);
	const int32 N = UE_ARRAY_COUNT(GBackoffS);
	const double Delay = DelayOverrideS > 0 ? DelayOverrideS : FMath::Min((double)GBackoffS[FMath::Min(BackoffIndex, N - 1)], (double)MaxBackoffS);
	BackoffIndex = FMath::Min(BackoffIndex + 1, N - 1);
	NextReconnectAt = FPlatformTime::Seconds() + Delay;
}

void UHepgoClient::TryReconnect()
{
	// §3.3: her denemede GET /sessions/{id} — 404 → oturum bitmiş
	GetJson(FString::Printf(TEXT("/sessions/%s"), *WsSessionId), FHepgoJsonNative::CreateWeakLambda(this, [this](bool bOk, const TSharedPtr<FJsonObject>& O)
	{
		if (!bWantConnected) return;
		if (!bOk)
		{
			// Ağ hatası mı, 404 mü? O geçersiz ve bOk=false → hata ayrımı için yeniden sorgulamak yerine bir sonraki denemeye bırak;
			// kalıcı 404 ise kullanıcıya bildirmek için oturum durumu çağrısı OnSessionFetched üzerinden de görünür.
			ScheduleReconnect();
			return;
		}
		const FCaySimSession S = FCaySimSession::FromJson(O);
		if (S.State == ECaySimSessionState::Closed)
		{
			bWantConnected = false;
			SetWsState(EHepgoWsState::Failed);
			OnEnd.Broadcast(TEXT("kapatildi"));
			return;
		}
		OnSessionState.Broadcast(S);
		OpenWs();
	}), /*bSilent*/ true);
}

bool UHepgoClient::Tick(float /*Dt*/)
{
	const double Now = FPlatformTime::Seconds();
	if (bWantConnected && WsState == EHepgoWsState::Reconnecting && NextReconnectAt > 0 && Now >= NextReconnectAt)
	{
		NextReconnectAt = 0;
		TryReconnect();
	}
	if (WsState == EHepgoWsState::Connected && Ws.IsValid())
	{
		if (Now - LastPingAt >= PingIntervalS)
		{
			LastPingAt = PingSentAt = Now;
			TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
			P->SetStringField(TEXT("t"), TEXT("ping"));
			SendWsJson(P);
		}
		// Hiç çerçeve gelmiyorsa (bekçi ≥ 4 Hz olmalı) bağlantı ölü
		if (Now - LastRxAt > FMath::Max(TeleTimeoutS, PingIntervalS + 5.f))
		{
			UE_LOG(LogCaySim, Warning, TEXT("WS: %.0f sn veri yok, yeniden bağlanılıyor"), Now - LastRxAt);
			CloseWs();
			ScheduleReconnect();
		}
		// RC kısma (limits.rc_hz; varsayılan 40 Hz → istemci 25 Hz)
		const double RcInterval = 1.0 / FMath::Clamp((double)LastHello.RcHz * 0.625, 5.0, 40.0);
		if (bRcPending && Now - LastRcSentAt >= RcInterval)
		{
			bRcPending = false;
			LastRcSentAt = Now;
			TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("t"), TEXT("rc"));
			TArray<TSharedPtr<FJsonValue>> Ch;
			for (int32 V : PendingRc) Ch.Add(MakeShared<FJsonValueNumber>(V));
			R->SetArrayField(TEXT("ch"), Ch);
			SendWsJson(R);
		}
	}
	return true;
}

void UHepgoClient::SendWsJson(const TSharedRef<FJsonObject>& Obj)
{
	if (Ws.IsValid() && Ws->IsConnected()) Ws->Send(JsonToString(Obj));
}

FString UHepgoClient::SendCommand(const FString& Name, const FString& ParamsJson)
{
	const FString Rid = FString::Printf(TEXT("c%d"), ++RidCounter);
	if (!Ws.IsValid() || !Ws->IsConnected())
	{
		if (!WsSessionId.IsEmpty()) SendCommandRest(WsSessionId, Name, ParamsJson);
		else OnCommandResult.Broadcast(Name, Rid, false, TEXT("bağlı değil"));
		return Rid;
	}
	TSharedRef<FJsonObject> C = MakeShared<FJsonObject>();
	C->SetStringField(TEXT("t"), TEXT("cmd"));
	C->SetStringField(TEXT("name"), Name);
	C->SetObjectField(TEXT("params"), ParamsFromString(ParamsJson));
	C->SetStringField(TEXT("rid"), Rid);
	SendWsJson(C);
	return Rid;
}

FString UHepgoClient::CmdArm(bool bArm) { return SendCommand(bArm ? TEXT("arm") : TEXT("disarm"), TEXT("{}")); }
FString UHepgoClient::CmdMode(const FString& ModeName) { return SendCommand(TEXT("mode"), FString::Printf(TEXT("{\"mode\":\"%s\"}"), *ModeName)); }
FString UHepgoClient::CmdTakeoff(float AltM) { return SendCommand(TEXT("takeoff"), FString::Printf(TEXT("{\"alt\":%.1f}"), FMath::Clamp(AltM, 1.f, 1000.f))); }
FString UHepgoClient::CmdRtl() { return SendCommand(TEXT("rtl"), TEXT("{}")); }
FString UHepgoClient::CmdReset() { return SendCommand(TEXT("reset"), TEXT("{}")); }
FString UHepgoClient::CmdCrash() { return SendCommand(TEXT("crash"), TEXT("{}")); }
FString UHepgoClient::CmdSetHome(double Lat, double Lon, double Alt)
{
	return SendCommand(TEXT("set_home"), FString::Printf(TEXT("{\"lat\":%.7f,\"lon\":%.7f,\"alt\":%.1f}"), Lat, Lon, Alt));
}

void UHepgoClient::SendRc(const TArray<int32>& Channels)
{
	PendingRc = Channels;
	PendingRc.SetNum(8);
	for (int32& V : PendingRc) if (V != 0) V = FMath::Clamp(V, 1000, 2000);
	bRcPending = true;
	bool bAllZero = true;
	for (int32 V : PendingRc) if (V != 0) { bAllZero = false; break; }
	if (bAllZero) LastRcSentAt = 0; // serbest bırakma sınırlanmaz (§3.2)
}

void UHepgoClient::ReleaseRc()
{
	TArray<int32> Z;
	Z.Init(0, 8);
	SendRc(Z);
}

void UHepgoClient::HandleWsMessage(const FString& Msg)
{
	LastRxAt = FPlatformTime::Seconds();
	TSharedPtr<FJsonObject> O;
	if (!ParseJson(Msg, O)) return;
	FString T;
	if (!O->TryGetStringField(TEXT("t"), T)) return;

	if (T == TEXT("tele"))
	{
		const FCaySimTelemetry Tele = FCaySimTelemetry::FromJson(O);
		// Saat eşitleme: `tele.ts` − yerel saat, EMA (§3.3)
		if (Tele.Ts > 0)
		{
			const double Sample = Tele.Ts - LocalUnixNow();
			if (!bClockSynced) { ServerClockOffset = Sample; bClockSynced = true; }
			else ServerClockOffset += (Sample - ServerClockOffset) * 0.02;
		}
		OnTelemetryNative.Broadcast(Tele);
		if (OnTelemetry.IsBound()) OnTelemetry.Broadcast(Tele);
	}
	else if (T == TEXT("pong"))
	{
		RttMs = (float)((LastRxAt - PingSentAt) * 1000.0);
	}
	else if (T == TEXT("hello"))
	{
		const TSharedPtr<FJsonObject>* S = nullptr;
		FCaySimSession Sess;
		if (O->TryGetObjectField(TEXT("session"), S) && S) Sess = FCaySimSession::FromJson(*S);
		O->TryGetBoolField(TEXT("can_command"), Sess.bCanCommand);
		double V;
		if (O->TryGetNumberField(TEXT("v"), V) && (int32)V != 1) UE_LOG(LogCaySim, Warning, TEXT("WS hello v=%d (beklenen 1)"), (int32)V);
		OnWsHello.Broadcast(Sess);
	}
	else if (T == TEXT("event"))
	{
		FCaySimEvent E;
		O->TryGetStringField(TEXT("text"), E.Text);
		double N;
		if (O->TryGetNumberField(TEXT("severity"), N)) E.Severity = (int32)N;
		if (O->TryGetNumberField(TEXT("ts"), N)) E.T = N;
		OnEvent.Broadcast(E);
	}
	else if (T == TEXT("state"))
	{
		// `{t:'state', state, replay?}` (§3.1)
		FCaySimSession Sess;
		Sess.Id = WsSessionId;
		FString St;
		if (O->TryGetStringField(TEXT("state"), St)) Sess.State = CaySimTypes::StateFromString(St);
		const TSharedPtr<FJsonObject>* R = nullptr;
		if (O->TryGetObjectField(TEXT("replay"), R) && R && R->IsValid())
		{
			TSharedRef<FJsonObject> Wrap = MakeShared<FJsonObject>();
			Wrap->SetObjectField(TEXT("replay"), *R);
			Sess.Replay = FCaySimSession::FromJson(Wrap).Replay;
		}
		OnSessionState.Broadcast(Sess);
	}
	else if (T == TEXT("launch"))
	{
		// HEPGO §3.1/§13: {method: wheels|ramp|vehicle, phase: armed|launch|released, ts, angle_deg?, release_kmh?, speed_kmh?, alt?}
		OnLaunch.Broadcast(FCaySimLaunchEvent::FromJson(O));
	}
	else if (T == TEXT("end"))
	{
		FString R;
		O->TryGetStringField(TEXT("reason"), R);
		bWantConnected = false;
		SetWsState(EHepgoWsState::Failed);
		OnEnd.Broadcast(R);
	}
	else if (T == TEXT("res"))
	{
		FString Name, Rid, Err;
		bool bOk = false;
		O->TryGetStringField(TEXT("name"), Name);
		O->TryGetStringField(TEXT("rid"), Rid);
		O->TryGetBoolField(TEXT("ok"), bOk);
		O->TryGetStringField(TEXT("error"), Err);
		OnCommandResult.Broadcast(Name, Rid, bOk, Err);
	}
	else if (T == TEXT("replay"))
	{
		FString A;
		O->TryGetStringField(TEXT("action"), A);
		OnReplayControl.Broadcast(A);
	}
	else if (T == TEXT("error"))
	{
		FString E;
		O->TryGetStringField(TEXT("error"), E);
		ReportError(E, FString::Printf(TEXT("WebSocket hatası: %s"), *E));
	}
}
