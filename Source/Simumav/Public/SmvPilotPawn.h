// Pilot piyonu: kamera kipleri + kumanda (Enhanced Input; tüm IA/IMC varlıkları KODDA oluşturulur — içerik dosyası gerekmez).
// Kamera kipleri (SIMUMAV ile aynı adlar): Takip, FPV (gövde), Gimbal (eğim ayarlı), Yörünge, Yer (pilot gözü), Serbest, Tepeden.
// PiP: ikinci görüntü (gimbal kamerası) SceneCapture → render hedefi → HUD köşesi. Görüntü tipi: gündüz / termal / gece (son işlem ayarlarıyla).
// Kumanda → RC: ch1 roll, ch2 pitch (ters), ch3 gaz, ch4 yaw, ch5 uçuş kipi (1000/1500/2000); pwm = 1500 + v·500 (SIMUMAV fly/rc.js).
// Kaynaklar: klavye, gamepad (XInput), RC verici (RawInput HID: GenericUSBController_Axis1..8; eksen eşlemesi Ayarlar'da).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "SmvPilotPawn.generated.h"

class UCameraComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UInputMappingContext;
class UInputAction;
class ACaySimDroneActor;
struct FInputActionValue;

UENUM(BlueprintType)
enum class ESmvCamMode : uint8
{
	Chase, Fpv, Gimbal, Orbit, Ground, Free, Top
};

/** Görüntü tipi (sunucu `camera` alanı). */
UENUM(BlueprintType)
enum class ESmvCamType : uint8
{
	Day, Thermal, Night, None
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

	/** Eylemler: Arm, Disarm, Takeoff, Rtl, Land, Lock, Ack, Pause, Reset, CamCycle, CamType, Hud, Pip, SpeedUp, SpeedDown, Mode1, Mode2, Mode3, MotorCut */
	FSmvPawnAction OnAction;
	/** RC kanalları hazır (≈ 25 Hz) — GameMode bağlantıya iletir. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FSmvRcOut, const TArray<int32>&);
	FSmvRcOut OnRc;

	// ayarlar (Ayarlar ekranından)
	FString InputDevice = TEXT("keyboard");
	bool bStickMode2 = true;
	float Expo = 0.3f;
	TArray<int32> RcAxisMap;   // [roll, pitch, throttle, yaw, mode] → RawInput eksen numarası (1..8), 0 = yok
	TArray<bool> RcInvert;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneCaptureComponent2D> PipCapture;
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> PipTarget;
	UPROPERTY() TObjectPtr<UInputMappingContext> Imc;
	UPROPERTY() TMap<FName, TObjectPtr<UInputAction>> Actions;

private:
	TWeakObjectPtr<ACaySimDroneActor> Target;
	FString VType = TEXT("multirotor");
	ESmvCamMode Mode = ESmvCamMode::Chase;
	ESmvCamType CamType = ESmvCamType::Day;
	float GimbalPitch = -15.f;
	float OrbitYaw = 0.f, OrbitPitch = -20.f, OrbitDist = 15.f;
	float FreeYaw = 0.f, FreePitch = -10.f;
	FVector FreeVel = FVector::ZeroVector;
	FVector GroundPos = FVector::ZeroVector;
	bool bGroundPlaced = false;
	bool bHud = true, bPip = false, bFlying = false;
	float ChaseZoom = 1.f;

	// çubuk durumu (−1..1)
	FVector2D LeftStick = FVector2D::ZeroVector, RightStick = FVector2D::ZeroVector;
	float KeyThrottle = 0.f;   // klavye gazı (uçakta kalıcı, kopterde yaylı)
	int32 ModeSwitch = 1000;
	float RcAxis[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	bool bRcAxisSeen = false;
	TArray<int32> Rc;
	double LastRcSent = 0;
	FVector2D Look = FVector2D::ZeroVector;

	void BuildInput();
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
};
