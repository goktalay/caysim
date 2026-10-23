// Gizli değer deposu — simumav.com hesap belirteci (mastertool Bearer) ve oturum çerezi (smsid).
// Windows: Kimlik Bilgisi Yöneticisi (genel kimlik bilgisi "Simumav/<anahtar>", kullanıcı oturumuna bağlı, DPAPI ile şifreli).
// Diğer / yedek: Saved/Simumav/secrets.bin — AES-256 (anahtar: makine + kullanıcı kimliğinden SHA-256), yalnız bu makinede çözülür.
// Değerler hiçbir zaman günlüğe yazılmaz.
#pragma once

#include "CoreMinimal.h"

class SIMUMAV_API FSmvSecrets
{
public:
	static bool Save(const FString& Key, const FString& Value);
	static bool Load(const FString& Key, FString& OutValue);
	static void Delete(const FString& Key);

	static constexpr const TCHAR* KeyMtToken = TEXT("mt_token");
	static constexpr const TCHAR* KeySessionCookie = TEXT("smsid");
	static constexpr const TCHAR* KeyEmail = TEXT("email");

private:
	static bool FileSave(const FString& Key, const FString& Value);
	static bool FileLoad(const FString& Key, FString& OutValue);
	static void FileDelete(const FString& Key);
};
