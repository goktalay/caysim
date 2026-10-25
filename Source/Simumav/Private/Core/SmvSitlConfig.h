// SIMUMAV — yerel ArduPilot SITL oturum yapılandırması (misafir kip). Sunucu oturum mantığının istemci karşılığı:
// app/drone_sitl.py (komut satırı, tip eşlemesi, rover setleri), app/flightsim.py (_ilk_heartbeat parametre sırası, kalkış setleri, kontrol düzeni,
// rüzgâr esintisi, kip tabloları), app/flightsim_icept.py (INTERCEPTOR seti). Yalnız C++17, Unreal'sız; tests/cpp/test_core.py sınar.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace SmvCore
{
	using FParamList = std::vector<std::pair<std::string, double>>;

	/** SIMUMAV tipi (fpv/multirotor/plane/vtol/interceptor/heli/rover) → SITL tipi (multirotor/hexarotor/helikopter/sabit_kanat/vtol/interceptor/rover). */
	std::string SitlTypeFromSmv(const std::string& SmvType, const std::string& ModelKey = std::string());

	/** SITL tipi → ikili adı (ArduCopter / ArduPlane / ArduRover; Windows: <ad>.elf — PE dosyası) ve --model değeri. */
	void BinaryAndModel(const std::string& SitlType, std::string& OutBinary, std::string& OutModel);

	struct FSitlLaunch
	{
		std::string Method = "wheels";   // wheels | ramp | vehicle | hand | (interceptor: pod4|pod8|tube|handheld)
		double AngleDeg = 15, ReleaseKmh = 70;
		double HandSpeedMs = 7, HandPitchDeg = 12;   // elden atış (SITL: kısa mancınık itişi + ivme algılamalı kalkış)
		double TakeoffAltM = -1;          // TKOFF_ALT (yoksa yazılmaz)
		double AzimuthDeg = -1;
	};

	struct FSitlSession
	{
		std::string SitlType = "multirotor";
		std::string Airframe = "classic";  // classic | elevon | vtail (sabit kanat)
		std::string Drive = "ackermann";   // ackermann | skid (rover)
		double HomeLat = 0, HomeLon = 0, HomeAlt = 100, HomeHeading = 0;
		int SysId = 1;
		int BasePort = 15670;
		bool bBinLog = true;
		double WindMs = 0, WindDir = 0, WindTurb = 0, WindGust = 0;
		double Speedup = 1;
		double MaxSpeedMs = -1;            // uçak tipleri
		FSitlLaunch Launch;
		FParamList UserParams;             // dron kaydının kalıcı parametreleri (beyaz liste dışı adlar ayıklanmış olmalı)
		/** Katalog model anahtarı (SmvVehicles çeşidi: çerçeve, JSON, parametre seti). Boş ya da çeşit yoksa tipin eski eşlemesi. */
		std::string Vehicle;
	};

	/** Oturumun ikilisi (çeşit çerçevesinden; yoksa BinaryAndModel). */
	std::string SessionBinary(const FSitlSession& S);
	/** Oturumun --model değeri (çeşit çerçevesi + sabit kanat düzen/fırlatma eki + ":<json>"; yoksa tipin eski değeri). */
	std::string SessionModel(const FSitlSession& S);
	/** Çeşidin SITL çalışma dizinine yazılacak JSON'u (çerçeve / uçak katsayıları); yoksa false. */
	bool SessionJson(const FSitlSession& S, std::string& OutName, std::string& OutText);

	/** SITL komut satırı argümanları (ikili hariç): --model … --home lat,lon,alt,yön --base-port P --defaults <parm> --sysid N */
	std::vector<std::string> CommandLine(const FSitlSession& S, const std::string& DefaultsPath);
	/** Sabit kanat --model eki: -elevon / -vtail (+ rampa ise -catapult). */
	std::string PlaneModelSuffix(const std::string& Airframe, bool bRamp);
	/** varsayilan.parm içeriği (FRAME_CLASS, FS_GCS_ENABLE, MAV_SYSID, ARMING_SKIPCHK, LOG_*, tip seti, kayıtlı parametreler, düzen seti). */
	std::string DefaultsParmText(const FSitlSession& S);
	/** İlk HEARTBEAT'te PARAM_SET ile yazılacaklar (sıra önemli; flightsim._ilk_heartbeat). bAfterReset: SIM_SPEEDUP 1× de yazılır. */
	FParamList FirstHeartbeatParams(const FSitlSession& S, bool bAfterReset);
	/** Sabit kanat kalkış seti (launch_parametreleri). */
	FParamList LaunchParams(const FSitlLaunch& L);
	/** Kontrol düzeni seti (classic/elevon/vtail). */
	FParamList LayoutParams(const std::string& Airframe);
	const FParamList& InterceptorParams();
	const FParamList& InterceptorSessionParams();
	FParamList RoverParams(bool bSkid);

	/** SIM_WIND_SPD esintili değer (flightsim._esinti; seed 0). */
	double GustWindSpeed(double WindMs, double GustMs, double TbmS, double Seed = 0);

	/** ArduPilot kip tabloları (pymavlink mode_mapping). Vehicle: 'copter' | 'plane' | 'rover'. */
	const std::vector<std::pair<std::string, uint32_t>>& ModeTable(const std::string& Vehicle);
	bool ModeNumber(const std::string& Vehicle, const std::string& Name, uint32_t& Out);
	std::string ModeName(const std::string& Vehicle, uint32_t Number);
	/** SITL tipi → kip tablosu sınıfı. */
	std::string ModeVehicle(const std::string& SitlType);
	/** HEARTBEAT.type → kip sınıfı (1 sabit kanat, 10/11 rover; VTOL tipleri 19–22 plane; diğer copter). */
	std::string ModeVehicleFromMavType(uint8_t MavType);

	/** Uçuşta kullanıcıya izin verilen hız çarpanları. */
	const std::vector<double>& Speedups();
	bool IsVerticalType(const std::string& SitlType);
	bool IsPlaneType(const std::string& SitlType);   // sabit_kanat | vtol | interceptor
}
