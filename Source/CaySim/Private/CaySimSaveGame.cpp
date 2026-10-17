#include "CaySimSaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Base64.h"
#include "Misc/SecureHash.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"

UCaySimSaveGame* UCaySimSaveGame::LoadOrCreate()
{
	if (UGameplayStatics::DoesSaveGameExist(SlotName(), 0))
	{
		if (UCaySimSaveGame* S = Cast<UCaySimSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName(), 0))) return S;
	}
	return Cast<UCaySimSaveGame>(UGameplayStatics::CreateSaveGameObject(UCaySimSaveGame::StaticClass()));
}

bool UCaySimSaveGame::Save(UCaySimSaveGame* Data)
{
	return Data && UGameplayStatics::SaveGameToSlot(Data, SlotName(), 0);
}

void UCaySimSaveGame::SaveClientCode(const FString& Code)
{
	UCaySimSaveGame* S = LoadOrCreate();
	if (!S) return;
	S->ClientCodeEnc = Code.IsEmpty() ? FString() : Obfuscate(Code);
	Save(S);
}

FString UCaySimSaveGame::LoadClientCode()
{
	UCaySimSaveGame* S = LoadOrCreate();
	return (S && !S->ClientCodeEnc.IsEmpty()) ? Deobfuscate(S->ClientCodeEnc) : FString();
}

static TArray<uint8> MachineKey()
{
	const FString Seed = FPlatformMisc::GetLoginId() + TEXT("|CAYSIM|") + FPlatformProcess::ComputerName();
	const FTCHARToUTF8 Utf8(*Seed);
	FSHA256Signature Out;
	FPlatformMisc::GetSHA256Signature(Utf8.Get(), Utf8.Length(), Out);
	TArray<uint8> K;
	K.Append(Out.Signature, 32);
	return K;
}

FString UCaySimSaveGame::Obfuscate(const FString& Plain)
{
	const TArray<uint8> K = MachineKey();
	const FTCHARToUTF8 Utf8(*Plain);
	TArray<uint8> Bytes;
	Bytes.Append((const uint8*)Utf8.Get(), Utf8.Length());
	for (int32 i = 0; i < Bytes.Num(); ++i) Bytes[i] ^= K[i % K.Num()];
	return FBase64::Encode(Bytes);
}

FString UCaySimSaveGame::Deobfuscate(const FString& Enc)
{
	TArray<uint8> Bytes;
	if (!FBase64::Decode(Enc, Bytes)) return FString();
	const TArray<uint8> K = MachineKey();
	for (int32 i = 0; i < Bytes.Num(); ++i) Bytes[i] ^= K[i % K.Num()];
	const FUTF8ToTCHAR Conv((const ANSICHAR*)Bytes.GetData(), Bytes.Num());
	return FString(Conv.Length(), Conv.Get());
}
