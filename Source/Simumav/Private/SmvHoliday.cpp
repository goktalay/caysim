#include "SmvHoliday.h"
#include "SmvUiPrefs.h"
#include "SmvI18n.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	struct FYear { TSharedPtr<FJsonObject> Root; bool bTried = false; };
	TMap<int32, FYear>& Years() { static TMap<int32, FYear> Y; return Y; }

	TSharedPtr<FJsonObject> LoadYear(int32 Y)
	{
		FYear& F = Years().FindOrAdd(Y);
		if (F.bTried) return F.Root;
		F.bTried = true;
		FString Text;
		if (FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir() / FString::Printf(TEXT("Simumav/Holidays/%d.json"), Y))))
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), F.Root);
		return F.Root;
	}
}

namespace SmvHoliday
{
	const TArray<FString>& Themes()
	{
		static const TArray<FString> T = { TEXT("kurban"), TEXT("ramazan"), TEXT("hicri"), TEXT("noel"), TEXT("paskalya"), TEXT("hristiyan"), TEXT("yilbasi"), TEXT("cin_yeni"),
			TEXT("diwali"), TEXT("holi"), TEXT("hindu"), TEXT("yahudi"), TEXT("cocuk_genclik"), TEXT("kadin_anne"), TEXT("sevgi"), TEXT("cadilar"), TEXT("bagimsizlik"),
			TEXT("emek"), TEXT("bahar"), TEXT("genel") };
		return T;
	}

	FString& Preview() { static FString P; return P; }

	FString Country()
	{
		const FString& S = FSmvUiPrefs::Get().HolidayCountry;
		if (S.Len() == 2) return S.ToUpper();
		// Windows bölge ayarı (ör. "tr-TR")
		const FString Loc = FPlatformMisc::GetDefaultLocale();
		FString L, R;
		if (Loc.Split(TEXT("-"), &L, &R) || Loc.Split(TEXT("_"), &L, &R))
		{
			R = R.Left(2).ToUpper();
			if (R.Len() == 2 && FChar::IsAlpha(R[0]) && FChar::IsAlpha(R[1])) return R;
		}
		const FString Lang = FSmvI18n::Language();
		if (Lang == TEXT("tr")) return TEXT("TR");
		if (Lang == TEXT("de")) return TEXT("DE");
		if (Lang == TEXT("fr")) return TEXT("FR");
		if (Lang == TEXT("es")) return TEXT("ES");
		if (Lang == TEXT("ru")) return TEXT("RU");
		if (Lang == TEXT("ar")) return TEXT("SA");
		return TEXT("US");
	}

	TArray<FSmvHolidayItem> Today(const FString& C)
	{
		TArray<FSmvHolidayItem> Out;
		const FDateTime N = FDateTime::Now();
		const TSharedPtr<FJsonObject> Y = LoadYear(N.GetYear());
		if (!Y.IsValid()) return Out;
		const FString Day = N.ToString(TEXT("%Y-%m-%d"));
		const TSharedPtr<FJsonObject>* Cs = nullptr;
		if (!Y->TryGetObjectField(TEXT("c"), Cs) || !Cs) return Out;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!(*Cs)->TryGetArrayField(C, Arr) || !Arr) return Out;
		FString Local;
		const TSharedPtr<FJsonObject>* Ll = nullptr;
		if (Y->TryGetObjectField(TEXT("ll"), Ll) && Ll) (*Ll)->TryGetStringField(C, Local);
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TArray<TSharedPtr<FJsonValue>>& E = V->AsArray();
			if (E.Num() < 5 || E[0]->AsString() != Day) continue;
			FSmvHolidayItem I;
			I.Theme = E[1]->AsString();
			I.Name = (Local == FSmvI18n::Language() && !E[3]->AsString().IsEmpty()) ? E[3]->AsString() : E[2]->AsString();
			I.Country = C;
			I.bHalf = E[4]->AsNumber() > 0;
			if (I.bHalf && N.GetHour() < 13) continue;   // yarım gün: öğleden sonra
			Out.Add(I);
		}
		const TArray<FString>& T = Themes();
		Out.Sort([&T](const FSmvHolidayItem& A, const FSmvHolidayItem& B) { return T.IndexOfByKey(A.Theme) < T.IndexOfByKey(B.Theme); });
		return Out;
	}

	FString ActiveTheme(FSmvHolidayItem* OutItem)
	{
		if (!Preview().IsEmpty()) { if (OutItem) { OutItem->Theme = Preview(); OutItem->Name = FSmvI18n::T(TEXT("holiday.name_") + Preview()).ToString(); OutItem->Country = Country(); } return Preview(); }
		if (FSmvUiPrefs::Get().Season == TEXT("off")) return FString();
		const TArray<FSmvHolidayItem> L = Today(Country());
		if (L.Num() == 0) return FString();
		if (OutItem) *OutItem = L[0];
		return L[0].Theme;
	}

	FLinearColor ThemeColor(const FString& T)
	{
		auto H = [](uint32 X) { return FLinearColor::FromSRGBColor(FColor((X >> 16) & 255, (X >> 8) & 255, X & 255, 230)); };
		if (T == TEXT("kurban") || T == TEXT("ramazan") || T == TEXT("hicri")) return H(0x10b981);
		if (T == TEXT("noel")) return H(0xdc2626);
		if (T == TEXT("paskalya")) return H(0xf9a8d4);
		if (T == TEXT("hristiyan")) return H(0x93c5fd);
		if (T == TEXT("yilbasi")) return H(0xfbbf24);
		if (T == TEXT("cin_yeni")) return H(0xef4444);
		if (T == TEXT("diwali")) return H(0xf59e0b);
		if (T == TEXT("holi")) return H(0xec4899);
		if (T == TEXT("hindu")) return H(0xfb923c);
		if (T == TEXT("yahudi")) return H(0x3b82f6);
		if (T == TEXT("cocuk_genclik")) return H(0x22d3ee);
		if (T == TEXT("kadin_anne")) return H(0xc084fc);
		if (T == TEXT("sevgi")) return H(0xf43f5e);
		if (T == TEXT("cadilar")) return H(0xf97316);
		if (T == TEXT("bagimsizlik")) return H(0xe30a17);
		if (T == TEXT("emek")) return H(0xef4444);
		if (T == TEXT("bahar")) return H(0x84cc16);
		if (T == TEXT("genel")) return H(0x38bdf8);
		return FLinearColor::Transparent;
	}

	FLinearColor ThemeColor2(const FString& T)
	{
		if (T == TEXT("kurban") || T == TEXT("ramazan") || T == TEXT("hicri")) return FLinearColor(1.f, 0.85f, 0.35f);
		if (T == TEXT("noel")) return FLinearColor(0.1f, 0.55f, 0.25f);
		if (T == TEXT("bagimsizlik")) return FLinearColor::White;
		if (T == TEXT("cadilar")) return FLinearColor(0.35f, 0.1f, 0.5f);
		if (T == TEXT("yilbasi")) return FLinearColor(0.75f, 0.85f, 1.f);
		return FLinearColor(1.f, 1.f, 1.f);
	}

	TArray<FString> Countries()
	{
		TArray<FString> Out;
		const TSharedPtr<FJsonObject> Y = LoadYear(FDateTime::Now().GetYear());
		const TSharedPtr<FJsonObject>* Cs = nullptr;
		if (Y.IsValid() && Y->TryGetObjectField(TEXT("c"), Cs) && Cs) (*Cs)->Values.GetKeys(Out);
		Out.Sort();
		return Out;
	}
}
