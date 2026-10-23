// SmvMavlink sınama sürücüsü (Unreal'sız). stdin: her satır "<etiket> <hex çerçeve>" → çözüp alanları yazar.
// "ENC" komutuyla bilinen değerlerle kodlanmış çerçeveleri yazar (pymavlink tarafı doğrular). Bkz. test_mavlink.py.
#include "../../Source/Simumav/Private/Mavlink/SmvMavlink.h"
#include <cstdio>
#include <iostream>
#include <sstream>

using namespace SmvMav;

static std::vector<uint8_t> FromHex(const std::string& H)
{
	std::vector<uint8_t> B;
	for (size_t i = 0; i + 1 < H.size(); i += 2) B.push_back((uint8_t)std::stoi(H.substr(i, 2), nullptr, 16));
	return B;
}

static void PrintHex(const char* Tag, const std::vector<uint8_t>& B)
{
	std::printf("%s ", Tag);
	for (uint8_t X : B) std::printf("%02x", X);
	std::printf("\n");
}

static void Dump(const FFrame& F)
{
	switch (F.MsgId)
	{
	case MSG_HEARTBEAT: { auto M = DecodeHeartbeat(F); std::printf("HEARTBEAT custom_mode=%u type=%u autopilot=%u base_mode=%u system_status=%u\n", M.CustomMode, M.Type, M.Autopilot, M.BaseMode, M.SystemStatus); break; }
	case MSG_ATTITUDE: { auto M = DecodeAttitude(F); std::printf("ATTITUDE time_boot_ms=%u roll=%.6f pitch=%.6f yaw=%.6f rollspeed=%.6f pitchspeed=%.6f yawspeed=%.6f\n", M.TimeBootMs, M.Roll, M.Pitch, M.Yaw, M.RollSpeed, M.PitchSpeed, M.YawSpeed); break; }
	case MSG_GLOBAL_POSITION_INT: { auto M = DecodeGlobalPositionInt(F); std::printf("GLOBAL_POSITION_INT time_boot_ms=%u lat=%d lon=%d alt=%d relative_alt=%d vx=%d vy=%d vz=%d hdg=%u\n", M.TimeBootMs, M.Lat, M.Lon, M.Alt, M.RelativeAlt, M.Vx, M.Vy, M.Vz, M.Hdg); break; }
	case MSG_VFR_HUD: { auto M = DecodeVfrHud(F); std::printf("VFR_HUD airspeed=%.6f groundspeed=%.6f alt=%.6f climb=%.6f heading=%d throttle=%u\n", M.Airspeed, M.Groundspeed, M.Alt, M.Climb, M.Heading, M.Throttle); break; }
	case MSG_SYS_STATUS: { auto M = DecodeSysStatus(F); std::printf("SYS_STATUS load=%u voltage_battery=%u current_battery=%d battery_remaining=%d\n", M.Load, M.VoltageMv, M.CurrentCa, M.BatteryRemaining); break; }
	case MSG_GPS_RAW_INT: { auto M = DecodeGpsRawInt(F); std::printf("GPS_RAW_INT lat=%d lon=%d alt=%d fix_type=%u satellites_visible=%u vel=%u cog=%u\n", M.Lat, M.Lon, M.Alt, M.FixType, M.Sats, M.Vel, M.Cog); break; }
	case MSG_STATUSTEXT: { auto M = DecodeStatusText(F); std::printf("STATUSTEXT severity=%u text=%s\n", M.Severity, M.Text.c_str()); break; }
	case MSG_COMMAND_ACK: { auto M = DecodeCommandAck(F); std::printf("COMMAND_ACK command=%u result=%u\n", M.Command, M.Result); break; }
	case MSG_SERVO_OUTPUT_RAW: { auto M = DecodeServoOutputRaw(F); std::printf("SERVO_OUTPUT_RAW servo1_raw=%u servo3_raw=%u servo9_raw=%u port=%u\n", M.Servo[0], M.Servo[2], M.Servo[8], M.Port); break; }
	case MSG_RC_CHANNELS: { auto M = DecodeRcChannels(F); std::printf("RC_CHANNELS chan1_raw=%u chan8_raw=%u chan18_raw=%u chancount=%u rssi=%u\n", M.Chan[0], M.Chan[7], M.Chan[17], M.ChanCount, M.Rssi); break; }
	case MSG_PARAM_VALUE: { auto M = DecodeParamValue(F); std::printf("PARAM_VALUE param_id=%s param_value=%.6f param_count=%u param_index=%u param_type=%u\n", M.Id.c_str(), M.Value, M.Count, M.Index, M.Type); break; }
	case MSG_HOME_POSITION: { auto M = DecodeHomePosition(F); std::printf("HOME_POSITION latitude=%d longitude=%d altitude=%d\n", M.Latitude, M.Longitude, M.Altitude); break; }
	case MSG_BATTERY_STATUS: { auto M = DecodeBatteryStatus(F); std::printf("BATTERY_STATUS current_consumed=%d current_battery=%d battery_remaining=%d voltages0=%u\n", M.CurrentConsumed, M.CurrentBattery, M.Remaining, M.Voltages[0]); break; }
	case MSG_EXTENDED_SYS_STATE: { auto M = DecodeExtendedSysState(F); std::printf("EXTENDED_SYS_STATE vtol_state=%u landed_state=%u\n", M.VtolState, M.LandedState); break; }
	case MSG_WIND: { auto M = DecodeWind(F); std::printf("WIND direction=%.6f speed=%.6f speed_z=%.6f\n", M.Direction, M.Speed, M.SpeedZ); break; }
	default: std::printf("OTHER id=%u\n", F.MsgId); break;
	}
}

int main()
{
	std::string Line;
	FParser P;
	while (std::getline(std::cin, Line))
	{
		if (Line == "ENC")
		{
			uint8_t Seq = 0;
			PrintHex("HEARTBEAT", EncodeV2(MSG_HEARTBEAT, PackHeartbeatGcs(), Seq++, 255, 190));
			PrintHex("COMMAND_LONG", EncodeV2(MSG_COMMAND_LONG, PackCommandLong(1, 1, CMD_COMPONENT_ARM_DISARM, 1.f, ARM_FORCE_MAGIC), Seq++, 255, 190));
			PrintHex("PARAM_SET", EncodeV2(MSG_PARAM_SET, PackParamSet(1, 1, "SIM_WIND_SPD", 7.5f), Seq++, 255, 190));
			PrintHex("PARAM_REQUEST_READ", EncodeV2(MSG_PARAM_REQUEST_READ, PackParamRequestRead(1, 1, "SIM_SPEEDUP"), Seq++, 255, 190));
			uint16_t Ch[18] = { 1500, 1400, 1100, 1600, 1000, 2000, 0, 65535, 1234, 0, 0, 0, 0, 0, 0, 0, 0, 1999 };
			PrintHex("RC_CHANNELS_OVERRIDE", EncodeV2(MSG_RC_CHANNELS_OVERRIDE, PackRcOverride(1, 1, Ch), Seq++, 255, 190));
			PrintHex("REQUEST_DATA_STREAM", EncodeV2(MSG_REQUEST_DATA_STREAM, PackRequestDataStream(1, 1, 0, 25, true), Seq++, 255, 190));
			PrintHex("SET_MODE", EncodeV2(MSG_SET_MODE, PackSetMode(1, 5), Seq++, 255, 190));
			PrintHex("DISTANCE_SENSOR", EncodeV2(MSG_DISTANCE_SENSOR, PackDistanceSensor(123456, 20, 4000, 1234, 0, 1, 25), Seq++, 255, 190));
			uint16_t D[72];
			for (int i = 0; i < 72; ++i) D[i] = (uint16_t)(i < 8 ? 100 + i * 37 : 65535);
			D[3] = 3001;
			PrintHex("OBSTACLE_DISTANCE", EncodeV2(MSG_OBSTACLE_DISTANCE, PackObstacleDistance(9876543210ULL, D, 20, 3000, 45.f), Seq++, 255, 190));
			continue;
		}
		std::istringstream S(Line);
		std::string Tag, Hex;
		S >> Tag >> Hex;
		const std::vector<uint8_t> B = FromHex(Hex);
		FFrame F;
		bool bGot = false;
		for (uint8_t X : B) if (P.Push(X, F)) { Dump(F); bGot = true; }
		if (!bGot) std::printf("FAIL %s crc_errors=%u unknown=%u\n", Tag.c_str(), P.CrcErrors(), P.UnknownMessages());
	}
	return 0;
}
