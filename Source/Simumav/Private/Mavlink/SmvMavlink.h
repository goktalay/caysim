// SIMUMAV — asgari MAVLink v1/v2 kodlayıcı/çözücü (yalnız C++17 standart kitaplığı; Unreal'a bağımlı DEĞİL).
// Yerel SITL (misafir kip) ile konuşmak için gereken mesaj alt kümesi. Alan sıraları ve CRC_EXTRA değerleri
// pymavlink 2.4.50 ardupilotmega lehçesinden alınmıştır ve tests/cpp/test_mavlink.py ile pymavlink'e karşı doğrulanır.
// Bu dosya g++ ile Unreal olmadan derlenip sınanabilsin diye UE türleri kullanmaz.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace SmvMav
{
	// ---- mesaj kimlikleri ve CRC_EXTRA ----
	enum : uint32_t
	{
		MSG_HEARTBEAT = 0,
		MSG_SYS_STATUS = 1,
		MSG_SYSTEM_TIME = 2,
		MSG_SET_MODE = 11,
		MSG_PARAM_REQUEST_READ = 20,
		MSG_PARAM_VALUE = 22,
		MSG_PARAM_SET = 23,
		MSG_GPS_RAW_INT = 24,
		MSG_ATTITUDE = 30,
		MSG_GLOBAL_POSITION_INT = 33,
		MSG_SERVO_OUTPUT_RAW = 36,
		MSG_RC_CHANNELS = 65,
		MSG_REQUEST_DATA_STREAM = 66,
		MSG_RC_CHANNELS_OVERRIDE = 70,
		MSG_VFR_HUD = 74,
		MSG_COMMAND_LONG = 76,
		MSG_COMMAND_ACK = 77,
		MSG_DISTANCE_SENSOR = 132,
		MSG_BATTERY_STATUS = 147,
		MSG_WIND = 168,
		MSG_EKF_STATUS_REPORT = 193,
		MSG_HOME_POSITION = 242,
		MSG_EXTENDED_SYS_STATE = 245,
		MSG_STATUSTEXT = 253,
		// görev protokolü (mini harita: otopilottaki görevi okur — Mission Planner yüklemeleri dahil)
		MSG_MISSION_CURRENT = 42,
		MSG_MISSION_REQUEST = 40,
		MSG_MISSION_REQUEST_LIST = 43,
		MSG_MISSION_COUNT = 44,
		MSG_MISSION_ACK = 47,
		MSG_MISSION_REQUEST_INT = 51,
		MSG_MISSION_ITEM_INT = 73,
		MSG_COMMAND_INT = 75,
		MSG_OBSTACLE_DISTANCE = 330,
	};

	/** Bilinen mesaj için CRC_EXTRA ve azami yük uzunluğu (uzantılar dahil); bilinmiyorsa false. */
	bool MessageInfo(uint32_t MsgId, uint8_t& OutCrcExtra, uint8_t& OutMaxLen);

	// ---- MAV_CMD (common.xml) ----
	enum : uint16_t
	{
		CMD_NAV_RETURN_TO_LAUNCH = 20,
		CMD_NAV_LAND = 21,
		CMD_NAV_TAKEOFF = 22,
		CMD_DO_SET_MODE = 176,
		CMD_DO_SET_HOME = 179,
		CMD_DO_SET_SERVO = 183,
		CMD_PREFLIGHT_REBOOT_SHUTDOWN = 246,
		CMD_COMPONENT_ARM_DISARM = 400,
		CMD_SET_MESSAGE_INTERVAL = 511,
		CMD_REQUEST_MESSAGE = 512,
	};

	/** ARM/DISARM zorlama sihirli sayısı (param2) — ArduPilot `21196`. */
	constexpr float ARM_FORCE_MAGIC = 21196.f;
	/** MAV_MODE_FLAG_SAFETY_ARMED */
	constexpr uint8_t MODE_FLAG_ARMED = 128;
	/** MAV_MODE_FLAG_CUSTOM_MODE_ENABLED */
	constexpr uint8_t MODE_FLAG_CUSTOM = 1;
	/** MAV_PARAM_TYPE_REAL32 */
	constexpr uint8_t PARAM_TYPE_REAL32 = 9;

	// ---- CRC (X.25 / MCRF4XX) ----
	inline void CrcAccumulate(uint8_t Data, uint16_t& Crc)
	{
		uint8_t Tmp = Data ^ (uint8_t)(Crc & 0xff);
		Tmp ^= (uint8_t)(Tmp << 4);
		Crc = (uint16_t)((Crc >> 8) ^ ((uint16_t)Tmp << 8) ^ ((uint16_t)Tmp << 3) ^ ((uint16_t)Tmp >> 4));
	}

	/** Çözülmüş ham çerçeve (yük azami uzunluğa sıfırla doldurulur — v2 sondaki sıfır kırpması geri alınır). */
	struct FFrame
	{
		uint32_t MsgId = 0;
		uint8_t SysId = 0;
		uint8_t CompId = 0;
		uint8_t Seq = 0;
		bool bV2 = true;
		std::vector<uint8_t> Payload;
	};

	// ---- küçük-endian yardımcıları ----
	struct FWriter
	{
		std::vector<uint8_t> B;
		explicit FWriter(size_t Reserve = 64) { B.reserve(Reserve); }
		void U8(uint8_t V) { B.push_back(V); }
		void I8(int8_t V) { B.push_back((uint8_t)V); }
		void U16(uint16_t V) { B.push_back((uint8_t)(V & 0xff)); B.push_back((uint8_t)(V >> 8)); }
		void I16(int16_t V) { U16((uint16_t)V); }
		void U32(uint32_t V) { for (int i = 0; i < 4; ++i) B.push_back((uint8_t)((V >> (8 * i)) & 0xff)); }
		void I32(int32_t V) { U32((uint32_t)V); }
		void U64(uint64_t V) { for (int i = 0; i < 8; ++i) B.push_back((uint8_t)((V >> (8 * i)) & 0xff)); }
		void F32(float V) { uint32_t U; std::memcpy(&U, &V, 4); U32(U); }
		void Chars(const std::string& S, size_t N) { for (size_t i = 0; i < N; ++i) B.push_back((uint8_t)(i < S.size() ? S[i] : 0)); }   // açık döküm: int→uint8 (MSVC C4244)
	};

	struct FReader
	{
		const std::vector<uint8_t>& B;
		size_t P = 0;
		explicit FReader(const std::vector<uint8_t>& In) : B(In) {}
		uint8_t U8() { return P < B.size() ? B[P++] : 0; }
		int8_t I8() { return (int8_t)U8(); }
		uint16_t U16() { uint16_t L = U8(); uint16_t H = U8(); return (uint16_t)(L | (H << 8)); }
		int16_t I16() { return (int16_t)U16(); }
		uint32_t U32() { uint32_t V = 0; for (int i = 0; i < 4; ++i) V |= (uint32_t)U8() << (8 * i); return V; }
		int32_t I32() { return (int32_t)U32(); }
		uint64_t U64() { uint64_t V = 0; for (int i = 0; i < 8; ++i) V |= (uint64_t)U8() << (8 * i); return V; }
		float F32() { uint32_t U = U32(); float F; std::memcpy(&F, &U, 4); return F; }
		std::string Chars(size_t N)
		{
			std::string S;
			for (size_t i = 0; i < N; ++i) { const char C = (char)U8(); if (C) S.push_back(C); else { P += (N - i - 1); break; } }
			return S;
		}
	};

	/** Yükü v2 çerçevesine sarar (sondaki sıfırlar kırpılır, en az 1 bayt). */
	std::vector<uint8_t> EncodeV2(uint32_t MsgId, const std::vector<uint8_t>& Payload, uint8_t Seq, uint8_t SysId, uint8_t CompId);

	/** Bayt akışı çözücü (v1 0xFE + v2 0xFD). İmzalı v2 çerçeveleri (incompat bit 0x01) imza atlanarak kabul edilir. */
	class FParser
	{
	public:
		/** Bir bayt işler; tam ve CRC'si doğru bir çerçeve oluşunca true döner ve Out doldurulur. */
		bool Push(uint8_t Byte, FFrame& Out);
		uint32_t CrcErrors() const { return CrcErr; }
		uint32_t UnknownMessages() const { return Unknown; }
	private:
		enum class EState { Idle, Len, Incompat, Compat, Seq, Sys, Comp, Id0, Id1, Id2, Payload, Crc0, Crc1, Sig };
		EState St = EState::Idle;
		bool bV2 = false;
		uint8_t Len = 0, Incompat = 0, SigLeft = 0;
		uint16_t Crc = 0xffff, RxCrc = 0;
		FFrame Cur;
		uint32_t CrcErr = 0, Unknown = 0;
		void Reset() { St = EState::Idle; Cur = FFrame(); Crc = 0xffff; }
	};

	// ---- çözülmüş mesaj yapıları ----
	struct FHeartbeat { uint32_t CustomMode = 0; uint8_t Type = 0, Autopilot = 0, BaseMode = 0, SystemStatus = 0, MavlinkVersion = 0; };
	struct FSysStatus { uint16_t Load = 0, VoltageMv = 0; int16_t CurrentCa = -1; int8_t BatteryRemaining = -1; };
	struct FGpsRawInt { uint64_t TimeUsec = 0; int32_t Lat = 0, Lon = 0, Alt = 0; uint16_t Eph = 0, Epv = 0, Vel = 0, Cog = 0; uint8_t FixType = 0, Sats = 255; };
	struct FAttitude { uint32_t TimeBootMs = 0; float Roll = 0, Pitch = 0, Yaw = 0, RollSpeed = 0, PitchSpeed = 0, YawSpeed = 0; };
	struct FGlobalPositionInt { uint32_t TimeBootMs = 0; int32_t Lat = 0, Lon = 0, Alt = 0, RelativeAlt = 0; int16_t Vx = 0, Vy = 0, Vz = 0; uint16_t Hdg = 0; };
	struct FServoOutputRaw { uint32_t TimeUsec = 0; uint16_t Servo[16] = {}; uint8_t Port = 0; };
	struct FRcChannels { uint32_t TimeBootMs = 0; uint16_t Chan[18] = {}; uint8_t ChanCount = 0, Rssi = 0; };
	struct FVfrHud { float Airspeed = 0, Groundspeed = 0, Alt = 0, Climb = 0; int16_t Heading = 0; uint16_t Throttle = 0; };
	struct FCommandAck { uint16_t Command = 0; uint8_t Result = 0, Progress = 0; int32_t ResultParam2 = 0; uint8_t TargetSystem = 0, TargetComponent = 0; };
	struct FStatusText { uint8_t Severity = 6; std::string Text; uint16_t Id = 0; uint8_t ChunkSeq = 0; };
	struct FParamValue { float Value = 0; uint16_t Count = 0, Index = 0; std::string Id; uint8_t Type = 0; };
	struct FHomePosition { int32_t Latitude = 0, Longitude = 0, Altitude = 0; float X = 0, Y = 0, Z = 0; };
	struct FBatteryStatus { int32_t CurrentConsumed = -1, EnergyConsumed = -1; int16_t Temperature = 0; uint16_t Voltages[10] = {}; int16_t CurrentBattery = -1; uint8_t Id = 0; int8_t Remaining = -1; };
	struct FWind { float Direction = 0, Speed = 0, SpeedZ = 0; };
	struct FExtendedSysState { uint8_t VtolState = 0, LandedState = 0; };
	/** EKF_STATUS_REPORT: varyanslar (normalleştirilmiş) + EKF_STATUS_FLAGS (0x10 POS_HORIZ_ABS, 0x80 CONST_POS_MODE, 0x400 UNINITIALIZED) */
	struct FEkfStatus { float VelVar = 0, PosHVar = 0, PosVVar = 0, CompassVar = 0, TerrainVar = 0; uint16_t Flags = 0; };

	FHeartbeat DecodeHeartbeat(const FFrame& F);
	FSysStatus DecodeSysStatus(const FFrame& F);
	FGpsRawInt DecodeGpsRawInt(const FFrame& F);
	FAttitude DecodeAttitude(const FFrame& F);
	FGlobalPositionInt DecodeGlobalPositionInt(const FFrame& F);
	FServoOutputRaw DecodeServoOutputRaw(const FFrame& F);
	FRcChannels DecodeRcChannels(const FFrame& F);
	FVfrHud DecodeVfrHud(const FFrame& F);
	FCommandAck DecodeCommandAck(const FFrame& F);
	FStatusText DecodeStatusText(const FFrame& F);
	FParamValue DecodeParamValue(const FFrame& F);
	FHomePosition DecodeHomePosition(const FFrame& F);
	FBatteryStatus DecodeBatteryStatus(const FFrame& F);
	FWind DecodeWind(const FFrame& F);
	FExtendedSysState DecodeExtendedSysState(const FFrame& F);
	FEkfStatus DecodeEkfStatus(const FFrame& F);

	// ---- gönderilecek mesaj yükleri ----
	std::vector<uint8_t> PackHeartbeatGcs();
	std::vector<uint8_t> PackCommandLong(uint8_t TargetSys, uint8_t TargetComp, uint16_t Command, float P1, float P2 = 0, float P3 = 0, float P4 = 0, float P5 = 0, float P6 = 0, float P7 = 0, uint8_t Confirmation = 0);
	std::vector<uint8_t> PackParamSet(uint8_t TargetSys, uint8_t TargetComp, const std::string& Id, float Value, uint8_t Type = PARAM_TYPE_REAL32);
	std::vector<uint8_t> PackParamRequestRead(uint8_t TargetSys, uint8_t TargetComp, const std::string& Id, int16_t Index = -1);
	/** 18 kanal (MAVLink tanımı): kanal 1–8 için 0 = kanalı RC'ye geri bırak, 65535 = dokunma;
	 *  kanal 9–18 için 0 ya da 65535 = dokunma, 65534 = geri bırak. */
	std::vector<uint8_t> PackRcOverride(uint8_t TargetSys, uint8_t TargetComp, const uint16_t Chan[18]);
	std::vector<uint8_t> PackRequestDataStream(uint8_t TargetSys, uint8_t TargetComp, uint8_t StreamId, uint16_t RateHz, bool bStart);
	std::vector<uint8_t> PackSetMode(uint8_t TargetSys, uint32_t CustomMode);
	/** DISTANCE_SENSOR (132): Type 0 = lazer, Orientation 25 = aşağı (PITCH_270), 24 = yukarı (PITCH_90); Covariance 255 = bilinmiyor. */
	std::vector<uint8_t> PackDistanceSensor(uint32_t TimeBootMs, uint16_t MinCm, uint16_t MaxCm, uint16_t CurCm, uint8_t Type, uint8_t Id, uint8_t Orientation,
		uint8_t Covariance = 255, float HFovRad = 0, float VFovRad = 0, uint8_t SignalQuality = 0);
	/** OBSTACLE_DISTANCE (330): 72 mesafe (cm; MaxCm + 1 = engel yok, 65535 = kullanılmıyor), IncrementDeg (increment_f), Frame 12 = MAV_FRAME_BODY_FRD. */
	std::vector<uint8_t> PackObstacleDistance(uint64_t TimeUsec, const uint16_t Distances[72], uint16_t MinCm, uint16_t MaxCm, float IncrementDeg,
		float AngleOffsetDeg = 0, uint8_t Frame = 12, uint8_t SensorType = 0);
}
