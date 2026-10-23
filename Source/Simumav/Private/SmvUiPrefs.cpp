#include "SmvUiPrefs.h"
#include "Misc/ConfigCacheIni.h"
#include "InputCoreTypes.h"

namespace
{
	const TCHAR* GSmvPrefsSection = TEXT("/Script/Simumav.SmvUiPrefs");
}

FSmvUiPrefs& FSmvUiPrefs::Get()
{
	static FSmvUiPrefs P;
	if (!P.bLoaded) P.Load();
	return P;
}

void FSmvUiPrefs::Load()
{
	bLoaded = true;
	if (!GConfig) return;
	const FString& Ini = GGameUserSettingsIni;
	GConfig->GetFloat(GSmvPrefsSection, TEXT("VolMotor"), VolMotor, Ini);
	GConfig->GetFloat(GSmvPrefsSection, TEXT("VolAlerts"), VolAlerts, Ini);
	GConfig->GetFloat(GSmvPrefsSection, TEXT("VolUi"), VolUi, Ini);
	GConfig->GetFloat(GSmvPrefsSection, TEXT("VolMusic"), VolMusic, Ini);
	GConfig->GetBool(GSmvPrefsSection, TEXT("MenuMusic"), bMenuMusic, Ini);
	GConfig->GetBool(GSmvPrefsSection, TEXT("TutorialDone"), bTutorialDone, Ini);
	GConfig->GetBool(GSmvPrefsSection, TEXT("FlightTipsDone"), bFlightTipsDone, Ini);
	GConfig->GetBool(GSmvPrefsSection, TEXT("Cockpit"), bCockpit, Ini);
	GConfig->GetBool(GSmvPrefsSection, TEXT("MenuScene"), bMenuScene, Ini);
	GConfig->GetBool(GSmvPrefsSection, TEXT("Preview3d"), bPreview3d, Ini);
	GConfig->GetString(GSmvPrefsSection, TEXT("Season"), Season, Ini);
	TArray<FString> Lines;
	GConfig->GetArray(GSmvPrefsSection, TEXT("Bind"), Lines, Ini);
	for (const FString& L : Lines)
	{
		FString K, V;
		if (L.Split(TEXT("="), &K, &V) && !K.IsEmpty() && !V.IsEmpty()) Binds.Add(K, V);
	}
	VolMotor = FMath::Clamp(VolMotor, 0.f, 1.f);
	VolAlerts = FMath::Clamp(VolAlerts, 0.f, 1.f);
	VolUi = FMath::Clamp(VolUi, 0.f, 1.f);
	VolMusic = FMath::Clamp(VolMusic, 0.f, 1.f);
}

void FSmvUiPrefs::Save()
{
	if (!GConfig) return;
	const FString& Ini = GGameUserSettingsIni;
	GConfig->SetFloat(GSmvPrefsSection, TEXT("VolMotor"), VolMotor, Ini);
	GConfig->SetFloat(GSmvPrefsSection, TEXT("VolAlerts"), VolAlerts, Ini);
	GConfig->SetFloat(GSmvPrefsSection, TEXT("VolUi"), VolUi, Ini);
	GConfig->SetFloat(GSmvPrefsSection, TEXT("VolMusic"), VolMusic, Ini);
	GConfig->SetBool(GSmvPrefsSection, TEXT("MenuMusic"), bMenuMusic, Ini);
	GConfig->SetBool(GSmvPrefsSection, TEXT("TutorialDone"), bTutorialDone, Ini);
	GConfig->SetBool(GSmvPrefsSection, TEXT("FlightTipsDone"), bFlightTipsDone, Ini);
	GConfig->SetBool(GSmvPrefsSection, TEXT("Cockpit"), bCockpit, Ini);
	GConfig->SetBool(GSmvPrefsSection, TEXT("MenuScene"), bMenuScene, Ini);
	GConfig->SetBool(GSmvPrefsSection, TEXT("Preview3d"), bPreview3d, Ini);
	GConfig->SetString(GSmvPrefsSection, TEXT("Season"), *Season, Ini);
	TArray<FString> Lines;
	for (const TPair<FString, FString>& B : Binds) Lines.Add(B.Key + TEXT("=") + B.Value);
	GConfig->SetArray(GSmvPrefsSection, TEXT("Bind"), Lines, Ini);
	GConfig->Flush(false, Ini);
}

const TArray<FSmvBindDef>& FSmvUiPrefs::Bindable()
{
	static const TArray<FSmvBindDef> D = {
		{ TEXT("ArmToggle"), EKeys::X, EKeys::Gamepad_FaceButton_Bottom, TEXT("bind.arm") },
		{ TEXT("Takeoff"), EKeys::T, EKeys::Gamepad_FaceButton_Left, TEXT("bind.takeoff") },
		{ TEXT("Land"), EKeys::L, FKey(), TEXT("bind.land") },
		{ TEXT("Rtl"), EKeys::R, EKeys::Gamepad_FaceButton_Top, TEXT("bind.rtl") },
		{ TEXT("ModeMenu"), EKeys::F, EKeys::Gamepad_Special_Left, TEXT("bind.mode_menu") },
		{ TEXT("Ch5Down"), EKeys::Q, FKey(), TEXT("bind.ch5_down") },
		{ TEXT("Ch5Up"), EKeys::E, FKey(), TEXT("bind.ch5_up") },
		{ TEXT("ThrCenter"), EKeys::SpaceBar, FKey(), TEXT("bind.thr_center") },
		{ TEXT("MotorCut"), EKeys::K, FKey(), TEXT("bind.motor_cut") },
		{ TEXT("Lock"), EKeys::Tab, EKeys::Gamepad_RightTrigger, TEXT("bind.lock") },
		{ TEXT("Ack"), EKeys::B, EKeys::Gamepad_FaceButton_Right, TEXT("bind.ack") },
		{ TEXT("Reset"), EKeys::BackSpace, FKey(), TEXT("bind.reset") },
		{ TEXT("CamCycle"), EKeys::C, EKeys::Gamepad_RightShoulder, TEXT("bind.cam_cycle") },
		{ TEXT("CamType"), EKeys::N, FKey(), TEXT("bind.cam_type") },
		{ TEXT("Pip"), EKeys::I, EKeys::Gamepad_LeftShoulder, TEXT("bind.pip") },
		{ TEXT("Hud"), EKeys::H, FKey(), TEXT("bind.hud") },
		{ TEXT("Panel"), EKeys::P, FKey(), TEXT("bind.panel") },
		{ TEXT("Sticks"), EKeys::V, FKey(), TEXT("bind.sticks") },
		{ TEXT("Cockpit"), EKeys::G, FKey(), TEXT("bind.cockpit") },
		{ TEXT("SpeedUp"), EKeys::Add, FKey(), TEXT("bind.speed_up") },
		{ TEXT("SpeedDown"), EKeys::Subtract, FKey(), TEXT("bind.speed_down") },
		{ TEXT("GimbalUp"), EKeys::PageUp, FKey(), TEXT("bind.gimbal_up") },
		{ TEXT("GimbalDown"), EKeys::PageDown, FKey(), TEXT("bind.gimbal_down") },
		{ TEXT("Help"), EKeys::F1, FKey(), TEXT("bind.help") },
		{ TEXT("Pause"), EKeys::Escape, EKeys::Gamepad_Special_Right, TEXT("bind.pause") },
	};
	return D;
}

FKey FSmvUiPrefs::KeyFor(FName Action, bool bPad) const
{
	const FString K = FString(bPad ? TEXT("pad.") : TEXT("kb.")) + Action.ToString();
	if (const FString* V = Binds.Find(K)) return FKey(FName(**V));
	for (const FSmvBindDef& D : Bindable()) if (D.Action == Action) return bPad ? D.Pad : D.Kb;
	return FKey();
}

FKey FSmvUiPrefs::Resolve(FName Action, const FKey& Default) const
{
	const bool bPad = Default.IsGamepadKey();
	for (const FSmvBindDef& D : Bindable())
	{
		if (D.Action != Action) continue;
		const FString K = FString(bPad ? TEXT("pad.") : TEXT("kb.")) + Action.ToString();
		if (const FString* V = Binds.Find(K)) return FKey(FName(**V));
		return Default;
	}
	return Default;
}

void FSmvUiPrefs::SetKey(FName Action, bool bPad, const FKey& Key)
{
	Binds.Add(FString(bPad ? TEXT("pad.") : TEXT("kb.")) + Action.ToString(), Key.GetFName().ToString());
}

FName FSmvUiPrefs::ConflictOf(FName Action, bool bPad, const FKey& Key) const
{
	for (const FSmvBindDef& D : Bindable())
	{
		if (D.Action == Action) continue;
		if (KeyFor(D.Action, bPad) == Key) return D.Action;
	}
	return NAME_None;
}
