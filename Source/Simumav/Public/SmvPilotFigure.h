// 3B tam boy pilot figürü (web crew/avatar_body.js + avatar_mesh.js eşi; temel şekillerden kodla): beden (erkek/kadın, ince/orta/geniş),
// ten, yüz (şekil, göz + göz rengi, kaş + kalınlık, burun, ağız, sakal, çil, ben, gözlük), saç (11 model + renk), kıyafet (tulum / ceket /
// gündelik + arma), başlık (kask + vizör / açık vizör + ayna, şapka, kulaklık, yok), aksesuar (mikrofon, tepelik, anten, şerit).
// Mürettebat ekranında 3B önizleme (ASmvMenuScene yakalaması) ve uçuşta RC kumanda tutan pilot (başparmaklar çubuk girdisini izler,
// nefes ve ağırlık aktarma, baş dronu izler) olarak kullanılır.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimumavTypes.h"
#include "SmvPilotFigure.generated.h"

class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;

UCLASS()
class SIMUMAV_API ASmvPilotFigure : public AActor
{
	GENERATED_BODY()

public:
	ASmvPilotFigure();
	virtual void Tick(float DeltaSeconds) override;
	/** Görünümü (yeniden) kur */
	void Build(const FSmvAvatar& A, bool bWithRc);
	/** Uçuşta: çubuk konumları (−1..1) ve bakılacak dünya noktası */
	void SetPose(const FVector2D& StickL, const FVector2D& StickR, const FVector& LookAt, bool bHasLook);
	/** Göz yüksekliği (cm, aktör tabanından) — kamera çerçeveleme */
	float HeadHeightCm() const { return HeadZ; }
	/** Düşük ayrıntı (küçük resimler): MetaHuman yerine temel şekil figürü */
	bool bLowDetail = false;
	/** Figür MetaHuman (MCC) ile mi kuruldu */
	bool IsMetaHuman() const { return bMh; }

protected:
	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<USceneComponent> Torso;
	UPROPERTY() TObjectPtr<USceneComponent> Head;
	UPROPERTY() TObjectPtr<USceneComponent> Rc;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> ThumbL;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> ThumbR;
	UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> CylMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> SphMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> ConeMesh;
	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMat;
	/** MetaHuman (MCC): gövde (poz verilebilir; kollar RC tutar, baş drona bakar), lider pozlu yüz, tüyler (groom) */
	UPROPERTY() TObjectPtr<class UPoseableMeshComponent> MhBody;
	UPROPERTY() TObjectPtr<class USkeletalMeshComponent> MhFace;
	UPROPERTY() TArray<TObjectPtr<class USceneComponent>> MhGrooms;

private:
	float HeadZ = 165.f;
	float T = 0.f;
	FVector2D Sl = FVector2D::ZeroVector, Sr = FVector2D::ZeroVector;
	FVector Look = FVector::ZeroVector;
	bool bLook = false;
	FVector ThumbL0 = FVector::ZeroVector, ThumbR0 = FVector::ZeroVector;
	/** Parça: şekil 0 küp, 1 silindir, 2 küre, 3 koni; konum/ölçek cm (ebeveyne göre) */
	UStaticMeshComponent* Part(USceneComponent* Parent, int32 Shape, const FVector& Pos, const FVector& SizeCm, const FLinearColor& Color, const FRotator& Rot = FRotator::ZeroRotator);
	/** Parça malzemesi: pürüzlülük / metaliklik / ışıma (kask, vizör, kayış, toka). Rot verilirse parça döndürülür. */
	static UStaticMeshComponent* Mat(UStaticMeshComponent* C, float Rough, float Metal, const FLinearColor& Emissive = FLinearColor::Black, const FRotator& Rot = FRotator::ZeroRotator);
	void Clear();
	/** Kask / vizör / şapka / kulaklık / gözlük / mikrofon (ve bPrimHair ise temel şekil saç) — Head düğümüne */
	void HeadGear(const FSmvAvatar& A, const FVector& Hs, float Fx, bool bPrimHair);
	bool BuildMetaHuman(const FSmvAvatar& A, bool bWithRc);
	/** MetaHuman kol pozu (bileşen uzayında kemik yönlendirme): RC tutma ya da serbest duruş */
	void PoseMhArms(bool bWithRc);
	bool bMh = false;
	FQuat MhHeadRef = FQuat::Identity;
	FQuat MhNeckRef = FQuat::Identity;
	FVector MhHeadLoc = FVector::ZeroVector;
	FVector MhHeadOff = FVector(2.f, 0.f, 9.f);
	FRotator HeadRel = FRotator::ZeroRotator;
};
