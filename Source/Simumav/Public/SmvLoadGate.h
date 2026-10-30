// Yükleme kapısı: uçuşa / harita turuna / ana menüye girişte sahne TAMAMEN hazır olmadan oyuncuya gösterilmez.
// Aşamalar ölçülebilir ilerlemeyle izlenir (harita + alt seviye akışı, dekor/kalabalık/araç gövdesi, atmosfer ilk karesi, SITL GPS/EKF,
// gölgelendirici/PSO derlemesi, doku akışı, son ön ısınma kareleri); her aşamanın zaman aşımı ve günlük satırı vardır ("SMVLOAD").
// Oyun iş parçacığı kilitlenmez: kapı her karede yoklanır, sahne arkada çizilmeye devam eder (Lumen / bulut / pozlama ısınır),
// kabuk tam ekran yükleme katmanı ile sahneyi örter.
#pragma once

#include "CoreMinimal.h"

class UWorld;

enum class ESmvLoad : uint8 { World, Scenery, Atmos, Sitl, Shaders, Streaming, Warmup, Num };

struct SIMUMAV_API FSmvLoadStage
{
	bool bUsed = true;
	bool bDone = false;
	bool bTimeout = false;
	float Progress = 0.f;
	double DoneAt = 0;      // kapı başlangıcından itibaren (sn)
	FString Detail;
	int32 StableFrames = 0;
};

class SIMUMAV_API FSmvLoadGate
{
public:
	/** Kapıyı başlat (Label: günlük etiketi; ucus | tur | menu). Kullanılmayan aşamalar baştan tamam sayılır. */
	void Begin(const FString& InLabel, std::initializer_list<ESmvLoad> Used);
	void Clear() { bActive = false; bFailed = false; FailText.Reset(); FailStage = ESmvLoad::Num; }
	bool IsActive() const { return bActive; }
	bool IsDone() const;
	/** Kapı başarısız: bu aşama tamamlanamadı (ör. harita / zemin yüklenemedi) — uçuşa GİRİLMEZ; yükleme ekranı neden + Yeniden dene / Menü gösterir */
	void Fail(ESmvLoad S, const FString& Reason);
	bool IsFailed() const { return bActive && bFailed; }
	const FString& FailReason() const { return FailText; }
	ESmvLoad FailedStage() const { return FailStage; }
	/** Aşamayı güncelle: ilerleme 0..1, hazır mı (StableNeed kare üst üste hazır kalınca tamam), ayrıntı metni.
	 *  TimeoutS: kapı başlangıcından bu süre geçince zaman aşımıyla tamam (günlükte uyarı). */
	void Update(ESmvLoad S, float Progress, bool bReady, const FString& Detail, double TimeoutS, int32 StableNeed = 1);
	const FSmvLoadStage& Get(ESmvLoad S) const { return Stages[(int32)S]; }
	bool Done(ESmvLoad S) const { return Stages[(int32)S].bDone; }
	/** Ağırlıklı toplam ilerleme (0..1; azalmaz) */
	float Overall() const;
	double Elapsed() const;
	/** Şu an üzerinde çalışılan (ilk bitmemiş) aşama; hepsi bitti → Num */
	ESmvLoad Current() const;
	const FString& Label() const { return LabelStr; }
	/** Bitince bir kez: toplam süre ve aşama süreleri günlüğe ("SMVLOAD [etiket] tamam ...") */
	void LogSummary();
	static const TCHAR* StageKey(ESmvLoad S);      // dil anahtarı (load.st.world …)
	static const TCHAR* StageLogName(ESmvLoad S);  // günlük adı (ASCII)

	// ---- ölçüm yoklamaları (motor) ----
	/** Gölgelendirici derleme kuyruğunda kalan iş (paketli oyunda çoğunlukla 0) */
	static int32 ShaderJobs();
	/** PSO: önbellek ön derlemesinde kalan + etkin PSO ön önbellekleme istekleri */
	static int32 PsoPending();
	/** Doku/ağ akışı: daha yüksek çözünürlük bekleyen kaynak sayısı (IStreamingManager) */
	static int32 StreamingPending();
	/** Bekleyen zaman uyumsuz paket yüklemesi */
	static int32 AsyncPackages();
	/** World Partition akışı (dünya WP değilse true) */
	static bool WorldPartitionDone(UWorld* W);
	/** Sahnedeki tüm gök ışıklarını yeniden yakala (UDS/yerleşik); bileşen sayısı */
	static int32 RecaptureSky(UWorld* W);

private:
	FSmvLoadStage Stages[(int32)ESmvLoad::Num];
	FString LabelStr;
	double T0 = 0;
	bool bActive = false;
	bool bSummaryDone = false;
	bool bFailed = false;
	FString FailText;
	ESmvLoad FailStage = ESmvLoad::Num;
	mutable float Shown = 0.f;
};
