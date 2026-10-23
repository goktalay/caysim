// SIMUMAV — atmosfer: Ultra Dynamic Sky + Ultra Dynamic Weather (Fab; varsa) ya da yerleşik gök (Sky Atmosphere, hacimsel bulut, üstel yükseklik
// sisi + hacimsel sis), gerçek güneş/ay konumu (parkur enlem/boylamı, bugünün tarihi, yaklaşık saat dilimi), SITL rüzgârıyla aynı yön/hızda bulut ve
// rüzgâr, yağmur/kar/şimşek, ıslak zemin/su birikintisi, sınırsız post-process hacmi (Lumen GI + yansıma, otomatik pozlama, hafif renk düzeltme)
// ve grafik ön ayarına (Düşük…Sinematik) bağlı kalite. Hem düz dünyada (prosedürel / Fab haritası / şehir) hem Cesium gerçek dünyada çalışır.
// UDS / UDW değişkenleri Blueprint'te adla (boşluk/büyük-küçük harf duyarsız) aranır; bulunan / bulunamayan adlar günlüğe yazılır
// ("Atmosfer:" satırları). Ad listesi derleme günlüğündeki UDS özellik dökümüyle (USmvAtmosphereLibrary::DumpUdsProperties) doğrulanır.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SimumavTypes.h"
#include "SmvAtmosphere.generated.h"

class UWorld;
class AActor;

/** Atmosfer kurulum girdisi (parkurdan). */
struct SIMUMAV_API FSmvAtmosphereCfg
{
	double Lat = 39.9334, Lon = 32.8597;
	FString TimeOfDay = TEXT("day");   // day | dusk | night | dawn
	FString Weather = TEXT("clear");   // clear | cloudy | rain | fog | snow
	double WindMs = 0, WindDirDeg = 0, WindGustMs = 0;   // SITL ile aynı: yön = rüzgârın ESTİĞİ yön (kuzeyden, derece)
	double Fog01 = 0.05;
	bool bCesium = false;
	/** Grafik kalitesi 0..4 (Düşük, Orta, Yüksek, Epik, Sinematik); < 0 → kullanıcı ayarından */
	int32 Quality = -1;
	static FSmvAtmosphereCfg FromTrack(const FSmvTrack& T, bool bCesium);
};

class SIMUMAV_API FSmvAtmosphere
{
public:
	/** UDS (+UDW) kurar (varsa → true; çağıran yerleşik göğü kapatır) ya da yerleşik göğü ayarlar; her durumda post-process hacmi ve zemin
	 *  ıslaklığı. OutActors: sahne temizlenirken yok edilecek aktörler. */
	static bool Apply(UWorld* W, const FSmvAtmosphereCfg& Cfg, TArray<AActor*>& OutActors);
	/** Grafik ön ayarı değişti: cvar'lar + sahnedeki atmosfer post-process hacmi ve UDS kalite değişkenleri. */
	static void ApplyQuality(UWorld* W, int32 Quality);
	/** Kullanıcının kalite düzeyi (0..4). */
	static int32 CurrentQuality();
};

UCLASS()
class SIMUMAV_API USmvAtmosphereLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Ultra Dynamic Sky / Weather Blueprint değişkenlerini (ad, tür, varsayılan) günlüğe döker — derleme betiği (editor Python) çağırır. */
	UFUNCTION(BlueprintCallable, Category = "Simumav|Atmosfer")
	static FString DumpUdsProperties();
};
