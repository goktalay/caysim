#include "SmvI18n.h"
#include "SimumavModule.h"
#include "SimumavTypes.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"

namespace
{
	FString GSmvLang = TEXT("tr");
	TMap<FString, TMap<FString, FString>> GSmvTables;
}

const TArray<FString>& FSmvI18n::Languages()
{
	static const TArray<FString> L = { TEXT("tr"), TEXT("en"), TEXT("de"), TEXT("fr"), TEXT("es"), TEXT("ru"), TEXT("ar") };
	return L;
}

void FSmvI18n::SetLanguage(const FString& Lang)
{
	GSmvLang = Languages().Contains(Lang) ? Lang : SystemLanguage();
}

const FString& FSmvI18n::Language() { return GSmvLang; }

FString FSmvI18n::SystemLanguage()
{
	const FString Code = FInternationalization::Get().GetCurrentCulture()->GetTwoLetterISOLanguageName();
	return Languages().Contains(Code) ? Code : FString(TEXT("en"));
}

TMap<FString, FString>& FSmvI18n::Table(const FString& Lang)
{
	// yerel ad "T" olmamalı: FSmvI18n::T üye işlevini gölgeler
	if (TMap<FString, FString>* Found = GSmvTables.Find(Lang)) return *Found;
	TMap<FString, FString>& Tbl = GSmvTables.Add(Lang);
	FString Text;
	const FString File = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Simumav"), TEXT("Lang"), Lang + TEXT(".json"));
	if (FFileHelper::LoadFileToString(Text, *File))
	{
		if (const TSharedPtr<FJsonObject> O = SmvJson::Parse(Text))
		{
			for (const auto& Kv : O->Values) if (Kv.Value.IsValid()) Tbl.Add(FString(*Kv.Key), Kv.Value->AsString());
		}
	}
	else
	{
		UE_LOG(LogSimumav, Warning, TEXT("Dil dosyası yok: %s"), *File);
	}
	return Tbl;
}

FText FSmvI18n::T(const FString& Key)
{
	for (const FString& L : { GSmvLang, FString(TEXT("en")), FString(TEXT("tr")) })
	{
		if (const FString* S = Table(L).Find(Key)) return FText::FromString(*S);
	}
	return FText::FromString(Key);
}

FText FSmvI18n::Format(const FString& Key, const TMap<FString, FString>& Args)
{
	FString S = T(Key).ToString();
	for (const TPair<FString, FString>& A : Args) S.ReplaceInline(*FString::Printf(TEXT("{%s}"), *A.Key), *A.Value);
	return FText::FromString(S);
}
