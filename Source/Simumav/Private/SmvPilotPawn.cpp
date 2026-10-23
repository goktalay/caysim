#include "SmvPilotPawn.h"
#include "SimumavModule.h"
#include "DroneActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputModifiers.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "Kismet/KismetMathLibrary.h"

ASmvPilotPawn::ASmvPilotPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;   // dron PrePhysics'te güncellenir
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->FieldOfView = 75.f;
	PipCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("PipCapture"));
	PipCapture->SetupAttachment(Root);
	PipCapture->bCaptureEveryFrame = false;
	PipCapture->bCaptureOnMovement = false;
	PipCapture->FOVAngle = 60.f;
	PipCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Rc.Init(0, 8);
}

void ASmvPilotPawn::BeginPlay()
{
	Super::BeginPlay();
	PipTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("PipTarget"));
	PipTarget->InitAutoFormat(640, 360);
	PipTarget->UpdateResourceImmediate(true);
	PipCapture->TextureTarget = PipTarget;
	ApplyCamTypePost();
}

void ASmvPilotPawn::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	BuildInput();
	if (APlayerController* PC = Cast<APlayerController>(NewController))
	{
		if (ULocalPlayer* LP = PC->GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* Sub = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				Sub->ClearAllMappings();
				Sub->AddMappingContext(Imc, 0);
			}
		}
	}
}

// ---------------------------------------------------------------------------
// girdi — çalışma anında oluşturulan Enhanced Input varlıkları
// ---------------------------------------------------------------------------

UInputAction* ASmvPilotPawn::MakeAction(FName Name, int32 ValueType)
{
	if (TObjectPtr<UInputAction>* Found = Actions.Find(Name)) return *Found;
	UInputAction* A = NewObject<UInputAction>(this, Name);
	A->ValueType = ValueType == 2 ? EInputActionValueType::Axis2D : ValueType == 1 ? EInputActionValueType::Axis1D : EInputActionValueType::Boolean;
	Actions.Add(Name, A);
	return A;
}

void ASmvPilotPawn::MapKey(FName Action, const FKey& Key, bool bNegate, bool bSwizzle)
{
	UInputAction* A = Actions.FindRef(Action);
	if (!A || !Imc) return;
	FEnhancedActionKeyMapping& M = Imc->MapKey(A, Key);
	if (bSwizzle)
	{
		UInputModifierSwizzleAxis* S = NewObject<UInputModifierSwizzleAxis>(Imc);
		S->Order = EInputAxisSwizzle::YXZ;
		M.Modifiers.Add(S);
	}
	if (bNegate) M.Modifiers.Add(NewObject<UInputModifierNegate>(Imc));
}

void ASmvPilotPawn::BuildInput()
{
	if (Imc) return;
	Imc = NewObject<UInputMappingContext>(this, TEXT("IMC_Simumav"));
	MakeAction(TEXT("Left"), 2);    // Mod-2: X yaw, Y gaz
	MakeAction(TEXT("Right"), 2);   // X roll, Y pitch
	MakeAction(TEXT("Look"), 2);
	MakeAction(TEXT("Zoom"), 1);
	for (int32 i = 0; i < 8; ++i) MakeAction(*FString::Printf(TEXT("RcAxis%d"), i + 1), 1);
	const FName Btn[] = { TEXT("Arm"), TEXT("Disarm"), TEXT("Takeoff"), TEXT("Rtl"), TEXT("Land"), TEXT("Lock"), TEXT("Ack"), TEXT("Pause"), TEXT("Reset"),
		TEXT("CamCycle"), TEXT("CamType"), TEXT("Hud"), TEXT("Pip"), TEXT("SpeedUp"), TEXT("SpeedDown"), TEXT("Mode1"), TEXT("Mode2"), TEXT("Mode3"), TEXT("MotorCut"),
		TEXT("GimbalUp"), TEXT("GimbalDown") };
	for (const FName& B : Btn) MakeAction(B, 0);

	// klavye: W/S gaz, A/D yaw, oklar pitch/roll (Mod-2 düzeni)
	MapKey(TEXT("Left"), EKeys::W, false, true);
	MapKey(TEXT("Left"), EKeys::S, true, true);
	MapKey(TEXT("Left"), EKeys::D);
	MapKey(TEXT("Left"), EKeys::A, true);
	MapKey(TEXT("Right"), EKeys::Up, false, true);
	MapKey(TEXT("Right"), EKeys::Down, true, true);
	MapKey(TEXT("Right"), EKeys::Right);
	MapKey(TEXT("Right"), EKeys::Left, true);
	// gamepad (XInput): sol çubuk gaz/yaw, sağ çubuk pitch/roll
	MapKey(TEXT("Left"), EKeys::Gamepad_Left2D);
	MapKey(TEXT("Right"), EKeys::Gamepad_Right2D);
	// fare: serbest/yörünge kamera bakışı, tekerlek yakınlaştırma
	MapKey(TEXT("Look"), EKeys::Mouse2D);
	MapKey(TEXT("Zoom"), EKeys::MouseWheelAxis);
	// RC verici (RawInput eklentisi; Ayarlar → Kumanda'dan eksen eşlemesi)
	for (int32 i = 0; i < 8; ++i) MapKey(*FString::Printf(TEXT("RcAxis%d"), i + 1), FKey(*FString::Printf(TEXT("GenericUSBController_Axis%d"), i + 1)));

	MapKey(TEXT("Arm"), EKeys::E);               MapKey(TEXT("Arm"), EKeys::Gamepad_FaceButton_Bottom);
	MapKey(TEXT("Disarm"), EKeys::Q);
	MapKey(TEXT("Takeoff"), EKeys::T);           MapKey(TEXT("Takeoff"), EKeys::Gamepad_FaceButton_Left);
	MapKey(TEXT("Rtl"), EKeys::R);               MapKey(TEXT("Rtl"), EKeys::Gamepad_FaceButton_Top);
	MapKey(TEXT("Land"), EKeys::L);
	MapKey(TEXT("Lock"), EKeys::SpaceBar);       MapKey(TEXT("Lock"), EKeys::Gamepad_RightTrigger);
	MapKey(TEXT("Ack"), EKeys::B);               MapKey(TEXT("Ack"), EKeys::Gamepad_FaceButton_Right);
	MapKey(TEXT("Pause"), EKeys::Escape);        MapKey(TEXT("Pause"), EKeys::Gamepad_Special_Right);
	MapKey(TEXT("Reset"), EKeys::BackSpace);
	MapKey(TEXT("CamCycle"), EKeys::C);          MapKey(TEXT("CamCycle"), EKeys::Gamepad_RightShoulder);
	MapKey(TEXT("CamType"), EKeys::N);
	MapKey(TEXT("Hud"), EKeys::H);
	MapKey(TEXT("Pip"), EKeys::P);               MapKey(TEXT("Pip"), EKeys::Gamepad_LeftShoulder);
	MapKey(TEXT("SpeedUp"), EKeys::Add);         MapKey(TEXT("SpeedUp"), EKeys::Equals);
	MapKey(TEXT("SpeedDown"), EKeys::Subtract);  MapKey(TEXT("SpeedDown"), EKeys::Hyphen);
	MapKey(TEXT("Mode1"), EKeys::One);           MapKey(TEXT("Mode1"), EKeys::Gamepad_DPad_Left);
	MapKey(TEXT("Mode2"), EKeys::Two);           MapKey(TEXT("Mode2"), EKeys::Gamepad_DPad_Up);
	MapKey(TEXT("Mode3"), EKeys::Three);         MapKey(TEXT("Mode3"), EKeys::Gamepad_DPad_Right);
	MapKey(TEXT("MotorCut"), EKeys::K);
	MapKey(TEXT("GimbalUp"), EKeys::PageUp);
	MapKey(TEXT("GimbalDown"), EKeys::PageDown);
}

void ASmvPilotPawn::SetupPlayerInputComponent(UInputComponent* PIC)
{
	Super::SetupPlayerInputComponent(PIC);
	BuildInput();
	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PIC);
	if (!EIC) { UE_LOG(LogSimumav, Warning, TEXT("Enhanced Input bileşeni yok (DefaultInput.ini)")); return; }
	EIC->BindAction(Actions.FindRef(TEXT("Left")), ETriggerEvent::Triggered, this, &ASmvPilotPawn::OnLeft);
	EIC->BindAction(Actions.FindRef(TEXT("Left")), ETriggerEvent::Completed, this, &ASmvPilotPawn::OnLeft);
	EIC->BindAction(Actions.FindRef(TEXT("Right")), ETriggerEvent::Triggered, this, &ASmvPilotPawn::OnRight);
	EIC->BindAction(Actions.FindRef(TEXT("Right")), ETriggerEvent::Completed, this, &ASmvPilotPawn::OnRight);
	EIC->BindAction(Actions.FindRef(TEXT("Look")), ETriggerEvent::Triggered, this, &ASmvPilotPawn::OnLook);
	EIC->BindAction(Actions.FindRef(TEXT("Zoom")), ETriggerEvent::Triggered, this, &ASmvPilotPawn::OnZoom);
	for (int32 i = 0; i < 8; ++i)
	{
		EIC->BindActionValueLambda(Actions.FindRef(*FString::Printf(TEXT("RcAxis%d"), i + 1)), ETriggerEvent::Triggered,
			[this, i](const FInputActionValue& V) { OnRcAxis(V, i); });
	}
	for (const TPair<FName, TObjectPtr<UInputAction>>& Kv : Actions)
	{
		if (Kv.Value->ValueType != EInputActionValueType::Boolean) continue;
		const FName N = Kv.Key;
		EIC->BindActionValueLambda(Kv.Value, ETriggerEvent::Started, [this, N](const FInputActionValue&) { Fire(N); });
	}
}

void ASmvPilotPawn::OnLeft(const FInputActionValue& V) { LeftStick = V.Get<FVector2D>(); }
void ASmvPilotPawn::OnRight(const FInputActionValue& V) { RightStick = V.Get<FVector2D>(); }
void ASmvPilotPawn::OnLook(const FInputActionValue& V) { Look += V.Get<FVector2D>(); }
void ASmvPilotPawn::OnZoom(const FInputActionValue& V)
{
	const float Z = V.Get<float>();
	if (Mode == ESmvCamMode::Orbit) OrbitDist = FMath::Clamp(OrbitDist * (1.f - 0.1f * Z), 3.f, 400.f);
	else ChaseZoom = FMath::Clamp(ChaseZoom * (1.f - 0.1f * Z), 0.3f, 6.f);
}

void ASmvPilotPawn::OnRcAxis(const FInputActionValue& V, int32 Index)
{
	if (Index < 0 || Index >= 8) return;
	RcAxis[Index] = V.Get<float>();
	bRcAxisSeen = true;
}

void ASmvPilotPawn::Fire(FName Name)
{
	if (Name == TEXT("CamCycle")) { CycleMode(); return; }
	if (Name == TEXT("CamType")) { SetCamType((ESmvCamType)(((uint8)CamType + 1) % 3)); }
	if (Name == TEXT("Hud")) bHud = !bHud;
	if (Name == TEXT("Pip")) bPip = !bPip;
	if (Name == TEXT("Mode1")) ModeSwitch = 1000;
	if (Name == TEXT("Mode2")) ModeSwitch = 1500;
	if (Name == TEXT("Mode3")) ModeSwitch = 2000;
	if (Name == TEXT("GimbalUp")) GimbalPitch = FMath::Clamp(GimbalPitch + 5.f, -90.f, 20.f);
	if (Name == TEXT("GimbalDown")) GimbalPitch = FMath::Clamp(GimbalPitch - 5.f, -90.f, 20.f);
	OnAction.Broadcast(Name);
}

bool ASmvPilotPawn::IsPlaneLike() const
{
	return VType == TEXT("plane") || VType == TEXT("sabit_kanat");
}

static float SmvExpo(float V, float E) { return V * (1.f - E) + V * V * V * E; }
static int32 SmvPwm(float V) { return FMath::RoundToInt(FMath::Clamp(1500.f + V * 500.f, 1000.f, 2000.f)); }

void ASmvPilotPawn::ComputeRc(float Dt)
{
	float Roll = 0, Pitch = 0, Thr = 0, Yaw = 0;
	int32 ModePwm = ModeSwitch;
	if (InputDevice == TEXT("rc") && bRcAxisSeen)
	{
		auto Ax = [this](int32 Slot, int32 Def) -> float
		{
			const int32 A = RcAxisMap.IsValidIndex(Slot) && RcAxisMap[Slot] > 0 ? RcAxisMap[Slot] : Def;
			float V = (A >= 1 && A <= 8) ? RcAxis[A - 1] : 0.f;
			if (RcInvert.IsValidIndex(Slot) && RcInvert[Slot]) V = -V;
			return FMath::Clamp(V, -1.f, 1.f);
		};
		Roll = Ax(0, 1); Pitch = Ax(1, 2); Thr = Ax(2, 3); Yaw = Ax(3, 4);
		const float M = Ax(4, 5);
		if (RcAxisMap.IsValidIndex(4) && RcAxisMap[4] > 0) ModePwm = M < -0.33f ? 1000 : M > 0.33f ? 2000 : 1500;
	}
	else
	{
		const FVector2D L = bStickMode2 ? LeftStick : FVector2D(LeftStick.X, RightStick.Y);
		const FVector2D R = bStickMode2 ? RightStick : FVector2D(RightStick.X, LeftStick.Y);
		Yaw = L.X; Roll = R.X; Pitch = R.Y;
		const bool bKeyboard = InputDevice == TEXT("keyboard");
		if (bKeyboard && IsPlaneLike())
		{
			// uçakta klavye gazı kalıcı: W/S ile 0,6/sn hızında değişir
			KeyThrottle = FMath::Clamp(KeyThrottle + L.Y * 0.6f * Dt, -1.f, 1.f);
			Thr = KeyThrottle;
		}
		else Thr = L.Y;   // kopter: yaylı (orta = irtifa tut)
		Roll = SmvExpo(Roll, Expo); Pitch = SmvExpo(Pitch, Expo); Yaw = SmvExpo(Yaw, Expo);
	}
	Rc.SetNum(8);
	if (VType == TEXT("rover"))
	{
		// rover: [RC1 direksiyon, 0, RC3 gaz (1500 = dur), 0, ch5, 0, 0, 0]
		Rc = { SmvPwm(Yaw + Roll), 0, SmvPwm(Thr + Pitch), 0, ModePwm, 0, 0, 0 };
		return;
	}
	Rc = { SmvPwm(Roll), SmvPwm(-Pitch), SmvPwm(Thr), SmvPwm(Yaw), ModePwm, 0, 0, 0 };
}

void ASmvPilotPawn::Tick(float Dt)
{
	Super::Tick(Dt);
	ComputeRc(Dt);
	const double Now = FPlatformTime::Seconds();
	if (bFlying && Now - LastRcSent >= 0.04)
	{
		LastRcSent = Now;
		OnRc.Broadcast(Rc);
	}
	UpdateCamera(Dt);
	Look = FVector2D::ZeroVector;
}

// ---------------------------------------------------------------------------
// kamera
// ---------------------------------------------------------------------------

void ASmvPilotPawn::SetTarget(ACaySimDroneActor* InTarget, const FString& VehicleType)
{
	Target = InTarget;
	VType = VehicleType;
	bGroundPlaced = false;
	KeyThrottle = IsPlaneLike() ? -1.f : 0.f;
}

void ASmvPilotPawn::SetMode(ESmvCamMode M)
{
	Mode = M;
	Camera->SetFieldOfView(GetFov());
	if (M == ESmvCamMode::Ground) bGroundPlaced = false;
}

void ASmvPilotPawn::CycleMode()
{
	SetMode((ESmvCamMode)(((uint8)Mode + 1) % 7));
}

float ASmvPilotPawn::GetFov() const
{
	switch (Mode)
	{
	case ESmvCamMode::Fpv: return 110.f;
	case ESmvCamMode::Gimbal: return 60.f;
	case ESmvCamMode::Ground: return 45.f;
	default: return 75.f;
	}
}

void ASmvPilotPawn::SetCamType(ESmvCamType T)
{
	CamType = T;
	ApplyCamTypePost();
}

void ASmvPilotPawn::ApplyCamTypePost()
{
	// Gece / termal: içerik malzemesi olmadan son işlem ayarlarıyla yaklaşık görünüm (tam termal malzeme planlandı — docs/ozellik-esligi.md)
	FPostProcessSettings P;
	if (CamType == ESmvCamType::Night)
	{
		P.bOverride_ColorSaturation = true; P.ColorSaturation = FVector4(0, 0, 0, 1);
		P.bOverride_ColorGain = true; P.ColorGain = FVector4(0.35f, 1.f, 0.4f, 1.f);
		P.bOverride_AutoExposureBias = true; P.AutoExposureBias = 3.f;
		P.bOverride_FilmGrainIntensity = true; P.FilmGrainIntensity = 0.6f;
		P.bOverride_VignetteIntensity = true; P.VignetteIntensity = 0.8f;
	}
	else if (CamType == ESmvCamType::Thermal)
	{
		P.bOverride_ColorSaturation = true; P.ColorSaturation = FVector4(0, 0, 0, 1);
		P.bOverride_ColorContrast = true; P.ColorContrast = FVector4(1.8f, 1.8f, 1.8f, 1.f);
		P.bOverride_ColorGamma = true; P.ColorGamma = FVector4(0.7f, 0.7f, 0.7f, 1.f);
		P.bOverride_FilmGrainIntensity = true; P.FilmGrainIntensity = 0.3f;
	}
	Camera->PostProcessSettings = (Mode == ESmvCamMode::Gimbal || Mode == ESmvCamMode::Fpv) ? P : FPostProcessSettings();
	Camera->PostProcessBlendWeight = 1.f;
	PipCapture->PostProcessSettings = P;
	PipCapture->PostProcessBlendWeight = 1.f;
}

void ASmvPilotPawn::UpdateCamera(float Dt)
{
	ACaySimDroneActor* D = Target.Get();
	if (!D) return;
	const FVector Pos = D->GetActorLocation();
	const FRotator Body = D->GetActorRotation();
	FVector CamPos = GetActorLocation();
	FRotator CamRot = GetActorRotation();
	switch (Mode)
	{
	case ESmvCamMode::Chase:
	{
		const FRotator YawOnly(0.f, Body.Yaw, 0.f);
		const FVector Want = Pos - YawOnly.Vector() * 1200.f * ChaseZoom + FVector(0, 0, 350.f * ChaseZoom);
		CamPos = FMath::VInterpTo(CamPos, Want, Dt, 5.f);
		CamRot = (Pos + FVector(0, 0, 80.f) - CamPos).Rotation();
		break;
	}
	case ESmvCamMode::Fpv:
		CamPos = Pos + Body.RotateVector(FVector(30.f, 0, 8.f));
		CamRot = (FQuat(Body) * FQuat(FRotator(IsPlaneLike() ? 0.f : 20.f, 0, 0))).Rotator();   // FPV kamera eğimi (uptilt)
		break;
	case ESmvCamMode::Gimbal:
		CamPos = Pos + FVector(0, 0, -20.f);
		CamRot = FRotator(GimbalPitch, Body.Yaw, 0.f);   // stabilize gimbal: roll/pitch'ten bağımsız
		break;
	case ESmvCamMode::Orbit:
		OrbitYaw += Look.X * 0.3f;
		OrbitPitch = FMath::Clamp(OrbitPitch + Look.Y * 0.3f, -85.f, 10.f);
		CamPos = Pos - FRotator(OrbitPitch, OrbitYaw, 0).Vector() * OrbitDist * 100.f;
		CamRot = (Pos - CamPos).Rotation();
		break;
	case ESmvCamMode::Ground:
		if (!bGroundPlaced) { GroundPos = Pos + FVector(-1500.f, 600.f, 170.f - 0.f); bGroundPlaced = true; }
		CamPos = GroundPos;
		CamRot = (Pos - CamPos).Rotation();
		Camera->SetFieldOfView(FMath::Clamp(FMath::RadiansToDegrees(2.f * FMath::Atan(800.f / FMath::Max(1.f, (float)FVector::Dist(Pos, CamPos)))), 5.f, 60.f));
		break;
	case ESmvCamMode::Free:
		FreeYaw += Look.X * 0.3f;
		FreePitch = FMath::Clamp(FreePitch + Look.Y * 0.3f, -89.f, 89.f);
		CamRot = FRotator(FreePitch, FreeYaw, 0.f);
		CamPos += (CamRot.Vector() * RightStick.Y + FRotator(0, FreeYaw + 90.f, 0).Vector() * RightStick.X + FVector(0, 0, LeftStick.Y)) * 3000.f * Dt;
		break;
	case ESmvCamMode::Top:
		CamPos = Pos + FVector(0, 0, 8000.f * ChaseZoom);
		CamRot = FRotator(-90.f, 0.f, 0.f);
		break;
	}
	SetActorLocationAndRotation(CamPos, CamRot);
	if (bPip)
	{
		// PiP = gimbal kamerası
		PipCapture->SetWorldLocationAndRotation(Pos + FVector(0, 0, -20.f), FRotator(GimbalPitch, Body.Yaw, 0.f));
		PipCapture->CaptureScene();
	}
}
