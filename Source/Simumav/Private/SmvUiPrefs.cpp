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
	GConfig->GetString(GSmvPrefsSection, TEXT("SensPreset"), SensPreset, Ini);
	GConfig->GetBool(GSmvPrefsSection, TEXT("LinkRP"), bLinkRP, Ini);
	GConfig->GetBool(GSmvPrefsSection, TEXT("GpwsSpeech"), bGpwsSpeech, Ini);
	GConfig->GetString(GSmvPrefsSection, TEXT("HolidayCountry"), HolidayCountry, Ini);
	GConfig->GetBool(GSmvPrefsSection, TEXT("RecordVideo"), bRecordVideo, Ini);
	GConfig->GetString(GSmvPrefsSection, TEXT("RealWorld"), RealWorld, Ini);
	// sürüm 2: Google fotogerçekçi 3B artık çalışıyor (paketli oyunda glTF dönüştürücüleri kayıtsızdı) → varsayılan google3d;
	// eski varsayılanla kaydedilmiş "terrain" bir kez google3d'ye çekilir (kullanıcı sonra yine seçebilir)
	int32 RwVer = 0;
	GConfig->GetInt(GSmvPrefsSection, TEXT("RealWorldVer"), RwVer, Ini);
	if (RwVer < 2 && RealWorld == TEXT("terrain")) RealWorld = TEXT("google3d");
	GConfig->GetInt(GSmvPrefsSection, TEXT("SpeedFx"), SpeedFx, Ini);
	SpeedFx = FMath::Clamp(SpeedFx, 0, 2);
	GConfig->GetString(GSmvPrefsSection, TEXT("AlertLevel"), AlertLevel, Ini);
	GConfig->GetString(GSmvPrefsSection, TEXT("ThermalPalette"), ThermalPalette, Ini);
	GConfig->GetFloat(GSmvPrefsSection, TEXT("PipW"), PipW, Ini);
	GConfig->GetBool(GSmvPrefsSection, TEXT("Map"), bMap, Ini);
	for (TPair<const TCHAR*, float*> Kv : { TPair<const TCHAR*, float*>(TEXT("MapL"), &MapL), TPair<const TCHAR*, float*>(TEXT("MapT"), &MapT), TPair<const TCHAR*, float*>(TEXT("MapW"), &MapW),
		TPair<const TCHAR*, float*>(TEXT("MapRangeM"), &MapRangeM), TPair<const TCHAR*, float*>(TEXT("ViewW"), &ViewW), TPair<const TCHAR*, float*>(TEXT("ViewR"), &ViewR), TPair<const TCHAR*, float*>(TEXT("ViewB"), &ViewB) })
		GConfig->GetFloat(GSmvPrefsSection, Kv.Key, *Kv.Value, Ini);
	MapRangeM = FMath::Clamp(MapRangeM, 80.f, 20000.f);
	GConfig->GetFloat(GSmvPrefsSection, TEXT("PanelR"), PanelR, Ini);
	GConfig->GetFloat(GSmvPrefsSection, TEXT("PanelT"), PanelT, Ini);
	GConfig->GetBool(GSmvPrefsSection, TEXT("PanelMin"), bPanelMin, Ini);
	GConfig->GetString(GSmvPrefsSection, TEXT("PanelFold"), PanelFold, Ini);
	GConfig->GetFloat(GSmvPrefsSection, TEXT("PipR"), PipR, Ini);
	GConfig->GetFloat(GSmvPrefsSection, TEXT("PipB"), PipB, Ini);
	GConfig->GetString(GSmvPrefsSection, TEXT("DialTheme"), DialTheme, Ini);
	for (int32 i = 0; i < 4; ++i)
	{
		GConfig->GetFloat(GSmvPrefsSection, *FString::Printf(TEXT("Dead%d"), i), Dead[i], Ini);
		GConfig->GetFloat(GSmvPrefsSection, *FString::Printf(TEXT("Expo%d"), i), Ex[i], Ini);
		GConfig->GetFloat(GSmvPrefsSection, *FString::Printf(TEXT("Rate%d"), i), Rate[i], Ini);
	}
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
	GConfig->SetString(GSmvPrefsSection, TEXT("SensPreset"), *SensPreset, Ini);
	GConfig->SetBool(GSmvPrefsSection, TEXT("LinkRP"), bLinkRP, Ini);
	GConfig->SetBool(GSmvPrefsSection, TEXT("GpwsSpeech"), bGpwsSpeech, Ini);
	GConfig->SetString(GSmvPrefsSection, TEXT("HolidayCountry"), *HolidayCountry, Ini);
	GConfig->SetBool(GSmvPrefsSection, TEXT("RecordVideo"), bRecordVideo, Ini);
	GConfig->SetString(GSmvPrefsSection, TEXT("RealWorld"), *RealWorld, Ini);
	GConfig->SetInt(GSmvPrefsSection, TEXT("RealWorldVer"), 2, Ini);
	GConfig->SetInt(GSmvPrefsSection, TEXT("SpeedFx"), SpeedFx, Ini);
	GConfig->SetString(GSmvPrefsSection, TEXT("AlertLevel"), *AlertLevel, Ini);
	GConfig->SetString(GSmvPrefsSection, TEXT("ThermalPalette"), *ThermalPalette, Ini);
	GConfig->SetFloat(GSmvPrefsSection, TEXT("PipW"), PipW, Ini);
	GConfig->SetBool(GSmvPrefsSection, TEXT("Map"), bMap, Ini);
	for (TPair<const TCHAR*, float> Kv : { TPair<const TCHAR*, float>(TEXT("MapL"), MapL), TPair<const TCHAR*, float>(TEXT("MapT"), MapT), TPair<const TCHAR*, float>(TEXT("MapW"), MapW),
		TPair<const TCHAR*, float>(TEXT("MapRangeM"), MapRangeM), TPair<const TCHAR*, float>(TEXT("ViewW"), ViewW), TPair<const TCHAR*, float>(TEXT("ViewR"), ViewR), TPair<const TCHAR*, float>(TEXT("ViewB"), ViewB) })
		GConfig->SetFloat(GSmvPrefsSection, Kv.Key, Kv.Value, Ini);
	GConfig->SetFloat(GSmvPrefsSection, TEXT("PanelR"), PanelR, Ini);
	GConfig->SetFloat(GSmvPrefsSection, TEXT("PanelT"), PanelT, Ini);
	GConfig->SetBool(GSmvPrefsSection, TEXT("PanelMin"), bPanelMin, Ini);
	GConfig->SetString(GSmvPrefsSection, TEXT("PanelFold"), *PanelFold, Ini);
	GConfig->SetFloat(GSmvPrefsSection, TEXT("PipR"), PipR, Ini);
	GConfig->SetFloat(GSmvPrefsSection, TEXT("PipB"), PipB, Ini);
	GConfig->SetString(GSmvPrefsSection, TEXT("DialTheme"), *DialTheme, Ini);
	for (int32 i = 0; i < 4; ++i)
	{
		GConfig->SetFloat(GSmvPrefsSection, *FString::Printf(TEXT("Dead%d"), i), Dead[i], Ini);
		GConfig->SetFloat(GSmvPrefsSection, *FString::Printf(TEXT("Expo%d"), i), Ex[i], Ini);
		GConfig->SetFloat(GSmvPrefsSection, *FString::Printf(TEXT("Rate%d"), i), Rate[i], Ini);
	}
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
		{ TEXT("Radar"), EKeys::Y, FKey(), TEXT("site.radar_title") },
		{ TEXT("Eo"), EKeys::U, FKey(), TEXT("site.eo_title") },
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

void FSmvUiPrefs::ApplySensPreset(const FString& P)
{
	// web RCFEEL_PRESETS: soft (Hassas) / normal / sport (Çevik) — sıra roll, pitch, gaz, yaw
	SensPreset = P;
	auto Set = [this](const float (&D)[4], const float (&E)[4], const float (&R)[4]) { for (int32 i = 0; i < 4; ++i) { Dead[i] = D[i]; Ex[i] = E[i]; Rate[i] = R[i]; } };
	if (P == TEXT("precise")) Set({ 0.04f, 0.04f, 0.04f, 0.05f }, { 0.45f, 0.45f, 0.3f, 0.4f }, { 0.7f, 0.7f, 1.f, 0.6f });
	else if (P == TEXT("normal")) Set({ 0.03f, 0.03f, 0.03f, 0.04f }, { 0.2f, 0.2f, 0.1f, 0.2f }, { 1.f, 1.f, 1.f, 0.9f });
	else if (P == TEXT("agile")) Set({ 0.02f, 0.02f, 0.02f, 0.02f }, { 0.f, 0.f, 0.f, 0.05f }, { 1.f, 1.f, 1.f, 1.f });
}

void FSmvUiPrefs::SyncLink()
{
	if (bLinkRP) { Dead[1] = Dead[0]; Ex[1] = Ex[0]; Rate[1] = Rate[0]; }
}

float FSmvUiPrefs::Shape(float V, int32 Axis, float FallbackExpo) const
{
	if (SensPreset.IsEmpty()) return Axis == 2 ? V : V * (1.f - FallbackExpo) + V * V * V * FallbackExpo;
	const int32 Ax = FMath::Clamp(Axis, 0, 3);
	const float A = FMath::Abs(FMath::Clamp(V, -1.f, 1.f));
	const float D = FMath::Clamp(Dead[Ax], 0.f, 0.5f);
	if (A <= D) return 0.f;
	const float N = (A - D) / FMath::Max(0.01f, 1.f - D);
	const float E = FMath::Clamp(Ex[Ax], 0.f, 1.f);
	const float R = FMath::Clamp(Rate[Ax], 0.f, 1.f);
	return FMath::Sign(V) * ((1.f - E) * N + E * N * N * N) * R;
}

void FSmvUiPrefs::SetMeasured(const FString& DroneId, double KmPerL, double W)
{
	if (!GConfig || DroneId.IsEmpty()) return;
	GConfig->SetString(TEXT("/Script/Simumav.SmvMeasured"), *DroneId, *FString::Printf(TEXT("%.3f,%.2f"), KmPerL, W), GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

bool FSmvUiPrefs::GetMeasured(const FString& DroneId, double& KmPerL, double& W)
{
	KmPerL = -1; W = -1;
	FString V;
	if (!GConfig || DroneId.IsEmpty() || !GConfig->GetString(TEXT("/Script/Simumav.SmvMeasured"), *DroneId, V, GGameUserSettingsIni)) return false;
	FString A, B;
	if (!V.Split(TEXT(","), &A, &B)) return false;
	KmPerL = FCString::Atod(*A); W = FCString::Atod(*B);
	return KmPerL > 0 || W > 0;
}
