// Oturum sihirbazı — UI akışı: istemci kodu → şirket logosu → kip → merkez/log → tip → model → dünya → kalkış → başlat
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CaySimTypes.h"
#include "LaunchRig.h"
#include "CesiumWorldSetup.h"
#include "SessionWizard.generated.h"

class UHepgoClient;
class UTexture2D;

/** Sihirbaz adımları (UMG ekranları bu enum'a göre geçiş yapar). */
UENUM(BlueprintType)
enum class ECaySimWizardStep : uint8
{
	ClientCode   UMETA(DisplayName = "İstemci kodu"),
	Connecting   UMETA(DisplayName = "Bağlanıyor"),
	Welcome      UMETA(DisplayName = "Şirket / hoş geldiniz"),
	Mode         UMETA(DisplayName = "Kip (canlı / kayıt)"),
	Center       UMETA(DisplayName = "Merkez"),
	Log          UMETA(DisplayName = "Uçuş logu"),
	DroneType    UMETA(DisplayName = "Dron tipi"),
	Model        UMETA(DisplayName = "Model"),
	World        UMETA(DisplayName = "Dünya"),
	Launch       UMETA(DisplayName = "Kalkış yöntemi"),
	Summary      UMETA(DisplayName = "Özet / Başlat"),
	Starting     UMETA(DisplayName = "Oturum açılıyor"),
	Running      UMETA(DisplayName = "Uçuş")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWizardStepDelegate, ECaySimWizardStep, Step);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWizardMessageDelegate, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWizardLogoDelegate, UTexture2D*, Logo);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWizardThumbDelegate, const FString&, ModelId);

/**
 * Sihirbaz durum makinesi. UMG (Blueprint) ekranları yalnız gösterir; veri ve geçişler burada.
 * Girilen istemci kodu UCaySimSaveGame'e kaydedilir (bir sonraki açılışta doğrudan Welcome).
 */
UCLASS(BlueprintType)
class CAYSIM_API UCaySimSessionWizard : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Sihirbaz") void Init(UHepgoClient* Client);

	// ---- adım 1: istemci kodu ----
	/** Kodu doğrular, kaydeder, `/hello` çağırır → Welcome. Hata OnError + ClientCode adımında kalır. */
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Sihirbaz") void SubmitClientCode(const FString& Text);
	/** Kayıtlı kod varsa onunla bağlanır; yoksa false. */
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Sihirbaz") bool TrySavedClientCode();
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Sihirbaz") void ForgetClientCode();

	// ---- adım 2/3 ----
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Sihirbaz") void ChooseMode(ECaySimSessionMode InMode);
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Sihirbaz") void ChooseCenter(int32 InCenterId);
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Sihirbaz") void SearchLogs(const FString& Query);
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Sihirbaz") void ChooseLog(int32 InLogId);
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Sihirbaz") void ChooseType(const FString& InTypeKey);
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Sihirbaz") void ChooseModel(const FString& InModelId);
	/** Oturuma özel yönelim düzeltmesi (kütüphane kaydını değiştirmez). */
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Sihirbaz") void SetModelOrientOverride(const FCaySimModelOrient& Orient, bool bEnabled);
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Sihirbaz") void SetWorld(const FCaySimWorld& InWorld, ECaySimWorldSource Source);
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Sihirbaz") void SetLaunch(const FCaySimLaunchConfig& InLaunch);
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Sihirbaz") void GoTo(ECaySimWizardStep InStep);
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Sihirbaz") void Back();
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Sihirbaz") void Next();

	// ---- başlat ----
	/** `POST /sessions` → Starting → (HAZIR) Running. */
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Sihirbaz") void Start();
	/** Var olan açık bir oturuma izleyici olarak katıl. */
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Sihirbaz") void JoinSession(const FString& SessionId);
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Sihirbaz") void EndSession();

	// ---- durum ----
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Sihirbaz") ECaySimWizardStep GetStep() const { return Step; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Sihirbaz") FCaySimHello GetHello() const { return Hello; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Sihirbaz") UTexture2D* GetLogo() const { return Logo; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Sihirbaz") TArray<FCaySimCenter> GetCenters() const { return Centers; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Sihirbaz") TArray<FCaySimDroneTypeInfo> GetTypes() const { return Types; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Sihirbaz") TArray<FCaySimModelInfo> GetModels() const { return Models; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Sihirbaz") TArray<FCaySimLogInfo> GetLogs() const { return Logs; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Sihirbaz") TArray<FCaySimSession> GetOpenSessions() const { return OpenSessions; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Sihirbaz") ECaySimSessionMode GetMode() const { return Mode; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Sihirbaz") int32 GetCenterId() const { return CenterId; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Sihirbaz") int32 GetLogId() const { return LogId; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Sihirbaz") FString GetTypeKey() const { return TypeKey; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Sihirbaz") FString GetModelId() const { return ModelId; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Sihirbaz") FCaySimWorld GetWorldSettings() const { return World; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Sihirbaz") ECaySimWorldSource GetWorldSource() const { return WorldSource; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Sihirbaz") FCaySimLaunchConfig GetLaunch() const { return Launch; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Sihirbaz") FCaySimSession GetSession() const { return Session; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Sihirbaz") UHepgoClient* GetClient() const { return Hepgo; }
	/** Model küçük resmi (indirildiyse). */
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Sihirbaz") UTexture2D* GetModelThumb(const FString& InModelId) const;
	/** Seçilen tipe uygun kalkış yöntemleri (multirotor/heli → dikey). */
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Sihirbaz") TArray<ECaySimLaunchMethod> AvailableLaunchMethods() const;

	UPROPERTY(BlueprintAssignable, Category = "CAYSIM|Sihirbaz") FWizardStepDelegate OnStepChanged;
	UPROPERTY(BlueprintAssignable, Category = "CAYSIM|Sihirbaz") FWizardMessageDelegate OnError;
	UPROPERTY(BlueprintAssignable, Category = "CAYSIM|Sihirbaz") FWizardMessageDelegate OnStatus;
	UPROPERTY(BlueprintAssignable, Category = "CAYSIM|Sihirbaz") FWizardLogoDelegate OnLogoReady;
	UPROPERTY(BlueprintAssignable, Category = "CAYSIM|Sihirbaz") FWizardStepDelegate OnDataChanged; // listeler yenilendi (adım parametresi: hangi liste)
	UPROPERTY(BlueprintAssignable, Category = "CAYSIM|Sihirbaz") FWizardThumbDelegate OnThumbReady;

private:
	UPROPERTY() TObjectPtr<UHepgoClient> Hepgo;
	UPROPERTY() TObjectPtr<UTexture2D> Logo;
	UPROPERTY() TMap<FString, TObjectPtr<UTexture2D>> Thumbs;

	ECaySimWizardStep Step = ECaySimWizardStep::ClientCode;
	FCaySimHello Hello;
	TArray<FCaySimCenter> Centers;
	TArray<FCaySimDroneTypeInfo> Types;
	TArray<FCaySimModelInfo> Models;
	TArray<FCaySimLogInfo> Logs;
	TArray<FCaySimSession> OpenSessions;
	ECaySimSessionMode Mode = ECaySimSessionMode::Sitl;
	int32 CenterId = 0;
	int32 LogId = 0;
	FString TypeKey;
	FString ModelId;
	FCaySimModelOrient OrientOverride;
	bool bOrientOverride = false;
	FCaySimWorld World;
	ECaySimWorldSource WorldSource = ECaySimWorldSource::CesiumIon;
	FCaySimLaunchConfig Launch;
	FCaySimSession Session;
	int32 PendingLoads = 0;

	void SetStep(ECaySimWizardStep S);
	void Status(const FString& S) { OnStatus.Broadcast(S); }
	void BindClient();
	void UnbindClient();
	/** Windows `LoadImage` makrosuyla (LoadImageW) çakışmasın diye farklı ad. */
	void LoadRemoteImage(const FString& Url, TFunction<void(UTexture2D*)> Done);
	void AfterHello();

	UFUNCTION() void HandleHello(bool bOk, const FCaySimHello& InHello);
	UFUNCTION() void HandleError(const FString& Code, const FString& Msg);
	UFUNCTION() void HandleCenters(const TArray<FCaySimCenter>& InCenters);
	UFUNCTION() void HandleTypes(const TArray<FCaySimDroneTypeInfo>& InTypes);
	UFUNCTION() void HandleModels(const TArray<FCaySimModelInfo>& InModels);
	UFUNCTION() void HandleLogs(const TArray<FCaySimLogInfo>& InLogs);
	UFUNCTION() void HandleWorldDefaults(const FCaySimWorld& InWorld);
	UFUNCTION() void HandleSessions(const TArray<FCaySimSession>& InSessions);
	UFUNCTION() void HandleSessionCreated(bool bOk, const FCaySimSession& InSession);
	UFUNCTION() void HandleSessionFetched(bool bOk, const FCaySimSession& InSession);
	UFUNCTION() void HandleSessionState(const FCaySimSession& InSession);
	UFUNCTION() void HandleEnd(const FString& Reason);
};
