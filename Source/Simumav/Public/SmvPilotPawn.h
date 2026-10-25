// Pilot piyonu: kamera kipleri + kumanda (Enhanced Input; tüm IA/IMC varlıkları KODDA oluşturulur — içerik dosyası gerekmez).
// Kamera kipleri (web flightsim3d CAMERA_MODES sırası, tuş 1–5, C döngü): Takip, FPV (gövde), Gimbal (eğim ayarlı), Serbest, Yer (pilot gözü);
// ek olarak 6 Yörünge, 7 Tepeden. Takip: aracın arkasından-üstünden, yön yumuşatılır, fare sürükle = bakış açısı, tekerlek = mesafe.
// PiP: ikinci görüntü (gimbal kamerası) SceneCapture → render hedefi → HUD köşesi. Görüntü tipi: gündüz / termal / gece (son işlem ayarlarıyla).
// Kumanda → RC: ch1 roll, ch2 pitch (ters), ch3 gaz, ch4 yaw, ch5 uçuş kipi (1000/1500/2000); pwm = 1500 + v·500 (SIMUMAV fly/rc.js).
// Klavye (web virtualRC ile aynı): WASD sol çubuk, oklar sağ çubuk (Mode 2: W/S gaz, A/D yaw, ↑↓ pitch, ←→ roll); tuş basılıyken eksen
// 3,5/sn ile uca gider, bırakınca yaylı döner; uçakta gaz tutmalıdır (0,7/sn). Boşluk = gaz orta (1500), Q/E = ch5 −/+ 500.
// Kopterde ch3 ilk gaz girdisine dek 0 (dokunma; ARM ön denetimi "gaz yüksek" olmasın — web lazyThr).
// Kaynaklar: klavye, ekrandaki sanal çubuklar (fare sürükle), gamepad (XInput), RC verici (RawInput HID: GenericUSBController_Axis1..8).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "SmvPilotPawn.generated.h"

class UCameraComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UTexture2D;
class UInputMappingContext;
class UInputAction;
class ACaySimDroneActor;
struct FInputActionValue;
struct FKey;   // InputCoreTypes.h (MapKey bildirimi için yeterli)

UENUM(BlueprintType)
enum class ESmvCamMode : uint8
{
	Chase, Fpv, Gimbal, Orbit, Ground, Free, Top,
	/** İnsan pilot görünümü: sahnedeki pilotun omzunun gerisinden drona bakar (web shoulderCam) */
	Pilot
};

/** Görüntü tipi (sunucu `camera` alanı). */
UENUM(BlueprintType)
enum class ESmvCamType : uint8
{
	Day, Thermal, Night, None,
	/** Multispektral (yol haritası "Hangar › Kamera"): NDVI benzeri sahte renk — bitki örtüsü kırmızı-eflatun */
	Multispectral
};

DECLARE_MULTICAST_DELEGATE_OneParam(FSmvPawnAction, FName /*Action*/);

UCLASS()
class SIMUMAV_API ASmvPilotPawn : public APawn
{
	GENERATED_BODY()

public:
	ASmvPilotPawn();
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PossessedBy(AController* NewController) override;

	void SetTarget(ACaySimDroneActor* InTarget, const FString& VehicleType);
	void SetMode(ESmvCamMode M);
	void CycleMode();
	ESmvCamMode GetMode() const { return Mode; }
	/** Kamera kipinin adı (arayüz dilinde; alt bant düğmesi). */
	static FString ModeLabel(ESmvCamMode M);
	/** Ekrandaki sanal çubuk (Side 0 sol, 1 sağ; V −1..1, yukarı +). bActive=false → bırakıldı (yaylı döner). */
	void SetVirtualStick(int32 Side, const FVector2D& V, bool bActive);
	/** Çubukların çizim konumu (−1..1; yukarı +) — klavye/sanal/gamepad birleşik, kip eşlemesine göre. */
	FVector2D GetStickL() const { return StickL; }
	FVector2D GetStickR() const { return StickR; }
	int32 GetCh5() const { return ModeSwitch; }
	bool IsThrottleEngaged() const { return bThrUsed; }
	void SetCamType(ESmvCamType T);
	ESmvCamType GetCamType() const { return CamType; }
	float GetGimbalPitchDeg() const { return GimbalPitch; }
	float GetFov() const;
	bool IsHudOn() const { return bHud; }
	bool IsPipOn() const { return bPip; }
	UTextureRenderTarget2D* GetPipTarget() const { return PipTarget; }
	/** Uçuşta değilken (menü) kumanda RC göndermez. */
	void SetFlying(bool bOn) { bFlying = bOn; }
	/** Son RC kanalları (HUD çubuk göstergesi). */
	const TArray<int32>& LastRc() const { return Rc; }

	/** Eylemler: Arm, ArmToggle, Takeoff, Rtl, Land, Lock, Ack, Pause, Reset, CamCycle, CamType, Hud, Pip, Panel, Sticks, ModeMenu, SpeedUp, SpeedDown,
	 *  Mode1, Mode2, Mode3, MotorCut, Cam1..Cam7, Ch5Up, Ch5Down, ThrCenter */
	FSmvPawnAction OnAction;
	/** RC kanalları hazır (≈ 25 Hz) — GameMode bağlantıya iletir. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FSmvRcOut, const TArray<int32>&);
	FSmvRcOut OnRc;

	// ayarlar (Ayarlar ekranından)
	FString InputDevice = TEXT("keyboard");
	/** Kumanda modu 1–4 (HEPGO §27.1.1): [sol X, sol Y, sağ X, sağ Y] = 1 yaw/pitch/roll/gaz · 2 yaw/gaz/roll/pitch ·
	 *  3 roll/pitch/yaw/gaz · 4 roll/gaz/yaw/pitch. Klavye WASD = sol, oklar = sağ; sanal çubuk ve gamepad aynı eşlemeyi izler. */
	int32 StickMode = 2;
	/** Radyal menü açıkken sağ çubuk uçuşa gitmez (menü seçimine gider) */
	bool bSuppressRightPad = false;
	FVector2D GetPadRight() const { return RightStick; }
	/** Tuş atamalarını yeniden kur (Ayarlar → Tuşlar; FSmvUiPrefs) */
	void RemapKeys();
	/** Giriş sekansı: kamera 100 m yükseklik / 80 m yarıçaptan 110°'lik sarmalla takip konumuna iner (web flightsim_intro) */
	void PlayIntro(float Seconds);
	/** Sahnedeki pilot figürü: ayak konumu (dünya) ve göz yüksekliği (cm) — Pilot kamera kipi */
	void SetPilotEye(const FVector& Base, float EyeCm) { PilotBase = Base; PilotEyeCm = EyeCm; bHasPilot = true; }
	/** Kamera sarsıntısı (çarpma) */
	void Shake(float AmpCm, float Seconds);
	/** Ham RC verici eksenleri (−1..1; Ayarlar → Kumanda → Algıla) */
	float GetRawAxis(int32 I) const { return (I >= 0 && I < 8) ? RcAxis[I] : 0.f; }
	float Expo = 0.3f;
	TArray<int32> RcAxisMap;   // [roll, pitch, throttle, yaw, mode] → RawInput eksen numarası (1..8), 0 = yok
	TArray<bool> RcInvert;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
	/** Hız çizgileri (Fab Speed Effects son işlem malzemesi; hıza göre ağırlık) */
	UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> SpeedMid;
	bool bSpeedTried = false;
	bool bSpdInit = false;
	FVector SpdLast = FVector::ZeroVector;
	float SpdSmooth = 0.f;
	void UpdateSpeedFx(float Dt);
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneCaptureComponent2D> PipCapture;
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> PipTarget;
	/** Termal palet renk tabloları (256×16 LUT; 0 beyaz-sıcak, 1 siyah-sıcak, 2 ironbow) */
	UPROPERTY() TArray<TObjectPtr<UTexture2D>> PaletteLuts;
	UTexture2D* PaletteLut(int32 Index);
	UPROPERTY() TObjectPtr<UInputMappingContext> Imc;
	UPROPERTY() TMap<FName, TObjectPtr<UInputAction>> Actions;

private:
	TWeakObjectPtr<ACaySimDroneActor> Target;
	FString VType = TEXT("multirotor");
	ESmvCamMode Mode = ESmvCamMode::Chase;
	ESmvCamType CamType = ESmvCamType::Day;
	float GimbalPitch = -15.f;
	float OrbitYaw = 0.f, OrbitPitch = -20.f, OrbitDist = 15.f;
	// takip kamerası: yumuşatılmış gövde yönü + kullanıcının sürükleyerek verdiği açı
	float ChaseYawSm = 0.f, ChaseYawOff = 0.f, ChasePitch = -14.f;
	bool bChaseInit = false;
	FVector ChaseOffSm = FVector::ZeroVector;
	float VisRadius = 1.f, VisNose = 0.5f;
	float FreeYaw = 0.f, FreePitch = -10.f;
	FVector FreeVel = FVector::ZeroVector;
	FVector GroundPos = FVector::ZeroVector;
	bool bGroundPlaced = false;
	bool bHud = true, bPip = false, bFlying = false;
	float ChaseZoom = 1.f;
	float IntroLeft = 0.f, IntroDur = 0.f, ShakeLeft = 0.f, ShakeAmp = 0.f;
	FVector PilotBase = FVector::ZeroVector;
	float PilotEyeCm = 165.f;
	bool bHasPilot = false;

	// gamepad çubukları (Enhanced Input; −1..1)
	FVector2D LeftStick = FVector2D::ZeroVector, RightStick = FVector2D::ZeroVector;
	// birleşik eksenler (−1..1): klavye entegrasyonu + yay, sanal çubuk / gamepad mutlak değer
	float AxRoll = 0.f, AxPitch = 0.f, AxYaw = 0.f, AxThr = 0.f;
	bool bThrUsed = true;
	FVector2D VStick[2] = { FVector2D::ZeroVector, FVector2D::ZeroVector };
	bool bVStick[2] = { false, false };
	FVector2D StickL = FVector2D::ZeroVector, StickR = FVector2D::ZeroVector;
	int32 ModeSwitch = 1000;
	float RcAxis[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	bool bRcAxisSeen = false;
	TArray<int32> Rc;
	double LastRcSent = 0;
	FVector2D Look = FVector2D::ZeroVector;

	void BuildInput();
	void MapDjiKeys();
	void MapAllKeys();
	UInputAction* MakeAction(FName Name, int32 ValueType);
	void MapKey(FName Action, const FKey& Key, bool bNegate = false, bool bSwizzle = false);
	void OnLeft(const FInputActionValue& V);
	void OnRight(const FInputActionValue& V);
	void OnLook(const FInputActionValue& V);
	void OnZoom(const FInputActionValue& V);
	void OnRcAxis(const FInputActionValue& V, int32 Index);
	void Fire(FName Name);
	void ComputeRc(float Dt);
	void UpdateCamera(float Dt);
	void ApplyCamTypePost();
	bool IsPlaneLike() const;
	bool KeyDown(const FKey& K) const;
};
