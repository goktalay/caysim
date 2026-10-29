#include "SmvLocalFlightLink.h"
#include "SimumavModule.h"
#include "HepgoClient.h"
#include "SmvMavlink.h"
#include "SmvSitlConfig.h"
#include "SmvPower.h"
#include "SmvTargets.h"
#include "SmvRace.h"
#include "SmvCollision.h"
#include "Misc/ConfigCacheIni.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	constexpr uint8 GSmvGcsSys = 255;    // SYSID_MYGCS — RC override bu kimlikle kabul edilir
	constexpr uint8 GSmvGcsComp = 191;   // Mission Planner 190 kullanır; çakışmaz
	constexpr double GSmvTeleTbmGate = 0.018;
	constexpr double GSmvWatchdogS = 0.25;

	ISocketSubsystem* SmvSockets() { return ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM); }

	FString SmvQuote(const FString& S) { return S.Contains(TEXT(" ")) ? FString::Printf(TEXT("\"%s\""), *S) : S; }

	TSharedRef<FJsonObject> SmvMsg(const TCHAR* T)
	{
		TSharedRef<FJsonObject> M = MakeShared<FJsonObject>();
		M->SetStringField(TEXT("t"), T);
		return M;
	}
}

USmvLocalFlightLink::USmvLocalFlightLink() {}

USmvLocalFlightLink::~USmvLocalFlightLink()
{
	FreeCore();
}

void USmvLocalFlightLink::FreeCore()
{
	delete Parser; Parser = nullptr;
	delete MpParser; MpParser = nullptr;
	delete Session; Session = nullptr;
	delete Power; Power = nullptr;
	delete Targets; Targets = nullptr;
	delete ServoTrig; ServoTrig = nullptr;
}

void USmvLocalFlightLink::BeginDestroy()
{
	if (TickHandle.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(TickHandle); TickHandle.Reset(); }
	CloseTcp();
	KillSitl();
	if (Udp) { Udp->Close(); SmvSockets()->DestroySocket(Udp); Udp = nullptr; }
	FreeCore();
	Super::BeginDestroy();
}

double USmvLocalFlightLink::Now() const { return UHepgoClient::LocalUnixNow(); }

FString USmvLocalFlightLink::SitlDir()
{
	const USmvLocalFlightLink* Cdo = GetDefault<USmvLocalFlightLink>();
	if (Cdo && !Cdo->SitlDirOverride.IsEmpty()) return Cdo->SitlDirOverride;
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("ThirdParty"), TEXT("ArduPilotSITL"), TEXT("Win64")));
}

bool USmvLocalFlightLink::SitlAvailable(FString* OutDir)
{
	const FString D = SitlDir();
	if (OutDir) *OutDir = D;
	return FPaths::FileExists(FPaths::Combine(D, TEXT("ArduCopter.exe"))) && FPaths::FileExists(FPaths::Combine(D, TEXT("cygwin1.dll")));
}

FString USmvLocalFlightLink::MissionPlannerInfo() const
{
	if (!bMpOut) return TEXT("Mission Planner çıkışı kapalı (Ayarlar → Mission Planner UDP çıkışı)");
	return FString::Printf(TEXT("Mission Planner: UDP %s:%d çıkışı — MP'de UDP, port %d ile bağlanın%s"), *MpHost, MpPort, MpPort, bGcsConnected ? TEXT(" (bağlı)") : TEXT(""));
}

// ---------------------------------------------------------------------------
// süreç
// ---------------------------------------------------------------------------

bool USmvLocalFlightLink::PortFree(int32 Port)
{
	ISocketSubsystem* SS = SmvSockets();
	FSocket* S = SS->CreateSocket(NAME_Stream, TEXT("SmvPortProbe"), false);
	if (!S) return false;
	TSharedRef<FInternetAddr> A = SS->CreateInternetAddr();
	bool bValid = false;
	A->SetIp(TEXT("127.0.0.1"), bValid);
	A->SetPort(Port);
	const bool bOk = S->Bind(*A);
	S->Close();
	SS->DestroySocket(S);
	return bOk;
}

int32 USmvLocalFlightLink::FindFreePortBlock() const
{
	// Tuzak B: SITL taban porttan itibaren bir blok bağlar (SERIAL0=taban, SERIAL1=+2, SERIAL2=+3…) → 10'luk bloğun hepsi boş olmalı
	for (int32 i = 0; i < 200; ++i)
	{
		const int32 Base = PortBase + i * 10;
		bool bAll = true;
		for (int32 k = 0; k < 10 && bAll; ++k) bAll = PortFree(Base + k);
		if (bAll) return Base;
	}
	return 0;
}

bool USmvLocalFlightLink::LaunchSitl(bool bClean)
{
	// ikili çeşit çerçevesinden (ör. heli → ArduHeli, coaxcopter → ArduCopter; SmvVehicles)
	const std::string BinA = SmvCore::SessionBinary(*Session);
	const FString Exe = FPaths::Combine(SitlDir(), FString(UTF8_TO_TCHAR(BinA.c_str())) + TEXT(".exe"));
	if (!FPaths::FileExists(Exe))
	{
		EmitEvent(FString::Printf(TEXT("SITL ikilisi bulunamadı: %s — oyun dosyalarını Steam'den doğrulayın ya da 'Bulutta uç'u seçin."), *Exe), 3);
		return false;
	}
	IFileManager& FM = IFileManager::Get();
	FM.MakeDirectory(*WorkDir, true);
	if (bClean)
	{
		// temiz oturum: eeprom.bin + Lua script dizinleri silinir (drone_sitl._baslat_port)
		FM.Delete(*FPaths::Combine(WorkDir, TEXT("eeprom.bin")), false, true, true);
		FM.DeleteDirectory(*FPaths::Combine(WorkDir, TEXT("scripts")), false, true);
		FM.DeleteDirectory(*FPaths::Combine(WorkDir, TEXT("APM"), TEXT("scripts")), false, true);
	}
	const FString Parm = FPaths::Combine(WorkDir, TEXT("varsayilan.parm"));
	std::string Text = SmvCore::DefaultsParmText(*Session);
	// fizik sensörleri (UE ışın izleri MAVLink ile gelir): RNGFND1 / PRX1 / AVOID — yeniden başlatma gerektiren parametreler, bu yüzden
	// açılış dosyasında. Kullanıcının kayıtlı parametresi aynı adı taşıyorsa onunki kalır.
	if (bSensRng || bSensPrx)
	{
		Text += "# SIMUMAV fizik sensorleri (UE isin izleri -> DISTANCE_SENSOR / OBSTACLE_DISTANCE)\n";
		for (const auto& Kv : SmvCore::PhysicsSensorParams(Session->SitlType, bSensAvoid))
		{
			bool bUser = false;
			for (const auto& U : Session->UserParams) if (U.first == Kv.first) bUser = true;
			if (!bUser) Text += Kv.first + " " + std::string(TCHAR_TO_UTF8(*FString::SanitizeFloat(Kv.second, 0))) + "\n";
		}
	}
	// kullanıcı / donanım parametreleri açılış dosyasına da yazılır: SPRAY_ENABLE, GRIP_ENABLE, CHUTE_ENABLED, SCR_ENABLE, FENCE_* gibi
	// yeniden başlatma isteyenler ilk açılışta etkin olsun (ilk heartbeat'te ayrıca PARAM_SET ile gönderilir)
	if (!Session->UserParams.empty())
	{
		Text += "# SIMUMAV dron parametreleri (hangar: kullanici + donanim)\n";
		for (const auto& U : Session->UserParams) Text += U.first + " " + std::string(TCHAR_TO_UTF8(*FString::SanitizeFloat(U.second, 0))) + "\n";
		// çeşidin SITL batarya modeli (SIM_BATT_*; DefaultsParmText) kayıtlı değerlerin önüne geçer: katalog varsayılanı SIM_BATT_VOLTAGE 16,8
		// 12S hexada "boş paket" demekti (5 V, Potential Thrust Loss + Battery Failsafe)
		std::string Batt;
		size_t Pos = 0;
		const std::string Base = SmvCore::DefaultsParmText(*Session);
		while (Pos < Base.size())
		{
			size_t E = Base.find('\n', Pos);
			if (E == std::string::npos) E = Base.size();
			const std::string Line = Base.substr(Pos, E - Pos);
			if (Line.rfind("SIM_BATT_", 0) == 0) Batt += Line + "\n";
			Pos = E + 1;
		}
		if (!Batt.empty()) Text += "# SIMUMAV cesit bataryasi\n" + Batt;
	}
	FFileHelper::SaveStringToFile(FString(UTF8_TO_TCHAR(Text.c_str())), *Parm, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	if (!SessionScript.TrimStartAndEnd().IsEmpty())
	{
		const FString ScrDir = FPaths::Combine(WorkDir, TEXT("scripts"));
		FM.MakeDirectory(*ScrDir, true);
		FFileHelper::SaveStringToFile(SessionScript, *FPaths::Combine(ScrDir, TEXT("simumav.lua")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
	// çeşidin çerçeve / kanat JSON'u: --model "<çerçeve>:<ad>.json" GÖRELİ yolla okunur (süreç çalışma dizini = WorkDir)
	std::string JName, JText;
	if (SmvCore::SessionJson(*Session, JName, JText))
	{
		FFileHelper::SaveStringToFile(FString(UTF8_TO_TCHAR(JText.c_str())), *FPaths::Combine(WorkDir, FString(UTF8_TO_TCHAR(JName.c_str()))), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
	// cygwin ikilisi ileri eğik çizgili Windows yolunu kabul eder
	FString ParmArg = Parm;
	ParmArg.ReplaceInline(TEXT("\\"), TEXT("/"));
	const std::vector<std::string> Args = SmvCore::CommandLine(*Session, std::string(TCHAR_TO_UTF8(*ParmArg)));
	FString ArgLine;
	for (const std::string& A : Args) { if (!ArgLine.IsEmpty()) ArgLine += TEXT(" "); ArgLine += SmvQuote(UTF8_TO_TCHAR(A.c_str())); }
	if (PipeRead || PipeWrite) { FPlatformProcess::ClosePipe(PipeRead, PipeWrite); PipeRead = PipeWrite = nullptr; }
	FPlatformProcess::CreatePipe(PipeRead, PipeWrite);
	uint32 Pid = 0;
	Proc = FPlatformProcess::CreateProc(*Exe, *ArgLine, false, true, true, &Pid, 0, *WorkDir, PipeWrite, nullptr);
	if (!Proc.IsValid())
	{
		EmitEvent(TEXT("SITL süreci başlatılamadı."), 3);
		return false;
	}
	ProcStartedAt = FPlatformTime::Seconds();
	NextConnectAt = ProcStartedAt + 1.0;
	UE_LOG(LogSimumav, Log, TEXT("SITL başlatıldı (pid %u): %s %s"), Pid, *Exe, *ArgLine);
	EmitEvent(FString::Printf(TEXT("ArduPilot SITL başlatılıyor (%s, port %d) — ilk açılış 10–15 sn sürebilir"), UTF8_TO_TCHAR(BinA.c_str()), BasePort), 6);
	return true;
}

void USmvLocalFlightLink::KillSitl()
{
	if (Proc.IsValid())
	{
		if (FPlatformProcess::IsProcRunning(Proc)) FPlatformProcess::TerminateProc(Proc, true);
		FPlatformProcess::CloseProc(Proc);
		Proc.Reset();
	}
	ReadProcPipe();
	if (PipeRead || PipeWrite) { FPlatformProcess::ClosePipe(PipeRead, PipeWrite); PipeRead = PipeWrite = nullptr; }
}

void USmvLocalFlightLink::ReadProcPipe()
{
	if (!PipeRead) return;
	const FString Out = FPlatformProcess::ReadPipe(PipeRead);
	if (Out.IsEmpty()) return;
	ProcLogTail = (ProcLogTail + Out).Right(4000);
	FFileHelper::SaveStringToFile(Out, *FPaths::Combine(WorkDir, TEXT("sitl.log")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
}

// ---------------------------------------------------------------------------
// TCP
// ---------------------------------------------------------------------------

bool USmvLocalFlightLink::TryConnect()
{
	ISocketSubsystem* SS = SmvSockets();
	FSocket* S = SS->CreateSocket(NAME_Stream, TEXT("SmvSitlTcp"), false);
	if (!S) return false;
	TSharedRef<FInternetAddr> A = SS->CreateInternetAddr();
	bool bValid = false;
	A->SetIp(TEXT("127.0.0.1"), bValid);
	A->SetPort(BasePort);
	S->SetNonBlocking(false);
	if (!S->Connect(*A))
	{
		S->Close();
		SS->DestroySocket(S);
		return false;
	}
	S->SetNonBlocking(true);
	S->SetNoDelay(true);
	int32 NewSize = 0;
	S->SetReceiveBufferSize(1 << 20, NewSize);
	Tcp = S;
	UE_LOG(LogSimumav, Log, TEXT("SITL TCP bağlandı: 127.0.0.1:%d"), BasePort);
	return true;
}

void USmvLocalFlightLink::CloseTcp()
{
	if (Tcp)
	{
		Tcp->Close();
		SmvSockets()->DestroySocket(Tcp);
		Tcp = nullptr;
	}
}

void USmvLocalFlightLink::SendRaw(const uint8* Data, int32 Num)
{
	if (!Tcp || Num <= 0) return;
	int32 Sent = 0;
	Tcp->Send(Data, Num, Sent);
}

void USmvLocalFlightLink::SendFrame(uint32 MsgId, const uint8* Payload, int32 Len)
{
	const std::vector<uint8_t> P(Payload, Payload + Len);
	const std::vector<uint8_t> F = SmvMav::EncodeV2(MsgId, P, Seq++, GSmvGcsSys, GSmvGcsComp);
	SendRaw(F.data(), (int32)F.size());
}

void USmvLocalFlightLink::ParamSet(const FString& Name, float Value, uint8 Type)
{
	const std::vector<uint8_t> P = SmvMav::PackParamSet(TargetSys, TargetComp, std::string(TCHAR_TO_UTF8(*Name)), Value, Type);
	SendFrame(SmvMav::MSG_PARAM_SET, P.data(), (int32)P.size());
}

void USmvLocalFlightLink::CommandLong(uint16 Cmd, float P1, float P2, float P3, float P4, float P5, float P6, float P7)
{
	const std::vector<uint8_t> P = SmvMav::PackCommandLong(TargetSys, TargetComp, Cmd, P1, P2, P3, P4, P5, P6, P7);
	SendFrame(SmvMav::MSG_COMMAND_LONG, P.data(), (int32)P.size());
}

bool USmvLocalFlightLink::SetMode(const FString& Name)
{
	uint32 Num = 0;
	if (!SmvCore::ModeNumber(std::string(TCHAR_TO_UTF8(*ModeVehicle)), std::string(TCHAR_TO_UTF8(*Name.ToUpper())), Num)) return false;
	CommandLong(SmvMav::CMD_DO_SET_MODE, 1.f, (float)Num);
	return true;
}

// ---------------------------------------------------------------------------
// başlat / durdur
// ---------------------------------------------------------------------------

void USmvLocalFlightLink::Start(const FSmvFlightRequest& Request)
{
	Req = Request;
	FreeCore();
	Parser = new SmvMav::FParser();
	MpParser = new SmvMav::FParser();
	Session = new SmvCore::FSitlSession();
	const std::string Type = SmvCore::SitlTypeFromSmv(std::string(TCHAR_TO_UTF8(*Req.Drone.Type)));
	Session->SitlType = Type;
	Session->Airframe = std::string(TCHAR_TO_UTF8(*Req.Drone.Airframe));
	Session->Drive = Req.Drone.Drive == TEXT("skid") ? "skid" : "ackermann";
	Session->HomeLat = Req.Track.Lat;
	Session->HomeLon = Req.Track.Lon;
	Session->HomeAlt = Req.Track.GroundMsl > -1000 ? Req.Track.GroundMsl : 100.0;
	Session->HomeHeading = 0;
	if (Type == "sabit_kanat") Session->HomeHeading = (Req.Launch.Method == TEXT("ramp") && Req.Launch.AimAzDeg > 0) ? Req.Launch.AimAzDeg : Req.Track.World.RunwayHeading;
	Session->SysId = 1;
	Session->bBinLog = Req.bRecordBin;
	Session->WindMs = Req.Track.WindMs;
	Session->WindDir = Req.Track.WindDir;
	Session->WindGust = Req.Track.WindGust;
	Session->Speedup = FMath::Clamp(Req.Speedup, 0.25, 8.0);
	Session->Launch.Method = std::string(TCHAR_TO_UTF8(*(Req.Launch.Method.IsEmpty() ? FString(TEXT("wheels")) : Req.Launch.Method)));
	Session->Launch.AngleDeg = Req.Launch.AngleDeg;
	Session->Launch.ReleaseKmh = Req.Launch.ReleaseKmh;
	Session->Launch.AzimuthDeg = Req.Launch.AimAzDeg;
	Session->Launch.HandSpeedMs = FMath::Clamp(Req.Launch.HandSpeedMs, 6.0, 8.0);
	Session->Launch.HandPitchDeg = FMath::Clamp(Req.Launch.HandPitchDeg, 10.0, 15.0);
	for (const TPair<FString, double>& Kv : Req.Drone.Params) Session->UserParams.push_back({ std::string(TCHAR_TO_UTF8(*Kv.Key)), Kv.Value });
	// Lua betiği (hangar → dron → Betik): SCR_ENABLE açılış dosyasında, betik WorkDir/scripts altında
	SessionScript = Req.Drone.Script;
	if (!SessionScript.TrimStartAndEnd().IsEmpty()) Session->UserParams.push_back({ std::string("SCR_ENABLE"), 1.0 });
	// araç çeşidi (katalog anahtarı → SITL çerçevesi/JSON/parametre seti; SmvVehicles). Tip uyuşmazsa çekirdek eski eşlemeye düşer.
	Session->Vehicle = std::string(TCHAR_TO_UTF8(*Req.Drone.ModelKey));
	bHeliInterlock = SmvCore::SessionBinary(*Session) == "ArduHeli";
	NextInterlockAt = 0;
	ModeVehicle = UTF8_TO_TCHAR(SmvCore::ModeVehicle(Type).c_str());
	{
		bool bSens = true;
		bSensAvoid = true;
		if (GConfig)
		{
			GConfig->GetBool(TEXT("/Script/Simumav.SmvPhysics"), TEXT("SitlSensors"), bSens, GGameIni);
			GConfig->GetBool(TEXT("/Script/Simumav.SmvPhysics"), TEXT("SitlAvoid"), bSensAvoid, GGameIni);
		}
		bSensRng = bSens && SmvCore::PhysicsHasRangefinder(Type);
		bSensPrx = bSens && SmvCore::PhysicsHasProximity(Type);
		bUeRest = false;
	}
	VehicleMavType = (uint8)(ModeVehicle == TEXT("plane") ? 1 : ModeVehicle == TEXT("rover") ? 10 : 2);

	// güç ayarı: dron önerisi → istek → tip varsayılanı (flightsim_power.dogrula)
	SmvCore::FPowerCfg Pc = SmvCore::FPowerCfg::Defaults(Type);
	auto Pick = [](double V, double D) { return V >= 0 ? V : D; };
	const FSmvPowerSettings& Pd = Req.Drone.Power;
	const FSmvPowerSettings& Pr = Req.Power;
	Pc.MaxSpeedMs = Pick(Pr.MaxSpeedMs, Pick(Pd.MaxSpeedMs, Pc.MaxSpeedMs));
	Pc.FuelL = Pick(Pr.FuelL, Pick(Pd.FuelL, Pc.FuelL));
	Pc.FuelKmPerL = Pick(Pr.FuelKmPerL, Pick(Pd.FuelKmPerL, Pc.FuelKmPerL));
	Pc.ElecCells = Pr.ElecCells > 0 ? Pr.ElecCells : (Pd.ElecCells > 0 ? Pd.ElecCells : Pc.ElecCells);
	Pc.ElecMah = Pick(Pr.ElecMah, Pick(Pd.ElecMah, Pc.ElecMah));
	Pc.ElecW = Pick(Pr.ElecW, Pick(Pd.ElecW, Pc.ElecW));
	Pc.Clamp(SmvCore::IsPlaneType(Type));
	Session->MaxSpeedMs = Pc.MaxSpeedMs;
	Power = new SmvCore::FPowerState(Pc);
	Power->SetSpeedup(Session->Speedup);

	// hedefler
	if (Req.Targets.Num())
	{
		std::vector<SmvCore::FTargetCfg> Items;
		for (int32 i = 0; i < Req.Targets.Num() && i < 8; ++i)
		{
			const FSmvTarget& T = Req.Targets[i];
			SmvCore::FTargetCfg C;
			C.I = i + 1;
			C.Name = std::string(TCHAR_TO_UTF8(*T.Model));
			C.Model = C.Name;
			C.AltM = FMath::Clamp(T.AltM, 10.0, 3000.0);
			C.DiameterM = FMath::Clamp(T.DiameterM, 50.0, 5000.0);
			C.SpeedMs = FMath::Max(1.0, T.SpeedMs);
			C.EastM = T.EastM; C.NorthM = T.NorthM;
			C.PhaseDeg = SmvCore::DefaultPhase(i);
			C.bCw = T.bCw;
			Items.push_back(C);
		}
		Targets = new SmvCore::FTargetTracker(Items, FMath::Clamp(Req.TargetRadiusM, 5.0, 200.0));
	}
	ServoTrig = new SmvCore::FServoTrigger();
	Servo16.Init(0, 16);

	BasePort = FindFreePortBlock();
	if (!BasePort)
	{
		EmitEvent(TEXT("Yerel SITL için boş port bloğu bulunamadı (15670–17660)."), 3);
		EmitState(TEXT("HATA"));
		OnStarted.Broadcast(false);
		return;
	}
	WorkDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Simumav"), TEXT("SitlRun"), FString::FromInt(BasePort)));
	FlightInfo = FSmvFlightInfo();
	FlightInfo.FlightId = FString::Printf(TEXT("l-%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(12).ToLower());
	FlightInfo.SimId = FString::FromInt(BasePort);
	if (bMpOut) { FlightInfo.MpHost = MpHost; FlightInfo.MpPort = MpPort; FlightInfo.MpProto = TEXT("udp"); }
	EmitState(TEXT("BASLIYOR"));
	if (!LaunchSitl(true))
	{
		EmitState(TEXT("HATA"));
		OnStarted.Broadcast(false);
		return;
	}
	StartedWall = FPlatformTime::Seconds();
	if (bMpOut) OpenMp();
	if (!TickHandle.IsValid()) TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &USmvLocalFlightLink::OnTick), 0.f);
	OnStarted.Broadcast(true);
}

void USmvLocalFlightLink::Stop(const FString& Reason)
{
	if (bStopped) return;
	EndReason = Reason;
	Finish();
}

void USmvLocalFlightLink::Finish()
{
	bStopped = true;
	FSmvFlightSummary S;
	S.FlightId = FlightInfo.FlightId;
	S.EndReason = EndReason;
	S.Result = bCrashed ? TEXT("crash") : (EndReason == TEXT("user") || EndReason == TEXT("ok") ? TEXT("ok") : TEXT("abort"));
	S.DurationS = StartedWall > 0 ? FPlatformTime::Seconds() - StartedWall : 0;
	if (Power)
	{
		S.DistM = SmvCore::RoundN(Power->DistM, 1);
		S.ArmedS = SmvCore::RoundN(Power->ArmedS, 1);
		S.MaxSpeedMs = SmvCore::RoundN(Power->MaxSpeedSeen, 1);
		S.FuelUsedL = SmvCore::RoundN(Power->FuelUsed, 3);
		S.WhUsed = SmvCore::RoundN(Power->WhUsed, 3);
	}
	if (Targets)
	{
		std::vector<std::string> B;
		S.TargetScore = Targets->Score(B);
		for (const std::string& X : B) S.Badges.Add(UTF8_TO_TCHAR(X.c_str()));
	}
	S.bServoTriggerFired = ServoTrig && ServoTrig->State() == SmvCore::FServoTrigger::EState::Fired;
	// .BIN kayıtlarını kalıcı klasöre taşı (SITL kapanmadan önce dosya açık olabilir → önce süreç kapanır)
	CloseTcp();
	KillSitl();
	if (Req.bRecordBin && !WorkDir.IsEmpty())
	{
		TArray<FString> Bins;
		IFileManager::Get().FindFiles(Bins, *FPaths::Combine(WorkDir, TEXT("logs"), TEXT("*.BIN")), true, false);
		const FString Dst = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Simumav"), TEXT("Logs"));
		for (const FString& B : Bins)
		{
			IFileManager::Get().Move(*FPaths::Combine(Dst, FString::Printf(TEXT("%s-%s"), *FlightInfo.FlightId, *B)), *FPaths::Combine(WorkDir, TEXT("logs"), B), true, true);
		}
	}
	EmitState(TEXT("KAPANDI"));
	TSharedRef<FJsonObject> End = SmvMsg(TEXT("end"));
	End->SetStringField(TEXT("reason"), EndReason);
	EmitMessage(End);
	OnSummary.Broadcast(S);
}

// ---------------------------------------------------------------------------
// döngü
// ---------------------------------------------------------------------------

bool USmvLocalFlightLink::OnTick(float /*Dt*/)
{
	if (bStopped) return true;
	const double W = FPlatformTime::Seconds();
	ReadProcPipe();

	// süreç öldü mü
	if (Proc.IsValid() && !FPlatformProcess::IsProcRunning(Proc) && !bResetting)
	{
		int32 Code = 0;
		FPlatformProcess::GetProcReturnCode(Proc, &Code);
		TArray<FString> Lines;
		ProcLogTail.ParseIntoArrayLines(Lines);
		const FString Tail = FString::Join(TArray<FString>(Lines.GetData() + FMath::Max(0, Lines.Num() - 3), FMath::Min(3, Lines.Num())), TEXT(" | "));
		EmitEvent(FString::Printf(TEXT("SITL süreci kapandı (çıkış %d): %s"), Code, *Tail), 3);
		FPlatformProcess::CloseProc(Proc);
		Proc.Reset();
		CloseTcp();
		EmitState(TEXT("HATA"));
		EndReason = TEXT("sitl");
		Finish();
		return true;
	}

	// bağlanma
	if (!Tcp && Proc.IsValid() && W >= NextConnectAt)
	{
		NextConnectAt = W + 1.0;
		if (!TryConnect() && W - ProcStartedAt > ConnectTimeoutS)
		{
			EmitEvent(FString::Printf(TEXT("SITL'e %.0f sn içinde bağlanılamadı."), ConnectTimeoutS), 3);
			EmitState(TEXT("HATA"));
			EndReason = TEXT("sitl");
			Finish();
			return true;
		}
		if (Tcp) bResetting = false;
	}

	if (bHeliInterlock && Tcp && bHeartbeatSeen) PumpHeliInterlock(W);

	// okuma
	if (Tcp)
	{
		uint8 Buf[16384];
		uint32 Pending = 0;
		int32 Guard = 0;
		while (Tcp && Tcp->HasPendingData(Pending) && Guard++ < 64)
		{
			int32 Read = 0;
			if (!Tcp->Recv(Buf, FMath::Min<int32>((int32)sizeof(Buf), (int32)FMath::Max<uint32>(Pending, 1u)), Read) || Read <= 0) break;
			// ham köprü: SITL → Mission Planner
			if (Udp && MpAddr.IsValid())
			{
				int32 Sent = 0;
				Udp->SendTo(Buf, Read, Sent, MpLocked.IsValid() ? *MpLocked : *MpAddr);
			}
			SmvMav::FFrame F;
			for (int32 i = 0; i < Read; ++i)
			{
				if (Parser->Push(Buf[i], F)) HandleFrame(F);
			}
		}
		if (Tcp && Tcp->GetConnectionState() == SCS_ConnectionError && !bResetting)
		{
			EmitEvent(TEXT("SITL bağlantısı koptu."), 4);
			CloseTcp();
		}
	}
	PumpMp();

	if (bHeartbeatSeen && Tcp)
	{
		if (W - LastGcsHb >= 1.0)
		{
			LastGcsHb = W;
			const std::vector<uint8_t> P = SmvMav::PackHeartbeatGcs();
			SendFrame(SmvMav::MSG_HEARTBEAT, P.data(), (int32)P.size());
		}
		if (W - LastStreamRefresh >= 3.0) { LastStreamRefresh = W; RequestStreams(false); if (!bHomeKnown) CommandLong(SmvMav::CMD_REQUEST_MESSAGE, 242.f); }
		TickMission(W);
		if (Session->WindGust > 0 && W - LastGust >= 1.0)
		{
			LastGust = W;
			const double Spd = SmvCore::GustWindSpeed(Session->WindMs, Session->WindGust, Tele.Tbm / 1000.0);
			ParamSet(TEXT("SIM_WIND_SPD"), (float)Spd);
			Extra.WindSpd = Spd;
		}
		if (W - LastPublishWall > GSmvWatchdogS) Publish(false);
	}

	// zamanlanmış işler
	for (int32 i = 0; i < Timed.Num();)
	{
		if (W >= Timed[i].At) { TFunction<void()> Fn = MoveTemp(Timed[i].Fn); Timed.RemoveAt(i); Fn(); }
		else ++i;
	}
	TickLaunch();
	TickPosReady(W);

	// çarpışma adayı zaman aşımı (SITL "Hit ground" doğrulamadı → çarpışma değil)
	if (CrashCandidateUntil > 0 && W > CrashCandidateUntil) CrashCandidateUntil = 0;

	// servo tetik duvar saati bekçisi
	if (ServoTrig && ServoTrig->Watchdog(W) == "fired") Crash(TEXT("servo_trigger"), FMath::Max(Tele.Airspeed, Tele.Groundspeed));
	return true;
}

// ---------------------------------------------------------------------------
// MAVLink
// ---------------------------------------------------------------------------

void USmvLocalFlightLink::RequestStreams(bool bFull)
{
	if (bFull)
	{
		const std::vector<uint8_t> P = SmvMav::PackRequestDataStream(TargetSys, TargetComp, 0, 10, true);
		SendFrame(SmvMav::MSG_REQUEST_DATA_STREAM, P.data(), (int32)P.size());
		CommandLong(SmvMav::CMD_SET_MESSAGE_INTERVAL, 242.f, 5000000.f);
	}
	if (bStreamFallback)
	{
		// ArduPlane 20 ms aralığı reddederse akış tabanlı: EXTRA1 50, POSITION 50, EXTRA2 20, RC_CHANNELS 10 Hz
		const uint8 Ids[5] = { 10, 6, 11, 3, 12 };
		const uint16 Hz[5] = { 50, 50, 20, 10, 2 };
		for (int32 i = 0; i < 5; ++i)
		{
			const std::vector<uint8_t> P = SmvMav::PackRequestDataStream(TargetSys, TargetComp, Ids[i], Hz[i], true);
			SendFrame(SmvMav::MSG_REQUEST_DATA_STREAM, P.data(), (int32)P.size());
		}
		return;
	}
	const struct { float Id; float Us; } Iv[] = { { 30, 20000 }, { 33, 20000 }, { 32, 20000 }, { 74, 50000 }, { 65, 100000 }, { 36, 50000 }, { 193, 500000 } };
	for (const auto& X : Iv) CommandLong(SmvMav::CMD_SET_MESSAGE_INTERVAL, X.Id, X.Us);
}

void USmvLocalFlightLink::MissionRequestList()
{
	SmvMav::FWriter Wr; Wr.U8(TargetSys); Wr.U8(TargetComp); Wr.U8(0);
	SendFrame(SmvMav::MSG_MISSION_REQUEST_LIST, Wr.B.data(), (int32)Wr.B.size());
	MissionReqAt = FPlatformTime::Seconds();
}

void USmvLocalFlightLink::MissionRequestItem(int32 SeqNo)
{
	SmvMav::FWriter Wr; Wr.U16((uint16)SeqNo); Wr.U8(TargetSys); Wr.U8(TargetComp); Wr.U8(0);
	SendFrame(SmvMav::MSG_MISSION_REQUEST_INT, Wr.B.data(), (int32)Wr.B.size());
	MissionItemAt = FPlatformTime::Seconds();
}

void USmvLocalFlightLink::TickMission(double Wall)
{
	// indirme sürüyor: öğe 2 sn içinde gelmezse yeniden iste (3 deneme), sonra bırak (bir sonraki değişiklikte yeniden)
	if (bMissionDl)
	{
		if (Wall - MissionItemAt > 2.0)
		{
			if (++MissionTries > 3) { bMissionDl = false; UE_LOG(LogSimumav, Warning, TEXT("Otopilot görevi indirilemedi (öğe %d)"), MissionNext); }
			else MissionRequestItem(MissionNext);
		}
		return;
	}
	// bağlantı sonrası bir kez + görev değişince (MISSION_CURRENT toplamı); Mission Planner aktarımıyla çakışmasın diye sürekli yoklama yok
	if (bMissionReq && Wall - MissionReqAt > 1.5) { bMissionReq = false; MissionRequestList(); }
}

void USmvLocalFlightLink::OnFirstHeartbeat()
{
	bHeartbeatSeen = true;
	LastStreamRefresh = LastGcsHb = FPlatformTime::Seconds();
	bMissionReq = true;   // otopilottaki görev (mini harita)
	RequestStreams(true);
	CommandLong(SmvMav::CMD_REQUEST_MESSAGE, 242.f);
	for (int32 i = 1; i <= 16; ++i)
	{
		const std::vector<uint8_t> P = SmvMav::PackParamRequestRead(TargetSys, TargetComp, TCHAR_TO_UTF8(*FString::Printf(TEXT("SERVO%d_FUNCTION"), i)));
		SendFrame(SmvMav::MSG_PARAM_REQUEST_READ, P.data(), (int32)P.size());
	}
	for (const auto& Kv : SmvCore::FirstHeartbeatParams(*Session, bAfterReset)) ParamSet(UTF8_TO_TCHAR(Kv.first.c_str()), (float)Kv.second);
	Extra.WindSpd = SmvCore::GustWindSpeed(Session->WindMs, Session->WindGust, 0);
	Extra.WindDir = Session->WindDir;
	if (bAfterReset)
	{
		for (const TPair<FString, float>& Kv : SessionUserParams) ParamSet(Kv.Key, Kv.Value);
		bAfterReset = false;
		EmitEvent(TEXT("SITL yeniden başladı; oturumda değiştirilen parametreler yeniden yazıldı (Sıfırla korunumu)."), 6);
	}
	EmitEvent(TEXT("SITL kalp atışı alındı; veri akışı istendi (ATTITUDE/konum 50 Hz)"), 6);
	EmitEvent(FString::Printf(TEXT("Ev irtifası %.1f m MSL (%s)"), Session->HomeAlt, Req.Track.GroundMsl > -1000 ? TEXT("parkur kaydı") : TEXT("varsayılan")), 6);
	EmitState(TEXT("HAZIR"));
	TSharedRef<FJsonObject> H = SmvMsg(TEXT("hello"));
	H->SetBoolField(TEXT("can_command"), true);
	H->SetStringField(TEXT("flight_id"), FlightInfo.FlightId);
	EmitMessage(H);
}

void USmvLocalFlightLink::HandleFrame(const SmvMav::FFrame& F)
{
	// `using namespace SmvMav` YOK: UE küresel adlarıyla (ör. FFrame) belirsizlik olmasın diye adlar nitelikli
	if (F.SysId == GSmvGcsSys) return;   // kendi yankımız
	if (F.MsgId == SmvMav::MSG_HEARTBEAT)
	{
		const SmvMav::FHeartbeat H = SmvMav::DecodeHeartbeat(F);
		if (H.Type == 6 || H.Type == 18) return;   // GCS / onboard controller
		if (!bHeartbeatSeen)
		{
			TargetSys = F.SysId; TargetComp = F.CompId;
			VehicleMavType = H.Type;
			ModeVehicle = UTF8_TO_TCHAR(SmvCore::ModeVehicleFromMavType(H.Type).c_str());
			OnFirstHeartbeat();
		}
		if (F.SysId != TargetSys) return;
		const bool bArmedNow = (H.BaseMode & SmvMav::MODE_FLAG_ARMED) != 0;
		if (bArmedNow && FirstArmWall <= 0) FirstArmWall = FPlatformTime::Seconds();
		Tele.bArmed = bArmedNow;
		Tele.Mode = UTF8_TO_TCHAR(SmvCore::ModeName(std::string(TCHAR_TO_UTF8(*ModeVehicle)), H.CustomMode).c_str());
		return;
	}
	if (bHeartbeatSeen && F.SysId != TargetSys) return;
	switch (F.MsgId)
	{
	case SmvMav::MSG_ATTITUDE:
	{
		const SmvMav::FAttitude A = SmvMav::DecodeAttitude(F);
		Tele.Roll = A.Roll; Tele.Pitch = A.Pitch; Tele.Yaw = A.Yaw;
		Tele.RollSpeed = A.RollSpeed; Tele.PitchSpeed = A.PitchSpeed; Tele.YawSpeed = A.YawSpeed;
		Tele.Tbm = A.TimeBootMs;
		LastAttWall = FPlatformTime::Seconds();
		Publish(true);
		break;
	}
	case SmvMav::MSG_GLOBAL_POSITION_INT:
	{
		const SmvMav::FGlobalPositionInt G = SmvMav::DecodeGlobalPositionInt(F);
		if (G.Lat != 0 || G.Lon != 0) { Tele.Lat = G.Lat / 1e7; Tele.Lon = G.Lon / 1e7; }
		Tele.AltMsl = G.Alt / 1000.0;
		Tele.AltRel = G.RelativeAlt / 1000.0;
		Tele.Vn = G.Vx / 100.f; Tele.Ve = G.Vy / 100.f; Tele.Vd = G.Vz / 100.f;
		if (G.Hdg != 65535) Tele.Heading = G.Hdg / 100.f;
		if (FPlatformTime::Seconds() - LastAttWall > 0.1) { Tele.Tbm = G.TimeBootMs; Publish(false); }
		break;
	}
	case SmvMav::MSG_VFR_HUD:
	{
		const SmvMav::FVfrHud V = SmvMav::DecodeVfrHud(F);
		Tele.Airspeed = V.Airspeed; Tele.Groundspeed = V.Groundspeed; Tele.Climb = V.Climb;
		Tele.Throttle = bMotorCut ? 0.f : (float)V.Throttle;
		Tele.Heading = (float)V.Heading;
		break;
	}
	case SmvMav::MSG_SYS_STATUS:
	{
		const SmvMav::FSysStatus S = SmvMav::DecodeSysStatus(F);
		Tele.BattV = S.VoltageMv == 65535 ? -1.f : S.VoltageMv / 1000.f;
		Tele.BattPct = S.BatteryRemaining < 0 ? -1.f : (float)S.BatteryRemaining;
		break;
	}
	case SmvMav::MSG_GPS_RAW_INT:
	{
		const SmvMav::FGpsRawInt G = SmvMav::DecodeGpsRawInt(F);
		Tele.GpsFix = G.FixType;
		Tele.Sats = G.Sats == 255 ? -1 : G.Sats;
		break;
	}
	case SmvMav::MSG_RC_CHANNELS:
	{
		const SmvMav::FRcChannels R = SmvMav::DecodeRcChannels(F);
		Tele.Rc.SetNum(8);
		for (int32 i = 0; i < 8; ++i) Tele.Rc[i] = R.Chan[i];
		break;
	}
	case SmvMav::MSG_SERVO_OUTPUT_RAW:
	{
		const SmvMav::FServoOutputRaw S = SmvMav::DecodeServoOutputRaw(F);
		if (S.Port != 0) break;
		Extra.Servo.SetNum(8);
		for (int32 i = 0; i < 8; ++i) Extra.Servo[i] = S.Servo[i];
		int Ch16[16];
		for (int32 i = 0; i < 16; ++i) { Servo16[i] = S.Servo[i]; Ch16[i] = S.Servo[i]; }
		if (ServoTrig)
		{
			const std::string R = ServoTrig->Sample(Ch16, Tele.Tbm, FPlatformTime::Seconds());
			if (!R.empty())
			{
				const SmvCore::FServoTrigCfg& C = ServoTrig->Cfg();
				TSharedRef<FJsonObject> M = SmvMsg(TEXT("servo_trigger"));
				M->SetStringField(TEXT("phase"), R == "already" ? TEXT("already") : R == "triggered" ? TEXT("triggered") : TEXT("fired"));
				M->SetNumberField(TEXT("ch"), C.Ch);
				M->SetNumberField(TEXT("pwm"), Ch16[C.Ch - 1]);
				M->SetNumberField(TEXT("agl"), Tele.AltRel);
				EmitMessage(M);
				if (R == "already") EmitEvent(FString::Printf(TEXT("Servo tetik: S%d zaten koşulda; yeni komut bekleniyor"), C.Ch), 4);
				if (R == "triggered") EmitEvent(FString::Printf(TEXT("SERVO TRIG S%d %dus AGL %.0fm"), C.Ch, Ch16[C.Ch - 1], Tele.AltRel), 4);
				if (R == "fired") Crash(TEXT("servo_trigger"), FMath::Max(Tele.Airspeed, Tele.Groundspeed));
			}
		}
		break;
	}
	case SmvMav::MSG_HOME_POSITION:
	{
		const SmvMav::FHomePosition H = SmvMav::DecodeHomePosition(F);
		Tele.bHasHome = true;
		Tele.HomeLat = H.Latitude / 1e7; Tele.HomeLon = H.Longitude / 1e7; Tele.HomeAlt = H.Altitude / 1000.0;
		HomeAltMsl = Tele.HomeAlt; bHomeKnown = true;
		break;
	}
	case SmvMav::MSG_PARAM_VALUE:
	{
		const SmvMav::FParamValue P = SmvMav::DecodeParamValue(F);
		const FString Id = UTF8_TO_TCHAR(P.Id.c_str());
		int32 N = 0;
		if (Id.StartsWith(TEXT("SERVO")) && Id.EndsWith(TEXT("_FUNCTION")) && LexTryParseString(N, *Id.Mid(5, Id.Len() - 14)) && N >= 1 && N <= 8)
		{
			Extra.ServoFn.SetNum(8);
			Extra.ServoFn[N - 1] = FMath::RoundToInt(P.Value);
		}
		break;
	}
	case SmvMav::MSG_COMMAND_ACK:
	{
		const SmvMav::FCommandAck A = SmvMav::DecodeCommandAck(F);
		if (A.Command == SmvMav::CMD_SET_MESSAGE_INTERVAL && A.Result != 0 && A.Result != 5 && ModeVehicle == TEXT("plane") && !bStreamFallback)
		{
			bStreamFallback = true;
			RequestStreams(false);
		}
		if (A.Command == 218 && A.Result != 0 && A.Result != 5 && bMotorCut)
		{
			bMotorCut = false;
			EmitEvent(TEXT("Motor kesme reddedildi (DO_AUX_FUNCTION)."), 4);
		}
		break;
	}
	case SmvMav::MSG_MISSION_CURRENT:
	{
		SmvMav::FReader Rd(F.Payload);
		MissionCur = Rd.U16();
		const int32 Total = F.Payload.size() >= 4 ? (int32)Rd.U16() : -1;
		// görev değişti (Mission Planner yükledi / temizledi): sayı farklıysa yeniden indir
		if (Total >= 0 && Total != MissionTotalSeen) { MissionTotalSeen = Total; if (!bMissionDl) bMissionReq = true; }
		break;
	}
	case SmvMav::MSG_MISSION_COUNT:
	{
		SmvMav::FReader Rd(F.Payload);
		const int32 N = Rd.U16();
		Rd.U8(); Rd.U8();
		const uint8 Type = F.Payload.size() >= 5 ? Rd.U8() : 0;
		if (Type != 0) break;
		MissionWant = N;
		MissionTmp.Reset();
		if (N <= 0) { bMissionDl = false; MissionWps.Reset(); ++MissionVer; break; }
		bMissionDl = true; MissionNext = 0; MissionTries = 0;
		MissionRequestItem(0);
		break;
	}
	case SmvMav::MSG_MISSION_ITEM_INT:
	{
		if (!bMissionDl) break;
		SmvMav::FReader Rd(F.Payload);
		FSmvMissionWp Wp;
		Rd.F32(); Rd.F32(); Rd.F32(); Rd.F32();
		const int32 X = Rd.I32(), Y = Rd.I32();
		Wp.Alt = Rd.F32();
		Wp.Seq = Rd.U16(); Wp.Cmd = Rd.U16();
		Rd.U8(); Rd.U8();
		Wp.Frame = Rd.U8();
		if (Wp.Seq != MissionNext) break;   // yinelenen / sırasız öğe
		Wp.Lat = X / 1e7; Wp.Lon = Y / 1e7;
		MissionTmp.Add(Wp);
		MissionNext = Wp.Seq + 1; MissionTries = 0;
		if (MissionNext < MissionWant) MissionRequestItem(MissionNext);
		else
		{
			// bitti: onay (MAV_MISSION_ACCEPTED) + yalnız konumlu noktalar (0 = ev; 0,0 konumlu komutlar çizilmez)
			SmvMav::FWriter Wr; Wr.U8(TargetSys); Wr.U8(TargetComp); Wr.U8(0); Wr.U8(0);
			SendFrame(SmvMav::MSG_MISSION_ACK, Wr.B.data(), (int32)Wr.B.size());
			MissionWps.Reset();
			for (const FSmvMissionWp& M : MissionTmp) if (M.Seq > 0 && (M.Lat != 0.0 || M.Lon != 0.0)) MissionWps.Add(M);
			bMissionDl = false; ++MissionVer;
			UE_LOG(LogSimumav, Log, TEXT("Otopilot görevi indirildi: %d öğe, %d konumlu nokta"), MissionWant, MissionWps.Num());
		}
		break;
	}
	case SmvMav::MSG_STATUSTEXT:
	{
		const SmvMav::FStatusText S = SmvMav::DecodeStatusText(F);
		const FString Text = UTF8_TO_TCHAR(S.Text.c_str());
		EmitEvent(Text, S.Severity);
		if (Text.Contains(TEXT("is using GPS"))) bEkfGpsText = true;
		// "SIM Hit ground at X m/s" — SITL fiziği yere çarptı
		int32 At = Text.Find(TEXT("Hit ground at"));
		if (At != INDEX_NONE && bUeRest)
		{
			// araç UE'de yapı/arazi üstünde duruyor; SITL kendi (daha alçak) zeminine indi — çarpışma değil
			EmitEvent(TEXT("SITL zemin teması yok sayıldı: araç 3B dünyada yüzey üstünde duruyor"), 6);
		}
		else if (At != INDEX_NONE)
		{
			const double X = FCString::Atod(*Text.Mid(At + 13).TrimStart());
			const bool bMoving = Tele.AltRel >= 1.0 || Tele.Groundspeed >= 3.f;
			if (CrashCandidateUntil > 0) { CrashCandidateUntil = 0; Crash(TEXT("terrain"), FMath::Max(X, CrashCandidateSpeed)); }
			else if (X >= 3.0 && bMoving) Crash(TEXT("terrain"), X);
		}
		break;
	}
	case SmvMav::MSG_EKF_STATUS_REPORT:
	{
		const SmvMav::FEkfStatus E = SmvMav::DecodeEkfStatus(F);
		EkfFlags = E.Flags;
		bEkfSeen = true;
		Extra.Ekf = FMath::Max(FMath::Max(E.VelVar, E.PosHVar), FMath::Max(E.PosVVar, E.CompassVar));
		break;
	}
	case SmvMav::MSG_EXTENDED_SYS_STATE:
	default:
		break;
	}
}

void USmvLocalFlightLink::Publish(bool bFromAttitude)
{
	if (bCrashed && ServoTrig && ServoTrig->State() == SmvCore::FServoTrigger::EState::Fired) return;   // görüntü donar
	const double W = FPlatformTime::Seconds();
	if (bFromAttitude)
	{
		if (Tele.Tbm - LastPublishTbm < (int64)(GSmvTeleTbmGate * 1000.0) && Tele.Tbm >= LastPublishTbm) return;
		if (Session && Session->Speedup > 1.0 && W - LastPublishWall < 0.9 * 0.020) return;
	}
	LastPublishTbm = Tele.Tbm;
	LastPublishWall = W;
	{
		const double Wall = Now();
		double Ts = Wall;
		if (LastTs > 0 && LastTsTbm >= 0 && Tele.Tbm >= LastTsTbm && Session)
		{
			if (Tele.Tbm == LastTsTbm) Ts = LastTs;   // yeni SITL karesi yok (bekçi yayını): aynı zaman → tampon son kareyi günceller
			else
			{
				const double Pred = LastTs + (double)(Tele.Tbm - LastTsTbm) / 1000.0 / FMath::Max(0.25, Session->Speedup);
				const double Err = Wall - Pred;
				if (FMath::Abs(Err) < 0.15) Ts = Pred + Err * 0.05;   // varış titremesini süz, saat kaymasını yavaşça izle
				Ts = FMath::Max(Ts, LastTs + 1e-4);
			}
		}
		LastTs = Ts;
		LastTsTbm = Tele.Tbm;
		Tele.Ts = Ts;
	}
	Tele.bGcsConnected = bGcsConnected;
	Extra.bGcsConnected = bGcsConnected;
	Extra.bMotorCut = bMotorCut;
	if (Power)
	{
		std::vector<int> Sv;
		for (int32 V : Extra.Servo) Sv.push_back(V);
		for (const std::string& E : Power->Step(Tele.bArmed, Tele.Groundspeed, Tele.Throttle, Sv, Tele.Tbm))
		{
			const bool bFuel = E == "fuel";
			TSharedRef<FJsonObject> M = SmvMsg(TEXT("warn"));
			M->SetStringField(TEXT("kind"), bFuel ? TEXT("fuel_empty") : TEXT("elec_empty"));
			M->SetStringField(TEXT("text"), bFuel ? TEXT("Yakıt bitti") : TEXT("Batarya bitti"));
			M->SetNumberField(TEXT("ts"), Tele.Ts);
			EmitMessage(M);
			EmitEvent(bFuel ? TEXT("YAKIT BİTTİ — motor gücü yok sayılır, süzülerek inin") : TEXT("BATARYA BİTTİ"), 2);
		}
		const SmvCore::FPowerTele P = Power->Tele();
		Extra.Speedup = P.Speedup;
		Extra.bFuel = P.bFuelOn; Extra.FuelL = P.FuelL; Extra.FuelPct = P.FuelPct; Extra.RangeKm = P.RangeKm; Extra.bFuelEmpty = P.bFuelEmpty;
		Extra.bElec = P.bElecOn; Extra.ElecWh = P.Wh; Extra.ElecPct = P.ElecPct; Extra.ElecW = P.W; Extra.ElecMinLeft = P.MinLeft; Extra.ElecV = P.V; Extra.bElecEmpty = P.bElecEmpty;
	}
	if (Targets && Tele.Lat != 0.0)
	{
		double X = 0, Y = 0;
		SmvCore::TargetLocal(Req.Track.Lat, Req.Track.Lon, Tele.Lat, Tele.Lon, X, Y);
		const double Z = Tele.AltMsl - (bHomeKnown ? HomeAltMsl : Session->HomeAlt);
		for (const SmvCore::FTargetEvent& E : Targets->Step(Tele.Tbm / 1000.0, X, Y, Z, Tele.bArmed))
		{
			TSharedRef<FJsonObject> M = SmvMsg(E.Kind == "intercept" ? TEXT("target") : TEXT("engage"));
			M->SetStringField(TEXT("kind"), E.Kind == "intercept" ? TEXT("intercept") : FString(UTF8_TO_TCHAR(E.Kind.c_str())));
			M->SetNumberField(TEXT("i"), E.I);
			M->SetNumberField(TEXT("dist_m"), E.DistM);
			M->SetNumberField(TEXT("n"), E.N);
			M->SetBoolField(TEXT("first"), E.bFirst);
			M->SetBoolField(TEXT("locked"), E.bLocked);
			if (!E.Reason.empty()) M->SetStringField(TEXT("reason"), UTF8_TO_TCHAR(E.Reason.c_str()));
			M->SetNumberField(TEXT("tt"), Tele.Tbm / 1000.0);
			EmitMessage(M);
		}
	}
	EmitTele();
}

// ---------------------------------------------------------------------------
// komutlar
// ---------------------------------------------------------------------------

void USmvLocalFlightLink::SendRc(const TArray<int32>& Ch)
{
	if (!Tcp || !bHeartbeatSeen || bCrashed) return;
	uint16 C[18];
	for (int32 i = 0; i < 18; ++i) C[i] = 0;
	bool bAllZero = true;
	for (int32 i = 0; i < 8 && i < Ch.Num(); ++i)
	{
		C[i] = Ch[i] == 0 ? (uint16)0 : (uint16)FMath::Clamp(Ch[i], 1000, 2000);
		if (Ch[i] != 0) bAllZero = false;
	}
	// helikopter: RC8 motor kilidi her zaman ARM durumundan (pilot kanalı ezmez)
	if (bHeliInterlock) { C[7] = Tele.bArmed ? 2000 : 1000; bAllZero = false; }
	// sunucu sözleşmesi: 0 = "o kanala dokunma" (MAVLink'te 1–8 için 65535); 8×0 = override'ı bırak (MAVLink'te 0)
	if (!bAllZero) for (int32 i = 0; i < 8; ++i) if (C[i] == 0) C[i] = 65535;
	const std::vector<uint8_t> P = SmvMav::PackRcOverride(TargetSys, TargetComp, C);
	SendFrame(SmvMav::MSG_RC_CHANNELS_OVERRIDE, P.data(), (int32)P.size());
}

FString USmvLocalFlightLink::Command(const FString& Name, const TSharedPtr<FJsonObject>& P)
{
	const FString Rid = NextRid();
	auto Fail = [this, &Rid, &Name](const FString& E) { EmitResult(Rid, Name, false, E); return Rid; };
	if (Name == TEXT("reset")) { BeginReset(); EmitResult(Rid, Name, true); return Rid; }
	if (!Tcp || !bHeartbeatSeen) return Fail(TEXT("SITL hazır değil"));
	const std::string Type = Session->SitlType;
	// konum tahmini hazır değilken kalkış / konum isteyen kipte ARM: kuyruğa alınır (TickPosReady hazır olunca yürütür)
	if (!bPosReady && NeedsPosition(Name))
	{
		const bool bWasQueued = !PendingCmd.IsEmpty();
		PendingCmd = Name;
		PendingParams = P;
		PendingSince = FPlatformTime::Seconds();
		Extra.bCmdQueued = true;
		if (!bWasQueued) EmitEvent(TEXT("Hazırlanıyor: GPS/EKF konumu bekleniyor — hazır olunca komut kendiliğinden gönderilecek"), 5);
		EmitResult(Rid, Name, true);
		return Rid;
	}
	if (Name == TEXT("arm") || Name == TEXT("takeoff")) bUeRest = false;
	if (Name == TEXT("arm") || Name == TEXT("disarm"))
	{
		if (Name == TEXT("arm") && bMotorCut) { CommandLong(218, 31.f, 0.f); bMotorCut = false; }
		if (bHeliInterlock) PumpHeliInterlock(FPlatformTime::Seconds(), true);   // ARM öncesi kilit düşük (ARM'sız durumda 1000)
		CommandLong(SmvMav::CMD_COMPONENT_ARM_DISARM, Name == TEXT("arm") ? 1.f : 0.f, SmvMav::ARM_FORCE_MAGIC);
	}
	else if (Name == TEXT("mode"))
	{
		if (!SetMode(SmvJson::Str(P, TEXT("mode")))) return Fail(TEXT("bilinmeyen kip"));
	}
	else if (Name == TEXT("takeoff"))
	{
		if (Type == "rover") return Fail(TEXT("kara aracında kalkış yok"));
		const double Def = SmvCore::IsPlaneType(Type) ? 50.0 : 10.0;
		StartTakeoff((float)FMath::Clamp(SmvJson::Num(P, TEXT("alt"), Def), 1.0, 1000.0));
	}
	else if (Name == TEXT("rtl")) CommandLong(SmvMav::CMD_NAV_RETURN_TO_LAUNCH);
	else if (Name == TEXT("land")) { if (!SetMode(ModeVehicle == TEXT("plane") ? TEXT("QLAND") : TEXT("LAND"))) CommandLong(SmvMav::CMD_NAV_LAND); }
	else if (Name == TEXT("set_home"))
	{
		if (P.IsValid() && P->HasField(TEXT("lat")))
			CommandLong(SmvMav::CMD_DO_SET_HOME, 0.f, 0, 0, 0, (float)SmvJson::Num(P, TEXT("lat")), (float)SmvJson::Num(P, TEXT("lon")), (float)SmvJson::Num(P, TEXT("alt"), Tele.AltMsl));
		else CommandLong(SmvMav::CMD_DO_SET_HOME, 1.f);
	}
	else if (Name == TEXT("speed"))
	{
		const double Fct = SmvJson::Num(P, TEXT("factor"), 1);
		bool bAllowed = false;
		for (double S : SmvCore::Speedups()) if (FMath::IsNearlyEqual(S, Fct)) bAllowed = true;
		if (!bAllowed) return Fail(TEXT("izinli hızlar: 0,25 0,5 1 2 4 8"));
		ParamSet(TEXT("SIM_SPEEDUP"), (float)Fct);
		Session->Speedup = Fct;
		if (Power) Power->SetSpeedup(Fct);
		SessionUserParams.RemoveAll([](const TPair<FString, float>& Kv) { return Kv.Key == TEXT("SIM_SPEEDUP"); });
	}
	else if (Name == TEXT("servo_set"))
	{
		const int32 Ch = (int32)SmvJson::Num(P, TEXT("ch"), 0), Pwm = (int32)SmvJson::Num(P, TEXT("pwm"), 0);
		if (Ch < 1 || Ch > 16 || Pwm < 800 || Pwm > 2200) return Fail(TEXT("kanal 1–16, pwm 800–2200"));
		CommandLong(SmvMav::CMD_DO_SET_SERVO, (float)Ch, (float)Pwm);
	}
	else if (Name == TEXT("servo_trigger"))
	{
		SmvCore::FServoTrigCfg C;
		C.bOn = SmvJson::Bool(P, TEXT("on"), true);
		C.Ch = (int32)SmvJson::Num(P, TEXT("ch"), 9);
		C.Pwm = (int32)SmvJson::Num(P, TEXT("pwm"), 1900);
		C.Cmp = std::string(TCHAR_TO_UTF8(*SmvJson::Str(P, TEXT("cmp"), TEXT("ge"))));
		C.Tol = (int32)SmvJson::Num(P, TEXT("tol"), 50);
		C.DelayMs = (int32)SmvJson::Num(P, TEXT("delay_ms"), 0);
		ServoTrig->Configure(C);
		EmitEvent(C.bOn ? FString::Printf(TEXT("Servo tetik dinleniyor: S%d %s %d µs, gecikme %d ms"), C.Ch, *SmvJson::Str(P, TEXT("cmp"), TEXT("ge")), C.Pwm, C.DelayMs) : TEXT("Servo tetik kapatıldı"), 6);
	}
	else if (Name == TEXT("motor_cut"))
	{
		if (!SmvCore::IsPlaneType(Type)) return Fail(TEXT("yalnız uçak tiplerinde"));
		bMotorCut = SmvJson::Bool(P, TEXT("on"), true);
		CommandLong(218, 31.f, bMotorCut ? 2.f : 0.f);
	}
	else if (Name == TEXT("crash"))
	{
		const FString Kind = SmvJson::Str(P, TEXT("kind"), TEXT("terrain"));
		const double Spd = SmvJson::Num(P, TEXT("speed"), Tele.Groundspeed);
		if (SmvJson::Bool(P, TEXT("physics"), false))
		{
			// Unreal fizik sorgusu (gövde süpürmesi) gerçek geometriye değdi: arazi dahil KESİN (SITL'in düz zemini 3B araziyi bilmez,
			// "Hit ground" teyidi beklenmez). Sunucu (bulut) kuralı değişmedi: orada arazi adaydır.
			Crash(Kind, Spd);
		}
		else if (Kind == TEXT("terrain"))
		{
			// arazi: aday — 3 sn içinde SITL "Hit ground" derse kesinleşir (flightsim_mission.carpisma)
			CrashCandidateUntil = FPlatformTime::Seconds() + 3.0;
			CrashCandidateSpeed = Spd;
		}
		else Crash(Kind, Spd);
	}
	else if (Name == TEXT("engage"))
	{
		if (!Targets) return Fail(TEXT("hedef yok"));
		const FString Act = SmvJson::Str(P, TEXT("action"));
		std::vector<SmvCore::FTargetEvent> Ev = Act == TEXT("lock") ? Targets->Lock((int32)SmvJson::Num(P, TEXT("i"), 1))
			: Targets->Release(std::string(TCHAR_TO_UTF8(*SmvJson::Str(P, TEXT("reason"), TEXT("released")))));
		for (const SmvCore::FTargetEvent& E : Ev)
		{
			TSharedRef<FJsonObject> M = SmvMsg(TEXT("engage"));
			M->SetStringField(TEXT("kind"), UTF8_TO_TCHAR(E.Kind.c_str()));
			M->SetNumberField(TEXT("i"), E.I);
			if (!E.Reason.empty()) M->SetStringField(TEXT("reason"), UTF8_TO_TCHAR(E.Reason.c_str()));
			EmitMessage(M);
		}
	}
	else if (Name == TEXT("land_rest"))
	{
		// UE: araç yapı/arazi üstüne indi ve SITL hâlâ alçalıyor (SITL zemini daha aşağıda) — otopilotun iniş algılayıcısı gerçek yüzeyde
		// motorları keserdi; burada zorla disarm + SITL'in kendi zeminine düşüşü çarpışma sayılmaz
		bUeRest = true;
		ForcedDisarm();
		EmitEvent(TEXT("Yüzey üstüne iniş (3B fizik): motorlar durduruldu"), 5);
	}
	else if (Name == TEXT("gripper"))
	{
		// paket bırakma aparatı (MAV_CMD_DO_GRIPPER 211: p1 tutucu no, p2 0 = bırak / 1 = tut)
		CommandLong(211, 1.f, SmvJson::Str(P, TEXT("action"), TEXT("release")) == TEXT("grab") ? 1.f : 0.f);
	}
	else if (Name == TEXT("sprayer"))
	{
		// ilaçlama (MAV_CMD_DO_SPRAYER 216: p1 1 aç / 0 kapat)
		CommandLong(216, SmvJson::Bool(P, TEXT("on"), true) ? 1.f : 0.f);
	}
	else if (Name == TEXT("parachute"))
	{
		// paraşüt (MAV_CMD_DO_PARACHUTE 208: p1 2 = aç/bırak)
		CommandLong(208, 2.f);
	}
	else if (Name == TEXT("camera") || Name == TEXT("gpws"))
	{
		// yalnız istemci tarafı (görüntü kipi / uyarı özeti oyun katmanında tutulur)
	}
	else if (Name == TEXT("goto"))
	{
		// haritadan "koordinata git": MAV_CMD_DO_REPOSITION (COMMAND_INT, kare GLOBAL_RELATIVE_ALT_INT); param2 bit 1 → kendiliğinden GUIDED
		// (Copter ve Plane). Yalnız oyuncu komutu — otomatik güdüm yok.
		const double Lat = SmvJson::Num(P, TEXT("lat")), Lon = SmvJson::Num(P, TEXT("lon"));
		const float AltRel = (float)SmvJson::Num(P, TEXT("alt"), Tele.AltRel);
		SmvMav::FWriter Wr;
		Wr.F32(-1.f); Wr.F32(1.f); Wr.F32(0.f); Wr.F32(NAN);
		Wr.I32((int32)FMath::RoundToInt(Lat * 1e7)); Wr.I32((int32)FMath::RoundToInt(Lon * 1e7)); Wr.F32(AltRel);
		Wr.U16(192); Wr.U8(TargetSys); Wr.U8(TargetComp); Wr.U8(6); Wr.U8(0); Wr.U8(0);
		SendFrame(SmvMav::MSG_COMMAND_INT, Wr.B.data(), (int32)Wr.B.size());
		EmitEvent(FString::Printf(TEXT("Konuma git: %.6f, %.6f · %.0f m"), Lat, Lon, AltRel), 6);
	}
	else if (Name == TEXT("yaw"))
	{
		// MAV_CMD_CONDITION_YAW (115): p1 hedef yön (°, kuzeyden saat yönü), p2 dönüş hızı (°/sn), p3 yön (0 en kısa), p4 0 = mutlak (GUIDED'da)
		CommandLong(115, (float)FMath::Fmod(SmvJson::Num(P, TEXT("deg")) + 360.0, 360.0), (float)SmvJson::Num(P, TEXT("rate"), 45.0), 0.f, 0.f);
	}
	else if (Name == TEXT("param_set"))
	{
		const FString Id = SmvJson::Str(P, TEXT("name")).ToUpper();
		if (Id.StartsWith(TEXT("SIM_")) && !Id.StartsWith(TEXT("SIM_BATT_"))) return Fail(TEXT("SIM_* parametreleri oturumda değiştirilemez"));
		const float V = (float)SmvJson::Num(P, TEXT("value"));
		ParamSet(Id, V);
		SessionUserParams.RemoveAll([&Id](const TPair<FString, float>& Kv) { return Kv.Key == Id; });
		SessionUserParams.Add(TPair<FString, float>(Id, V));
	}
	else return Fail(TEXT("bilinmeyen komut"));
	EmitResult(Rid, Name, true);
	return Rid;
}

void USmvLocalFlightLink::LaunchEvent(const FString& Phase)
{
	LaunchPhase = Phase;
	TSharedRef<FJsonObject> M = SmvMsg(TEXT("launch"));
	M->SetStringField(TEXT("method"), UTF8_TO_TCHAR(Session->Launch.Method.c_str()));
	M->SetStringField(TEXT("phase"), Phase);
	M->SetNumberField(TEXT("ts"), Now());
	if (Session->Launch.Method == "ramp") { M->SetNumberField(TEXT("angle_deg"), Session->Launch.AngleDeg); M->SetNumberField(TEXT("release_kmh"), Session->Launch.ReleaseKmh); }
	if (Session->Launch.Method == "vehicle") M->SetNumberField(TEXT("release_kmh"), Session->Launch.ReleaseKmh);
	if (Phase == TEXT("released")) M->SetNumberField(TEXT("alt"), Tele.AltRel);
	EmitMessage(M);
}

void USmvLocalFlightLink::StartTakeoff(float AltM)
{
	const std::string Type = Session->SitlType;
	TWeakObjectPtr<USmvLocalFlightLink> Weak(this);
	if (Type == "sabit_kanat")
	{
		// kalkış seti (TKOFF_ALT dahil) → TAKEOFF kipi → 0,3 sn sonra ARM (flightsim._kalkis_dizisi)
		SmvCore::FSitlLaunch L = Session->Launch;
		L.TakeoffAltM = AltM;
		for (const auto& Kv : SmvCore::LaunchParams(L)) ParamSet(UTF8_TO_TCHAR(Kv.first.c_str()), (float)Kv.second);
		if (bMotorCut) { CommandLong(218, 31.f, 0.f); bMotorCut = false; }
		SetMode(TEXT("TAKEOFF"));
		After(0.3, [Weak]()
		{
			USmvLocalFlightLink* S = Weak.Get();
			if (!S) return;
			S->CommandLong(SmvMav::CMD_COMPONENT_ARM_DISARM, 1.f, SmvMav::ARM_FORCE_MAGIC);
			S->LaunchEvent(TEXT("armed"));
			S->LaunchT0 = FPlatformTime::Seconds();
		});
		if (Session->Launch.Method == "hand")
		{
			// elden atış: 3 sn geri sayım ("3 · 2 · 1 · AT!" — HUD), ardından kol savurma; SITL mancınığı kısa darbe:
			// 15 m/s² × t ≈ atış hızı (6–8 m/s) + 4 m/s pervane itişi payı → servo 7 yüksek t sn, sonra bırak
			// SITL'de uçak yerden (1,8 m omuz yüksekliği yok) atılır: tutunma hızı için darbe ≈ (atış + 6 m/s) / 15 m/s²
			const double Pulse = FMath::Clamp((Session->Launch.HandSpeedMs + 6.0) / 15.0, 0.6, 1.0);
			After(3.3, [Weak]() { if (USmvLocalFlightLink* S = Weak.Get()) { S->LaunchEvent(TEXT("launch")); S->CommandLong(SmvMav::CMD_DO_SET_SERVO, 7.f, 1900.f); } });
			After(3.3 + Pulse, [Weak]() { if (USmvLocalFlightLink* S = Weak.Get()) S->CommandLong(SmvMav::CMD_DO_SET_SERVO, 7.f, 1100.f); });
			return;
		}
		After(0.6, [Weak]() { if (USmvLocalFlightLink* S = Weak.Get()) S->LaunchEvent(TEXT("launch")); });
		if (Session->Launch.Method == "ramp")
		{
			// catapult: SIM_Plane servos[6] > 1700 → 15 m/s² · 2 sn
			After(1.5, [Weak]() { if (USmvLocalFlightLink* S = Weak.Get()) S->CommandLong(SmvMav::CMD_DO_SET_SERVO, 7.f, 1900.f); });
		}
		return;
	}
	if (Type == "vtol" || Type == "interceptor")
	{
		// dikey kalkış: GUIDED → 0,4 sn → ARM → 1 sn → NAV_TAKEOFF; 3 sn'de 1 m'yi geçmezse en çok 3 kez yinele
		SetMode(TEXT("GUIDED"));
		const bool bArmed = Tele.bArmed;
		After(0.4, [Weak, bArmed]() { if (USmvLocalFlightLink* S = Weak.Get()) if (!bArmed) S->CommandLong(SmvMav::CMD_COMPONENT_ARM_DISARM, 1.f, SmvMav::ARM_FORCE_MAGIC); });
		for (int32 Try = 0; Try < 3; ++Try)
		{
			After(1.4 + Try * 3.0, [Weak, AltM, Try]()
			{
				USmvLocalFlightLink* S = Weak.Get();
				if (!S || (Try > 0 && S->Tele.AltRel > 1.0)) return;
				S->CommandLong(SmvMav::CMD_NAV_TAKEOFF, 0, 0, 0, 0, 0, 0, AltM);
			});
		}
		return;
	}
	// kopter / heli: GUIDED → (ARM) → NAV_TAKEOFF p7 = irtifa
	SetMode(TEXT("GUIDED"));
	const bool bArmed = Tele.bArmed;
	After(0.3, [Weak, bArmed]() { if (USmvLocalFlightLink* S = Weak.Get()) if (!bArmed) S->CommandLong(SmvMav::CMD_COMPONENT_ARM_DISARM, 1.f, SmvMav::ARM_FORCE_MAGIC); });
	if (bHeliInterlock)
	{
		// ArduHeli: ARM → RC8 yüksek → rotor devre girer (H_RSC_RUNUP_TIME ~10 sn); o bitene dek NAV_TAKEOFF reddedilir → 2 sn'de bir, 1 m'yi geçene dek
		for (int32 Try = 0; Try < 14; ++Try)
		{
			After(1.3 + Try * 2.0, [Weak, AltM, Try]()
			{
				USmvLocalFlightLink* S = Weak.Get();
				if (!S || (Try > 0 && S->Tele.AltRel > 1.0)) return;
				S->CommandLong(SmvMav::CMD_NAV_TAKEOFF, 0, 0, 0, 0, 0, 0, AltM);
			});
		}
		return;
	}
	After(bArmed ? 0.4 : 1.3, [Weak, AltM]() { if (USmvLocalFlightLink* S = Weak.Get()) S->CommandLong(SmvMav::CMD_NAV_TAKEOFF, 0, 0, 0, 0, 0, 0, AltM); });
}

void USmvLocalFlightLink::PumpHeliInterlock(double Wall, bool bForce)
{
	if (!bHeliInterlock || !Tcp || bCrashed) return;
	if (!bForce && Wall < NextInterlockAt) return;
	NextInterlockAt = Wall + 0.25;
	uint16 C[18];
	for (int32 i = 0; i < 18; ++i) C[i] = i < 8 ? 65535 : 0;   // 65535 = "bu kanala dokunma" (pilot kanalları korunur)
	C[7] = Tele.bArmed ? 2000 : 1000;
	const std::vector<uint8_t> P = SmvMav::PackRcOverride(TargetSys, TargetComp, C);
	SendFrame(SmvMav::MSG_RC_CHANNELS_OVERRIDE, P.data(), (int32)P.size());
}

void USmvLocalFlightLink::TickLaunch()
{
	if (LaunchPhase != TEXT("launch") && LaunchPhase != TEXT("armed")) return;
	const double W = FPlatformTime::Seconds();
	if (W - LaunchT0 > 90.0) { LaunchPhase.Empty(); return; }
	const std::string M = Session->Launch.Method;
	bool bReleased = false;
	if (M == "ramp") bReleased = Tele.Groundspeed >= 8.f || Tele.AltRel >= 2.0;
	else if (M == "vehicle") bReleased = Tele.Groundspeed >= 0.95 * Session->Launch.ReleaseKmh / 3.6 || Tele.AltRel >= 3.0;
	else if (M == "hand") bReleased = LaunchPhase == TEXT("launch") && (Tele.Groundspeed >= 4.f || W - LaunchT0 > 4.2);
	else bReleased = Tele.AltRel >= 1.0;
	if (bReleased && LaunchPhase == TEXT("launch"))
	{
		LaunchEvent(TEXT("released"));
		if (M == "ramp")
		{
			TWeakObjectPtr<USmvLocalFlightLink> Weak(this);
			After(2.5, [Weak]() { if (USmvLocalFlightLink* S = Weak.Get()) S->CommandLong(SmvMav::CMD_DO_SET_SERVO, 7.f, 1100.f); });
		}
	}
}

bool USmvLocalFlightLink::NeedsPosition(const FString& Cmd) const
{
	if (Session && Session->SitlType == "rover") return false;
	if (Cmd == TEXT("takeoff")) return true;
	if (Cmd != TEXT("arm") || ModeVehicle != TEXT("copter")) return false;
	// ArduCopter: konum isteyen kiplerde ARM "Need Position Estimate" ile reddedilir (zorla ARM da geçmez)
	static const TCHAR* PosModes[] = { TEXT("GUIDED"), TEXT("LOITER"), TEXT("AUTO"), TEXT("POSHOLD"), TEXT("RTL"), TEXT("CIRCLE"), TEXT("BRAKE"), TEXT("SMART_RTL"), TEXT("ZIGZAG"), TEXT("FOLLOW"), TEXT("AUTO_RTL") };
	const FString M = Tele.Mode.ToUpper();
	for (const TCHAR* X : PosModes) if (M == X) return true;
	return false;
}

void USmvLocalFlightLink::TickPosReady(double Wall)
{
	if (!bHeartbeatSeen || !Tcp) { Extra.PosReady = Session && Session->SitlType == "rover" ? 1 : 0; return; }
	// EKF_STATUS_REPORT: mutlak yatay konum var, sabit konum kipi / başlatılmamış değil; yoksa "EKFn IMUn is using GPS" metni
	const bool bEkfOk = bEkfSeen ? ((EkfFlags & 0x10) != 0 && (EkfFlags & 0x80) == 0 && (EkfFlags & 0x400) == 0) : bEkfGpsText;
	const bool bRaw = Tele.GpsFix >= 3 && bHomeKnown && bEkfOk;
	if (!bRaw) { PosRawSince = 0; bPosReady = false; }
	else if (PosRawSince <= 0) PosRawSince = Wall;
	// oturma: ham koşuldan 2 sn sonra (ev / köken yerleşir; ilk saniyelerde ARM hâlâ reddedilebiliyor)
	if (bRaw && !bPosReady && Wall - PosRawSince >= 2.0)
	{
		bPosReady = true;
		EmitEvent(TEXT("GPS/EKF hazır: konum tahmini var"), 6);
	}
	Extra.PosReady = bPosReady ? 1 : 0;
	if (PendingCmd.IsEmpty()) { Extra.bCmdQueued = false; return; }
	if (bPosReady)
	{
		const FString C = PendingCmd;
		const TSharedPtr<FJsonObject> Pp = PendingParams;
		PendingCmd.Empty(); PendingParams.Reset();
		Extra.bCmdQueued = false;
		EmitEvent(C == TEXT("takeoff") ? TEXT("GPS/EKF hazır — kuyruktaki kalkış başlatılıyor") : TEXT("GPS/EKF hazır — kuyruktaki ARM gönderiliyor"), 5);
		Command(C, Pp);
	}
	else if (Wall - PendingSince > 120.0)
	{
		PendingCmd.Empty(); PendingParams.Reset();
		Extra.bCmdQueued = false;
		EmitEvent(TEXT("GPS/EKF 120 sn içinde hazır olmadı; kuyruktaki komut iptal edildi"), 3);
	}
}

void USmvLocalFlightLink::ForcedDisarm()
{
	if (bMotorCut) { CommandLong(218, 31.f, 0.f); bMotorCut = false; }
	CommandLong(SmvMav::CMD_COMPONENT_ARM_DISARM, 0.f, SmvMav::ARM_FORCE_MAGIC);
}

void USmvLocalFlightLink::Crash(const FString& Reason, double Speed)
{
	if (bCrashed) return;
	bCrashed = true;
	ForcedDisarm();
	TSharedRef<FJsonObject> M = SmvMsg(TEXT("crash"));
	M->SetNumberField(TEXT("ts"), Now());
	M->SetNumberField(TEXT("lat"), Tele.Lat);
	M->SetNumberField(TEXT("lon"), Tele.Lon);
	M->SetNumberField(TEXT("alt_msl"), Tele.AltMsl);
	M->SetNumberField(TEXT("alt_rel"), Tele.AltRel);
	M->SetNumberField(TEXT("speed"), SmvCore::RoundN(Speed, 1));
	M->SetStringField(TEXT("mode"), Tele.Mode);
	M->SetStringField(TEXT("reason"), Reason);
	EmitMessage(M);
	EmitEvent(FString::Printf(TEXT("ÇARPIŞMA (%s) %.1f m/s"), *Reason, Speed), 2);
	if (Reason == TEXT("servo_trigger")) EndReason = TEXT("servo_trigger");
}

void USmvLocalFlightLink::BeginReset()
{
	// Sıfırla: TCP kapanır, SITL durur, tele sıfırlanır, aynı port/sysid ile eeprom KORUNARAK yeniden açılır (flightsim §35)
	bResetting = true;
	CloseTcp();
	KillSitl();
	bHeartbeatSeen = false;
	bAfterReset = true;
	bCrashed = false;
	bUeRest = false;
	CrashCandidateUntil = 0;
	LaunchPhase.Empty();
	Timed.Reset();
	Tele.Mode.Empty();
	Tele.bArmed = false;
	Tele.AltRel = 0;
	Tele.bHasHome = false;
	bHomeKnown = false;
	bEkfSeen = false; bEkfGpsText = false; EkfFlags = 0; PosRawSince = 0; bPosReady = false;
	PendingCmd.Empty(); PendingParams.Reset(); Extra.bCmdQueued = false; Extra.PosReady = 0;
	LastPublishTbm = -1000;
	LastTs = 0;
	LastTsTbm = -1;
	if (Power) Power->ResetTime();
	if (Targets) Targets->Release("reset");
	if (Parser) { delete Parser; Parser = new SmvMav::FParser(); }
	EmitEvent(TEXT("Sıfırla: SITL yeniden başlatılıyor (parametreler ve görev korunur)"), 5);
	EmitState(TEXT("BASLIYOR"));
	if (!LaunchSitl(false)) { EmitState(TEXT("HATA")); bResetting = false; }
}

// ---------------------------------------------------------------------------
// fizik sensörleri → SITL (MAVLink; ArduPilot RNGFND1_TYPE=10, PRX1_TYPE=2)
// ---------------------------------------------------------------------------

void USmvLocalFlightLink::SendRangeSensors(const FSmvRangeScan& Scan)
{
	if (!Tcp || !bHeartbeatSeen) return;
	const uint32 Tbm = (uint32)FMath::Max<int64>(0, Tele.Tbm);
	const uint16 MinCm = (uint16)FMath::RoundToInt(Scan.MinM * 100.f);
	if (bSensRng)
	{
		// aşağı (yön 25 = PITCH_270): RNGFND1 — irtifa tutma / yüzey izleme / iniş gerçek zemine (çatı, ağaç, arazi) göre
		const uint16 Max = (uint16)FMath::RoundToInt(Scan.DownMaxM * 100.f);
		const std::vector<uint8_t> P = SmvMav::PackDistanceSensor(Tbm, MinCm, Max, SmvCore::RangeToCm(Scan.DownM, Scan.MinM, Scan.DownMaxM), 0, 0, 25);
		SendFrame(SmvMav::MSG_DISTANCE_SENSOR, P.data(), (int32)P.size());
	}
	if (bSensPrx)
	{
		const uint16 Max = (uint16)FMath::RoundToInt(Scan.ProxMaxM * 100.f);
		// yukarı (yön 24 = PITCH_90): AP_Proximity tavan mesafesi
		const std::vector<uint8_t> U = SmvMav::PackDistanceSensor(Tbm, MinCm, Max, SmvCore::RangeToCm(Scan.UpM, Scan.MinM, Scan.ProxMaxM), 0, 1, 24);
		SendFrame(SmvMav::MSG_DISTANCE_SENSOR, U.data(), (int32)U.size());
		if (Scan.SectorM.Num() > 0)
		{
			double D[72];
			const int32 N = FMath::Min(72, Scan.SectorM.Num());
			for (int32 i = 0; i < N; ++i) D[i] = Scan.SectorM[i];
			uint16_t Arr[72];
			SmvCore::ObstacleArray(D, N, Scan.MinM, Scan.ProxMaxM, Arr);
			const std::vector<uint8_t> O = SmvMav::PackObstacleDistance((uint64_t)Tbm * 1000ull, Arr, MinCm, Max, 360.f / (float)N, 0.f, 12);
			SendFrame(SmvMav::MSG_OBSTACLE_DISTANCE, O.data(), (int32)O.size());
		}
	}
}

// ---------------------------------------------------------------------------
// Mission Planner UDP köprüsü
// ---------------------------------------------------------------------------

void USmvLocalFlightLink::OpenMp()
{
	ISocketSubsystem* SS = SmvSockets();
	Udp = SS->CreateSocket(NAME_DGram, TEXT("SmvMpUdp"), true);
	if (!Udp) { EmitEvent(TEXT("Mission Planner UDP soketi açılamadı."), 4); return; }
	Udp->SetNonBlocking(true);
	Udp->SetReuseAddr(true);
	TSharedRef<FInternetAddr> Local = SS->CreateInternetAddr();
	Local->SetAnyAddress();
	Local->SetPort(0);
	Udp->Bind(*Local);
	MpAddr = SS->CreateInternetAddr();
	bool bValid = false;
	MpAddr->SetIp(*MpHost, bValid);
	MpAddr->SetPort(MpPort);
	if (!bValid) { EmitEvent(FString::Printf(TEXT("Mission Planner adresi geçersiz: %s (yalnız IP)"), *MpHost), 4); MpAddr.Reset(); return; }
	EmitEvent(FString::Printf(TEXT("Mission Planner UDP çıkışı açık: %s:%d"), *MpHost, MpPort), 6);
}

void USmvLocalFlightLink::PumpMp()
{
	if (!Udp) return;
	uint8 Buf[4096];
	uint32 Pending = 0;
	ISocketSubsystem* SS = SmvSockets();
	int32 Guard = 0;
	while (Udp->HasPendingData(Pending) && Guard++ < 64)
	{
		TSharedRef<FInternetAddr> From = SS->CreateInternetAddr();
		int32 Read = 0;
		if (!Udp->RecvFrom(Buf, (int32)sizeof(Buf), Read, *From) || Read <= 0) break;
		const double W = FPlatformTime::Seconds();
		if (MpLocked.IsValid() && !MpLocked->CompareEndpoints(*From) && W - MpLastRx < 30.0) continue;   // GCS kilidi
		if (!MpLocked.IsValid() || !MpLocked->CompareEndpoints(*From)) MpLocked = From;
		MpLastRx = W;
		if (!bGcsConnected) { bGcsConnected = true; EmitEvent(FString::Printf(TEXT("Mission Planner bağlandı: %s"), *From->ToString(true)), 6); }
		SendRaw(Buf, Read);
		// yan dinleme: PARAM_SET → Sıfırla sonrası yeniden yazılacaklar
		SmvMav::FFrame F;
		for (int32 i = 0; i < Read; ++i)
		{
			if (!MpParser->Push(Buf[i], F) || F.MsgId != SmvMav::MSG_PARAM_SET) continue;
			SmvMav::FReader R(F.Payload);
			const float V = R.F32(); R.U8(); R.U8();
			const FString Id = UTF8_TO_TCHAR(R.Chars(16).c_str());
			static const TCHAR* Skip[] = { TEXT("MAV_SYSID"), TEXT("SYSID_THISMAV"), TEXT("FORMAT_VERSION"), TEXT("MIS_TOTAL"), TEXT("FENCE_TOTAL"), TEXT("RALLY_TOTAL") };
			bool bSkip = Id.StartsWith(TEXT("STAT_"));
			for (const TCHAR* S : Skip) if (Id == S) bSkip = true;
			if (bSkip) continue;
			SessionUserParams.RemoveAll([&Id](const TPair<FString, float>& Kv) { return Kv.Key == Id; });
			SessionUserParams.Add(TPair<FString, float>(Id, V));
		}
	}
	if (bGcsConnected && FPlatformTime::Seconds() - MpLastRx > 30.0)
	{
		bGcsConnected = false;
		MpLocked.Reset();
		EmitEvent(TEXT("Mission Planner sessiz (30 sn) — kilit kaldırıldı."), 5);
	}
}
