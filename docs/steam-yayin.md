# Steam'de yayın — yapılacaklar listesi

Simumav Steam'e **Steam Direct** ile çıkar. Aşağıdaki maddeler sırasıyla kullanıcıya (yayıncı) düşen işlerdir; kod tarafı hazırdır
(OnlineSubsystemSteam, 480 yer tutucu, 12 başarım, CI yükleme adımı).

## 1. Steamworks partner hesabı
1. https://partner.steamgames.com → *Join the Steamworks Program* → şirket (SKYREX HAVACILIK / GÖKKUBBE Teknoloji) adına kayıt.
2. Sözleşmeler (Steam Distribution Agreement), **kimlik doğrulama**, **banka bilgisi**, **vergi görüşmesi** (Türkiye — ABD vergi formu W-8BEN-E).
3. **Steam Direct ücreti**: uygulama başına 100 USD (uygulama brüt geliri 1.000 USD'yi aşınca iade edilir). Ödemeden sonra ilk ürün için **30 gün**
   bekleme süresi başlar; mağaza sayfası "Yakında" olarak en az **2 hafta** yayında kalmalıdır.

## 2. App ID ve depot
1. *Steamworks › Create new app* → **App ID** alınır (ör. 3xxxxxx). Ardından:
   - `Config/DefaultEngine.ini` `[OnlineSubsystemSteam] SteamDevAppId=<AppID>` ve kökteki `steam_appid.txt` (CI `vars.STEAM_APP_ID` ile
     otomatik değiştirir; elle değiştirmeye gerek yok).
2. *SteamPipe › Depots*: Windows için bir depot (ör. AppID+1). İşletim sistemi: Windows 64-bit; dil: tümü.
3. *Installation › General*: başlatma seçeneği **`Simumav.exe`** (çalışma dizini kök), işletim sistemi Windows 64.
4. *Installation › Redistributables*: **DirectX** ve **VC++ 2015–2022 x64** (UE `-prereqs` kurucusu pakette; Steam'in kendi yeniden dağıtımlarını
   seçmek daha temiz).
5. *Publish* (Steamworks ayarlarında her değişiklikten sonra).

## 3. Başarımlar (Stats & Achievements)
API adları kodda sabittir (`Source/Simumav/Public/SmvSteam.h`, `DefaultEngine.ini` `Achievement_N_Id`). Steamworks'te **aynı API adlarıyla**
oluşturun; her biri için 64×64 JPG **açık** ve **kilitli** simge:

| API adı | Görünen ad (tr) | Koşul |
|---|---|---|
| ACH_FIRST_FLIGHT | İlk Uçuş | ≥ 10 sn ARM'lı uçuş |
| ACH_SAFE_LANDING | Pürüzsüz İniş | kazasız ≥ 60 sn uçuş |
| ACH_RING_RACE_WIN | Halkaların Efendisi | halka yarışı galibiyeti |
| ACH_DOGFIGHT_WIN | Kuyruk Avcısı | it dalaşı galibiyeti |
| ACH_DISCIPLINE_A | Kusursuz Disiplin | uçuş disiplini notu A (≥ 60 sn) |
| ACH_ALL_RINGS | Tam Tur | parkurdaki tüm kapılar |
| ACH_INTERCEPT | Önleyici | ilk hedef önleme |
| ACH_ACE | As Pilot | tüm hedefler (ace rozeti) |
| ACH_RAMP_LAUNCH | Mancınık | rampadan fırlatma |
| ACH_ROVER | Yer Ekibi | kara aracıyla 100 m |
| ACH_HOURS_10 | Tecrübeli | toplam 10 saat |
| ACH_CLOUD_FLIGHT | Bulut Pilotu | simumav.com hesabıyla bulutta uçuş |

## 4. Mağaza sayfası (Store Presence)
Görseller (Steamworks belgesi, 2026-10 itibarıyla):

| Varlık | Boyut |
|---|---|
| Header Capsule | **920 × 430** |
| Small Capsule | **462 × 174** |
| Main Capsule | **1232 × 706** |
| Vertical Capsule | **748 × 896** |
| Ekran görüntüleri | en az **5** adet, **1920 × 1080** (16:9), yalnız oyun içi görüntü; ≥ 4'ü "tüm yaşlara uygun" işaretli |
| Page Background (isteğe bağlı) | 1438 × 810 |
| Library Capsule | 600 × 900 |
| Library Header | 920 × 430 |
| Library Hero | 3840 × 1240 (yazısız; güvenli alan ortada 860 × 380) |
| Library Logo | 1280 genişlik ve/veya 720 yükseklik, saydam PNG |
| Topluluk/istemci simgeleri | 184 × 184 JPG (topluluk), 32 × 32 ICO (istemci) — Steamworks *Community Assets* |

- **Fragman**: en az 1 oyun içi fragman (H.264/MP4, 1920×1080, 30/60 fps, ≥ 5000 kb/s önerilir; ilk 5 saniyede oyun görünsün).
- **Açıklamalar**: kısa açıklama (≤ 300 karakter), uzun açıklama, özellik listesi; 7 dil için yerelleştirme (tr, en, de, fr, es, ru, ar).
- **Etiketler, tür**: Simülasyon, Uçuş, Dron, Gerçekçi, Çok oyunculu (çevrim içi — hesapla), Tek oyunculu.
- **Sistem gereksinimleri**: asgari GTX 1660 / 16 GB RAM / Win10 64; önerilen RTX 3060+ (Lumen).
- **Üçüncü taraf lisanslar**: ArduPilot (GPLv3) + cygwin (LGPLv3) bildirimi mağaza "yasal" alanına ve oyunun kurulum klasörüne
  (`ThirdParty/ArduPilotSITL/LISANS.md`); kaynak bağlantısı commit `fbd0a316` (bkz. LISANS.md). ArduPilot adı/logosu ticari marka olarak
  kullanılmaz; yalnız "ArduPilot SITL ile" bilgisi.
- **Hesap/veri bildirimi**: Oyun simumav.com hesabı isteğe bağlıdır; mağaza sayfasında "isteğe bağlı üçüncü taraf hesap" (simumav.com)
  olarak belirtilmeli; gizlilik politikası bağlantısı (simumav.com).

## 5. Yaş derecelendirme
1. *Steamworks › Store › Content Survey (Mature Content)* doldurulur (şiddet: yok; it dalaşı/önleme yalnız temas/kilit, silah yok).
2. **IARC** derecelendirme anketi (Steamworks içinden ücretsiz): PEGI / USK / ESRB / ClassInd / ACB derecelerini otomatik verir. Almanya'da satış için
   USK derecesi gereklidir.

## 6. Yükleme (CI)
1. Ayrı bir **yükleme hesabı** (Steamworks kullanıcı; yalnız *Edit App Metadata* + *Publish App Changes to Steam* izinli) oluşturun.
2. Runner makinesinde bir kez etkileşimli giriş (Steam Guard):
   ```powershell
   .\steamcmd.exe +login <kullanici> +quit       # Steam Guard kodunu girin
   [Convert]::ToBase64String([IO.File]::ReadAllBytes("$PWD\config\config.vdf")) | Set-Clipboard
   ```
   Panodaki değeri GitHub sırrı **`STEAM_CONFIG_VDF`** olarak, kullanıcı adını **`STEAM_USERNAME`** olarak kaydedin; `STEAM_DEPOT_ID` değişkenini girin.
3. *Actions › build-windows › Run workflow* → **Steam'e yükle** işaretli, dal `beta` → derleme `beta` dalına çıkar. Varsayılan dala alma
   Steamworks › *SteamPipe › Builds* üzerinden elle (iş akışı varsayılan dalı canlıya almaz).
4. Sırlar yoksa adım kendiliğinden atlanır (günlükte "Steam yükleme ATLANDI").

## 7. Fiyat ve yayın
1. *Pricing*: taban fiyat USD olarak girilir; Steam bölgesel önerileri (TRY dahil) kabul ya da düzenleme. Öneri: erken erişim 9,99–14,99 USD.
2. **Erken Erişim** önerilir (çok oyunculu ve bazı özellikler "iskelet/planlandı" — docs/ozellik-esligi.md): Erken Erişim sorularını doldurun.
3. *Release* sekmesi: mağaza sayfası incelemesi (≈ 3–5 iş günü) → "Yakında" → derleme incelemesi → yayın tarihi.

## 8. Kullanıcı yapılacaklar özeti
- [ ] Steamworks hesabı, sözleşme, banka, vergi, kimlik
- [ ] 100 USD Steam Direct ücreti → App ID
- [ ] Depot, başlatma seçeneği, yeniden dağıtımlar
- [ ] 12 başarım + simgeleri
- [ ] Görseller (kapsüller, kütüphane, simgeler), 5+ ekran görüntüsü, fragman
- [ ] Açıklamalar (7 dil), etiketler, sistem gereksinimleri, lisans bildirimi, gizlilik
- [ ] İçerik anketi + IARC
- [ ] Yükleme hesabı + `STEAM_USERNAME` / `STEAM_CONFIG_VDF` / `STEAM_DEPOT_ID` / `STEAM_APP_ID`
- [ ] Fiyat, Erken Erişim kararı, yayın tarihi
