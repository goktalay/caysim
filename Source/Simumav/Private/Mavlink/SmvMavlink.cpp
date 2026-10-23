// SIMUMAV — asgari MAVLink kodlayıcı/çözücü (bkz. SmvMavlink.h). Unreal'dan bağımsız; tests/cpp altında g++ ile sınanır.
#include "SmvMavlink.h"

namespace SmvMav
{
	struct FInfo { uint32_t Id; uint8_t CrcExtra; uint8_t MaxLen; };

	// pymavlink 2.4.50 ardupilotmega: (kimlik, crc_extra, azami uzunluk)
	static const FInfo GInfos[] = {
		{ MSG_HEARTBEAT, 50, 9 },
		{ MSG_SYS_STATUS, 124, 43 },
		{ MSG_SYSTEM_TIME, 137, 12 },
		{ MSG_SET_MODE, 89, 6 },
		{ MSG_PARAM_REQUEST_READ, 214, 20 },
		{ MSG_PARAM_VALUE, 220, 25 },
		{ MSG_PARAM_SET, 168, 23 },
		{ MSG_GPS_RAW_INT, 24, 52 },
		{ MSG_ATTITUDE, 39, 28 },
		{ MSG_GLOBAL_POSITION_INT, 104, 28 },
		{ MSG_SERVO_OUTPUT_RAW, 222, 37 },
		{ MSG_RC_CHANNELS, 118, 42 },
		{ MSG_REQUEST_DATA_STREAM, 148, 6 },
		{ MSG_RC_CHANNELS_OVERRIDE, 124, 38 },
		{ MSG_VFR_HUD, 20, 20 },
		{ MSG_COMMAND_LONG, 152, 33 },
		{ MSG_COMMAND_ACK, 143, 10 },
		{ MSG_BATTERY_STATUS, 154, 54 },
		{ MSG_WIND, 1, 12 },
		{ MSG_HOME_POSITION, 104, 60 },
		{ MSG_EXTENDED_SYS_STATE, 130, 2 },
		{ MSG_STATUSTEXT, 83, 54 },
	};

	bool MessageInfo(uint32_t MsgId, uint8_t& OutCrcExtra, uint8_t& OutMaxLen)
	{
		for (const FInfo& I : GInfos)
		{
			if (I.Id == MsgId) { OutCrcExtra = I.CrcExtra; OutMaxLen = I.MaxLen; return true; }
		}
		return false;
	}

	std::vector<uint8_t> EncodeV2(uint32_t MsgId, const std::vector<uint8_t>& PayloadIn, uint8_t Seq, uint8_t SysId, uint8_t CompId)
	{
		uint8_t CrcExtra = 0, MaxLen = 0;
		MessageInfo(MsgId, CrcExtra, MaxLen);
		size_t Len = PayloadIn.size();
		while (Len > 1 && PayloadIn[Len - 1] == 0) --Len; // v2 sondaki sıfır kırpması (en az 1 bayt)
		std::vector<uint8_t> Out;
		Out.reserve(12 + Len);
		Out.push_back(0xFD);
		Out.push_back((uint8_t)Len);
		Out.push_back(0); // incompat
		Out.push_back(0); // compat
		Out.push_back(Seq);
		Out.push_back(SysId);
		Out.push_back(CompId);
		Out.push_back((uint8_t)(MsgId & 0xff));
		Out.push_back((uint8_t)((MsgId >> 8) & 0xff));
		Out.push_back((uint8_t)((MsgId >> 16) & 0xff));
		for (size_t i = 0; i < Len; ++i) Out.push_back(PayloadIn[i]);
		uint16_t Crc = 0xffff;
		for (size_t i = 1; i < Out.size(); ++i) CrcAccumulate(Out[i], Crc);
		CrcAccumulate(CrcExtra, Crc);
		Out.push_back((uint8_t)(Crc & 0xff));
		Out.push_back((uint8_t)(Crc >> 8));
		return Out;
	}

	bool FParser::Push(uint8_t B, FFrame& Out)
	{
		switch (St)
		{
		case EState::Idle:
			if (B == 0xFD || B == 0xFE)
			{
				Reset();
				bV2 = (B == 0xFD);
				Cur.bV2 = bV2;
				St = EState::Len;
			}
			return false;
		case EState::Len:
			Len = B; CrcAccumulate(B, Crc);
			St = bV2 ? EState::Incompat : EState::Seq;
			return false;
		case EState::Incompat:
			Incompat = B; CrcAccumulate(B, Crc);
			if (Incompat & ~0x01) { Reset(); return false; } // bilinmeyen uyumsuzluk bayrağı
			St = EState::Compat; return false;
		case EState::Compat:
			CrcAccumulate(B, Crc); St = EState::Seq; return false;
		case EState::Seq:
			Cur.Seq = B; CrcAccumulate(B, Crc); St = EState::Sys; return false;
		case EState::Sys:
			Cur.SysId = B; CrcAccumulate(B, Crc); St = EState::Comp; return false;
		case EState::Comp:
			Cur.CompId = B; CrcAccumulate(B, Crc); St = EState::Id0; return false;
		case EState::Id0:
			Cur.MsgId = B; CrcAccumulate(B, Crc);
			if (bV2) { St = EState::Id1; return false; }
			St = Len ? EState::Payload : EState::Crc0; return false;
		case EState::Id1:
			Cur.MsgId |= (uint32_t)B << 8; CrcAccumulate(B, Crc); St = EState::Id2; return false;
		case EState::Id2:
			Cur.MsgId |= (uint32_t)B << 16; CrcAccumulate(B, Crc);
			St = Len ? EState::Payload : EState::Crc0; return false;
		case EState::Payload:
			Cur.Payload.push_back(B); CrcAccumulate(B, Crc);
			if (Cur.Payload.size() >= Len) St = EState::Crc0;
			return false;
		case EState::Crc0:
			RxCrc = B; St = EState::Crc1; return false;
		case EState::Crc1:
		{
			RxCrc |= (uint16_t)B << 8;
			uint8_t CrcExtra = 0, MaxLen = 0;
			if (!MessageInfo(Cur.MsgId, CrcExtra, MaxLen))
			{
				++Unknown;
				if (bV2 && (Incompat & 0x01)) { SigLeft = 13; St = EState::Sig; } else St = EState::Idle;
				return false;
			}
			CrcAccumulate(CrcExtra, Crc);
			if (Crc != RxCrc)
			{
				++CrcErr;
				St = EState::Idle;
				return false;
			}
			if (Cur.Payload.size() < MaxLen) Cur.Payload.resize(MaxLen, 0);
			Out = Cur;
			if (bV2 && (Incompat & 0x01)) { SigLeft = 13; St = EState::Sig; } else St = EState::Idle;
			return true;
		}
		case EState::Sig:
			if (--SigLeft == 0) St = EState::Idle;
			return false;
		}
		return false;
	}

	// ---- çözme ----
	FHeartbeat DecodeHeartbeat(const FFrame& F)
	{
		FReader R(F.Payload); FHeartbeat M;
		M.CustomMode = R.U32(); M.Type = R.U8(); M.Autopilot = R.U8(); M.BaseMode = R.U8(); M.SystemStatus = R.U8(); M.MavlinkVersion = R.U8();
		return M;
	}

	FSysStatus DecodeSysStatus(const FFrame& F)
	{
		FReader R(F.Payload); FSysStatus M;
		R.U32(); R.U32(); R.U32();
		M.Load = R.U16(); M.VoltageMv = R.U16(); M.CurrentCa = R.I16();
		R.U16(); R.U16(); R.U16(); R.U16(); R.U16(); R.U16();
		M.BatteryRemaining = R.I8();
		return M;
	}

	FGpsRawInt DecodeGpsRawInt(const FFrame& F)
	{
		FReader R(F.Payload); FGpsRawInt M;
		M.TimeUsec = R.U64(); M.Lat = R.I32(); M.Lon = R.I32(); M.Alt = R.I32();
		M.Eph = R.U16(); M.Epv = R.U16(); M.Vel = R.U16(); M.Cog = R.U16(); M.FixType = R.U8(); M.Sats = R.U8();
		return M;
	}

	FAttitude DecodeAttitude(const FFrame& F)
	{
		FReader R(F.Payload); FAttitude M;
		M.TimeBootMs = R.U32(); M.Roll = R.F32(); M.Pitch = R.F32(); M.Yaw = R.F32();
		M.RollSpeed = R.F32(); M.PitchSpeed = R.F32(); M.YawSpeed = R.F32();
		return M;
	}

	FGlobalPositionInt DecodeGlobalPositionInt(const FFrame& F)
	{
		FReader R(F.Payload); FGlobalPositionInt M;
		M.TimeBootMs = R.U32(); M.Lat = R.I32(); M.Lon = R.I32(); M.Alt = R.I32(); M.RelativeAlt = R.I32();
		M.Vx = R.I16(); M.Vy = R.I16(); M.Vz = R.I16(); M.Hdg = R.U16();
		return M;
	}

	FServoOutputRaw DecodeServoOutputRaw(const FFrame& F)
	{
		FReader R(F.Payload); FServoOutputRaw M;
		M.TimeUsec = R.U32();
		for (int i = 0; i < 8; ++i) M.Servo[i] = R.U16();
		M.Port = R.U8();
		for (int i = 8; i < 16; ++i) M.Servo[i] = R.U16();
		return M;
	}

	FRcChannels DecodeRcChannels(const FFrame& F)
	{
		FReader R(F.Payload); FRcChannels M;
		M.TimeBootMs = R.U32();
		for (int i = 0; i < 18; ++i) M.Chan[i] = R.U16();
		M.ChanCount = R.U8(); M.Rssi = R.U8();
		return M;
	}

	FVfrHud DecodeVfrHud(const FFrame& F)
	{
		FReader R(F.Payload); FVfrHud M;
		M.Airspeed = R.F32(); M.Groundspeed = R.F32(); M.Alt = R.F32(); M.Climb = R.F32(); M.Heading = R.I16(); M.Throttle = R.U16();
		return M;
	}

	FCommandAck DecodeCommandAck(const FFrame& F)
	{
		FReader R(F.Payload); FCommandAck M;
		M.Command = R.U16(); M.Result = R.U8(); M.Progress = R.U8(); M.ResultParam2 = R.I32(); M.TargetSystem = R.U8(); M.TargetComponent = R.U8();
		return M;
	}

	FStatusText DecodeStatusText(const FFrame& F)
	{
		FReader R(F.Payload); FStatusText M;
		M.Severity = R.U8(); M.Text = R.Chars(50); M.Id = R.U16(); M.ChunkSeq = R.U8();
		return M;
	}

	FParamValue DecodeParamValue(const FFrame& F)
	{
		FReader R(F.Payload); FParamValue M;
		M.Value = R.F32(); M.Count = R.U16(); M.Index = R.U16(); M.Id = R.Chars(16); M.Type = R.U8();
		return M;
	}

	FHomePosition DecodeHomePosition(const FFrame& F)
	{
		FReader R(F.Payload); FHomePosition M;
		M.Latitude = R.I32(); M.Longitude = R.I32(); M.Altitude = R.I32(); M.X = R.F32(); M.Y = R.F32(); M.Z = R.F32();
		return M;
	}

	FBatteryStatus DecodeBatteryStatus(const FFrame& F)
	{
		FReader R(F.Payload); FBatteryStatus M;
		M.CurrentConsumed = R.I32(); M.EnergyConsumed = R.I32(); M.Temperature = R.I16();
		for (int i = 0; i < 10; ++i) M.Voltages[i] = R.U16();
		M.CurrentBattery = R.I16(); M.Id = R.U8(); R.U8(); R.U8(); M.Remaining = R.I8();
		return M;
	}

	FWind DecodeWind(const FFrame& F)
	{
		FReader R(F.Payload); FWind M;
		M.Direction = R.F32(); M.Speed = R.F32(); M.SpeedZ = R.F32();
		return M;
	}

	FExtendedSysState DecodeExtendedSysState(const FFrame& F)
	{
		FReader R(F.Payload); FExtendedSysState M;
		M.VtolState = R.U8(); M.LandedState = R.U8();
		return M;
	}

	// ---- paketleme ----
	std::vector<uint8_t> PackHeartbeatGcs()
	{
		FWriter W;
		W.U32(0);   // custom_mode
		W.U8(6);    // MAV_TYPE_GCS
		W.U8(8);    // MAV_AUTOPILOT_INVALID
		W.U8(0);    // base_mode
		W.U8(4);    // MAV_STATE_ACTIVE
		W.U8(3);    // mavlink_version
		return W.B;
	}

	std::vector<uint8_t> PackCommandLong(uint8_t TS, uint8_t TC, uint16_t Command, float P1, float P2, float P3, float P4, float P5, float P6, float P7, uint8_t Confirmation)
	{
		FWriter W;
		W.F32(P1); W.F32(P2); W.F32(P3); W.F32(P4); W.F32(P5); W.F32(P6); W.F32(P7);
		W.U16(Command); W.U8(TS); W.U8(TC); W.U8(Confirmation);
		return W.B;
	}

	std::vector<uint8_t> PackParamSet(uint8_t TS, uint8_t TC, const std::string& Id, float Value, uint8_t Type)
	{
		FWriter W;
		W.F32(Value); W.U8(TS); W.U8(TC); W.Chars(Id, 16); W.U8(Type);
		return W.B;
	}

	std::vector<uint8_t> PackParamRequestRead(uint8_t TS, uint8_t TC, const std::string& Id, int16_t Index)
	{
		FWriter W;
		W.I16(Index); W.U8(TS); W.U8(TC); W.Chars(Id, 16);
		return W.B;
	}

	std::vector<uint8_t> PackRcOverride(uint8_t TS, uint8_t TC, const uint16_t Chan[18])
	{
		FWriter W;
		for (int i = 0; i < 8; ++i) W.U16(Chan[i]);
		W.U8(TS); W.U8(TC);
		for (int i = 8; i < 18; ++i) W.U16(Chan[i]);
		return W.B;
	}

	std::vector<uint8_t> PackRequestDataStream(uint8_t TS, uint8_t TC, uint8_t StreamId, uint16_t RateHz, bool bStart)
	{
		FWriter W;
		W.U16(RateHz); W.U8(TS); W.U8(TC); W.U8(StreamId); W.U8(bStart ? 1 : 0);
		return W.B;
	}

	std::vector<uint8_t> PackSetMode(uint8_t TS, uint32_t CustomMode)
	{
		FWriter W;
		W.U32(CustomMode); W.U8(TS); W.U8(MODE_FLAG_CUSTOM);
		return W.B;
	}
}
