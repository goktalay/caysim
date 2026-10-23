#include "SimumavClient.h"
#include "SimumavModule.h"
#include "SimumavSecrets.h"
#include "SimumavSocket.h"
#include "SmvKdf.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// ---------------------------------------------------------------------------
// yardımcılar
// ---------------------------------------------------------------------------

FString USimumavClient::ResolveUrl(const FString& P) const
{
	if (P.StartsWith(TEXT("http://")) || P.StartsWith(TEXT("https://"))) return P;
	FString Base = ApiBase;
	Base.RemoveFromEnd(TEXT("/"));
	return Base + (P.StartsWith(TEXT("/")) ? P : TEXT("/") + P);
}

FString USimumavClient::WsUrl(const FString& Path) const
{
	FString U = ResolveUrl(Path);
	if (U.StartsWith(TEXT("https://"))) U = TEXT("wss://") + U.RightChop(8);
	else if (U.StartsWith(TEXT("http://"))) U = TEXT("ws://") + U.RightChop(7);
	return U;
}

TMap<FString, FString> USimumavClient::WsHeaders() const
{
	TMap<FString, FString> H;
	if (!SessionCookie.IsEmpty()) H.Add(TEXT("Cookie"), TEXT("smsid=") + SessionCookie);
	H.Add(TEXT("Origin"), OriginHeader);
	H.Add(TEXT("User-Agent"), FString::Printf(TEXT("Simumav/%s (Steam; Unreal Engine)"), ClientVersion()));
	return H;
}

TSharedRef<FSmvSocket> USimumavClient::MakeSocket() const { return MakeShared<FSmvSocket>(); }

FString USimumavClient::DeviceName()
{
#if PLATFORM_WINDOWS
	return TEXT("Simumav Steam (Windows)");
#elif PLATFORM_LINUX
	return TEXT("Simumav Steam (Linux)");
#else
	return TEXT("Simumav Steam");
#endif
}

FString USimumavClient::MtUrl(const FString& Path) const
{
	FString Mt = SmvJson::Str(Meta, TEXT("mt_url"), TEXT("https://mastertool.download"));
	Mt.RemoveFromEnd(TEXT("/"));
	return FString::Printf(TEXT("%s/api/%s%s"), *Mt, *SmvJson::Str(Meta, TEXT("mt_app"), TEXT("sv")), *Path);
}

void USimumavClient::ParseError(int32 Status, const FString& Body, FString& Code, FString& Msg)
{
	Code.Empty(); Msg.Empty();
	const TSharedPtr<FJsonObject> O = SmvJson::Parse(Body);
	if (O.IsValid())
	{
		if (const TSharedPtr<FJsonObject> D = SmvJson::Obj(O, TEXT("detail")))
		{
			Code = SmvJson::Str(D, TEXT("error"));
			Msg = SmvJson::Str(D, TEXT("message"));
		}
		else
		{
			Code = SmvJson::Str(O, TEXT("error"));
			Msg = SmvJson::Str(O, TEXT("message"), SmvJson::Str(O, TEXT("detail")));
		}
	}
	if (Code.IsEmpty())
	{
		switch (Status)
		{
		case 0: Code = TEXT("network"); break;
		case 401: Code = TEXT("unauthorized"); break;
		case 403: Code = TEXT("forbidden"); break;
		case 404: Code = TEXT("not_found"); break;
		case 409: Code = TEXT("conflict"); break;
		case 422: Code = TEXT("invalid"); break;
		case 429: Code = TEXT("rate_limited"); break;
		default: Code = FString::Printf(TEXT("http_%d"), Status); break;
		}
	}
	if (Msg.IsEmpty()) Msg = Status == 0 ? TEXT("Sunucuya ulaşılamadı (ağ / DNS / TLS).") : FString::Printf(TEXT("HTTP %d"), Status);
}

// ---------------------------------------------------------------------------
// REST
// ---------------------------------------------------------------------------

void USimumavClient::Request(const FString& Verb, const FString& UrlOrPath, const TSharedPtr<FJsonObject>& Body, FSmvRestDone Done, bool bAuth,
	const TMap<FString, FString>& Extra)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(ResolveUrl(UrlOrPath));
	Req->SetVerb(Verb);
	Req->SetTimeout(HttpTimeoutS);
	Req->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Req->SetHeader(TEXT("User-Agent"), FString::Printf(TEXT("Simumav/%s (Steam; Unreal Engine)"), ClientVersion()));
	if (bAuth)
	{
		if (!SessionCookie.IsEmpty()) Req->SetHeader(TEXT("Cookie"), TEXT("smsid=") + SessionCookie);
		// sunucu CSRF denetimi (auth.py): yazma isteklerinde izinli Origin
		if (Verb != TEXT("GET")) Req->SetHeader(TEXT("Origin"), OriginHeader);
	}
	for (const TPair<FString, FString>& H : Extra) Req->SetHeader(H.Key, H.Value);
	if (Body.IsValid())
	{
		Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Req->SetContentAsString(SmvJson::ToString(Body.ToSharedRef()));
	}
	TWeakObjectPtr<USimumavClient> Weak(this);
	Req->OnProcessRequestComplete().BindLambda([Weak, Done](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		USimumavClient* Self = Weak.Get();
		const int32 Status = (bOk && Resp.IsValid()) ? Resp->GetResponseCode() : 0;
		const FString Text = (bOk && Resp.IsValid()) ? Resp->GetContentAsString() : FString();
		if (Self && Resp.IsValid())
		{
			// smsid çerezi yenilenmiş/silinmiş olabilir
			const FString SetCookie = Resp->GetHeader(TEXT("Set-Cookie"));
			int32 At = SetCookie.Find(TEXT("smsid="));
			if (At != INDEX_NONE)
			{
				FString V = SetCookie.Mid(At + 6);
				int32 Semi;
				if (V.FindChar(TEXT(';'), Semi)) V = V.Left(Semi);
				V.TrimStartAndEndInline();
				V.TrimQuotesInline();
				if (V.IsEmpty() || V == TEXT("\"\""))
				{
					Self->SessionCookie.Empty();
					FSmvSecrets::Delete(FSmvSecrets::KeySessionCookie);
				}
				else if (V != Self->SessionCookie)
				{
					Self->SessionCookie = V;
					FSmvSecrets::Save(FSmvSecrets::KeySessionCookie, V);
				}
			}
		}
		if (Status < 200 || Status >= 300)
		{
			FString C, M;
			ParseError(Status, Text, C, M);
			Done.ExecuteIfBound(Status, SmvJson::Parse(Text), C, M);
			return;
		}
		TSharedPtr<FJsonObject> O;
		const FString Trim = Text.TrimStart();
		if (Trim.StartsWith(TEXT("[")))
		{
			TArray<TSharedPtr<FJsonValue>> Arr;
			TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Text);
			FJsonSerializer::Deserialize(R, Arr);
			O = MakeShared<FJsonObject>();
			O->SetArrayField(TEXT("items"), Arr);
		}
		else if (!Trim.IsEmpty())
		{
			O = SmvJson::Parse(Text);
		}
		Done.ExecuteIfBound(Status, O.IsValid() ? O : TSharedPtr<FJsonObject>(MakeShared<FJsonObject>()), FString(), FString());
	});
	Req->ProcessRequest();
}

void USimumavClient::Get(const FString& Path, FSmvRestDone Done) { Request(TEXT("GET"), Path, nullptr, Done); }
void USimumavClient::Post(const FString& Path, const TSharedRef<FJsonObject>& Body, FSmvRestDone Done) { Request(TEXT("POST"), Path, Body, Done); }
void USimumavClient::Put(const FString& Path, const TSharedRef<FJsonObject>& Body, FSmvRestDone Done) { Request(TEXT("PUT"), Path, Body, Done); }
void USimumavClient::Delete(const FString& Path, FSmvRestDone Done, const TSharedPtr<FJsonObject>& Body) { Request(TEXT("DELETE"), Path, Body, Done); }

void USimumavClient::GetBytes(const FString& UrlOrPath, TFunction<void(bool, const TArray<uint8>&)> Done)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(ResolveUrl(UrlOrPath));
	Req->SetVerb(TEXT("GET"));
	Req->SetTimeout(FMath::Max(HttpTimeoutS, 120.f));
	Req->SetHeader(TEXT("User-Agent"), FString::Printf(TEXT("Simumav/%s (Steam; Unreal Engine)"), ClientVersion()));
	if (!SessionCookie.IsEmpty()) Req->SetHeader(TEXT("Cookie"), TEXT("smsid=") + SessionCookie);
	Req->OnProcessRequestComplete().BindLambda([Done](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		const bool bGood = bOk && Resp.IsValid() && Resp->GetResponseCode() >= 200 && Resp->GetResponseCode() < 300;
		Done(bGood, bGood ? Resp->GetContent() : TArray<uint8>());
	});
	Req->ProcessRequest();
}

void USimumavClient::FetchMeta(TFunction<void(bool)> Done)
{
	TWeakObjectPtr<USimumavClient> Weak(this);
	Get(TEXT("/api/meta"), FSmvRestDone::CreateLambda([Weak, Done](int32 Status, const TSharedPtr<FJsonObject>& O, const FString&, const FString&)
	{
		if (USimumavClient* Self = Weak.Get())
		{
			if (Status == 200) Self->Meta = O;
		}
		if (Done) Done(Status == 200);
	}));
}

// ---------------------------------------------------------------------------
// kimlik
// ---------------------------------------------------------------------------

void USimumavClient::ApplySession(const TSharedPtr<FJsonObject>& O)
{
	SessionInfo = O;
	User = FSmvUser::FromJson(SmvJson::Obj(O, TEXT("user")));
	OnAuthChanged.Broadcast(IsLoggedIn());
}

void USimumavClient::ClearAuth(bool bForget)
{
	const bool bWas = IsLoggedIn();
	SessionCookie.Empty();
	User = FSmvUser();
	SessionInfo.Reset();
	if (bForget)
	{
		MtToken.Empty();
		FSmvSecrets::Delete(FSmvSecrets::KeySessionCookie);
		FSmvSecrets::Delete(FSmvSecrets::KeyMtToken);
	}
	if (bWas) OnAuthChanged.Broadcast(false);
}

void USimumavClient::ResumeSession(TFunction<void(bool)> Done)
{
	FSmvSecrets::Load(FSmvSecrets::KeySessionCookie, SessionCookie);
	FSmvSecrets::Load(FSmvSecrets::KeyMtToken, MtToken);
	if (SessionCookie.IsEmpty() && MtToken.IsEmpty()) { if (Done) Done(false); return; }
	TWeakObjectPtr<USimumavClient> Weak(this);
	auto TryToken = [Weak, Done]()
	{
		USimumavClient* Self = Weak.Get();
		if (!Self || Self->MtToken.IsEmpty()) { if (Done) Done(false); return; }
		Self->OpenSessionWithToken(Self->MtToken, [Done](bool bOk, const FString&, const FString&) { if (Done) Done(bOk); });
	};
	if (SessionCookie.IsEmpty()) { FetchMeta([TryToken](bool) { TryToken(); }); return; }
	Get(TEXT("/api/session"), FSmvRestDone::CreateLambda([Weak, Done, TryToken](int32 Status, const TSharedPtr<FJsonObject>& O, const FString&, const FString&)
	{
		USimumavClient* Self = Weak.Get();
		if (!Self) return;
		if (Status == 200) { Self->ApplySession(O); Self->FetchMeta(nullptr); if (Done) Done(true); return; }
		if (Status == 401) { Self->SessionCookie.Empty(); TryToken(); return; }
		// ağ hatası: kayıtlar korunur, misafir gibi devam (çevrim dışı)
		if (Done) Done(false);
	}));
}

void USimumavClient::OpenSessionWithToken(const FString& Token, TFunction<void(bool, const FString&, const FString&)> Done)
{
	TSharedRef<FJsonObject> B = MakeShared<FJsonObject>();
	B->SetStringField(TEXT("token"), Token);
	TWeakObjectPtr<USimumavClient> Weak(this);
	Post(TEXT("/api/session"), B, FSmvRestDone::CreateLambda([Weak, Done, Token](int32 Status, const TSharedPtr<FJsonObject>& O, const FString& C, const FString& M)
	{
		USimumavClient* Self = Weak.Get();
		if (!Self) return;
		if (Status == 200 || Status == 201)
		{
			Self->MtToken = Token;
			FSmvSecrets::Save(FSmvSecrets::KeyMtToken, Token);
			Self->ApplySession(O);
			if (Done) Done(Self->IsLoggedIn(), FString(), FString());
			return;
		}
		if (Status == 401 || C == TEXT("bad_token")) Self->ClearAuth(true);
		if (Done) Done(false, C, M);
	}));
}

void USimumavClient::Login(const FString& EmailIn, const FString& Password, TFunction<void(bool, const FString&, const FString&)> Done)
{
	const FString Email = EmailIn.TrimStartAndEnd().ToLower();
	TWeakObjectPtr<USimumavClient> Weak(this);
	FetchMeta([Weak, Email, Password, Done](bool bMeta)
	{
		USimumavClient* Self = Weak.Get();
		if (!Self) return;
		if (!bMeta) { Done(false, TEXT("network"), TEXT("simumav.com'a ulaşılamadı.")); return; }
		TMap<FString, FString> Mt;
		Mt.Add(TEXT("x-sshm"), TEXT("1"));
		TSharedRef<FJsonObject> Pre = MakeShared<FJsonObject>();
		Pre->SetStringField(TEXT("email"), Email);
		Self->Request(TEXT("POST"), Self->MtUrl(TEXT("/auth/prelogin")), Pre, FSmvRestDone::CreateLambda(
			[Weak, Email, Password, Done, Mt](int32 Status, const TSharedPtr<FJsonObject>& O, const FString& C, const FString& M)
		{
			USimumavClient* Self2 = Weak.Get();
			if (!Self2) return;
			const TSharedPtr<FJsonObject> Kdf = SmvJson::Obj(O, TEXT("kdf"));
			if (Status != 200 || !Kdf.IsValid()) { Done(false, C.IsEmpty() ? TEXT("account_service") : C, M); return; }
			bool bOk = false;
			const FTCHARToUTF8 Pw(*Password);
			const FTCHARToUTF8 Salt(*SmvJson::Str(Kdf, TEXT("salt")));
			const std::string Hash = SmvKdf::ComputeAuthHash(std::string((const char*)Pw.Get(), Pw.Length()), std::string((const char*)Salt.Get(), Salt.Length()),
				(uint32_t)SmvJson::Num(Kdf, TEXT("iter"), 0), bOk);
			if (!bOk) { Done(false, TEXT("account_service"), TEXT("Hesap servisi geçersiz KDF bildirdi.")); return; }
			TSharedRef<FJsonObject> L = MakeShared<FJsonObject>();
			L->SetStringField(TEXT("email"), Email);
			L->SetStringField(TEXT("auth_hash"), UTF8_TO_TCHAR(Hash.c_str()));
			L->SetStringField(TEXT("device"), DeviceName());
			L->SetStringField(TEXT("app_version"), FString::Printf(TEXT("steam-%s"), ClientVersion()));
			Self2->Request(TEXT("POST"), Self2->MtUrl(TEXT("/auth/login")), L, FSmvRestDone::CreateLambda(
				[Weak, Done](int32 St, const TSharedPtr<FJsonObject>& R, const FString& C2, const FString& M2)
			{
				USimumavClient* Self3 = Weak.Get();
				if (!Self3) return;
				const FString Token = SmvJson::Str(R, TEXT("token"));
				if (St != 200 || Token.IsEmpty())
				{
					const FString Code = St == 401 ? TEXT("bad_credentials") : (C2.IsEmpty() ? TEXT("account_service") : C2);
					Done(false, Code, M2);
					return;
				}
				Self3->OpenSessionWithToken(Token, Done);
			}), false, Mt);
		}), false, Mt);
	});
}

void USimumavClient::Logout(TFunction<void()> Done)
{
	TSharedRef<FJsonObject> B = MakeShared<FJsonObject>();
	if (!MtToken.IsEmpty()) B->SetStringField(TEXT("token"), MtToken);
	TWeakObjectPtr<USimumavClient> Weak(this);
	Delete(TEXT("/api/session"), FSmvRestDone::CreateLambda([Weak, Done](int32, const TSharedPtr<FJsonObject>&, const FString&, const FString&)
	{
		if (USimumavClient* Self = Weak.Get()) Self->ClearAuth(true);
		if (Done) Done();
	}), B);
}
