// GPWS sesli uyarı (web speechSynthesis eşi): Windows'ta SAPI (ISpVoice, eşzamansız; yeni uyarı öncekini keser), diğer platformlarda sessiz.
// Dil: havacılık kalıbı İngilizce (en-US), arayüz dilinde ise SSML <lang> ile o dildeki sistem sesi seçilir (yoksa varsayılan ses).
#pragma once

#include "CoreMinimal.h"

namespace SmvSpeech
{
	/** Metni seslendir (Lang: tr/en/de/fr/es/ru/ar; Volume 0..1; DelayMs: tondan sonra başlaması için sessizlik) */
	SIMUMAV_API void Say(const FString& Text, const FString& Lang, float Volume, int32 DelayMs = 0);
	/** Konuşmayı kes (uyarı onaylandı) */
	SIMUMAV_API void Stop();
	SIMUMAV_API bool IsAvailable();
}
