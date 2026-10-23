#include "SmvReplay.h"
#include "SimumavModule.h"
#include "SmvI18n.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	double Hav(double La1, double Lo1, double La2, double Lo2)
	{
		const double R = 6371000.0, P1 = FMath::DegreesToRadians(La1), P2 = FMath::DegreesToRadians(La2);
		const double Dp = P2 - P1, Dl = FMath::DegreesToRadians(Lo2 - Lo1);
		const double A = FMath::Sin(Dp / 2) * FMath::Sin(Dp / 2) + FMath::Cos(P1) * FMath::Cos(P2) * FMath::Sin(Dl / 2) * FMath::Sin(Dl / 2);
		return 2 * R * FMath::Atan2(FMath::Sqrt(A), FMath::Sqrt(FMath::Max(0.0, 1 - A)));
	}
	FString HtmlEsc(const FString& S) { return S.Replace(TEXT("&"), TEXT("&amp;")).Replace(TEXT("<"), TEXT("&lt;")).Replace(TEXT(">"), TEXT("&gt;")).Replace(TEXT("\""), TEXT("&quot;")); }
}

namespace SmvReplay
{
	FString Dir() { return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Simumav/Replays")); }

	double PathLengthM(const TArray<FSmvReplayFrame>& F)
	{
		double D = 0;
		for (int32 i = 1; i < F.Num(); ++i) D += Hav(F[i - 1].Lat, F[i - 1].Lon, F[i].Lat, F[i].Lon);
		return D;
	}

	FString Save(const FSmvFlightRequest& Req, const FSmvFlightSummary& Sum, const TArray<FSmvReplayFrame>& Frames)
	{
		if (Frames.Num() < 10) return FString();
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("v"), 1);
		O->SetStringField(TEXT("created"), FDateTime::Now().ToIso8601());
		TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
		FJsonObjectConverter::UStructToJsonObject(FSmvFlightRequest::StaticStruct(), &Req, R, 0, 0);
		O->SetObjectField(TEXT("request"), R);
		O->SetObjectField(TEXT("summary"), Sum.ToJson());
		TArray<FString> Modes;
		TArray<TSharedPtr<FJsonValue>> Arr;
		Arr.Reserve(Frames.Num());
		for (const FSmvReplayFrame& F : Frames)
		{
			int32 Mi = Modes.IndexOfByKey(F.Mode);
			if (Mi == INDEX_NONE) Mi = Modes.Add(F.Mode);
			// [t, lat, lon, altMsl, altRel, roll, pitch, yaw, vn, ve, vd, as, gs, hdg, climb, thr, batt, armed, modeIdx]
			Arr.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%.2f,%.7f,%.7f,%.2f,%.2f,%.4f,%.4f,%.4f,%.2f,%.2f,%.2f,%.2f,%.2f,%.1f,%.2f,%.0f,%.0f,%d,%d"),
				F.T, F.Lat, F.Lon, F.AltMsl, F.AltRel, F.Roll, F.Pitch, F.Yaw, F.Vn, F.Ve, F.Vd, F.As, F.Gs, F.Hdg, F.Climb, F.Thr, F.Batt, F.bArmed ? 1 : 0, Mi)));
		}
		TArray<TSharedPtr<FJsonValue>> Mv;
		for (const FString& M : Modes) Mv.Add(MakeShared<FJsonValueString>(M));
		O->SetArrayField(TEXT("modes"), Mv);
		O->SetArrayField(TEXT("frames"), Arr);
		FString Out;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(O, W);
		const FString Name = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT("_") + FPaths::MakeValidFileName(Sum.FlightId.IsEmpty() ? FString(TEXT("local")) : Sum.FlightId) + TEXT(".json");
		const FString Path = Dir() / Name;
		if (!FFileHelper::SaveStringToFile(Out, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) return FString();
		// en yeni 30 kayıt
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Dir() / TEXT("*.json")), true, false);
		Files.Sort([](const FString& A, const FString& B) { return A > B; });
		for (int32 i = 30; i < Files.Num(); ++i) IFileManager::Get().Delete(*(Dir() / Files[i]));
		return Path;
	}

	static bool ParseHeader(const TSharedPtr<FJsonObject>& O, const FString& File, FSmvReplayInfo& I, FSmvFlightRequest* OutReq)
	{
		I.File = File;
		I.Created = SmvJson::Str(O, TEXT("created"));
		const TSharedPtr<FJsonObject>* R = nullptr;
		FSmvFlightRequest Req;
		if (O->TryGetObjectField(TEXT("request"), R) && R) FJsonObjectConverter::JsonObjectToUStruct(R->ToSharedRef(), FSmvFlightRequest::StaticStruct(), &Req, 0, 0);
		I.Drone = Req.Drone.Name.IsEmpty() ? Req.Drone.Type : Req.Drone.Name;
		I.DroneType = Req.Drone.Type;
		I.Track = Req.Track.Name;
		const TSharedPtr<FJsonObject>* S = nullptr;
		if (O->TryGetObjectField(TEXT("summary"), S) && S)
		{
			I.Result = SmvJson::Str(*S, TEXT("result"));
			I.DurationS = SmvJson::Num(*S, TEXT("duration_s"));
			I.DistM = SmvJson::Num(*S, TEXT("dist_m"));
		}
		if (OutReq) *OutReq = Req;
		return true;
	}

	bool Load(const FString& File, FSmvFlightRequest& OutReq, TArray<FSmvReplayFrame>& OutFrames, FSmvReplayInfo* OutInfo)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *File)) return false;
		TSharedPtr<FJsonObject> O;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), O) || !O.IsValid()) return false;
		FSmvReplayInfo I;
		ParseHeader(O, File, I, &OutReq);
		TArray<FString> Modes;
		const TArray<TSharedPtr<FJsonValue>>* Mv = nullptr;
		if (O->TryGetArrayField(TEXT("modes"), Mv)) for (const TSharedPtr<FJsonValue>& V : *Mv) Modes.Add(V->AsString());
		const TArray<TSharedPtr<FJsonValue>>* Fr = nullptr;
		OutFrames.Reset();
		if (O->TryGetArrayField(TEXT("frames"), Fr))
		{
			OutFrames.Reserve(Fr->Num());
			TArray<FString> P;
			for (const TSharedPtr<FJsonValue>& V : *Fr)
			{
				V->AsString().ParseIntoArray(P, TEXT(","), false);
				if (P.Num() < 19) continue;
				FSmvReplayFrame F;
				F.T = FCString::Atod(*P[0]); F.Lat = FCString::Atod(*P[1]); F.Lon = FCString::Atod(*P[2]); F.AltMsl = FCString::Atod(*P[3]); F.AltRel = FCString::Atod(*P[4]);
				F.Roll = FCString::Atof(*P[5]); F.Pitch = FCString::Atof(*P[6]); F.Yaw = FCString::Atof(*P[7]);
				F.Vn = FCString::Atof(*P[8]); F.Ve = FCString::Atof(*P[9]); F.Vd = FCString::Atof(*P[10]);
				F.As = FCString::Atof(*P[11]); F.Gs = FCString::Atof(*P[12]); F.Hdg = FCString::Atof(*P[13]); F.Climb = FCString::Atof(*P[14]);
				F.Thr = FCString::Atof(*P[15]); F.Batt = FCString::Atof(*P[16]); F.bArmed = FCString::Atoi(*P[17]) != 0;
				const int32 Mi = FCString::Atoi(*P[18]);
				F.Mode = Modes.IsValidIndex(Mi) ? Modes[Mi] : FString();
				OutFrames.Add(F);
			}
		}
		I.Frames = OutFrames.Num();
		for (const FSmvReplayFrame& F : OutFrames) I.MaxAltM = FMath::Max(I.MaxAltM, F.AltRel);
		if (I.DurationS <= 0 && OutFrames.Num()) I.DurationS = OutFrames.Last().T;
		if (I.DistM <= 0) I.DistM = PathLengthM(OutFrames);
		if (OutInfo) *OutInfo = I;
		return OutFrames.Num() > 1;
	}

	TArray<FSmvReplayInfo> List()
	{
		TArray<FSmvReplayInfo> Out;
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Dir() / TEXT("*.json")), true, false);
		Files.Sort([](const FString& A, const FString& B) { return A > B; });
		for (const FString& F : Files)
		{
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *(Dir() / F))) continue;
			// başlık: kareleri ayrıştırmadan (dosyanın başı yeter)
			const int32 Cut = Text.Find(TEXT(",\"modes\""));
			if (Cut > 0) Text = Text.Left(Cut) + TEXT("}");
			TSharedPtr<FJsonObject> O;
			if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), O) || !O.IsValid()) continue;
			FSmvReplayInfo I;
			ParseHeader(O, Dir() / F, I, nullptr);
			Out.Add(I);
		}
		return Out;
	}

	FString WriteReport(const FSmvFlightRequest& Req, const FSmvFlightSummary& Sum, const TArray<FSmvReplayFrame>& F)
	{
		auto Tx = [](const TCHAR* K) { return HtmlEsc(FSmvI18n::T(K).ToString()); };
		// rota SVG (yerel düzlem; kuzey yukarı)
		FString Route, Prof;
		double MaxAlt = 1;
		if (F.Num() > 1)
		{
			const double La0 = F[0].Lat, Lo0 = F[0].Lon, Kx = 111320.0 * FMath::Cos(FMath::DegreesToRadians(La0)), Ky = 110540.0;
			double X0 = 1e18, X1 = -1e18, Y0 = 1e18, Y1 = -1e18;
			for (const FSmvReplayFrame& Q : F) { const double X = (Q.Lon - Lo0) * Kx, Y = (Q.Lat - La0) * Ky; X0 = FMath::Min(X0, X); X1 = FMath::Max(X1, X); Y0 = FMath::Min(Y0, Y); Y1 = FMath::Max(Y1, Y); MaxAlt = FMath::Max(MaxAlt, Q.AltRel); }
			const double Span = FMath::Max3(X1 - X0, Y1 - Y0, 20.0), S = 560.0 / Span;
			FString Pts;
			for (const FSmvReplayFrame& Q : F) Pts += FString::Printf(TEXT("%.1f,%.1f "), 20 + ((Q.Lon - Lo0) * Kx - X0) * S, 580 - ((Q.Lat - La0) * Ky - Y0) * S);
			Route = FString::Printf(TEXT("<svg viewBox='0 0 600 600' class='map'><rect width='600' height='600' fill='#0b1222'/><polyline points='%s' fill='none' stroke='#38bdf8' stroke-width='2.5' stroke-linejoin='round'/>"), *Pts);
			const FString P0 = Pts.Left(Pts.Find(TEXT(" ")));
			Route += FString::Printf(TEXT("<circle cx='%s' cy='%s' r='6' fill='#34d399'/>"), *P0.Left(P0.Find(TEXT(","))), *P0.Mid(P0.Find(TEXT(",")) + 1));
			Route += FString::Printf(TEXT("<text x='580' y='30' fill='#9aa8c7' font-size='16' text-anchor='end'>N ↑ · %.0f m</text></svg>"), Span);
			FString Pp;
			const double Tm = FMath::Max(1.0, F.Last().T);
			for (const FSmvReplayFrame& Q : F) Pp += FString::Printf(TEXT("%.1f,%.1f "), 10 + Q.T / Tm * 880, 190 - FMath::Max(0.0, Q.AltRel) / MaxAlt * 170);
			Prof = FString::Printf(TEXT("<svg viewBox='0 0 900 200' class='prof'><rect width='900' height='200' fill='#0b1222'/><polyline points='%s' fill='none' stroke='#fbbf24' stroke-width='2'/><text x='12' y='18' fill='#9aa8c7' font-size='13'>%.0f m</text></svg>"), *Pp, MaxAlt);
		}
		const FString Row = TEXT("<tr><th>%s</th><td>%s</td></tr>");
		FString Rows;
		auto AddRow = [&Rows](const FString& K, const FString& V) { Rows += FString::Printf(TEXT("<tr><th>%s</th><td>%s</td></tr>"), *K, *HtmlEsc(V)); };
		AddRow(Tx(TEXT("report.drone")), Req.Drone.Name.IsEmpty() ? Req.Drone.Type : Req.Drone.Name);
		AddRow(Tx(TEXT("report.pilot")), Req.Pilot.Name);
		AddRow(Tx(TEXT("report.track")), Req.Track.Name);
		AddRow(Tx(TEXT("report.result")), FSmvI18n::T(TEXT("result.") + Sum.Result).ToString());
		AddRow(Tx(TEXT("report.duration")), FString::Printf(TEXT("%02d:%02d"), (int32)Sum.DurationS / 60, (int32)Sum.DurationS % 60));
		AddRow(Tx(TEXT("report.distance")), FString::Printf(TEXT("%.2f km"), (Sum.DistM > 0 ? Sum.DistM : PathLengthM(F)) / 1000.0));
		AddRow(Tx(TEXT("report.max_speed")), FString::Printf(TEXT("%.0f km/h"), Sum.MaxSpeedMs * 3.6));
		AddRow(Tx(TEXT("report.max_alt")), FString::Printf(TEXT("%.0f m"), MaxAlt));
		if (Sum.AlertScore >= 0) AddRow(Tx(TEXT("report.discipline")), FString::Printf(TEXT("%s %d"), *Sum.AlertGrade, Sum.AlertScore));
		if (Sum.GatesPassed || Sum.GatesMissed) AddRow(Tx(TEXT("report.gates")), FString::Printf(TEXT("%d / %d"), Sum.GatesPassed, Sum.GatesPassed + Sum.GatesMissed));
		if (Sum.Badges.Num()) AddRow(Tx(TEXT("summary.badges")), FString::Join(Sum.Badges, TEXT(", ")));
		(void)Row;
		const FString Html = FString::Printf(TEXT("<!doctype html><html lang='%s' dir='%s'><head><meta charset='utf-8'><title>CAYSIM — %s</title><style>")
			TEXT("body{font-family:Segoe UI,Arial,sans-serif;background:#070b14;color:#e8eefb;margin:24px}h1{margin:0 0 4px;font-size:26px}small{color:#9aa8c7}")
			TEXT(".grid{display:grid;grid-template-columns:600px 1fr;gap:24px;margin-top:18px}.map{width:600px;height:600px;border-radius:14px}.prof{width:100%%;height:200px;border-radius:14px;margin-top:18px}")
			TEXT("table{border-collapse:collapse;width:100%%}th,td{padding:8px 10px;border-bottom:1px solid #1f2b45;text-align:start}th{color:#9aa8c7;font-weight:600;width:45%%}")
			TEXT("button{background:#38bdf8;color:#071019;border:0;border-radius:10px;padding:10px 18px;font-weight:700;cursor:pointer;margin-top:16px}")
			TEXT("@media print{body{background:#fff;color:#111}th{color:#444}button{display:none}}</style></head><body>")
			TEXT("<h1>%s</h1><small>%s · %s</small><div class='grid'><div>%s</div><div><table>%s</table><button onclick='print()'>%s</button></div></div><h3>%s</h3>%s</body></html>"),
			*FSmvI18n::Language(), FSmvI18n::IsRtl() ? TEXT("rtl") : TEXT("ltr"), *Tx(TEXT("report.title")), *Tx(TEXT("report.title")), *HtmlEsc(FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M"))),
			*HtmlEsc(Sum.FlightId), *Route, *Rows, *Tx(TEXT("report.print")), *Tx(TEXT("report.alt_profile")), *Prof);
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Simumav/Reports")) / (FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT(".html"));
		return FFileHelper::SaveStringToFile(Html, *Path, FFileHelper::EEncodingOptions::ForceUTF8) ? Path : FString();
	}
}

// ---------------------------------------------------------------------------
// USmvReplayFlightLink
// ---------------------------------------------------------------------------

void USmvReplayFlightLink::Init(const TArray<FSmvReplayFrame>& InFrames, float InSpeed)
{
	Frames = InFrames;
	Speed = FMath::Clamp(InSpeed, 0.25f, 8.f);
}

void USmvReplayFlightLink::Start(const FSmvFlightRequest& Request)
{
	Req = Request;
	FlightInfo.FlightId = TEXT("replay");
	PlayT = 0; Cursor = 0; bReady = false; bEndSaid = false; StartTicks = 0;
	Unix0 = (double)FDateTime::UtcNow().ToUnixTimestamp(); Wall0 = FPlatformTime::Seconds();
	if (!TickHandle.IsValid()) TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &USmvReplayFlightLink::OnTick), 0.f);
	EmitEvent(FSmvI18n::T(TEXT("replay.loading")).ToString(), 6);
}

void USmvReplayFlightLink::BeginDestroy()
{
	if (TickHandle.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(TickHandle); TickHandle.Reset(); }
	Super::BeginDestroy();
}

void USmvReplayFlightLink::Stop(const FString& Reason)
{
	if (TickHandle.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(TickHandle); TickHandle.Reset(); }
	FSmvFlightSummary S;
	S.FlightId = TEXT("replay");
	S.Result = TEXT("abort");
	S.EndReason = Reason;
	OnSummary.Broadcast(S);
}

FString USmvReplayFlightLink::Command(const FString& Name, const TSharedPtr<FJsonObject>& InParams)
{
	if (Name == TEXT("speed")) Speed = FMath::Clamp((float)SmvJson::Num(InParams, TEXT("factor"), 1.0), 0.25f, 8.f);
	else if (Name == TEXT("reset")) { PlayT = 0; Cursor = 0; bEndSaid = false; }
	return NextRid();
}

double USmvReplayFlightLink::Now() const { return Unix0 + (FPlatformTime::Seconds() - Wall0); }

bool USmvReplayFlightLink::OnTick(float Dt)
{
	if (Frames.Num() < 2) return true;
	if (!bReady)
	{
		// sahne kurulsun diye iki kare bekle, sonra HAZIR
		if (++StartTicks < 3) return true;
		bReady = true;
		EmitState(TEXT("HAZIR"));
	}
	PlayT = FMath::Min(PlayT + FMath::Min(Dt, 0.1f) * Speed, Frames.Last().T);
	while (Cursor + 1 < Frames.Num() - 1 && Frames[Cursor + 1].T <= PlayT) ++Cursor;
	while (Cursor > 0 && Frames[Cursor].T > PlayT) --Cursor;
	const FSmvReplayFrame& A = Frames[Cursor];
	const FSmvReplayFrame& B = Frames[FMath::Min(Cursor + 1, Frames.Num() - 1)];
	const double U = B.T > A.T ? FMath::Clamp((PlayT - A.T) / (B.T - A.T), 0.0, 1.0) : 0.0;
	auto L = [U](double X, double Y) { return X + (Y - X) * U; };
	auto Ang = [U](float X, float Y) { const float D = FMath::FindDeltaAngleRadians(X, Y); return X + D * (float)U; };
	Tele.Ts = Now();
	Tele.Tbm = (int64)(PlayT * 1000.0);
	Tele.Lat = L(A.Lat, B.Lat); Tele.Lon = L(A.Lon, B.Lon); Tele.AltMsl = L(A.AltMsl, B.AltMsl); Tele.AltRel = L(A.AltRel, B.AltRel);
	Tele.Roll = Ang(A.Roll, B.Roll); Tele.Pitch = Ang(A.Pitch, B.Pitch); Tele.Yaw = Ang(A.Yaw, B.Yaw);
	Tele.Vn = (float)L(A.Vn, B.Vn); Tele.Ve = (float)L(A.Ve, B.Ve); Tele.Vd = (float)L(A.Vd, B.Vd);
	Tele.Airspeed = (float)L(A.As, B.As); Tele.Groundspeed = (float)L(A.Gs, B.Gs); Tele.Heading = (float)L(A.Hdg, A.Hdg + FMath::FindDeltaAngleDegrees(A.Hdg, B.Hdg));
	Tele.Climb = (float)L(A.Climb, B.Climb); Tele.Throttle = (float)L(A.Thr, B.Thr); Tele.BattPct = A.Batt;
	Tele.bArmed = A.bArmed; Tele.Mode = A.Mode; Tele.GpsFix = 3; Tele.Sats = 12;
	Tele.bHasHome = true; Tele.HomeLat = Frames[0].Lat; Tele.HomeLon = Frames[0].Lon; Tele.HomeAlt = Frames[0].AltMsl - Frames[0].AltRel;
	Extra.Speedup = Speed;
	EmitTele();
	if (PlayT >= Frames.Last().T && !bEndSaid) { bEndSaid = true; EmitEvent(FSmvI18n::T(TEXT("replay.end")).ToString(), 5); }
	return true;
}
