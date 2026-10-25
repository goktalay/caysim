// SIMUMAV — uçuş uyarıları gösterim politikası (bkz. SmvAlertPolicy.h).
#include "SmvAlertPolicy.h"
#include <algorithm>
#include <cmath>

namespace SmvCore
{
	bool IsAutoMode(const std::string& M)
	{
		static const char* A[] = { "AUTO", "AUTO.MISSION", "GUIDED", "LOITER", "RTL", "QRTL", "SMART_RTL", "CIRCLE", "CRUISE", "TAKEOFF", "QTAKEOFF",
			"QLOITER", "QHOVER", "QLAND", "LAND", "AUTO.LAND", "AUTO.RTL", "AUTO.LOITER", "AUTO.TAKEOFF", "POSHOLD", "BRAKE", "FOLLOW", "ZIGZAG" };
		for (const char* X : A) if (M == X) return true;
		return false;
	}

	static bool IsLanding(const std::string& M) { return M == "LAND" || M == "QLAND" || M == "AUTO.LAND" || M == "AUTO.PRECLAND"; }
	static bool IsReturn(const std::string& M) { return M == "RTL" || M == "QRTL" || M == "SMART_RTL" || M == "AUTO.RTL" || M == "AUTO" || M == "AUTO.MISSION"; }

	bool FAlertPolicy::Suppressed(const std::string& Code, const FGpwsInput& I, bool bPlane, double BankCfg) const
	{
		const bool bTk = PhaseStr == "takeoff" || PhaseStr == "ground";
		const bool bApp = PhaseStr == "approach";
		// kalkış / iniş: beklenen durumlar
		static const char* Phased[] = { "BANK_ANGLE", "PITCH", "SINK_RATE", "DONT_SINK", "TOO_LOW", "TERRAIN", "WIND", "VIBRATION", "EKF" };
		if (bTk || bApp)
		{
			for (const char* X : Phased)
			{
				if (Code != X) continue;
				// aşırı yatış / yunuslama kalkışta da gerçek tehlikedir
				if (Code == "BANK_ANGLE" && Has(I.Roll) && std::fabs(I.Roll) >= (bPlane ? 70.0 : 55.0)) return false;
				if (Code == "PITCH" && Has(I.Pitch) && std::fabs(I.Pitch) >= 50.0) return false;
				return true;
			}
			if (Code == "STALL" && bTk && (!Has(I.Agl) || I.Agl < 20.0)) return true;   // kalkış koşusu / fırlatma: hız henüz yok
			if (Code == "OVERSPEED" && bTk) return true;
		}
		// otomatik kip: otopilot kendi yatış / yunuslama sınırında uçar (ArduPlane ROLL_LIMIT_DEG 45, LOITER/TAKEOFF dönüşleri)
		if (IsAutoMode(I.Mode))
		{
			if (Code == "BANK_ANGLE" && Has(I.Roll) && std::fabs(I.Roll) < std::max(BankCfg + 10.0, bPlane ? 60.0 : 45.0)) return true;
			if (Code == "PITCH" && Has(I.Pitch) && std::fabs(I.Pitch) < 45.0) return true;
		}
		return false;
	}

	std::vector<FAlertShow> FAlertPolicy::Filter(const FGpwsInput& I, const FGpwsStepResult& R, const std::string& VClass, const std::string& Level, double BankCfg, bool bLaunch)
	{
		std::vector<FAlertShow> Out;
		const double T = I.T;
		if (Has(LastT) && T < LastT - 0.5) Reset();
		LastT = T;
		const bool bPlane = VClass == "plane" || ((VClass == "vtol" || VClass == "icept") && !(I.Mode.size() && I.Mode[0] == 'Q'));
		// ---- evre ----
		const double AirAgl = bPlane ? 5.0 : 1.5;
		const bool bAir = I.bArmed && !I.bCrashed && Has(I.Agl) && I.Agl > AirAgl;
		if (!I.bArmed) AirAt = NaN;
		else if (bAir && !Has(AirAt)) AirAt = T;
		const double SinceAir = Has(AirAt) ? T - AirAt : 0.0;
		const double Agl = Has(I.Agl) ? I.Agl : 0.0;
		if (!I.bArmed || (!bAir && !Has(AirAt))) PhaseStr = "ground";
		else if (bLaunch || I.Mode == "TAKEOFF" || I.Mode == "QTAKEOFF" || I.Mode == "AUTO.TAKEOFF" || SinceAir < (bPlane ? 20.0 : 6.0)
			|| (Agl < (bPlane ? 40.0 : 8.0) && I.Vs > 0.5 && SinceAir < 60.0)) PhaseStr = "takeoff";
		else if (IsLanding(I.Mode) || (IsReturn(I.Mode) && Agl < (bPlane ? 60.0 : 15.0) && I.Vs < 0.3)
			|| (Has(I.DistHome) && I.DistHome < (bPlane ? 250.0 : 25.0) && Agl < (bPlane ? 40.0 : 6.0) && I.Vs < -0.3)) PhaseStr = "approach";
		else if (I.Vs > 1.5) PhaseStr = "climb";
		else PhaseStr = "cruise";
		// ---- biten bölümler: yalnız gösterilmiş olanlar özete ----
		for (const FGpwsEpisode& E : R.Ended)
		{
			auto It = St.find(E.Code);
			if (It != St.end())
			{
				if (It->second.bShown) Shown.push_back(E);
				It->second.Since = NaN;
				It->second.bShown = false;
			}
		}
		if (Level == "off") return Out;
		std::map<std::string, bool> Act;
		for (const FGpwsActive& A : R.Active)
		{
			Act[A.Code] = true;
			FS& S = St[A.Code];
			if (!Has(S.Since)) S.Since = T;
			if (Level == "critical" && A.Level != "warning") continue;
			if (Suppressed(A.Code, I, bPlane, BankCfg)) { if (!S.bShown) S.Since = T; continue; }   // bastırıldığı süre gecikmeye sayılmaz
			const bool bFast = A.Code == "PULL_UP" || A.Code == "TERRAIN" || A.Code == "ENGINE" || A.Code == "IMU_FAULT" || A.Code == "BATT_CRIT" || A.Code == "FUEL";
			const double Need = bFast ? 1.0 : A.Level == "warning" ? 2.0 : 3.0;
			if (!S.bShown && T - S.Since < Need - 1e-9) continue;
			FAlertShow X;
			X.Code = A.Code; X.Level = A.Level; X.Prio = A.Prio; X.bAcked = A.bAcked;
			if (!S.bShown)
			{
				S.bShown = true;
				// tekrar bastırma: son seslendirmeden 30 sn geçmediyse "yeni" sayılmaz (ses / olay yok; ekranda görünür)
				if (T - S.LastSay >= 30.0) { X.bFresh = true; S.LastSay = T; }
			}
			Out.push_back(X);
		}
		// artık etkin olmayan kodların başlangıcı sıfırlanır
		for (auto& Kv : St) if (!Act.count(Kv.first) && !Kv.second.bShown) Kv.second.Since = NaN;
		std::stable_sort(Out.begin(), Out.end(), [](const FAlertShow& A, const FAlertShow& B) { return A.Prio > B.Prio; });
		return Out;
	}
}
