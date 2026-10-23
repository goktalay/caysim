// Uçuş video kaydı (web WebM kaydının eşi): oyun görüntüsü 15 kare/sn, en çok 1280 px genişlikte JPEG kareler olarak alınır ve
// MJPEG AVI dosyasına akıtılır (Saved/Simumav/Videos/<tarih>.avi; Windows Filmler ve TV, VLC, tarayıcı dışı oynatıcılar açar).
// Kare yakalama oyun iş parçacığında (Viewport->ReadPixels) yapılır — kayıt sırasında kare hızı bir miktar düşebilir.
#pragma once

#include "CoreMinimal.h"

class IFileHandle;

class SIMUMAV_API FSmvVideoRec
{
public:
	~FSmvVideoRec();
	bool Start(int32 Fps = 15, int32 MaxW = 1280);
	void Tick(float Dt);
	/** Dosyayı kapatır; dönüş: yol (kare yoksa boş) */
	FString Stop();
	bool IsOn() const { return File != nullptr; }
	double Seconds() const { return Frames / (double)FMath::Max(1, Fps); }

private:
	IFileHandle* File = nullptr;
	FString Path;
	int32 Fps = 15, MaxW = 1280, W = 0, H = 0, Frames = 0;
	int64 MoviPos = 0, MaxChunk = 0;
	float Acc = 0.f;
	struct FIdx { uint32 Off, Size; };
	TArray<FIdx> Index;
	void WriteHeader();
	void Grab();
};
