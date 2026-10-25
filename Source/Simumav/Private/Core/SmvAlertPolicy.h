// SIMUMAV — uçuş uyarılarının GÖSTERİM politikası (SmvGpws değerlendiricisinin üstünde; değerlendirici web ile birebir kalır).
// Amaç: ekranda / seste yalnız gerçekten eyleme dönük uyarılar.
//   • Evre farkındalığı: yerde, kalkış (TAKEOFF/QTAKEOFF kipi, fırlatma, havalanmadan sonraki ilk saniyeler), tırmanış, seyir, yaklaşma/iniş
//     (LAND/QLAND/RTL/AUTO alçakta). Kalkış ve inişte beklenen durumlar (yatış, yunuslama, alçalma, arazi yakınlığı, irtifa kaybı, rüzgâr,
//     titreşim, EKF) gösterilmez.
//   • Otomatik kiplerde (AUTO, GUIDED, LOITER, RTL, CIRCLE, CRUISE, TAKEOFF, Q*) yatış/yunuslama otopilotun kendi sınırı içindedir:
//     yalnız sınırın çok üstünde (sabit kanat ≥ 60°, kopter ≥ 45°; ayardaki eşikten büyükse o) uyarı.
//   • Gecikme: koşul sürekli ≥ 2 sn (uyarı) / ≥ 3 sn (dikkat) sürmeli; PULL UP / TERRAIN / motor / IMU / batarya kritik 1 sn.
//   • Tekrar bastırma: aynı uyarı kapandıktan sonra 30 sn içinde yeniden seslendirilmez / olay şeridine yazılmaz.
//   • Kademe: off | critical (yalnız "warning" düzeyi; varsayılan) | all (dikkat düzeyi de).
// Puan / özet yalnız gösterilmiş bölümlerden hesaplanır (Shown()).
#pragma once

#include "SmvGpws.h"
#include <map>
#include <string>
#include <vector>

namespace SmvCore
{
	struct FAlertShow { std::string Code, Level; int Prio = 0; bool bAcked = false; bool bFresh = false; };

	class FAlertPolicy
	{
	public:
		void Reset() { St.clear(); Shown.clear(); AirAt = NaN; LastT = NaN; }
		/** Level: off | critical | all. BankCfg: ayardaki yatış eşiği (°). bLaunch: fırlatma / kalkış dizisi sürüyor. */
		std::vector<FAlertShow> Filter(const FGpwsInput& I, const FGpwsStepResult& R, const std::string& VClass, const std::string& Level, double BankCfg, bool bLaunch);
		/** Gösterilmiş (ekrana çıkmış) bölümler — özet / puan */
		const std::vector<FGpwsEpisode>& ShownEpisodes() const { return Shown; }
		/** Evre (günlük / HUD): ground | takeoff | climb | cruise | approach */
		const std::string& Phase() const { return PhaseStr; }
	private:
		struct FS { double Since = NaN; bool bShown = false; double LastSay = -1e9; };
		std::map<std::string, FS> St;
		std::vector<FGpwsEpisode> Shown;
		double AirAt = NaN, LastT = NaN;
		std::string PhaseStr = "ground";
		bool Suppressed(const std::string& Code, const FGpwsInput& I, bool bPlane, double BankCfg) const;
	};

	bool IsAutoMode(const std::string& Mode);
}
