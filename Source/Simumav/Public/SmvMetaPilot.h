// MetaHuman pilot (Fab "MCC – Meta Character Creator" içeriği). Editörsüz entegrasyon:
//   - Derlemede Scripts/Python/simumav_varliklar.py → pilot_kiti() MCC klasöründen seçilen yüz / gövde / saç-kaş-sakal-kirpik groom'ları
//     ve yüz başına groom bağlamalarını /Game/Simumav/Pilot/DA_SmvPilotKit (USmvPilotKit) varlığına YUMUŞAK başvuru olarak yazar
//     → yalnız bunlar pakete pişirilir (MCC'nin tamamı ~15 GB; anime/elf/goblin yüzleri, mayo/kulak/kuyruk aksesuarları alınmaz).
//   - Çalışma anında FSmvMetaPilot avatar alanlarını (cinsiyet, ten s1–s8, yüz/göz/burun → yüz seçimi, saç modeli, kaş kalınlığı, sakal)
//     kit girdilerine eşler; ASmvPilotFigure gövdeyi PoseableMesh (kollar RC tutar, baş drona bakar), yüzü lider pozlu SkeletalMesh,
//     tüyleri GroomComponent olarak kurar. Kit yoksa temel şekil figürü kalır (docs/pilot-metahuman.md).
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SimumavTypes.h"
#include "SmvMetaPilot.generated.h"

class USkeletalMesh;

USTRUCT(BlueprintType)
struct SIMUMAV_API FSmvPilotKitEntry
{
	GENERATED_BODY()
	/** body_m | body_f | face | groom | binding */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simumav") FString Kind;
	/** face: yüz adı (Bruce, Jelena…); groom: groom adı (Hair_S_Casual…); binding: groom adı */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simumav") FString Name;
	/** binding: yüz adı */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simumav") FString Extra;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simumav") bool bFemale = false;
	/** face: yaklaşık ten kademesi 1 (açık) … 8 (koyu) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simumav") int32 Tone = 3;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simumav") TSoftObjectPtr<UObject> Asset;
};

UCLASS(BlueprintType)
class SIMUMAV_API USmvPilotKit : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simumav") TArray<FSmvPilotKitEntry> Entries;
};

/** Avatar → MetaHuman parça seçimi */
struct SIMUMAV_API FSmvMetaPilotParts
{
	USkeletalMesh* Body = nullptr;
	USkeletalMesh* Face = nullptr;
	FString FaceName;
	/** (groom, bağlama) çiftleri: saç, kaş, sakal/bıyık, kirpik (yoksa boş) */
	TArray<TPair<UObject*, UObject*>> Grooms;
};

struct SIMUMAV_API FSmvMetaPilot
{
	/** Kit pakette var ve etkin mi ([/Script/Simumav.SmvPilot] MetaHuman=True) */
	static bool Available();
	/** Avatar için parçaları seçer ve yükler (eşzamanlı; yüz ağları büyüktür — önbelleğe alınır). false → kit / yüz yok. */
	static bool Resolve(const FSmvAvatar& A, FSmvMetaPilotParts& Out);
};
