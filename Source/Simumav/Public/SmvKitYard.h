// Parça paketi sahası (ör. KitBash3D Refineries): paketin hazır haritası yok (Main.umap'te yalnız 10 × 10 m zemin — Rafineri "hiç
// gelmiyordu", kullanıcı 2026-10-28) → paketin statik ağları (FSmvMapDef::KitDir) varlık kayıt defterinden okunur, büyükten küçüğe
// raf yöntemiyle bir tesis sahasına dizilir (aralarda 9 m yol), altına beton zemin. Harita seviyesinin içine kurulur: yerleştirme,
// kalkış araması, saha çiti ve çevre diski seviyenin kendi aktörleri gibi görür.
#pragma once

#include "CoreMinimal.h"

class AActor;
class ULevel;
class UWorld;

struct SIMUMAV_API FSmvKitYard
{
	/** Into seviyesine (nullptr: kalıcı seviye) saha aktörü kurar; ağ yoksa nullptr */
	static AActor* Build(UWorld* W, ULevel* Into, const FString& Dir, int32 Seed);
};
