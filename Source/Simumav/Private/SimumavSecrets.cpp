#include "SimumavSecrets.h"
#include "SimumavModule.h"
#include "SmvKdf.h"
#include "Misc/AES.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#if PLATFORM_WINDOWS && SIMUMAV_WITH_WINCRED
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <wincred.h>
#include "Windows/HideWindowsPlatformTypes.h"
#define SMV_WINCRED 1
#else
#define SMV_WINCRED 0
#endif

namespace
{
	FString SmvCredTarget(const FString& Key) { return FString::Printf(TEXT("Simumav/%s"), *Key); }

	FString SmvSecretFile() { return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Simumav"), TEXT("secrets.bin")); }

	FAES::FAESKey SmvMachineKey()
	{
		// Makineye + kullanıcıya bağlı anahtar (dosya başka makinede/kullanıcıda çözülemez). Gerçek kasa Kimlik Bilgisi Yöneticisi'dir.
		const FString Seed = FString::Printf(TEXT("Simumav/v1|%s|%s|%s"), *FPlatformMisc::GetLoginId(), FPlatformProcess::UserName(false), FPlatformProcess::ComputerName());
		const FTCHARToUTF8 Utf8(*Seed);
		const SmvKdf::FBytes H = SmvKdf::Sha256((const uint8_t*)Utf8.Get(), (size_t)Utf8.Length());
		FAES::FAESKey Key;
		FMemory::Memcpy(Key.Key, H.data(), FMath::Min<int32>(FAES::FAESKey::KeySize, (int32)H.size()));
		return Key;
	}

	bool SmvReadAll(TMap<FString, FString>& Out)
	{
		Out.Reset();
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *SmvSecretFile()) || Bytes.Num() < (int32)FAES::AESBlockSize || Bytes.Num() % FAES::AESBlockSize != 0) return false;
		FAES::DecryptData(Bytes.GetData(), Bytes.Num(), SmvMachineKey());
		uint32 Len = 0;
		FMemory::Memcpy(&Len, Bytes.GetData(), 4);
		if (Len + 4 > (uint32)Bytes.Num()) return false;
		// dönüşüm nesnesi uzunluklu ve sıfırla bitmez → FString(Uzunluk, işaretçi) ile açıkça kurulur
		const FUTF8ToTCHAR JsonConv((const ANSICHAR*)Bytes.GetData() + 4, (int32)Len);
		const FString Json(JsonConv.Length(), JsonConv.Get());
		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(R, O) || !O.IsValid()) return false;
		for (const auto& Kv : O->Values) Out.Add(FString(*Kv.Key), Kv.Value->AsString());
		return true;
	}

	bool SmvWriteAll(const TMap<FString, FString>& In)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		for (const TPair<FString, FString>& Kv : In) O->SetStringField(FString(*Kv.Key), Kv.Value);
		FString Json;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Json);
		FJsonSerializer::Serialize(O, W);
		const FTCHARToUTF8 Utf8(*Json);
		const uint32 Len = (uint32)Utf8.Length();
		TArray<uint8> Bytes;
		Bytes.SetNumZeroed(Align((int32)Len + 4, (int32)FAES::AESBlockSize));
		FMemory::Memcpy(Bytes.GetData(), &Len, 4);
		FMemory::Memcpy(Bytes.GetData() + 4, Utf8.Get(), Len);
		FAES::EncryptData(Bytes.GetData(), Bytes.Num(), SmvMachineKey());
		return FFileHelper::SaveArrayToFile(Bytes, *SmvSecretFile());
	}
}

bool FSmvSecrets::Save(const FString& Key, const FString& Value)
{
#if SMV_WINCRED
	const FTCHARToUTF8 Utf8(*Value);
	const FString Target = SmvCredTarget(Key);
	CREDENTIALW Cred;
	FMemory::Memzero(&Cred, sizeof(Cred));
	Cred.Type = CRED_TYPE_GENERIC;
	Cred.TargetName = (LPWSTR)*Target;
	Cred.CredentialBlobSize = (DWORD)Utf8.Length();
	Cred.CredentialBlob = (LPBYTE)Utf8.Get();
	Cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
	Cred.UserName = (LPWSTR)TEXT("Simumav");
	if (CredWriteW(&Cred, 0)) return true;
	UE_LOG(LogSimumav, Warning, TEXT("Kimlik Bilgisi Yöneticisi'ne yazılamadı (%u); şifreli dosyaya düşülüyor"), (uint32)GetLastError());
#endif
	return FileSave(Key, Value);
}

bool FSmvSecrets::Load(const FString& Key, FString& OutValue)
{
#if SMV_WINCRED
	PCREDENTIALW P = nullptr;
	const FString Target = SmvCredTarget(Key);
	if (CredReadW(*Target, CRED_TYPE_GENERIC, 0, &P) && P)
	{
		const FUTF8ToTCHAR BlobConv((const ANSICHAR*)P->CredentialBlob, (int32)P->CredentialBlobSize);
		OutValue = FString(BlobConv.Length(), BlobConv.Get());
		CredFree(P);
		return true;
	}
#endif
	return FileLoad(Key, OutValue);
}

void FSmvSecrets::Delete(const FString& Key)
{
#if SMV_WINCRED
	const FString Target = SmvCredTarget(Key);
	CredDeleteW(*Target, CRED_TYPE_GENERIC, 0);
#endif
	FileDelete(Key);
}

bool FSmvSecrets::FileSave(const FString& Key, const FString& Value)
{
	TMap<FString, FString> All;
	SmvReadAll(All);
	All.Add(Key, Value);
	return SmvWriteAll(All);
}

bool FSmvSecrets::FileLoad(const FString& Key, FString& OutValue)
{
	TMap<FString, FString> All;
	if (!SmvReadAll(All)) return false;
	if (const FString* V = All.Find(Key)) { OutValue = *V; return true; }
	return false;
}

void FSmvSecrets::FileDelete(const FString& Key)
{
	TMap<FString, FString> All;
	if (!SmvReadAll(All) || !All.Remove(Key)) return;
	SmvWriteAll(All);
}
