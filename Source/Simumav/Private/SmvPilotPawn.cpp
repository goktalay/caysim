#include "SmvPilotPawn.h"
#include "SimumavModule.h"
#include "DroneActor.h"
#include "SmvWorldActors.h"
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
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "SmvUiPrefs.h"
#include "SmvI18n.h"

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
	// kullanıcı tuş ataması (Ayarlar → Tuşlar): varsayılan tuş yerine atanmış olanı
	const FKey Bound = FSmvUiPrefs::Get().Resolve(Action, Key);
	if (!Bound.IsValid()) return;
	FEnhancedActionKeyMapping& M = Imc->MapKey(A, Bound);
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
	MakeAction(TEXT("Left"), 2);    // gamepad sol çubuk (klavye çubukları ComputeRc'de tuş durumundan)
	MakeAction(TEXT("Right"), 2);   // gamepad sağ çubuk
	MakeAction(TEXT("Look"), 2);
	MakeAction(TEXT("Zoom"), 1);
	for (int32 i = 0; i < 8; ++i) MakeAction(*FString::Printf(TEXT("RcAxis%d"), i + 1), 1);
	const FName Btn[] = { TEXT("Arm"), TEXT("ArmToggle"), TEXT("Takeoff"), TEXT("Rtl"), TEXT("Land"), TEXT("Lock"), TEXT("Ack"), TEXT("Pause"), TEXT("Reset"),
		TEXT("CamCycle"), TEXT("CamType"), TEXT("Hud"), TEXT("Pip"), TEXT("Panel"), TEXT("Sticks"), TEXT("ModeMenu"), TEXT("SpeedUp"), TEXT("SpeedDown"),
		TEXT("Mode1"), TEXT("Mode2"), TEXT("Mode3"), TEXT("MotorCut"), TEXT("GimbalUp"), TEXT("GimbalDown"),
		TEXT("Cam1"), TEXT("Cam2"), TEXT("Cam3"), TEXT("Cam4"), TEXT("Cam5"), TEXT("Cam6"), TEXT("Cam7"), TEXT("Ch5Up"), TEXT("Ch5Down"), TEXT("ThrCenter"),
		TEXT("Help"), TEXT("Cockpit") };
	for (const FName& B : Btn) MakeAction(B, 0);
	MapAllKeys();
}

void ASmvPilotPawn::RemapKeys()
{
	if (!Imc) return;
	Imc->UnmapAll();
	MapAllKeys();
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (ULocalPlayer* LP = PC->GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* Sub = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				Sub->RequestRebuildControlMappings();
			}
		}
	}
}

void ASmvPilotPawn::MapAllKeys()
{

	// gamepad (XInput): sol çubuk gaz/yaw, sağ çubuk pitch/roll
	MapKey(TEXT("Left"), EKeys::Gamepad_Left2D);
	MapKey(TEXT("Right"), EKeys::Gamepad_Right2D);
	// fare: sürükle = kamera bakışı (takip/yörünge/serbest), tekerlek yakınlaştırma
	MapKey(TEXT("Look"), EKeys::Mouse2D);
	MapKey(TEXT("Zoom"), EKeys::MouseWheelAxis);
	// RC verici (RawInput eklentisi; Ayarlar → Kumanda'dan eksen eşlemesi)
	for (int32 i = 0; i < 8; ++i) MapKey(*FString::Printf(TEXT("RcAxis%d"), i + 1), FKey(*FString::Printf(TEXT("GenericUSBController_Axis%d"), i + 1)));

	// klavye: W/S/A/D + oklar çubukları sürer (ComputeRc); Q/E ch5, Boşluk gaz orta (web virtualRC)
	MapKey(TEXT("Ch5Down"), EKeys::Q);
	MapKey(TEXT("Ch5Up"), EKeys::E);
	MapKey(TEXT("ThrCenter"), EKeys::SpaceBar);
	MapKey(TEXT("ArmToggle"), EKeys::X);         MapKey(TEXT("Arm"), EKeys::Gamepad_FaceButton_Bottom);
	MapKey(TEXT("Takeoff"), EKeys::T);           MapKey(TEXT("Takeoff"), EKeys::Gamepad_FaceButton_Left);
	MapKey(TEXT("Rtl"), EKeys::R);               MapKey(TEXT("Rtl"), EKeys::Gamepad_FaceButton_Top);
	MapKey(TEXT("Land"), EKeys::L);
	MapKey(TEXT("ModeMenu"), EKeys::F);          MapKey(TEXT("ModeMenu"), EKeys::Gamepad_Special_Left);
	MapKey(TEXT("Help"), EKeys::F1);
	MapKey(TEXT("Cockpit"), EKeys::G);
	MapKey(TEXT("Lock"), EKeys::Tab);            MapKey(TEXT("Lock"), EKeys::Gamepad_RightTrigger);
	MapKey(TEXT("Ack"), EKeys::B);               MapKey(TEXT("Ack"), EKeys::Gamepad_FaceButton_Right);
	MapKey(TEXT("Pause"), EKeys::Escape);        MapKey(TEXT("Pause"), EKeys::Gamepad_Special_Right);
	MapKey(TEXT("Reset"), EKeys::BackSpace);
	MapKey(TEXT("CamCycle"), EKeys::C);          MapKey(TEXT("CamCycle"), EKeys::Gamepad_RightShoulder);
	MapKey(TEXT("Cam1"), EKeys::One);
	MapKey(TEXT("Cam2"), EKeys::Two);
	MapKey(TEXT("Cam3"), EKeys::Three);
	MapKey(TEXT("Cam4"), EKeys::Four);
	MapKey(TEXT("Cam5"), EKeys::Five);
	MapKey(TEXT("Cam6"), EKeys::Six);
	MapKey(TEXT("Cam7"), EKeys::Seven);
	MapKey(TEXT("CamType"), EKeys::N);
	MapKey(TEXT("Hud"), EKeys::H);
	MapKey(TEXT("Panel"), EKeys::P);
	MapKey(TEXT("Pip"), EKeys::I);               MapKey(TEXT("Pip"), EKeys::Gamepad_LeftShoulder);
	MapKey(TEXT("Sticks"), EKeys::V);
	MapKey(TEXT("SpeedUp"), EKeys::Add);         MapKey(TEXT("SpeedUp"), EKeys::Equals);
	MapKey(TEXT("SpeedDown"), EKeys::Subtract);  MapKey(TEXT("SpeedDown"), EKeys::Hyphen);
	MapKey(TEXT("Mode1"), EKeys::Gamepad_DPad_Left);
	MapKey(TEXT("Mode2"), EKeys::Gamepad_DPad_Up);
	MapKey(TEXT("Mode3"), EKeys::Gamepad_DPad_Right);
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
	else ChaseZoom = FMath::Clamp(ChaseZoom * (1.f - 0.1f * Z), 0.35f, 6.f);
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
	static const FName CamKeys[] = { TEXT("Cam1"), TEXT("Cam2"), TEXT("Cam3"), TEXT("Cam4"), TEXT("Cam5"), TEXT("Cam6"), TEXT("Cam7") };
	static const ESmvCamMode CamModes[] = { ESmvCamMode::Chase, ESmvCamMode::Fpv, ESmvCamMode::Gimbal, ESmvCamMode::Free, ESmvCamMode::Ground, ESmvCamMode::Orbit, ESmvCamMode::Top };
	for (int32 i = 0; i < 7; ++i) if (Name == CamKeys[i]) { SetMode(CamModes[i]); OnAction.Broadcast(TEXT("CamCycle")); return; }
	if (Name == TEXT("CamType")) { SetCamType((ESmvCamType)(((uint8)CamType + 1) % 3)); }
	if (Name == TEXT("Hud")) bHud = !bHud;
	if (Name == TEXT("Pip")) bPip = !bPip;
	if (Name == TEXT("Mode1")) ModeSwitch = 1000;
	if (Name == TEXT("Mode2")) ModeSwitch = 1500;
	if (Name == TEXT("Mode3")) ModeSwitch = 2000;
	if (Name == TEXT("Ch5Up")) ModeSwitch = FMath::Clamp(ModeSwitch + 500, 1000, 2000);
	if (Name == TEXT("Ch5Down")) ModeSwitch = FMath::Clamp(ModeSwitch - 500, 1000, 2000);
	if (Name == TEXT("ThrCenter")) { AxThr = 0.f; bThrUsed = true; if (VType == TEXT("rover")) AxRoll = 0.f; }
	if (Name == TEXT("GimbalUp")) GimbalPitch = FMath::Clamp(GimbalPitch + 5.f, -90.f, 20.f);
	if (Name == TEXT("GimbalDown")) GimbalPitch = FMath::Clamp(GimbalPitch - 5.f, -90.f, 20.f);
	OnAction.Broadcast(Name);
}

bool ASmvPilotPawn::IsPlaneLike() const
{
	return VType == TEXT("plane") || VType == TEXT("sabit_kanat") || VType == TEXT("vtol") || VType == TEXT("interceptor");
}

bool ASmvPilotPawn::KeyDown(const FKey& K) const
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	return PC && PC->IsInputKeyDown(K);
}

void ASmvPilotPawn::SetVirtualStick(int32 Side, const FVector2D& V, bool bActive)
{
	if (Side < 0 || Side > 1) return;
	VStick[Side] = FVector2D(FMath::Clamp(V.X, -1.0, 1.0), FMath::Clamp(V.Y, -1.0, 1.0));
	bVStick[Side] = bActive;
}

static int32 SmvPwm(float V) { return FMath::RoundToInt(FMath::Clamp(1500.f + V * 500.f, 1000.f, 2000.f)); }

void ASmvPilotPawn::ComputeRc(float Dt)
{
	Dt = FMath::Min(Dt, 0.2f);
	float Roll = 0, Pitch = 0, Thr = 0, Yaw = 0;
	int32 ModePwm = ModeSwitch;
	const bool bRover = VType == TEXT("rover");
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
		AxRoll = Roll; AxPitch = Pitch; AxThr = Thr; AxYaw = Yaw; bThrUsed = true;
		{
			// eksen sırası: 0 roll, 1 pitch, 2 gaz, 3 yaw; mod eşlemesi [solX, solY, sağX, sağY]
			static const int32 Map[5][4] = { { 3, 2, 0, 1 }, { 3, 1, 0, 2 }, { 3, 2, 0, 1 }, { 0, 1, 3, 2 }, { 0, 2, 3, 1 } };
			const int32* M = Map[FMath::Clamp(StickMode, 0, 4)];
			const float V[4] = { Roll, Pitch, Thr, Yaw };
			StickL = FVector2D(V[M[0]], V[M[1]]);
			StickR = FVector2D(V[M[2]], V[M[3]]);
		}
	}
	else
	{
		// klavye: serbest kamerada WASD/oklar kamerayı sürer (araca gitmez)
		const bool bKeys = bFlying && Mode != ESmvCamMode::Free;
		auto Kx = [this, bKeys](const FKey& Neg, const FKey& Pos) { return bKeys ? (KeyDown(Pos) ? 1.f : 0.f) - (KeyDown(Neg) ? 1.f : 0.f) : 0.f; };
		const float KLx = Kx(EKeys::A, EKeys::D), KLy = Kx(EKeys::S, EKeys::W), KRx = Kx(EKeys::Left, EKeys::Right), KRy = Kx(EKeys::Down, EKeys::Up);
		// mutlak kaynak (sanal çubuk > gamepad): o tarafın eksenlerini yazar; radyal menü açıkken sağ çubuk menüye gider
		const bool bPadL = LeftStick.SizeSquared() > 0.0025, bPadR = !bSuppressRightPad && RightStick.SizeSquared() > 0.0025;
		const bool bAbsL = bVStick[0] || bPadL, bAbsR = bVStick[1] || bPadR;
		const FVector2D AbsL = bVStick[0] ? VStick[0] : LeftStick, AbsR = bVStick[1] ? VStick[1] : RightStick;
		const float KeyRate = 3.5f, Decay = FMath::Exp(-9.f * Dt);
		auto Spring = [KeyRate, Decay, Dt](float& A, float Dir)
		{
			if (Dir != 0.f) A = FMath::Clamp(A + Dir * KeyRate * Dt, -1.f, 1.f);
			else { A *= Decay; if (FMath::Abs(A) < 0.004f) A = 0.f; }
		};
		if (bRover)
		{
			// kara aracı: harf ve ok tuşları aynı ekseni sürer; iki eksen de yaylı (bırakınca dur / düz)
			const float Steer = FMath::Clamp(KLx + KRx, -1.f, 1.f), Gas = FMath::Clamp(KLy + KRy, -1.f, 1.f);
			if (bAbsR) AxRoll = (float)AbsR.X; else Spring(AxRoll, Steer);
			if (bAbsL) AxThr = (float)AbsL.Y; else Spring(AxThr, Gas);
			StickL = FVector2D(0, AxThr); StickR = FVector2D(AxRoll, 0);
			Rc.SetNum(8);
			Rc = { SmvPwm(AxRoll), 0, SmvPwm(AxThr), 0, ModePwm, 0, 0, 0 };
			return;
		}
		// kumanda modu (1–4): yuva [solX, solY, sağX, sağY] → eksen (0 roll, 1 pitch, 2 gaz, 3 yaw)
		static const int32 Map[5][4] = { { 3, 2, 0, 1 }, { 3, 1, 0, 2 }, { 3, 2, 0, 1 }, { 0, 1, 3, 2 }, { 0, 2, 3, 1 } };
		const int32* M = Map[FMath::Clamp(StickMode, 0, 4)];
		float* Ax[4] = { &AxRoll, &AxPitch, &AxThr, &AxYaw };
		const float KeySlot[4] = { KLx, KLy, KRx, KRy };
		const bool AbsSlot[4] = { bAbsL, bAbsL, bAbsR, bAbsR };
		const float AbsVal[4] = { (float)AbsL.X, (float)AbsL.Y, (float)AbsR.X, (float)AbsR.Y };
		const bool VirtSlot[4] = { bVStick[0], bVStick[0], bVStick[1], bVStick[1] };
		int32 ThrSlot = 1;
		for (int32 k = 0; k < 4; ++k)
		{
			if (M[k] == 2) { ThrSlot = k; continue; }   // gaz aşağıda (uçakta tutmalı)
			if (AbsSlot[k]) *Ax[M[k]] = AbsVal[k]; else Spring(*Ax[M[k]], KeySlot[k]);
		}
		const float KThr = KeySlot[ThrSlot];
		const bool bAbsThr = AbsSlot[ThrSlot];
		const float AbsThr = AbsVal[ThrSlot];
		const bool bThrVirtual = VirtSlot[ThrSlot];
		if (IsPlaneLike())
		{
			// uçak: tutmalı gaz (W/S ve gamepad kolu hızla kaydırır; sanal çubuk mutlak)
			if (bThrVirtual) AxThr = AbsThr;
			else if (bAbsThr) AxThr = FMath::Clamp(AxThr + AbsThr * 0.7f * Dt, -1.f, 1.f);
			else if (KThr != 0.f) AxThr = FMath::Clamp(AxThr + KThr * 0.7f * Dt, -1.f, 1.f);
		}
		else
		{
			if (bAbsThr) AxThr = AbsThr; else Spring(AxThr, KThr);
		}
		if (KThr != 0.f || (bAbsThr && FMath::Abs(AbsThr) > 0.05f)) bThrUsed = true;
		StickL = FVector2D(*Ax[M[0]], *Ax[M[1]]);
		StickR = FVector2D(*Ax[M[2]], *Ax[M[3]]);
		// hassasiyet (Ayarlar → Kumanda: ölü bölge / expo / oran; hazır ayar yoksa tek expo)
		const FSmvUiPrefs& Pf = FSmvUiPrefs::Get();
		Roll = Pf.Shape(AxRoll, 0, Expo); Pitch = Pf.Shape(AxPitch, 1, Expo); Yaw = Pf.Shape(AxYaw, 3, Expo); Thr = AxThr;
	}
	Rc.SetNum(8);
	if (bRover)
	{
		// rover (RC verici): [RC1 direksiyon, 0, RC3 gaz (1500 = dur), 0, ch5, 0, 0, 0]
		Rc = { SmvPwm(Yaw + Roll), 0, SmvPwm(Thr + Pitch), 0, ModePwm, 0, 0, 0 };
		return;
	}
	Rc = { SmvPwm(Roll), SmvPwm(-Pitch), bThrUsed ? SmvPwm(Thr) : 0, SmvPwm(Yaw), ModePwm, 0, 0, 0 };
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
	// imleç görünür (alt bant düğmeleri): bakış yalnız fare tuşu basılıyken (sürükle) — imleci gezdirmek kamerayı döndürmesin
	if (!KeyDown(EKeys::LeftMouseButton) && !KeyDown(EKeys::RightMouseButton)) Look = FVector2D::ZeroVector;
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
	bChaseInit = false;
	ChaseYawOff = 0.f; ChasePitch = -14.f; ChaseZoom = 1.f;
	AxRoll = AxPitch = AxYaw = 0.f;
	AxThr = (IsPlaneLike() && VType != TEXT("rover")) ? -1.f : 0.f;
	// kopter: ch3 ilk gaz girdisine dek 0 (dokunma) — web lazyThr
	bThrUsed = IsPlaneLike() || VType == TEXT("rover");
	bVStick[0] = bVStick[1] = false;
	if (const ASmvDroneActor* D = Cast<ASmvDroneActor>(InTarget))
	{
		VisRadius = D->GetVisualRadiusM();
		VisNose = D->GetVisualNoseM();
	}
	else { VisRadius = 1.f; VisNose = 0.5f; }
	OrbitDist = FMath::Max(6.f, VisRadius * 7.f);
}

FString ASmvPilotPawn::ModeLabel(ESmvCamMode M)
{
	switch (M)
	{
	case ESmvCamMode::Chase: return FSmvI18n::T(TEXT("cammode.chase")).ToString();
	case ESmvCamMode::Fpv: return TEXT("FPV");
	case ESmvCamMode::Gimbal: return TEXT("Gimbal");
	case ESmvCamMode::Free: return FSmvI18n::T(TEXT("cammode.free")).ToString();
	case ESmvCamMode::Ground: return FSmvI18n::T(TEXT("cammode.ground")).ToString();
	case ESmvCamMode::Orbit: return FSmvI18n::T(TEXT("cammode.orbit")).ToString();
	case ESmvCamMode::Top: return FSmvI18n::T(TEXT("cammode.top")).ToString();
	default: return TEXT("?");
	}
}

void ASmvPilotPawn::SetMode(ESmvCamMode M)
{
	Mode = M;
	Camera->SetFieldOfView(GetFov());
	if (M == ESmvCamMode::Ground) bGroundPlaced = false;
	if (M == ESmvCamMode::Free) { FreeYaw = GetActorRotation().Yaw; FreePitch = GetActorRotation().Pitch; }
	if (M == ESmvCamMode::Chase) bChaseInit = false;
	ApplyCamTypePost();
}

void ASmvPilotPawn::CycleMode()
{
	// web sırası: Takip → FPV → Gimbal → Serbest → Yer → (Yörünge → Tepeden)
	static const ESmvCamMode Order[] = { ESmvCamMode::Chase, ESmvCamMode::Fpv, ESmvCamMode::Gimbal, ESmvCamMode::Free, ESmvCamMode::Ground, ESmvCamMode::Orbit, ESmvCamMode::Top };
	int32 Cur = 0;
	for (int32 i = 0; i < 7; ++i) if (Order[i] == Mode) Cur = i;
	SetMode(Order[(Cur + 1) % 7]);
}

float ASmvPilotPawn::GetFov() const
{
	switch (Mode)
	{
	case ESmvCamMode::Fpv: return 110.f;
	case ESmvCamMode::Gimbal: return 60.f;
	case ESmvCamMode::Ground: return 45.f;
	default: return 70.f;
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
	Dt = FMath::Min(Dt, 0.1f);
	const FVector Pos = D->GetActorLocation();
	const FRotator Body = D->GetActorRotation();
	FVector CamPos = GetActorLocation();
	FRotator CamRot = GetActorRotation();
	UWorld* W = GetWorld();
	FCollisionQueryParams Q(SCENE_QUERY_STAT(SmvCam), false, D);
	Q.AddIgnoredActor(this);
	const float R = FMath::Max(0.3f, VisRadius);
	switch (Mode)
	{
	case ESmvCamMode::Chase:
	{
		// arkadan-üstten takip: gövde yönü yumuşatılır (yaw ~0,3 sn), konum dronun çizilen konumuna kilitli (gecikme yok → titreme yok);
		// fare sürükle bakış açısını, tekerlek mesafeyi değiştirir
		ChaseYawOff += (float)Look.X * 0.25f;
		ChasePitch = FMath::Clamp(ChasePitch - (float)Look.Y * 0.25f, -75.f, 15.f);
		if (!bChaseInit) { ChaseYawSm = Body.Yaw; bChaseInit = true; }
		const float dYaw = FRotator::NormalizeAxis(Body.Yaw - ChaseYawSm);
		ChaseYawSm = FRotator::NormalizeAxis(ChaseYawSm + dYaw * FMath::Clamp(Dt * 3.5f, 0.f, 1.f));
		const float Dist = (2.8f + 3.6f * R) * ChaseZoom * 100.f;
		const FVector Off = FRotator(ChasePitch, ChaseYawSm + ChaseYawOff, 0.f).Vector() * -Dist;
		ChaseOffSm = ChaseOffSm.IsNearlyZero() ? Off : FMath::VInterpTo(ChaseOffSm, Off, Dt, 8.f);
		const FVector Aim = Pos + FVector(0, 0, 25.f * R);
		CamPos = Aim + ChaseOffSm;
		// kamera yerin / binanın içine girmesin
		FHitResult Hit;
		if (W && W->LineTraceSingleByChannel(Hit, Aim, CamPos, ECC_Visibility, Q)) CamPos = Hit.Location + Hit.ImpactNormal * 40.f;
		CamRot = (Aim - CamPos).Rotation();
		break;
	}
	case ESmvCamMode::Fpv:
		CamPos = Pos + Body.RotateVector(FVector(VisNose * 100.f, 0, 6.f * R));
		CamRot = (FQuat(Body) * FQuat(FRotator(IsPlaneLike() ? 0.f : 20.f, 0, 0))).Rotator();   // FPV kamera eğimi (uptilt)
		break;
	case ESmvCamMode::Gimbal:
		CamPos = Pos + FVector(0, 0, -12.f * R);
		CamRot = FRotator(GimbalPitch, Body.Yaw, 0.f);   // stabilize gimbal: roll/pitch'ten bağımsız
		break;
	case ESmvCamMode::Orbit:
		OrbitYaw += (float)Look.X * 0.3f;
		OrbitPitch = FMath::Clamp(OrbitPitch - (float)Look.Y * 0.3f, -85.f, 10.f);
		CamPos = Pos - FRotator(OrbitPitch, OrbitYaw, 0).Vector() * OrbitDist * 100.f;
		CamRot = (Pos - CamPos).Rotation();
		break;
	case ESmvCamMode::Ground:
		if (!bGroundPlaced)
		{
			// pilot gözü: aracın 15 m gerisinde-yanında, zeminde 1,7 m
			const FVector Back = FRotator(0.f, Body.Yaw, 0.f).Vector();
			GroundPos = Pos - Back * 1500.f + FRotator(0.f, Body.Yaw + 90.f, 0.f).Vector() * 600.f;
			FHitResult Hit;
			if (W && W->LineTraceSingleByChannel(Hit, GroundPos + FVector(0, 0, 50000.f), GroundPos - FVector(0, 0, 200000.f), ECC_Visibility, Q)) GroundPos.Z = Hit.ImpactPoint.Z + 170.f;
			else GroundPos.Z = Pos.Z + 170.f;
			bGroundPlaced = true;
		}
		CamPos = GroundPos;
		CamRot = (Pos - CamPos).Rotation();
		Camera->SetFieldOfView(FMath::Clamp(FMath::RadiansToDegrees(2.f * FMath::Atan(600.f * R / FMath::Max(1.f, (float)FVector::Dist(Pos, CamPos)))), 5.f, 60.f));
		break;
	case ESmvCamMode::Free:
	{
		FreeYaw += (float)Look.X * 0.3f;
		FreePitch = FMath::Clamp(FreePitch + (float)Look.Y * 0.3f, -89.f, 89.f);
		CamRot = FRotator(FreePitch, FreeYaw, 0.f);
		// serbest kamera: WASD yatay, oklar ↑↓ yükseklik, ←→ dönüş; Shift hızlı
		auto Ax = [this](const FKey& Neg, const FKey& P) { return (KeyDown(P) ? 1.f : 0.f) - (KeyDown(Neg) ? 1.f : 0.f); };
		const float Fw = Ax(EKeys::S, EKeys::W) + (float)RightStick.Y, Rt = Ax(EKeys::A, EKeys::D) + (float)RightStick.X;
		const float Up = Ax(EKeys::Down, EKeys::Up) + (float)LeftStick.Y;
		FreeYaw += Ax(EKeys::Left, EKeys::Right) * 60.f * Dt;
		const float Spd = (KeyDown(EKeys::LeftShift) ? 9000.f : 2500.f);
		CamPos += (CamRot.Vector() * Fw + FRotator(0, FreeYaw + 90.f, 0).Vector() * Rt + FVector(0, 0, Up)) * Spd * Dt;
		break;
	}
	case ESmvCamMode::Top:
		CamPos = Pos + FVector(0, 0, 8000.f * ChaseZoom);
		CamRot = FRotator(-90.f, 0.f, 0.f);
		break;
	}
	SetActorLocationAndRotation(CamPos, CamRot);
	if (bPip)
	{
		// PiP = gimbal kamerası
		PipCapture->SetWorldLocationAndRotation(Pos + FVector(0, 0, -12.f * R), FRotator(GimbalPitch, Body.Yaw, 0.f));
		PipCapture->CaptureScene();
	}
}
