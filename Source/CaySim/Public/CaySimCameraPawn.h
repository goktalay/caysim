// Kamera piyonu — takip / FPV (gimbal) / serbest / yer kameraları; klavye-fare ve gamepad RC girişi
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "CaySimCameraPawn.generated.h"

class UCameraComponent;
class USpringArmComponent;
class ACaySimDroneActor;
class UInputMappingContext;
class UInputAction;
struct FInputActionValue;

UENUM(BlueprintType)
enum class ECaySimCameraMode : uint8
{
	Follow  UMETA(DisplayName = "Takip (1)"),
	Fpv     UMETA(DisplayName = "Dron kamerası (2)"),
	Free    UMETA(DisplayName = "Serbest (3)"),
	Ground  UMETA(DisplayName = "Yer kamerası (4)")
};

/**
 * Oyuncu piyonu = kamera. Fizik yok. Takip: yaylı kol dronun arkasında; FPV: dron gövdesine kilitli, gimbal pitch;
 * Serbest: WASD + fare; Yer: kalkış noktasında sabit, dronu izler (zoom otomatik).
 * Gamepad eksenleri (isteğe bağlı) RC_CHANNELS_OVERRIDE olarak HEPGO'ya gider (UHepgoClient::SendRc).
 */
UCLASS()
class CAYSIM_API ACaySimCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	ACaySimCameraPawn();
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Kamera") void SetTarget(ACaySimDroneActor* InTarget);
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Kamera") void SetMode(ECaySimCameraMode InMode);
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Kamera") void CycleMode();
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Kamera") ECaySimCameraMode GetMode() const { return Mode; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Kamera") float GetGimbalPitch() const { return GimbalPitch; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Kamera") float GetFov() const;
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Kamera") bool IsHudEnabled() const { return bHudEnabled; }
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Kamera") void ToggleHud() { bHudEnabled = !bHudEnabled; }

	/** Gamepad → RC gönderimi açık mı (Mission Planner joystick ile çakışmasın diye varsayılan kapalı). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|RC") bool bGamepadRc = false;
	/** RC kanal eşlemesi: 0 roll, 1 pitch, 2 throttle, 3 yaw (ArduPilot AETR). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|RC") bool bInvertPitchStick = true;

	UPROPERTY(EditAnywhere, Category = "CAYSIM|Kamera") float FollowDistanceM = 12.f;
	UPROPERTY(EditAnywhere, Category = "CAYSIM|Kamera") float FollowHeightM = 3.f;
	UPROPERTY(EditAnywhere, Category = "CAYSIM|Kamera") float FollowLag = 4.f;
	UPROPERTY(EditAnywhere, Category = "CAYSIM|Kamera") float FpvFov = 78.f;
	UPROPERTY(EditAnywhere, Category = "CAYSIM|Kamera") float FreeSpeedMs = 30.f;
	/** FPV kamera ofseti gövde çerçevesinde (m): burun önü */
	UPROPERTY(EditAnywhere, Category = "CAYSIM|Kamera") FVector FpvOffsetM = FVector(0.4f, 0.f, 0.05f);

	// Enhanced Input varlıkları (Content/Input; Blueprint alt sınıfında atanır). Atanmazsa eski eksen eşlemeleri kullanılır.
	UPROPERTY(EditDefaultsOnly, Category = "CAYSIM|Giriş") TObjectPtr<UInputMappingContext> MappingContext;
	UPROPERTY(EditDefaultsOnly, Category = "CAYSIM|Giriş") TObjectPtr<UInputAction> IA_Look;
	UPROPERTY(EditDefaultsOnly, Category = "CAYSIM|Giriş") TObjectPtr<UInputAction> IA_Move;
	UPROPERTY(EditDefaultsOnly, Category = "CAYSIM|Giriş") TObjectPtr<UInputAction> IA_Ascend;
	UPROPERTY(EditDefaultsOnly, Category = "CAYSIM|Giriş") TObjectPtr<UInputAction> IA_Zoom;
	UPROPERTY(EditDefaultsOnly, Category = "CAYSIM|Giriş") TObjectPtr<UInputAction> IA_CycleCamera;
	UPROPERTY(EditDefaultsOnly, Category = "CAYSIM|Giriş") TObjectPtr<UInputAction> IA_ToggleHud;
	UPROPERTY(EditDefaultsOnly, Category = "CAYSIM|Giriş") TObjectPtr<UInputAction> IA_RcRollPitch;
	UPROPERTY(EditDefaultsOnly, Category = "CAYSIM|Giriş") TObjectPtr<UInputAction> IA_RcThrottleYaw;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USpringArmComponent> Arm;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;

private:
	TWeakObjectPtr<ACaySimDroneActor> Target;
	ECaySimCameraMode Mode = ECaySimCameraMode::Follow;
	float GimbalPitch = -10.f;
	float FreeYaw = 0.f, FreePitch = -15.f;
	FVector FreeVelocity = FVector::ZeroVector;
	FVector2D MoveInput = FVector2D::ZeroVector;
	float AscendInput = 0.f;
	FVector GroundCamLocation = FVector::ZeroVector;
	bool bGroundCamPlaced = false;
	bool bHudEnabled = true;
	FVector2D RcRollPitch = FVector2D::ZeroVector;
	FVector2D RcThrottleYaw = FVector2D::ZeroVector;
	float FollowZoom = 1.f;

	void OnLook(const FInputActionValue& V);
	void OnMove(const FInputActionValue& V);
	void OnAscend(const FInputActionValue& V);
	void OnZoom(const FInputActionValue& V);
	void OnRcRollPitch(const FInputActionValue& V);
	void OnRcThrottleYaw(const FInputActionValue& V);
	void UpdateFollow(float Dt);
	void UpdateFpv(float Dt);
	void UpdateFree(float Dt);
	void UpdateGround(float Dt);
	void SendRc();
	double LastRcSend = 0.0;
};
