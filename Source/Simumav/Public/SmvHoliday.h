// Bayram / tatil temaları (web holiday_fx.js eşi): 247 ülke, 2026–2030 (Content/Simumav/Holidays; python-holidays kaynaklı, Tools/holiday/tatil_aktar.py),
// 20 tema (kurban, ramazan, hicri, noel, paskalya, hristiyan, yilbasi, cin_yeni, diwali, holi, hindu, yahudi, cocuk_genclik, kadin_anne, sevgi,
// cadilar, bagimsizlik, emek, bahar, genel). Ülke: Ayarlar'daki seçim, yoksa Windows bölge ayarı (ör. tr-TR → TR), o da yoksa arayüz dili.
// Ana menüde kutlama şeridi + tema rengi + parçacıklar; uçuş sahnesinde çarpışmasız 3B süsler (SmvHolidayDecor).
#pragma once

#include "CoreMinimal.h"

struct SIMUMAV_API FSmvHolidayItem
{
	FString Theme, Name, Country;
	bool bHalf = false;
};

namespace SmvHoliday
{
	SIMUMAV_API const TArray<FString>& Themes();
	/** Ayar (FSmvUiPrefs::HolidayCountry: "" otomatik) → ülke kodu (2 harf) */
	SIMUMAV_API FString Country();
	/** Bugünün bayramları (ülke) — önceliğe göre sıralı (tema listesi sırası) */
	SIMUMAV_API TArray<FSmvHolidayItem> Today(const FString& Country);
	/** Etkin tema (önizleme > bugün; Ayarlar → Kapalı ise boş) */
	SIMUMAV_API FString ActiveTheme(FSmvHolidayItem* OutItem = nullptr);
	SIMUMAV_API FLinearColor ThemeColor(const FString& Theme);
	SIMUMAV_API FLinearColor ThemeColor2(const FString& Theme);
	/** Önizleme (Ayarlar): boş = yok */
	SIMUMAV_API FString& Preview();
	SIMUMAV_API TArray<FString> Countries();
}
