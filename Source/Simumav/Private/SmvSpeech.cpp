#include "SmvSpeech.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
#include <sapi.h>
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"

namespace
{
	ISpVoice* GSmvVoice = nullptr;
	bool GSmvVoiceTried = false;

	ISpVoice* Voice()
	{
		if (GSmvVoice || GSmvVoiceTried) return GSmvVoice;
		GSmvVoiceTried = true;
		CLSID Cls;
		if (FAILED(CLSIDFromString(L"{96749377-3391-11D2-9EE3-00C04F797396}", &Cls))) return nullptr;   // SpVoice
		void* P = nullptr;
		if (SUCCEEDED(CoCreateInstance(Cls, nullptr, CLSCTX_ALL, __uuidof(ISpVoice), &P))) GSmvVoice = (ISpVoice*)P;
		return GSmvVoice;
	}

	const TCHAR* LangId(const FString& L)
	{
		if (L == TEXT("tr")) return TEXT("41F");
		if (L == TEXT("de")) return TEXT("407");
		if (L == TEXT("fr")) return TEXT("40C");
		if (L == TEXT("es")) return TEXT("C0A");
		if (L == TEXT("ru")) return TEXT("419");
		if (L == TEXT("ar")) return TEXT("401");
		return TEXT("409");
	}
}

namespace SmvSpeech
{
	bool IsAvailable() { return Voice() != nullptr; }

	void Say(const FString& Text, const FString& Lang, float Volume, int32 DelayMs)
	{
		ISpVoice* V = Voice();
		if (!V || Text.IsEmpty()) return;
		FString Esc = Text.Replace(TEXT("&"), TEXT("&amp;")).Replace(TEXT("<"), TEXT("&lt;")).Replace(TEXT(">"), TEXT("&gt;"));
		const FString Xml = FString::Printf(TEXT("<lang langid='%s'>%s%s</lang>"), LangId(Lang), DelayMs > 0 ? *FString::Printf(TEXT("<silence msec='%d'/>"), DelayMs) : TEXT(""), *Esc);
		V->SetVolume((USHORT)FMath::Clamp(FMath::RoundToInt(Volume * 100.f), 0, 100));
		V->Speak(*Xml, SPF_ASYNC | SPF_PURGEBEFORESPEAK | SPF_IS_XML, nullptr);
	}

	void Stop()
	{
		if (GSmvVoice) GSmvVoice->Speak(nullptr, SPF_ASYNC | SPF_PURGEBEFORESPEAK, nullptr);
	}
}
#else
namespace SmvSpeech
{
	bool IsAvailable() { return false; }
	void Say(const FString&, const FString&, float, int32) {}
	void Stop() {}
}
#endif
