// SIMUMAV — araç çeşitleri: her katalog modelinin ArduPilot SITL fiziği (ikili, --model çerçevesi, çerçeve/aerodinamik JSON'u, parametre seti)
// ve tanıtım istatistikleri. Yalnız C++17, Unreal'sız (tests/cpp ile g++'ta derlenir; tests/sitl_varyant.py Linux SITL ile uçurur).
//
// Fizik kaynakları (ArduPilot 4.7.2, Tools/autotest): default_params/copter*.parm (çerçeve sınıfı/tipi), copter-heli*.parm, copter-single/coax,
// quadplane.parm, plane-tailsitter.parm, plane-jet.parm, models/Callisto.{json,param}, models/freestyle.{json,param}; SITL Frame JSON
// (libraries/SITL/SIM_Frame.cpp load_frame_params: kütle, köşegen, pervane alanı, batarya, askı gazı …) ve uçak katsayı JSON'u
// (SIM_Plane.cpp load_coeffs: kanat alanı/açıklığı/veter, sürükleme, kaldırma …). JSON dosyası SITL çalışma dizinine yazılır,
// --model "<çerçeve>:<dosya>.json" ile verilir (göreli yol: cygwin ikilisi Windows sürücü harfli yolu ayrıştırmaz).
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace SmvCore
{
	using FVehParams = std::vector<std::pair<std::string, double>>;

	struct FVehicleSpec
	{
		/** Katalog anahtarı (glb:* sunucu kataloğuyla ortak; smv:* yalnız yerel SITL; fab:* Fab gövdesi seçenekleri) */
		std::string Key;
		/** SIMUMAV tipi: fpv | multirotor | plane | vtol | interceptor | heli | rover */
		std::string SmvType;
		/** SITL --model temel adı ("" → tipin eski eşlemesi: SmvSitlConfig BinaryAndModel). Ör. x, hexax, octa, octa-quad, y6, heli, heli-dual,
		 *  coaxcopter, plane, plane-heavy, plane-jet, plane-tailsitter, quadplane, quadplane-elevon. */
		std::string Frame;
		/** Çerçeve (çok rotor / quadplane kaldırma) ya da uçak katsayı JSON'u; boş → SITL varsayılanı */
		std::string Json;
		/** Sabit kanat kontrol düzeni (classic | elevon | vtail) — --model eki ve SERVOn_FUNCTION seti bundan; diğer tiplerde boş */
		std::string Airframe;
		/** Varsayılan parametreler (varsayilan.parm; kayıtlı parametrelerden ÖNCE — kullanıcı ezebilir) */
		FVehParams Params;
		/** Çerçeve parametreleri (FRAME_CLASS/FRAME_TYPE …; kayıtlı parametrelerden SONRA — fizik modeliyle eşleşmek zorunda) */
		FVehParams FrameParams;
		// ---- tanıtım istatistikleri (hangar) ----
		double MassKg = 0, SpanM = 0, LengthM = 0, CruiseKmh = 0, MaxKmh = 0, EnduranceMin = 0, PayloadKg = 0;
		int Motors = 0;
		std::string Battery;   // ör. "4S 5000 mAh" | "benzin" | ""
		std::string Physics;   // insan okunur özet: "ArduCopter · octa-quad X · Callisto JSON"
	};

	/** Tüm çeşitler (katalog sırasıyla). */
	const std::vector<FVehicleSpec>& VehicleSpecs();
	const FVehicleSpec* FindVehicleSpec(const std::string& Key);
	/** Çerçeve adından ikili: heli* → ArduHeli, plane* / quadplane* → ArduPlane, rover* → ArduRover, diğer → ArduCopter. */
	std::string BinaryForFrame(const std::string& Frame);
	/** Çeşidin JSON dosya adı (SITL çalışma dizininde): çok rotor/quadplane "cerceve.json", uçak "kanat.json". */
	std::string VehicleJsonName(const FVehicleSpec& S);
	/** plane* / quadplane* çerçevesi mi (ArduPlane). */
	bool IsPlaneFrame(const std::string& Frame);
}
