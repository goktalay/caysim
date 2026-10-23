#include "SmvVideoRec.h"
#include "SimumavModule.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"

namespace
{
	void Put32(TArray<uint8>& B, uint32 V) { B.Add(V & 0xff); B.Add((V >> 8) & 0xff); B.Add((V >> 16) & 0xff); B.Add((V >> 24) & 0xff); }
	void Put16(TArray<uint8>& B, uint16 V) { B.Add(V & 0xff); B.Add((V >> 8) & 0xff); }
	void PutCC(TArray<uint8>& B, const char* C) { for (int32 i = 0; i < 4; ++i) B.Add((uint8)C[i]); }
	// başlık yerleşimi (bayt konumları, Patch için)
	constexpr int64 OffRiffSize = 4, OffAvihFrames = 48, OffStrhLength = 140, OffMoviSize = 216, HeaderBytes = 224;
}

FSmvVideoRec::~FSmvVideoRec() { Stop(); }

bool FSmvVideoRec::Start(int32 InFps, int32 InMaxW)
{
	Stop();
	if (!GEngine || !GEngine->GameViewport || !GEngine->GameViewport->Viewport) return false;
	const FIntPoint Vs = GEngine->GameViewport->Viewport->GetSizeXY();
	if (Vs.X < 64 || Vs.Y < 64) return false;
	Fps = FMath::Clamp(InFps, 5, 30); MaxW = InMaxW;
	const float K = Vs.X > MaxW ? (float)MaxW / Vs.X : 1.f;
	W = (FMath::RoundToInt(Vs.X * K) / 2) * 2; H = (FMath::RoundToInt(Vs.Y * K) / 2) * 2;
	Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Simumav/Videos")) / (FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT(".avi"));
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	PF.CreateDirectoryTree(*FPaths::GetPath(Path));
	File = PF.OpenWrite(*Path);
	if (!File) return false;
	Frames = 0; Index.Reset(); MaxChunk = 0; Acc = 0.f;
	WriteHeader();
	MoviPos = HeaderBytes - 4;   // 'movi' fourcc konumu (idx1 göreli konumları buradan)
	return true;
}

void FSmvVideoRec::WriteHeader()
{
	TArray<uint8> B;
	PutCC(B, "RIFF"); Put32(B, 0); PutCC(B, "AVI ");
	PutCC(B, "LIST"); Put32(B, 192); PutCC(B, "hdrl");
	PutCC(B, "avih"); Put32(B, 56);
	Put32(B, 1000000 / Fps); Put32(B, 0); Put32(B, 0); Put32(B, 0x10); Put32(B, (uint32)Frames); Put32(B, 0); Put32(B, 1);
	Put32(B, (uint32)MaxChunk); Put32(B, W); Put32(B, H); Put32(B, 0); Put32(B, 0); Put32(B, 0); Put32(B, 0);
	PutCC(B, "LIST"); Put32(B, 116); PutCC(B, "strl");
	PutCC(B, "strh"); Put32(B, 56);
	PutCC(B, "vids"); PutCC(B, "MJPG"); Put32(B, 0); Put16(B, 0); Put16(B, 0); Put32(B, 0); Put32(B, 1); Put32(B, Fps); Put32(B, 0);
	Put32(B, (uint32)Frames); Put32(B, (uint32)MaxChunk); Put32(B, 0xFFFFFFFF); Put32(B, 0); Put16(B, 0); Put16(B, 0); Put16(B, (uint16)W); Put16(B, (uint16)H);
	PutCC(B, "strf"); Put32(B, 40);
	Put32(B, 40); Put32(B, W); Put32(B, H); Put16(B, 1); Put16(B, 24); PutCC(B, "MJPG"); Put32(B, W * H * 3); Put32(B, 0); Put32(B, 0); Put32(B, 0); Put32(B, 0);
	PutCC(B, "LIST"); Put32(B, 4); PutCC(B, "movi");
	check(B.Num() == HeaderBytes);
	File->Seek(0);
	File->Write(B.GetData(), B.Num());
}

void FSmvVideoRec::Tick(float Dt)
{
	if (!File) return;
	Acc += Dt;
	const float Step = 1.f / Fps;
	if (Acc < Step) return;
	Acc = FMath::Min(Acc - Step, Step);   // geride kalınca kare atla (süre gerçek zamandan sapmasın diye en çok bir kare borç)
	Grab();
}

void FSmvVideoRec::Grab()
{
	if (!GEngine || !GEngine->GameViewport || !GEngine->GameViewport->Viewport) return;
	FViewport* V = GEngine->GameViewport->Viewport;
	TArray<FColor> Px;
	if (!V->ReadPixels(Px) || Px.Num() == 0) return;
	const FIntPoint Vs = V->GetSizeXY();
	if (Px.Num() != Vs.X * Vs.Y) return;
	TArray<FColor> Out;
	Out.SetNumUninitialized(W * H);
	for (int32 y = 0; y < H; ++y)
	{
		const int32 Sy = FMath::Min(Vs.Y - 1, y * Vs.Y / H);
		for (int32 x = 0; x < W; ++x)
		{
			FColor C = Px[Sy * Vs.X + FMath::Min(Vs.X - 1, x * Vs.X / W)];
			C.A = 255;
			Out[y * W + x] = C;
		}
	}
	IImageWrapperModule& M = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> Jpg = M.CreateImageWrapper(EImageFormat::JPEG);
	if (!Jpg.IsValid() || !Jpg->SetRaw(Out.GetData(), Out.Num() * 4, W, H, ERGBFormat::BGRA, 8)) return;
	const TArray64<uint8> Data = Jpg->GetCompressed(80);
	if (Data.Num() == 0) return;
	TArray<uint8> Ck;
	PutCC(Ck, "00dc"); Put32(Ck, (uint32)Data.Num());
	const int64 Pos = File->Tell();
	File->Write(Ck.GetData(), Ck.Num());
	File->Write(Data.GetData(), Data.Num());
	if (Data.Num() & 1) { const uint8 Z = 0; File->Write(&Z, 1); }
	Index.Add({ (uint32)(Pos - MoviPos), (uint32)Data.Num() });
	MaxChunk = FMath::Max<int64>(MaxChunk, Data.Num());
	++Frames;
}

FString FSmvVideoRec::Stop()
{
	if (!File) return FString();
	// idx1
	const int64 MoviEnd = File->Tell();
	TArray<uint8> B;
	PutCC(B, "idx1"); Put32(B, Index.Num() * 16);
	for (const FIdx& I : Index) { PutCC(B, "00dc"); Put32(B, 0x10); Put32(B, I.Off); Put32(B, I.Size); }
	File->Write(B.GetData(), B.Num());
	const int64 End = File->Tell();
	// başlığı güncelle (kare sayısı, boyutlar)
	WriteHeader();
	auto Patch = [this](int64 Off, uint32 V) { File->Seek(Off); File->Write((const uint8*)&V, 4); };
	Patch(OffRiffSize, (uint32)(End - 8));
	Patch(OffMoviSize, (uint32)(MoviEnd - (OffMoviSize + 4)));
	(void)OffAvihFrames; (void)OffStrhLength;
	File->Flush();
	delete File;
	File = nullptr;
	const FString P = Path;
	if (Frames == 0) { FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*Path); return FString(); }
	return P;
}
