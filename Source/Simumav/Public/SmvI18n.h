// Arayüz dilleri (7): tr, en, de, fr, es, ru, ar — Content/Simumav/Lang/<dil>.json (anahtar → metin; UFS ile paketlenir).
// Eksik anahtar: önce İngilizce, sonra Türkçe, en sonda anahtarın kendisi. `{ad}` yer tutucuları Format ile doldurulur.
#pragma once

#include "CoreMinimal.h"

class SIMUMAV_API FSmvI18n
{
public:
	static void SetLanguage(const FString& Lang);
	static const FString& Language();
	/** İşletim sistemi dilinden (yalnız desteklenenler; yoksa en). */
	static FString SystemLanguage();
	static const TArray<FString>& Languages();
	static FText T(const FString& Key);
	static FText Format(const FString& Key, const TMap<FString, FString>& Args);
	static bool IsRtl() { return Language() == TEXT("ar"); }
private:
	static TMap<FString, FString>& Table(const FString& Lang);
};

/** Kısa yazım: SMV_T("menu.fly") */
#define SMV_T(Key) FSmvI18n::T(TEXT(Key))
