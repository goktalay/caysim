// Coğrafi dönüşümler: WGS84 ↔ Unreal (Cesium georeference ya da düz ENU yedeği), NED → Unreal yönelim
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CaySimGeo.generated.h"

/** Dünya kurulumunun (Cesium ya da düz dünya) sağladığı dönüşüm arayüzü; DroneActor Cesium'a bağımlı kalmaz. */
UINTERFACE(MinimalAPI, BlueprintType)
class UCaySimGeoTransform : public UInterface
{
	GENERATED_BODY()
};

class CAYSIM_API ICaySimGeoTransform
{
	GENERATED_BODY()
public:
	/** (lon, lat, yükseklik m, elipsoid ya da MSL — kurulum belirtir) → Unreal konumu (cm). */
	virtual FVector GeoToUnreal(double Lon, double Lat, double HeightM) const = 0;
	/** Unreal konumu → (lon, lat, yükseklik m). */
	virtual void UnrealToGeo(const FVector& Unreal, double& Lon, double& Lat, double& HeightM) const = 0;
	/** Verilen Unreal konumunda yerel Doğu-Kuzey-Yukarı (ENU) eksenlerini Unreal eksenlerine çeviren dönüş. */
	virtual FQuat EnuToUnrealRotation(const FVector& UnrealLocation) const = 0;
	/** MSL → bu dünyanın yükseklik referansı (Cesium elipsoid ise geoid ayrımı eklenir). */
	virtual double MslToWorldHeight(double Lat, double Lon, double MslM) const { return MslM; }
	/** Yer yüzeyinin dünya yüksekliği (m); bilinmiyorsa false. */
	virtual bool SampleGroundHeight(double Lat, double Lon, double& OutHeightM) const { return false; }
};

namespace CaySimGeo
{
	constexpr double WGS84_A = 6378137.0;
	constexpr double WGS84_F = 1.0 / 298.257223563;
	constexpr double WGS84_E2 = WGS84_F * (2.0 - WGS84_F);

	/** Yerel düz ENU (metre) — küçük alanlar (≤ ~20 km) için yeterli; Cesium yoksa yedek. */
	CAYSIM_API FVector GeoToEnuMeters(double Lon, double Lat, double HeightM, double Lon0, double Lat0, double Height0);
	CAYSIM_API void EnuMetersToGeo(const FVector& Enu, double Lon0, double Lat0, double Height0, double& Lon, double& Lat, double& HeightM);

	/**
	 * NED gövde Euler (rad; roll sağ kanat aşağı +, pitch burun yukarı +, yaw kuzeyden saat yönü +)
	 * → Unreal FRotator (derece), Unreal dünyası için eksen sözleşmesi:
	 *   Cesium for Unreal varsayılanı: +X Doğu, +Y Güney, +Z Yukarı (sol el).
	 *   UE Yaw 0° = +X = Doğu; Yaw pozitif = yukarıdan bakışla saat yönü. Kuzey = Yaw −90°.
	 *   → Yaw_UE = heading − 90°; Pitch_UE = pitch; Roll_UE = roll.
	 * `bEnuXNorth` true ise düz ENU dünyasında X=Kuzey, Y=Doğu alınır (Yaw_UE = heading).
	 */
	CAYSIM_API FRotator NedToUnrealRotator(float RollRad, float PitchRad, float YawRad, bool bEnuXNorth = false);

	/** NED hız (vn, ve, vd) m/s → Unreal yön vektörü (cm/s) aynı sözleşmeyle. */
	CAYSIM_API FVector NedVelocityToUnreal(const FVector& VelNed, bool bEnuXNorth = false);

	/** İki nokta arası kuş uçuşu mesafe (m) ve yön (derece, kuzeyden). */
	CAYSIM_API double HaversineM(double Lat1, double Lon1, double Lat2, double Lon2);
	CAYSIM_API double BearingDeg(double Lat1, double Lon1, double Lat2, double Lon2);
}
