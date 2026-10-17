#include "ReplayPlayer.h"
#include "CaySim.h"
#include "HepgoClient.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Containers/Ticker.h"

THIRD_PARTY_INCLUDES_START
#include "zlib.h"
THIRD_PARTY_INCLUDES_END

bool UCaySimReplayPlayer::GunzipIfNeeded(const TArray<uint8>& In, TArray<uint8>& Out)
{
	// gzip sihirli baytları 1f 8b; değilse düz JSON (HTTP katmanı çözmüş olabilir)
	if (In.Num() < 2 || !(In[0] == 0x1f && In[1] == 0x8b))
	{
		Out = In;
		return true;
	}
	z_stream Z;
	FMemory::Memzero(Z);
	if (inflateInit2(&Z, 16 + MAX_WBITS) != Z_OK) return false; // 16 + MAX_WBITS: gzip başlığını kabul et
	Out.Reset();
	Out.Reserve(In.Num() * 6);
	Z.next_in = const_cast<Bytef*>(In.GetData());
	Z.avail_in = In.Num();
	uint8 Buf[64 * 1024];
	int Ret;
	do
	{
		Z.next_out = Buf;
		Z.avail_out = sizeof(Buf);
		Ret = inflate(&Z, Z_NO_FLUSH);
		if (Ret != Z_OK && Ret != Z_STREAM_END)
		{
			inflateEnd(&Z);
			return false;
		}
		Out.Append(Buf, sizeof(Buf) - Z.avail_out);
	} while (Ret != Z_STREAM_END);
	inflateEnd(&Z);
	return true;
}

void UCaySimReplayPlayer::Load(UHepgoClient* Client, const FString& InSessionId)
{
	Hepgo = Client;
	SessionId = InSessionId;
	RetryCount = 0;
	LoadProgress = 0.f;
	Status = TEXT("Kayıt indiriliyor…");
	if (!Client) { OnLoaded.Broadcast(false, Header); return; }
	Client->FetchBytes(FString::Printf(TEXT("/sessions/%s/replay"), *SessionId), FHepgoBytesNative::CreateUObject(this, &UCaySimReplayPlayer::OnBytes));
}

void UCaySimReplayPlayer::OnBytes(bool bOk, const TArray<uint8>& Bytes)
{
	if (!bOk)
	{
		// 409 HAZIR değil: sunucu ayrıştırırken yeniden dene (en çok 60 × 2 sn)
		if (RetryCount++ < 60 && Hepgo.IsValid())
		{
			Status = FString::Printf(TEXT("Kayıt hazırlanıyor… (%d)"), RetryCount);
			TWeakObjectPtr<UCaySimReplayPlayer> WeakThis(this);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis](float)
			{
				if (UCaySimReplayPlayer* Self = WeakThis.Get())
				{
					if (UHepgoClient* C = Self->Hepgo.Get())
					{
						C->FetchBytes(FString::Printf(TEXT("/sessions/%s/replay"), *Self->SessionId), FHepgoBytesNative::CreateUObject(Self, &UCaySimReplayPlayer::OnBytes));
					}
				}
				return false; // tek sefer
			}), 2.f);
			return;
		}
		Status = TEXT("Kayıt alınamadı.");
		OnLoaded.Broadcast(false, Header);
		return;
	}
	LoadProgress = 0.5f;
	Status = TEXT("Kareler çözülüyor…");
	const bool bLoaded = LoadFromBytes(Bytes);
	LoadProgress = 1.f;
	Status = bLoaded ? FString::Printf(TEXT("%d kare, %.0f sn"), Frames.Num(), Header.DurationS) : TEXT("Kayıt biçimi tanınmadı.");
	OnLoaded.Broadcast(bLoaded, Header);
}

bool UCaySimReplayPlayer::LoadFromBytes(const TArray<uint8>& Bytes)
{
	TArray<uint8> Plain;
	if (!GunzipIfNeeded(Bytes, Plain))
	{
		UE_LOG(LogCaySim, Warning, TEXT("Replay: gzip çözülemedi"));
		return false;
	}
	// UTF-8 → FString
	FUTF8ToTCHAR Conv((const ANSICHAR*)Plain.GetData(), Plain.Num());
	const FString Text(Conv.Length(), Conv.Get());
	return ParseJson(Text);
}

bool UCaySimReplayPlayer::ParseJson(const FString& Text)
{
	TSharedPtr<FJsonObject> O;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, O) || !O.IsValid()) return false;

	Frames.Reset();
	Header = FCaySimReplayHeader();
	double N;
	if (O->TryGetNumberField(TEXT("v"), N)) Header.Version = (int32)N;
	if (O->TryGetNumberField(TEXT("hz"), N)) Header.Hz = (float)N;
	if (O->TryGetNumberField(TEXT("duration_s"), N)) Header.DurationS = (float)N;
	if (O->TryGetNumberField(TEXT("takeoff_t"), N)) Header.TakeoffT = (float)N;
	if (O->TryGetNumberField(TEXT("start_utc"), N)) Header.StartUtc = N;
	O->TryGetStringField(TEXT("vehicle"), Header.Vehicle);
	O->TryGetStringField(TEXT("type"), Header.TypeKey);
	const TSharedPtr<FJsonObject>* Home = nullptr;
	if (O->TryGetObjectField(TEXT("home"), Home) && Home && Home->IsValid())
	{
		(*Home)->TryGetNumberField(TEXT("lat"), Header.HomeLat);
		(*Home)->TryGetNumberField(TEXT("lon"), Header.HomeLon);
		(*Home)->TryGetNumberField(TEXT("alt"), Header.HomeAlt);
	}
	const TArray<TSharedPtr<FJsonValue>>* Ev = nullptr;
	if (O->TryGetArrayField(TEXT("events"), Ev) && Ev)
	{
		for (const auto& V : *Ev)
		{
			const TSharedPtr<FJsonObject> EO = V->AsObject();
			if (!EO.IsValid()) continue;
			FCaySimEvent E;
			EO->TryGetNumberField(TEXT("t"), E.T);
			EO->TryGetStringField(TEXT("text"), E.Text);
			if (EO->TryGetNumberField(TEXT("sev"), N)) E.Severity = (int32)N;
			Header.Events.Add(E);
		}
	}

	// fields: [t, lat, lon, alt_msl, alt_rel, roll, pitch, yaw, vn, ve, vd, gs, as, hdg, thr, mode, armed, batt_v, batt_pct, sats, fix, rc]
	TMap<FString, int32> Idx;
	const TArray<TSharedPtr<FJsonValue>>* Fields = nullptr;
	if (O->TryGetArrayField(TEXT("fields"), Fields) && Fields)
	{
		for (int32 i = 0; i < Fields->Num(); ++i) Idx.Add((*Fields)[i]->AsString(), i);
	}
	auto Col = [&Idx](const TCHAR* Name) { const int32* P = Idx.Find(Name); return P ? *P : -1; };
	const int32 cT = Col(TEXT("t")), cLat = Col(TEXT("lat")), cLon = Col(TEXT("lon")), cMsl = Col(TEXT("alt_msl")), cRel = Col(TEXT("alt_rel"));
	const int32 cR = Col(TEXT("roll")), cP = Col(TEXT("pitch")), cY = Col(TEXT("yaw")), cVn = Col(TEXT("vn")), cVe = Col(TEXT("ve")), cVd = Col(TEXT("vd"));
	const int32 cGs = Col(TEXT("gs")), cAs = Col(TEXT("as")), cHdg = Col(TEXT("hdg")), cThr = Col(TEXT("thr")), cMode = Col(TEXT("mode"));
	const int32 cArm = Col(TEXT("armed")), cBv = Col(TEXT("batt_v")), cBp = Col(TEXT("batt_pct")), cSat = Col(TEXT("sats")), cFix = Col(TEXT("fix")), cRc = Col(TEXT("rc"));

	const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
	if (!O->TryGetArrayField(TEXT("frames"), Rows) || !Rows) return false;
	Frames.Reserve(Rows->Num());
	auto Num = [](const TArray<TSharedPtr<FJsonValue>>& R, int32 c, double Def) -> double
	{
		if (c < 0 || c >= R.Num() || !R[c].IsValid() || R[c]->IsNull()) return Def;
		return R[c]->AsNumber();
	};
	for (const auto& RowV : *Rows)
	{
		const TArray<TSharedPtr<FJsonValue>>* RP = nullptr;
		if (!RowV->TryGetArray(RP) || !RP) continue;
		const TArray<TSharedPtr<FJsonValue>>& R = *RP;
		FFrame F;
		F.T = (float)Num(R, cT, 0);
		FCaySimTelemetry& T = F.Tele;
		T.Ts = Header.StartUtc + F.T;
		T.Lat = Num(R, cLat, 0); T.Lon = Num(R, cLon, 0);
		T.AltMsl = Num(R, cMsl, 0); T.AltRel = Num(R, cRel, 0);
		T.Roll = (float)Num(R, cR, 0); T.Pitch = (float)Num(R, cP, 0); T.Yaw = (float)Num(R, cY, 0);
		T.Vn = (float)Num(R, cVn, 0); T.Ve = (float)Num(R, cVe, 0); T.Vd = (float)Num(R, cVd, 0);
		T.Groundspeed = (float)Num(R, cGs, 0); T.Airspeed = (float)Num(R, cAs, 0);
		T.Heading = (float)Num(R, cHdg, FMath::RadiansToDegrees(T.Yaw));
		T.Climb = -T.Vd;
		T.Throttle = (float)Num(R, cThr, 0);
		if (cMode >= 0 && cMode < R.Num() && R[cMode].IsValid() && !R[cMode]->IsNull()) T.Mode = R[cMode]->AsString();
		if (cArm >= 0 && cArm < R.Num() && R[cArm].IsValid() && !R[cArm]->IsNull())
		{
			T.bArmed = (R[cArm]->Type == EJson::Boolean) ? R[cArm]->AsBool() : (R[cArm]->AsNumber() != 0);
		}
		T.BattV = (float)Num(R, cBv, -1); T.BattPct = (float)Num(R, cBp, -1);
		T.Sats = (int32)Num(R, cSat, -1); T.GpsFix = (int32)Num(R, cFix, 0);
		T.Rc.Init(0, 8);
		const TArray<TSharedPtr<FJsonValue>>* Rc = nullptr;
		if (cRc >= 0 && cRc < R.Num() && R[cRc].IsValid() && R[cRc]->TryGetArray(Rc) && Rc)
		{
			for (int32 i = 0; i < FMath::Min(8, Rc->Num()); ++i) T.Rc[i] = (int32)(*Rc)[i]->AsNumber();
		}
		T.bHasHome = true;
		T.HomeLat = Header.HomeLat; T.HomeLon = Header.HomeLon; T.HomeAlt = Header.HomeAlt;
		F.QNed = FTelemetryBuffer::NedEulerToQuat(T.Roll, T.Pitch, T.Yaw);
		Frames.Add(MoveTemp(F));
	}
	Header.Count = Frames.Num();
	if (Header.DurationS <= 0.f && Frames.Num()) Header.DurationS = Frames.Last().T;

	// Başlangıç: ilk ARM − 5 sn; otomatik oynat
	TimeS = FMath::Max(0.f, Header.TakeoffT - 5.f);
	Speed = 1.f;
	bPlaying = Frames.Num() > 0;
	UE_LOG(LogCaySim, Log, TEXT("Replay yüklendi: %d kare, %.1f Hz, %.0f sn, %d olay"), Frames.Num(), Header.Hz, Header.DurationS, Header.Events.Num());
	return Frames.Num() > 0;
}

void UCaySimReplayPlayer::Play() { bPlaying = Frames.Num() > 0; }
void UCaySimReplayPlayer::Pause() { bPlaying = false; }
void UCaySimReplayPlayer::TogglePlay() { if (bPlaying) Pause(); else Play(); }
void UCaySimReplayPlayer::Restart() { TimeS = FMath::Max(0.f, Header.TakeoffT - 5.f); bPlaying = true; OnTime.Broadcast(TimeS); }
void UCaySimReplayPlayer::SetSpeed(float Mult) { Speed = FMath::Clamp(Mult, 0.25f, 16.f); }
void UCaySimReplayPlayer::Seek(float T) { TimeS = FMath::Clamp(T, 0.f, Header.DurationS); OnTime.Broadcast(TimeS); }
void UCaySimReplayPlayer::Skip(float DeltaS) { Seek(TimeS + DeltaS); }

void UCaySimReplayPlayer::Tick(float DeltaSeconds)
{
	if (!bPlaying || Frames.Num() == 0) return;
	TimeS += DeltaSeconds * Speed;
	if (TimeS >= Header.DurationS)
	{
		TimeS = Header.DurationS;
		bPlaying = false;
	}
	OnTime.Broadcast(TimeS);
}

int32 UCaySimReplayPlayer::FindFrame(float T) const
{
	// Frames[i].T <= T < Frames[i+1].T
	int32 Lo = 0, Hi = Frames.Num() - 1;
	if (T <= Frames[0].T) return 0;
	if (T >= Frames[Hi].T) return Hi;
	while (Hi - Lo > 1)
	{
		const int32 Mid = (Lo + Hi) / 2;
		if (Frames[Mid].T <= T) Lo = Mid; else Hi = Mid;
	}
	return Lo;
}

FCaySimPose UCaySimReplayPlayer::SampleAt(float T) const
{
	FCaySimPose P;
	if (Frames.Num() == 0) return P;
	const int32 i = FindFrame(T);
	const FFrame& A = Frames[i];
	const FFrame* B = (i + 1 < Frames.Num()) ? &Frames[i + 1] : nullptr;
	float Alpha = 0.f;
	if (B)
	{
		const float Span = B->T - A.T;
		Alpha = (Span > 1e-4f && Span <= 5.f) ? FMath::Clamp((T - A.T) / Span, 0.f, 1.f) : 0.f; // 5 sn+ boşlukta tut
	}
	const FCaySimTelemetry& TA = A.Tele;
	const FCaySimTelemetry& TB = B ? B->Tele : A.Tele;
	P.T = T;
	P.Lat = FMath::Lerp(TA.Lat, TB.Lat, (double)Alpha);
	P.Lon = FMath::Lerp(TA.Lon, TB.Lon, (double)Alpha);
	P.AltMsl = FMath::Lerp(TA.AltMsl, TB.AltMsl, (double)Alpha);
	P.AltRel = FMath::Lerp(TA.AltRel, TB.AltRel, (double)Alpha);
	P.QNed = B ? FQuat::Slerp(A.QNed, B->QNed, Alpha) : A.QNed;
	FTelemetryBuffer::QuatToNedEuler(P.QNed, P.Roll, P.Pitch, P.Yaw);
	P.VelNed = FMath::Lerp(FVector(TA.Vn, TA.Ve, TA.Vd), FVector(TB.Vn, TB.Ve, TB.Vd), Alpha);
	P.Throttle = FMath::Lerp(TA.Throttle, TB.Throttle, Alpha);
	P.Airspeed = FMath::Lerp(TA.Airspeed, TB.Airspeed, Alpha);
	P.Groundspeed = FMath::Lerp(TA.Groundspeed, TB.Groundspeed, Alpha);
	P.Latest = (Alpha < 0.5f) ? TA : TB;
	P.Latest.Roll = P.Roll; P.Latest.Pitch = P.Pitch; P.Latest.Yaw = P.Yaw;
	P.Latest.Heading = FMath::Fmod(FMath::RadiansToDegrees(P.Yaw) + 360.f, 360.f);
	P.bValid = true;
	return P;
}

TArray<FCaySimEvent> UCaySimReplayPlayer::EventsBetween(float T0, float T1) const
{
	TArray<FCaySimEvent> Out;
	for (const FCaySimEvent& E : Header.Events)
	{
		if (E.T >= T0 && E.T <= T1) Out.Add(E);
	}
	return Out;
}
