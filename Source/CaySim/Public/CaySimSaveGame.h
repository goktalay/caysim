// Yerel ayarlar — istemci kodu, HUD tercihleri (SaveGame yuvası "CaySimAyarlar")
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "CaySimSaveGame.generated.h"

UCLASS()
class CAYSIM_API UCaySimSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/** HEPGO istemci kodu — makineye bağlı anahtarla karartılmış (Base64); bkz. Obfuscate/Deobfuscate. */
	UPROPERTY() FString ClientCodeEnc;
	UPROPERTY() uint8 HudColor = 0;
	UPROPERTY() bool bHudOn = true;
	UPROPERTY() float InterpolationDelayS = 0.08f;
	UPROPERTY() FString CesiumIonToken;

	static const TCHAR* SlotName() { return TEXT("CaySimAyarlar"); }

	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Ayarlar") static UCaySimSaveGame* LoadOrCreate();
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Ayarlar") static bool Save(UCaySimSaveGame* Data);
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Ayarlar") static void SaveClientCode(const FString& Code);
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Ayarlar") static FString LoadClientCode();

private:
	/** Basit koruma: makine kimliği (FPlatformMisc::GetLoginId) türevli XOR + Base64. Gerçek sır yönetimi değildir;
	 *  amaç kodun kayıt dosyasında düz metin görünmemesi ve dosyanın başka makinede işe yaramaması. */
	static FString Obfuscate(const FString& Plain);
	static FString Deobfuscate(const FString& Enc);
};
