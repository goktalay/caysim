#include "TelemetryBuffer.h"

FTelemetryBuffer::FTelemetryBuffer(double InDelayS, int32 InCapacity)
	: Capacity(FMath::Max(8, InCapacity)), DelayS(InDelayS)
{
	Frames.Reserve(Capacity);
}

FQuat FTelemetryBuffer::NedEulerToQuat(float Roll, float Pitch, float Yaw)
{
	// NED: x ileri, y sağ, z aşağı; sıra yaw(z) → pitch(y) → roll(x). Sağ el kuralı.
	// FQuat sol el UE eksenlerinde çalışır ama burada yalnız cebirsel bir ZYX bileşimi yapıyoruz;
	// dönüşüm matrisi DroneActor'da ayrıca ele alınır (bkz. CaySimGeo::NedToUnrealRotator).
	const double cr = FMath::Cos(Roll * 0.5), sr = FMath::Sin(Roll * 0.5);
	const double cp = FMath::Cos(Pitch * 0.5), sp = FMath::Sin(Pitch * 0.5);
	const double cy = FMath::Cos(Yaw * 0.5), sy = FMath::Sin(Yaw * 0.5);
	FQuat Q;
	Q.W = cr * cp * cy + sr * sp * sy;
	Q.X = sr * cp * cy - cr * sp * sy;
	Q.Y = cr * sp * cy + sr * cp * sy;
	Q.Z = cr * cp * sy - sr * sp * cy;
	Q.Normalize();
	return Q;
}

void FTelemetryBuffer::QuatToNedEuler(const FQuat& Q, float& Roll, float& Pitch, float& Yaw)
{
	const double sinr_cosp = 2.0 * (Q.W * Q.X + Q.Y * Q.Z);
	const double cosr_cosp = 1.0 - 2.0 * (Q.X * Q.X + Q.Y * Q.Y);
	Roll = (float)FMath::Atan2(sinr_cosp, cosr_cosp);
	const double sinp = FMath::Clamp(2.0 * (Q.W * Q.Y - Q.Z * Q.X), -1.0, 1.0);
	Pitch = (float)FMath::Asin(sinp);
	const double siny_cosp = 2.0 * (Q.W * Q.Z + Q.X * Q.Y);
	const double cosy_cosp = 1.0 - 2.0 * (Q.Y * Q.Y + Q.Z * Q.Z);
	Yaw = (float)FMath::Atan2(siny_cosp, cosy_cosp);
}

FCaySimPose FTelemetryBuffer::FromTele(const FCaySimTelemetry& T, double Time)
{
	FCaySimPose P;
	P.T = Time;
	P.Lat = T.Lat; P.Lon = T.Lon; P.AltMsl = T.AltMsl; P.AltRel = T.AltRel;
	P.Roll = T.Roll; P.Pitch = T.Pitch; P.Yaw = T.Yaw;
	P.QNed = NedEulerToQuat(T.Roll, T.Pitch, T.Yaw);
	P.VelNed = FVector(T.Vn, T.Ve, T.Vd);
	P.Throttle = T.Throttle;
	P.Airspeed = T.Airspeed; P.Groundspeed = T.Groundspeed;
	P.Latest = T;
	P.bValid = true;
	return P;
}

void FTelemetryBuffer::Push(const FCaySimTelemetry& Tele, double LocalNow)
{
	double T = Tele.Ts;
	if (T <= 0.0)
	{
		// Zaman damgası yok (sahte kip): yerel saatle besle
		T = LocalNow;
	}
	if (Frames.Num())
	{
		// `tbm` belirgin geri gidiyorsa SITL reset olmuştur: tamponu sıfırla (sözleşme §3.3)
		if (Tele.Tbm > 0 && LastTbm > 0 && Tele.Tbm + 1000 < LastTbm)
		{
			Reset();
		}
	}
	LastTbm = Tele.Tbm;
	if (Frames.Num())
	{
		const double Last = Frames.Last().T;
		if (T <= Last)
		{
			// aynı ya da geri zaman: son kareyi güncelle (TCP parçasında birlikte gelen kareler)
			if (FMath::IsNearlyEqual(T, Last, 1e-6)) Frames.Last().Pose = FromTele(Tele, T);
			return;
		}
		const float Dt = (float)((T - Last) * 1000.0);
		if (Dt < 1000.f) MeanDtMs += (Dt - MeanDtMs) * 0.1f;
	}
	if (Frames.Num() >= Capacity) Frames.RemoveAt(0, 1, EAllowShrinking::No);
	FFrame F;
	F.T = T;
	F.Pose = FromTele(Tele, T);
	Frames.Add(MoveTemp(F));
}

void FTelemetryBuffer::Reset()
{
	Frames.Reset();
	MeanDtMs = 20.f;
	LastTbm = 0;
}

FCaySimPose FTelemetryBuffer::Lerp(const FCaySimPose& A, const FCaySimPose& B, double Alpha)
{
	FCaySimPose P = (Alpha < 0.5) ? A : B; // ayrık alanlar en yakın kareden
	const float a = (float)Alpha;
	P.T = FMath::Lerp(A.T, B.T, Alpha);
	P.Lat = FMath::Lerp(A.Lat, B.Lat, Alpha);
	P.Lon = FMath::Lerp(A.Lon, B.Lon, Alpha);
	P.AltMsl = FMath::Lerp(A.AltMsl, B.AltMsl, Alpha);
	P.AltRel = FMath::Lerp(A.AltRel, B.AltRel, Alpha);
	P.QNed = FQuat::Slerp(A.QNed, B.QNed, a);
	QuatToNedEuler(P.QNed, P.Roll, P.Pitch, P.Yaw);
	P.VelNed = FMath::Lerp(A.VelNed, B.VelNed, a);
	P.Throttle = FMath::Lerp(A.Throttle, B.Throttle, a);
	P.Airspeed = FMath::Lerp(A.Airspeed, B.Airspeed, a);
	P.Groundspeed = FMath::Lerp(A.Groundspeed, B.Groundspeed, a);
	P.bValid = true;
	return P;
}

FCaySimPose FTelemetryBuffer::Sample(double Now) const
{
	if (Frames.Num() == 0) return FCaySimPose();
	const double T = Now - DelayS;
	if (Frames.Num() == 1 || T <= Frames[0].T) return Frames[0].Pose;

	const FFrame& Last = Frames.Last();
	if (T >= Last.T)
	{
		// Tampon kuru: hız vektörüyle sınırlı ileri taşıma, sonra tut
		const double Dt = FMath::Min(T - Last.T, MaxExtrapolateS);
		if (Dt <= 0.0) return Last.Pose;
		FCaySimPose P = Last.Pose;
		const double CosLat = FMath::Max(1e-6, FMath::Cos(FMath::DegreesToRadians(P.Lat)));
		P.Lat += (P.VelNed.X * Dt) / 111320.0;
		P.Lon += (P.VelNed.Y * Dt) / (111320.0 * CosLat);
		P.AltMsl -= P.VelNed.Z * Dt;
		P.AltRel -= P.VelNed.Z * Dt;
		P.T = Last.T + Dt;
		return P;
	}

	// İkili arama: Frames[i].T <= T < Frames[i+1].T
	int32 Lo = 0, Hi = Frames.Num() - 1;
	while (Hi - Lo > 1)
	{
		const int32 Mid = (Lo + Hi) / 2;
		if (Frames[Mid].T <= T) Lo = Mid; else Hi = Mid;
	}
	const FFrame& A = Frames[Lo];
	const FFrame& B = Frames[Hi];
	const double Span = B.T - A.T;
	if (Span <= 1e-6) return B.Pose;
	// 5 sn'den uzun boşlukta tutma (kayıt boşlukları)
	if (Span > 5.0) return A.Pose;
	return Lerp(A.Pose, B.Pose, (T - A.T) / Span);
}
