// simumav.com API istemcisi (SIMUMAV oyun ön yüzünün kullandığı aynı uçlar; app/simumav/routers/* — docs/simumav-api-esleme.md).
//
// KİMLİK (sunucu sözleşmesi, değiştirilmeden):
//   1) GET /api/meta → mt_url (mastertool hesap servisi), mt_app ('sv')
//   2) POST {mt_url}/api/sv/auth/prelogin {email} → kdf{alg, iter, salt}
//   3) auth_hash = b64(PBKDF2(PBKDF2(şifre, salt, iter), şifre, 1))   — şifre ağdan gitmez (SmvKdf)
//   4) POST {mt_url}/api/sv/auth/login {email, auth_hash, device, app_version} → {token}  (mastertool Bearer)
//   5) POST /api/session {token} → HttpOnly `smsid` çerezi. Sonraki tüm REST/WS istekleri `Cookie: smsid=…` ile.
//   Yazma isteklerinde sunucu CSRF için Origin denetler → yerel istemci `Origin: https://simumav.com` gönderir.
//   Belirteç + çerez FSmvSecrets'ta (Windows Kimlik Bilgisi Yöneticisi). Çerez 30 gün; düşerse belirteçle yeniden açılır.
//
// Giriş ZORUNLU DEĞİL: misafir kipte bu nesne yalnız oturumsuz uçları (meta, leaderboard) kullanır.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Interfaces/IHttpRequest.h"
#include "SimumavTypes.h"
#include "SimumavClient.generated.h"

class FSmvSocket;

/** REST sonucu: Status (0 = ağ), Json (dizi yanıtlar {"items":[…]} olarak sarılır), hata kodu/metni (`detail.error` / `detail.message`). */
DECLARE_DELEGATE_FourParams(FSmvRestDone, int32 /*Status*/, const TSharedPtr<FJsonObject>& /*Json*/, const FString& /*ErrCode*/, const FString& /*ErrMsg*/);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSmvAuthChanged, bool, bLoggedIn);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSmvClientError, const FString&, Code, const FString&, Message);

UCLASS(Config = Game)
class SIMUMAV_API USimumavClient : public UObject
{
	GENERATED_BODY()

public:
	/** Sürüm — `User-Agent: Simumav/<sürüm> (Steam; UE)` ve `app_version` */
	static const TCHAR* ClientVersion() { return TEXT("0.2.0"); }

	/** API kökü (Config/DefaultGame.ini [/Script/Simumav.SimumavClient] ApiBase; geliştirmede yerel sunucu verilebilir). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Simumav") FString ApiBase = TEXT("https://simumav.com");
	/** Yazma isteklerinde gönderilecek Origin (sunucunun izinli kökenlerinden biri). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Simumav") FString OriginHeader = TEXT("https://simumav.com");
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Simumav") float HttpTimeoutS = 20.f;

	// ---- kimlik ----
	/** Kayıtlı çerez/belirteçle sessiz giriş denemesi (açılışta). Done(bOk). */
	void ResumeSession(TFunction<void(bool)> Done);
	/** E-posta + şifre ile giriş (mastertool). Done(bOk, hata kodu, mesaj). Kodlar: unverified | bad_credentials | rate_limited | account_service | network */
	void Login(const FString& Email, const FString& Password, TFunction<void(bool, const FString&, const FString&)> Done);
	void Logout(TFunction<void()> Done = nullptr);
	UFUNCTION(BlueprintPure, Category = "Simumav") bool IsLoggedIn() const { return !SessionCookie.IsEmpty() && User.Id != 0; }
	UFUNCTION(BlueprintPure, Category = "Simumav") FSmvUser GetUser() const { return User; }
	/** Tarayıcıda kayıt / şifre sıfırlama sayfası (oyun içinde şifre belirlenmez). */
	FString RegisterUrl() const { return ApiBase + TEXT("/?giris=1"); }   // site giriş penceresi (kayıt / şifre sıfırlama sekmeleri orada)

	// ---- REST ----
	void Get(const FString& Path, FSmvRestDone Done);
	void Post(const FString& Path, const TSharedRef<FJsonObject>& Body, FSmvRestDone Done);
	void Put(const FString& Path, const TSharedRef<FJsonObject>& Body, FSmvRestDone Done);
	void Delete(const FString& Path, FSmvRestDone Done, const TSharedPtr<FJsonObject>& Body = nullptr);
	void Request(const FString& Verb, const FString& UrlOrPath, const TSharedPtr<FJsonObject>& Body, FSmvRestDone Done, bool bAuthHeaders = true,
		const TMap<FString, FString>& Extra = TMap<FString, FString>());
	/** Ham bayt (GLB model, önizleme PNG) — oturumsuz statik dosyalar da. */
	void GetBytes(const FString& UrlOrPath, TFunction<void(bool, const TArray<uint8>&)> Done);

	/** Meta (`/api/meta`): mt_url, sınırlar, tipler… (oturumsuz da) */
	void FetchMeta(TFunction<void(bool)> Done);
	TSharedPtr<FJsonObject> GetMeta() const { return Meta; }

	// ---- WS ----
	FString WsUrl(const FString& Path) const;
	TMap<FString, FString> WsHeaders() const;
	/** Yeni WS (çağıran saklar). */
	TSharedRef<FSmvSocket> MakeSocket() const;

	FString ResolveUrl(const FString& UrlOrPath) const;

	UPROPERTY(BlueprintAssignable, Category = "Simumav") FSmvAuthChanged OnAuthChanged;
	UPROPERTY(BlueprintAssignable, Category = "Simumav") FSmvClientError OnError;

private:
	FString SessionCookie;
	FString MtToken;
	FSmvUser User;
	TSharedPtr<FJsonObject> Meta;
	TSharedPtr<FJsonObject> SessionInfo;

	void OpenSessionWithToken(const FString& Token, TFunction<void(bool, const FString&, const FString&)> Done);
	void ApplySession(const TSharedPtr<FJsonObject>& O);
	void ClearAuth(bool bForgetSecrets);
	static void ParseError(int32 Status, const FString& Body, FString& Code, FString& Msg);
	static FString DeviceName();
	FString MtUrl(const FString& Path) const;
};
