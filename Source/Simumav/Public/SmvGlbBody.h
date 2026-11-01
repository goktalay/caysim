// SIMUMAV — GLB dron gövdesi (çalışma anı): pakete gömülü Content/Simumav/ModelsSrc/<ad>.glb dosyası SmvCore::FGlbModel ile okunur, düğüm adlarına
// göre hareketli parçalara ayrılır ve ProceduralMesh bileşenleriyle kurulur. Hareket:
//   • pervane / rotor (rotor_*, prop_*, propeller, blade*) — ARM'da ve SERVO_OUTPUT_RAW motor/gaz çıkışına göre dönüş; hızda bulanık disk
//   • helikopter ana / kuyruk / tandem / koaksiyel rotorları — rotor devir kontrolcüsü gibi (ARM'da ~4 sn'de tam devir)
//   • kontrol yüzeyleri (aileron_l/r, elevon_l/r, elevator, rudder, ruddervator_l/r, flap_l/r) — SITL'in kendi kanal yorumuyla (SIM_Plane:
//     kanal 1/2/4 + elevon/vtail karışımı) sapma; menteşe = yüzeyin ön kenarı, eksen = açıklık doğrultusu (PCA)
//   • tekerlek / direksiyon (wheel_*, steer_*) — yer hızı ve SERVO1 direksiyonu
//   • seyir ışıkları (nav_l kırmızı, nav_r yeşil, nav_tail beyaz, beacon kırmızı çakar; modelde yoksa sınır kutusundan üretilir)
// Eksen: glTF (sağ el, Y yukarı, burun +X, sağ +Z, metre) → Unreal (X ileri, Y sağ, Z yukarı, cm): (x, z, y)·100; katalog YawDeg önce uygulanır.
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "SmvGlbBody.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class UStaticMeshComponent;
class UTexture2D;

/** Gövde kurulum seçenekleri (katalogdan). */
struct FSmvGlbBuildOptions
{
	/** Modelin glTF Y ekseni etrafında ön döndürmesi (burun +X olmayan dosyalar; ör. Caymaz +Z → 90). */
	float YawDeg = 0.f;
	/** SIMUMAV tipi: fpv | multirotor | plane | vtol | interceptor | heli | rover */
	FString Type;
	/** Sabit kanat düzeni: classic | elevon | vtail (SITL kanal karışımı) */
	FString Airframe;
	/** Modelde olmayan VTOL kaldırma rotorlarını kollarla ekle (VTOL-Caymaz) */
	bool bAddLiftRotors = false;
	/** Seyir ışıkları (modelde yoksa sınır kutusundan) */
	bool bNavLights = true;
};

/** Her karede gövdeye verilen girdiler. */
struct FSmvGlbInputs
{
	bool bArmed = false;
	float ThrottlePct = 0.f;      // telemetri gaz %
	float GroundspeedMs = 0.f;
	const TArray<int32>* Servo = nullptr;     // SERVO_OUTPUT_RAW (PWM; 0 = yok)
	const TArray<int32>* ServoFn = nullptr;   // SERVOn_FUNCTION (bilinmiyorsa 0)
};

UCLASS()
class SIMUMAV_API USmvGlbBody : public USceneComponent
{
	GENERATED_BODY()

public:
	USmvGlbBody();
	/** GLB'den gövdeyi kurar (eski parçalar silinir). Başarısızsa false (çağıran yer tutucu gövdeye düşer). */
	bool Build(const TArray<uint8>& Bytes, const FSmvGlbBuildOptions& Opt, const FString& DebugName);
	void Clear();
	/** Pervane/yüzey/teker/ışık hareketi. */
	void Animate(float Dt, const FSmvGlbInputs& In);

	/** Ölçeksiz (gerçek) sınır bilgisi, cm, gövde çerçevesi */
	FBox GetLocalBounds() const { return Bounds; }
	int32 NumSpinners() const { return Parts.FilterByPredicate([](const FPart& P) { return P.Kind == EKind::Spinner; }).Num(); }
	int32 NumSurfaces() const { return Parts.FilterByPredicate([](const FPart& P) { return P.Kind == EKind::Surface; }).Num(); }
	FString Summary() const { return BuildSummary; }
	/** Gece / alacakaranlık: seyir ışıklarına küçük, gölgesiz nokta ışıklar eklenir (gündüz yalnız parlayan küreler). Atmosfer kurulumu ayarlar. */
	static void SetNightLighting(bool bOn) { bNightLighting = bOn; }
	/** seyir ışığı kürelerinin parlaklık çarpanı: menü vitrini yakın plan (hangar) 0,25 — yakından ışık taşması gövdeyi beyaz bir pusla örtüyordu */
	static void SetGlowScale(float S) { GlowScale = S; }

private:
	enum class EKind : uint8 { Spinner, Surface, Wheel, Steer, Light, Static };
	enum class ESpinRole : uint8 { Lift, Forward, HeliMain, HeliTail };
	struct FPart
	{
		FString Name;
		EKind Kind = EKind::Static;
		int32 ParentPart = INDEX_NONE;
		FVector PivotBody = FVector::ZeroVector;   // gövde çerçevesi (cm)
		FVector Axis = FVector::UpVector;          // dönüş / menteşe ekseni (gövde çerçevesi, birim)
		FVector Extent = FVector::ZeroVector;
		float RadiusCm = 10.f;
		float Dir = 1.f;                           // pervane dönüş yönü / yüzey işaret düzeltmesi
		ESpinRole Role = ESpinRole::Lift;
		float Speed = 0.f;                         // 0..1 yumuşatılmış
		float Phase = 0.f;                         // derece
		FLinearColor LightColor = FLinearColor::White;
		bool bBeacon = false;
		USceneComponent* Pivot = nullptr;
		UStaticMeshComponent* Disc = nullptr;
		UMaterialInstanceDynamic* DiscMat = nullptr;
		UPointLightComponent* Light = nullptr;
	};
	TArray<FPart> Parts;
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> Owned;
	UPROPERTY() TArray<TObjectPtr<UTexture2D>> Textures;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Mids;
	FBox Bounds = FBox(ForceInit);
	FSmvGlbBuildOptions Options;
	FString BuildSummary;
	float RotorSpool = 0.f;   // helikopter rotor devri 0..1
	double TimeS = 0;
	static bool bNightLighting;
	static float GlowScale;

	FPart& AddPart(const FString& Name, EKind Kind);
	void FinishSpinners();
	void AddNavLight(const FString& Name, const FVector& PosCm, const FLinearColor& Color, bool bBeacon);
	void AddLiftRotors();
	static UMaterialInterface* LoadMat(const TCHAR* Path);
};
