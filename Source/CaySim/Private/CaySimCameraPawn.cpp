#include "CaySimCameraPawn.h"
#include "CaySim.h"
#include "DroneActor.h"
#include "CaySimGameInstance.h"
#include "HepgoClient.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "GameFramework/PlayerController.h"

ACaySimCameraPawn::ACaySimCameraPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics; // dron PrePhysics'te güncellenir
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Arm = CreateDefaultSubobject<USpringArmComponent>(TEXT("Arm"));
	Arm->SetupAttachment(Root);
	Arm->bDoCollisionTest = false;
	Arm->bEnableCameraLag = true;
	Arm->CameraLagSpeed = 6.f;
	Arm->TargetArmLength = 0.f;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Arm);
	Camera->FieldOfView = 70.f;
	bUseControllerRotationYaw = false;
}

void ACaySimCameraPawn::BeginPlay()
{
	Super::BeginPlay();
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (ULocalPlayer* LP = PC->GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* Sub = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				if (MappingContext) Sub->AddMappingContext(MappingContext, 0);
			}
		}
	}
}

void ACaySimCameraPawn::SetupPlayerInputComponent(UInputComponent* PIC)
{
	Super::SetupPlayerInputComponent(PIC);
	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PIC))
	{
		if (IA_Look) EIC->BindAction(IA_Look, ETriggerEvent::Triggered, this, &ACaySimCameraPawn::OnLook);
		if (IA_Move) EIC->BindAction(IA_Move, ETriggerEvent::Triggered, this, &ACaySimCameraPawn::OnMove);
		if (IA_Move) EIC->BindAction(IA_Move, ETriggerEvent::Completed, this, &ACaySimCameraPawn::OnMove);
		if (IA_Ascend) EIC->BindAction(IA_Ascend, ETriggerEvent::Triggered, this, &ACaySimCameraPawn::OnAscend);
		if (IA_Ascend) EIC->BindAction(IA_Ascend, ETriggerEvent::Completed, this, &ACaySimCameraPawn::OnAscend);
		if (IA_Zoom) EIC->BindAction(IA_Zoom, ETriggerEvent::Triggered, this, &ACaySimCameraPawn::OnZoom);
		if (IA_CycleCamera) EIC->BindAction(IA_CycleCamera, ETriggerEvent::Started, this, &ACaySimCameraPawn::CycleMode);
		if (IA_ToggleHud) EIC->BindAction(IA_ToggleHud, ETriggerEvent::Started, this, &ACaySimCameraPawn::ToggleHud);
		if (IA_RcRollPitch) EIC->BindAction(IA_RcRollPitch, ETriggerEvent::Triggered, this, &ACaySimCameraPawn::OnRcRollPitch);
		if (IA_RcRollPitch) EIC->BindAction(IA_RcRollPitch, ETriggerEvent::Completed, this, &ACaySimCameraPawn::OnRcRollPitch);
		if (IA_RcThrottleYaw) EIC->BindAction(IA_RcThrottleYaw, ETriggerEvent::Triggered, this, &ACaySimCameraPawn::OnRcThrottleYaw);
		if (IA_RcThrottleYaw) EIC->BindAction(IA_RcThrottleYaw, ETriggerEvent::Completed, this, &ACaySimCameraPawn::OnRcThrottleYaw);
	}
	// Enhanced Input varlıkları yoksa: Config/DefaultInput.ini eski eşlemeleri (1-4 kamera, H HUD)
	PIC->BindAction(TEXT("Kamera1"), IE_Pressed, this, &ACaySimCameraPawn::CycleMode);
	PIC->BindAction(TEXT("Hud"), IE_Pressed, this, &ACaySimCameraPawn::ToggleHud);
}

void ACaySimCameraPawn::SetTarget(ACaySimDroneActor* InTarget)
{
	Target = InTarget;
	bGroundCamPlaced = false;
}

void ACaySimCameraPawn::SetMode(ECaySimCameraMode InMode)
{
	Mode = InMode;
	Arm->bEnableCameraLag = (Mode == ECaySimCameraMode::Follow);
	Camera->SetFieldOfView(Mode == ECaySimCameraMode::Fpv ? FpvFov : 70.f);
	if (Mode == ECaySimCameraMode::Free)
	{
		const FRotator R = Camera->GetComponentRotation();
		FreeYaw = R.Yaw; FreePitch = R.Pitch;
		SetActorLocation(Camera->GetComponentLocation());
		Arm->SetRelativeLocation(FVector::ZeroVector);
	}
	if (Mode == ECaySimCameraMode::Ground) bGroundCamPlaced = false;
}

void ACaySimCameraPawn::CycleMode()
{
	SetMode((ECaySimCameraMode)(((uint8)Mode + 1) % 4));
}

float ACaySimCameraPawn::GetFov() const { return Camera ? Camera->FieldOfView : 70.f; }

void ACaySimCameraPawn::OnLook(const FInputActionValue& V)
{
	const FVector2D D = V.Get<FVector2D>();
	if (Mode == ECaySimCameraMode::Fpv) GimbalPitch = FMath::Clamp(GimbalPitch + D.Y * 0.5f, -90.f, 20.f);
	else if (Mode == ECaySimCameraMode::Free) { FreeYaw += D.X * 0.3f; FreePitch = FMath::Clamp(FreePitch - D.Y * 0.3f, -89.f, 89.f); }
	else if (Mode == ECaySimCameraMode::Follow) { FreeYaw += D.X * 0.3f; FreePitch = FMath::Clamp(FreePitch - D.Y * 0.3f, -60.f, 10.f); }
}

void ACaySimCameraPawn::OnMove(const FInputActionValue& V) { MoveInput = V.Get<FVector2D>(); }
void ACaySimCameraPawn::OnAscend(const FInputActionValue& V) { AscendInput = V.Get<float>(); }
void ACaySimCameraPawn::OnZoom(const FInputActionValue& V)
{
	const float Z = V.Get<float>();
	if (Mode == ECaySimCameraMode::Follow) FollowZoom = FMath::Clamp(FollowZoom * (1.f - Z * 0.1f), 0.3f, 6.f);
	else Camera->SetFieldOfView(FMath::Clamp(Camera->FieldOfView * (1.f - Z * 0.08f), 10.f, 110.f));
}
void ACaySimCameraPawn::OnRcRollPitch(const FInputActionValue& V) { RcRollPitch = V.Get<FVector2D>(); }
void ACaySimCameraPawn::OnRcThrottleYaw(const FInputActionValue& V) { RcThrottleYaw = V.Get<FVector2D>(); }

void ACaySimCameraPawn::UpdateFollow(float Dt)
{
	ACaySimDroneActor* T = Target.Get();
	if (!T) return;
	// Dronun gidiş yönünün arkasında, FreeYaw/FreePitch ile çevresinde döndürülebilir
	const FVector Fwd = T->GetForwardWorld().GetSafeNormal2D();
	const float BaseYaw = Fwd.IsNearlyZero() ? 0.f : FMath::RadiansToDegrees(FMath::Atan2(Fwd.Y, Fwd.X));
	const FRotator Orbit(FreePitch, BaseYaw + 180.f + FreeYaw, 0.f);
	const FVector Offset = Orbit.Vector() * FollowDistanceM * FollowZoom * 100.f + FVector(0, 0, FollowHeightM * 100.f * FollowZoom);
	const FVector Desired = T->GetActorLocation() + Offset;
	SetActorLocation(FMath::VInterpTo(GetActorLocation(), Desired, Dt, FollowLag));
	Arm->SetRelativeLocation(FVector::ZeroVector);
	Camera->SetWorldRotation((T->GetActorLocation() - Camera->GetComponentLocation()).Rotation());
}

void ACaySimCameraPawn::UpdateFpv(float Dt)
{
	ACaySimDroneActor* T = Target.Get();
	if (!T) return;
	const FTransform Body = T->GetActorTransform();
	SetActorLocation(Body.TransformPosition(FpvOffsetM * 100.f));
	Arm->SetRelativeLocation(FVector::ZeroVector);
	const FQuat Gimbal = FQuat(FVector::RightVector, FMath::DegreesToRadians(-GimbalPitch));
	Camera->SetWorldRotation(Body.GetRotation() * Gimbal);
}

void ACaySimCameraPawn::UpdateFree(float Dt)
{
	const FRotator Rot(FreePitch, FreeYaw, 0.f);
	const FVector Dir = Rot.Vector() * MoveInput.Y + FRotationMatrix(Rot).GetScaledAxis(EAxis::Y) * MoveInput.X + FVector::UpVector * AscendInput;
	FreeVelocity = FMath::VInterpTo(FreeVelocity, Dir.GetClampedToMaxSize(1.f) * FreeSpeedMs * 100.f, Dt, 5.f);
	SetActorLocation(GetActorLocation() + FreeVelocity * Dt);
	Camera->SetWorldRotation(Rot);
}

void ACaySimCameraPawn::UpdateGround(float Dt)
{
	ACaySimDroneActor* T = Target.Get();
	if (!T) return;
	if (!bGroundCamPlaced)
	{
		// Kalkış noktasının 30 m yanında, 2 m yükseklikte
		GroundCamLocation = T->GetActorLocation() + FVector(-3000.f, 2000.f, 200.f);
		bGroundCamPlaced = true;
	}
	SetActorLocation(GroundCamLocation);
	const FVector To = T->GetActorLocation() - GroundCamLocation;
	Camera->SetWorldRotation(To.Rotation());
	// Otomatik zoom: dron ekranda ~%5 kalsın
	const float Dist = To.Size() / 100.f;
	Camera->SetFieldOfView(FMath::Clamp(FMath::RadiansToDegrees(2.f * FMath::Atan(40.f / FMath::Max(10.f, Dist))), 5.f, 70.f));
}

void ACaySimCameraPawn::SendRc()
{
	if (!bGamepadRc) return;
	const double Now = FPlatformTime::Seconds();
	if (Now - LastRcSend < 0.04) return;
	LastRcSend = Now;
	UCaySimGameInstance* GI = UCaySimGameInstance::Get(this);
	if (!GI || !GI->GetHepgo()) return;
	auto Us = [](float v) { return FMath::Clamp(1500 + FMath::RoundToInt(v * 500.f), 1000, 2000); };
	TArray<int32> Ch;
	Ch.Init(0, 8);
	Ch[0] = Us(RcRollPitch.X);                                           // roll
	Ch[1] = Us(bInvertPitchStick ? -RcRollPitch.Y : RcRollPitch.Y);     // pitch
	Ch[2] = Us(RcThrottleYaw.Y);                                         // throttle (−1..1 → 1000..2000)
	Ch[3] = Us(RcThrottleYaw.X);                                         // yaw
	GI->GetHepgo()->SendRc(Ch);
}

void ACaySimCameraPawn::Tick(float Dt)
{
	Super::Tick(Dt);
	switch (Mode)
	{
	case ECaySimCameraMode::Follow: UpdateFollow(Dt); break;
	case ECaySimCameraMode::Fpv:    UpdateFpv(Dt); break;
	case ECaySimCameraMode::Free:   UpdateFree(Dt); break;
	case ECaySimCameraMode::Ground: UpdateGround(Dt); break;
	}
	SendRc();
}
