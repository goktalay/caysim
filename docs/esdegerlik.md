# SIMUMAV (web) ↔ Simumav UE5 istemcisi — özellik eşdeğerlik tablosu

Hedef: web SIMUMAV'da (simumav.com, `fabrika-yonetim/static/simumav/app`) ve HEPGO uçuş simülatöründe (`static/js/modules/flightsim*.js`,
`docs/ucus-simulatoru.md`) oyuncunun gördüğü **her şeyin** UE5 istemcisinde de bulunması. Sunucu sözleşmesi: `docs/caysim-api.md`.
Ayrıntılı eski eşlik tablosu: `docs/ozellik-esligi.md` (fizik, MAVLink, sunucu uçları). Bu belge oyuncu tarafını ekran ekran izler.

Durum: **✓** var (kod yazıldı) · **◐** kısmi · **✗** yok · **—** kapsam dışı / web'de de yok. "Plan" sütunu öncelik sırasıdır
(P0 = ilk 10 dakika, P1 = modlar, P2 = ikincil).

## 1. Kabuk, gezinme, ortak arayüz

| Özellik | Web kaynağı | UE karşılığı | Durum | Plan |
|---|---|---|---|---|
| Görsel dil: koyu cam kartlar, neon mavi vurgu, yumuşak animasyon | `theme.css`, `fx.css` | `SmvUiKit` (renk/yazı tipi/fırça, `SSmvButton` parıltı + büyüme + basma, giriş solması) | ✓ | — |
| Sahne geçişleri (FLIP, ışık süpürmesi) | `ui/transition.js` | Ekran geçişinde içerik solup kayarak gelir (menüye dönüşte ters yön) + çapraz ışık süpürmesi + açılma sesi; hareket azaltmada kapalı | ✓ | P2 |
| Bölüme özel hareketli arka planlar | `ui/backdrop.js` | `SSmvBackdrop` ortak katman + bölüme özel sahne: hangar içi (projektör, pist ışıkları), brifing radarı (Görevler/Sihirbaz/Parkurlar/Yükleme), tel kafes küre (Lobi/TOP10/Hesap), ilaçlama tarlası (Seyir defteri/Özet), gökyüzü paralaksı (Uç/FPV/Ayarlar/Destek) | ✓ | P2 |
| Menü canlı uçuş sahnesi (3B, sinematik kamera) | `menu/flight.js`, `flightcore.js` | `ASmvMenuScene`: prosedürel arazi (TR TEST Kalecik), sekizli tur atan sabit kanat, kamera döngüsü takip → yan → yörünge → yerden; Ayarlar'dan kapatılır | ✓ | — |
| Klavye / gamepad tam gezinme, odak halkası | `menu/index.js` | Slate odak gezinmesi; odak halkası yalnız klavye/gamepad'de; A/Enter seç, B/Esc geri, LB/RB-Q/E sekme, Y/F1 yardım | ✓ | — |
| Arayüz sesleri (hover, tık, geri, hata, süpürme) | `ui/sfx.js` | `FSmvUiSound` → `USmvToneSynth` (ayrı kazanç; ses dosyası yok) | ✓ | — |
| Bildirimler (toast; boş metin bastırılır) | `ui/components.js` | Sağ üst yığın (en çok 4), önem rengi + simge, kayarak girer, 5 sn | ✓ | — |
| Onay / soru diyalogları | `modal`, `confirmDlg` | Onay diyaloğu bileşeni (cam kart, karartma, ölçekle açılış; odak Vazgeç'te, Esc/B kapatır): silmeler (dron/pilot/parkur/görev), oyundan çık, uçuşu bitir, oturumu kapat | ✓ | P2 |
| Tema koyu / açık / otomatik | `sm:theme` | Ayarlar → Genel → Arayüz teması: koyu / açık / otomatik (07–19 açık); tüm renkler ve cam kartlar temaya göre; sunucu ayarı `theme` | ✓ | P2 |
| 7 dil, Arapça sağdan sola | `app/i18n` | `Content/Simumav/Lang/*.json` (7 dil, eksiksiz); Arapçada tüm kabuk sağdan sola (FlowDirection RTL) | ✓ | P1 |
| Hareket azaltma | `prefers-reduced-motion` | Ayarlar → Genel → Hareketi azalt | ✓ | — |
| Çerez onayı / GA4 | `analytics.js` | Masaüstü oyunda yok | — | — |
| Tanıtım / onboarding | (web'de yok; ipucu satırı) | İlk açılışta 6 sayfalık tanıtım + ilk uçuşta ipucu kartı + ana menüde dönen ipuçları | ✓ | — |
| Kısayol yardımı | — | F1 / gamepad Y: kullanıcının atadığı tuşlarla 3 sütun + kumanda modu şeması | ✓ | — |

## 2. Hoş geldin / giriş / hesap

| Özellik | Web | UE | Durum | Plan |
|---|---|---|---|---|
| Misafir olarak devam / giriş | `site/js/auth.js` | Hoş geldin ekranı: büyük kartlar, dil seçici | ✓ | — |
| E-posta + şifre girişi (PBKDF2), kayıt bağlantısı | Mastertool | Giriş ekranı (cam kart) | ✓ | — |
| Misafir verisini hesaba aktarma | — | Hesap ekranı | ✓ | — |
| Destek: sorun bildir / taleplerim | `support/index.js` | Destek ekranı (ana menü → Sorun bildir): kategori, başlık, açıklama, cihaz/son uçuş bağlamı, sahne ekran görüntüsü (PNG ≤ 3 MB); Taleplerim listesi, talep yazışması ve yanıt — /api/support | ✓ | P2 |

## 3. Ana menü

| Özellik | Web | UE | Durum | Plan |
|---|---|---|---|---|
| Logo SIMU**MAV** | `menu/index.js` | ✓ (parıltılı) | ✓ | — |
| 6 kart: Hangar, Mürettebat, Parkur, Görevler, Uç, Operasyon (renkleri) | `menu/index.js` | "UÇ" büyük kahraman kartı + 6 kart (Hangar #38bdf8, Mürettebat #f472b6, Parkur #34d399, Görevler #a78bfa, Karşılaşma, Kayıtlar #fb923c) | ✓ | — |
| Kart 3B eğilme (±9°) + ışık noktası | `menu/index.js` | Fare konumuna göre ±9° eğilme (kayma dönüşümü) + ışık noktası; büyüme + parıltı | ✓ | P2 |
| TOP 10 düğmesi, canlı uçuş rozeti, ipucu satırı | `menu/index.js` | ✓ (+ pilot kartı, SITL/Steam/bulut/harita durumu) | ✓ | — |

## 4. Hangar

| Özellik | Web | UE | Durum | Plan |
|---|---|---|---|---|
| Seçili dronun sürekli dönen 3B modeli | `hangar/index.js` | `ASmvMenuScene` önizleme yakalaması (render hedefi; yalnız araç, kendi ışıkları) + döner platform | ✓ | — |
| Dron şeridi (mini 3B kartlar) | `hangar/index.js` | Hangar üstünde yatay dron şeridi: mini 3B kartlar (menü sahnesinde çekilen anlık görüntü; model yüklenince yenilenir), ★ varsayılan, seçili vurgulu | ✓ | P2 |
| Tür seçimi + 4 stat çubuğu (hız/menzil/dayanıklılık/çeviklik) | `hangar/index.js` | Aynı değerler, animasyonlu çubuklar | ✓ | — |
| Model kartları (GLB / PROC), kontrol düzeni, sürüş | `bridge.py GLB_MODELS` | Seçici + çipler (katalog sunucuyla aynı anahtarlar) | ✓ | — |
| 10 renk örneği + ad | `hangar/index.js` | ✓ (+ #rrggbb) | ✓ | — |
| "Hangara indi" animasyonu | `hangar/index.js` | Yeni dron kaydedilince önizlemede yukarıdan dönerek süzülüp iner + "Hangara indi" bildirimi | ✓ | P2 |
| Güç ve yakıt düzenleyici (aralıklar) | `fly/power.js` | Kaydırıcılar (−1 = varsayılan) | ✓ | — |
| "★ Önerilen" (son uçuşta ölçülen) | `fly/power.js` | Son uçuşta ölçülen tüketim (km/L, ortalama W) dron başına saklanır; sihirbazda "★ Önerilen" düğmesi tek tıkla uygular | ✓ | P2 |

## 5. Mürettebat / pilot oluşturucu

| Özellik | Web | UE | Durum | Plan |
|---|---|---|---|---|
| Pilot kartları, ★ varsayılan, sil | `crew/index.js` | ✓ | ✓ | — |
| Canlı 3B tam boy önizleme, sekmeler (Beden/Yüz/Saç/Kıyafet/Aksesuar) | `avatar*.js` | Pilot oluşturucu: 3B tam boy figür (ASmvPilotFigure; sürükle-döndür, tam boy/yüz yakınlaştırma), Beden/Yüz/Saç/Kıyafet/Aksesuar sekmeleri, avatar v2 alanları, rastgele | ✓ | P1 |
| Rastgele, hazır pilotlar (p01–p10) | `crew/index.js` | Rastgele görünüm + karakter seçici | ✓ | — |
| "Hoş geldin, {ad}!" | `crew/index.js` | ✓ | ✓ | — |
| Sahnede 3B pilot (RC tutan) | `flightsim_pilot*.js` | Uçuşta ev konumunun yanında RC kumanda tutan 3B pilot: başparmaklar çubuk girdisini izler, baş dronu takip eder, nefes | ✓ | P2 |

## 6. Parkur / harita

| Özellik | Web | UE | Durum | Plan |
|---|---|---|---|---|
| Parkurlarım / hazır FPV parkurları sekmeleri | `tracks/index.js`, `fly/fpv.js` | Parkurlar ekranı + "Hazır haritalar" (SmvMapRegistry) + "Parkurlarım" + Hazır FPV parkurları galerisi (8 parkur) | ✓ | P1 |
| Konum (harita tıkla / ara) | `gmap.js`, `satmap.js` | Parkurlar: uydu haritası (Esri World Imagery karoları; sürükle/tekerlek), tıkla → konum, yer araması (OpenStreetMap Nominatim) | ✓ | P2 |
| Dünya kipi gerçek / Google 3B / prosedürel + ayarlar | `tracks/index.js` | ✓ (prosedürel ayarları kaydırıcı) | ✓ | — |
| Rüzgâr kadranı, saat, hava | `tracks/index.js` | Kaydırıcılar + simgeli çipler; sihirbazda pusula | ✓ | — |
| Halka editörü (haritada) | `tracks/index.js` | Haritada halka düzenleyici: tıkla → ekle, sürükle → taşı, sağ tık → sil (numaralı, yarıçap ölçekli, tur çizgisi) + 8 halkalık tur üretici | ✓ | P2 |

## 7. Görevler

| Özellik | Web | UE | Durum | Plan |
|---|---|---|---|---|
| Koridor / serbest / ilaçlama / FPV bayrak / tarama / FPV halka / yer kontrolünü koru | `missions/editor.js` | Görev düzenleyici harita üzerinde: koridor, serbest rota, ilaçlama/tarama hatları, FPV bayrak/halka sırası, yer kontrolünü koru; web plan biçimi | ✓ | P1 |

## 8. Uç — seçim ve sihirbazlar

| Özellik | Web | UE | Durum | Plan |
|---|---|---|---|---|
| "Nasıl uçmak istersin?" (tek / FPV / çok oyunculu) | `fly/index.js` | "Nasıl uçmak istersin?" ekranı: tek uçuş / FPV parkuru / çok oyunculu / yapay zekâ rakip | ✓ | P1 |
| Adımlı tek oyunculu sihirbaz | `fly/wizard.js` | 7 adım: Araç (3B önizleme + statlar + pilot) → Harita (kartlar, önizleme görseli) → Ortam (saat, hava, rüzgâr pusulası; UDS'ye gider) → Kumanda (Mode 1–4 şemalı kartlar, cihaz, expo) → Görev ve kurallar (görev, yarış/it dalaşı, hedefler) → Gelişmiş (kalkış, güç, kamera, hız, arka uç, .BIN) → Özet + BAŞLAT; LB/RB / Q-E adım; "Hemen başlat" | ✓ | — |
| Kalkış yöntemleri + rüzgâr önerisi | `wizard.js` | Yöntemler ve aralıklar + rüzgâra karşı kalkış önerisi (baş/yan/kuyruk bileşeni, uygun değilse amber; "Rüzgâra karşı çevir" pist yönünü ayarlar) | ✓ | P2 |
| Yer kontrol (radar/EO) adımı | `wizard.js` | Sihirbaz 6. adım "Yer kontrol": istasyon / radar (1–50 km) / EO (0,5–15 km) seçimi + kuzey-yukarı yerleşim şeması; bulut isteğinde site gövdesi | ✓ | P2 |
| Kamera türü + termal paleti, gösterge tasarımı, grafik ön ayarı | `wizard.js` | Kamera türü + termal paleti (beyaz-sıcak / siyah-sıcak / ironbow; renk tablosu LUT), gösterge tasarımı ve grafik ön ayarı Ayarlar'da | ✓ | P2 |
| Video kaydı (WebM) | `wizard.js` | Video kaydı: sihirbaz anahtarı + uçuşta ● REC düğmesi (süre sayacı); 15 kare/sn, ≤ 1280 px MJPEG AVI → Saved/Simumav/Videos | ✓ | P2 |

## 9. Karşılaşma / lobi / yapay zekâ

| Özellik | Web | UE | Durum | Plan |
|---|---|---|---|---|
| Halka yarışı / it dalaşı kurucu, davet + şifre | `fly/match.js` | Oluştur / YZ / Katıl sütunları | ✓ | — |
| Bekleme salonu (oyuncu satırları, hazır, bot ekle, ayrıl) | `fly/match.js` | Renkli oyuncu satırları, hazır rozeti | ✓ | — |
| Bot adları AI-Şahin… | `flightsim_match_bot.py` | Yerel yarışta aynı adlar | ✓ | — |
| Sonuç penceresi, podyum, puan sayacı | `fly/match.js` | Uçuş özeti + yarış podyumu (1–3) + sayan oyun puanı + FPV sonuç bölümü | ✓ | P1 |
| Halka/it dalaşı ses efektleri | `flightsim_sfx.js` | Prosedürel efektler: halka (artan perde), bitiş, ters yön, kilit/angajman, kazanma/kaybetme, önleme, çarpma, kişisel rekor | ✓ | P2 |

## 10. FPV parkurları + hayalet

| Özellik | Web | UE | Durum | Plan |
|---|---|---|---|---|
| 8 hazır FPV parkuru, kapı türleri, sektörler, hayalet | `fly/fpv*.js`, `flightsim_fpvcourse*.js` | 8 hazır parkur (sunucu tanımı birebir), kapı türleri (çerçeve/halka/bayrak), sektörler, kaçırma/çarpma cezası, kişisel rekor hayaleti | ✓ | P1 |

## 11. Uçuş ekranı

| Özellik | Web | UE | Durum | Plan |
|---|---|---|---|---|
| Bilgi kartı, dron paneli (PFD, telemetri, haberleşme) | `flightsim_panel.js` | `SSmvFlightUi` (cam tema, 7 dil) | ✓ | — |
| Güç kadranları (yakıt/hız/motor/batarya) | `flightsim_power.js` | ✓ | ✓ | — |
| Gösterge temaları (klasik / uçak kokpiti / dijital cam / minimal / retro) | `gtheme.js` | Uçak kokpiti / klasik otomobil / dijital cam (MFD) / minimal / retro askerî / göstergesiz + klasik altılı kokpit (sabit kanat/VTOL); G ile döngü, Ayarlar → Genel | ✓ | P2 |
| Alt bant (ARM, Kalkış, İniş, RTL, mod, Sıfırla, kamera, kumanda, panel, menü) | `flightsim_fltbar.js` | ✓ + Kokpit + "?" | ✓ | — |
| Uçuş modu seçici | `flightsim_fltbar.js` | **Radyal tekerlek** (F / gamepad View; fare açısı ya da sağ çubuk; A/F seç, B kapat) | ✓ | — |
| Sanal kumanda + Mode 1–4 + klavye WASD/oklar | `rc.js`, `flightsim_stickmode.js` | ✓ (aynı eşleme; çubuk etiketleri ve ipucu moda göre) | ✓ | — |
| Hassasiyet ⚙ (ölü bölge/expo/oran, hazır ayarlar, eğri) | `flightsim_sens*.js` | Ayarlar → Kumanda: Hassas / Normal / Çevik / Özel; eksen başına (roll, pitch, yaw, gaz) oran, expo ve ölü bölge, roll+pitch bağla; canlı eğri; gaz eğrisi yalnız kopterde | ✓ | P2 |
| Gamepad profili / eksen algıla | `flightsim_gamepad.js` | XInput sabit eşleme + tuş atama (Ayarlar → Tuşlar) + RC verici eksen eşlemesi ve "Algıla" (kolu oynat → eksen + ters yön otomatik) | ✓ | P2 |
| Kamera kipleri 1–7, PiP, FPV FOV | `flightsim_camera.js` | ✓ — PiP (gimbal) penceresi sürükleyerek taşınır, sağ alt köşeden boyutlandırılır (16:9), konum/boyut kalıcı | ✓ | P2 |
| Rüzgâr göstergesi (pusula, bileşenler) | `flightsim_wind.js` | Sol üstte kart: dönen pusula, akış oku, hız/yön, esinti, baş-kuyruk / yan bileşeni (yan > 6 m/s amber); 3B rüzgâr tulumu yok | ◐ | P2 |
| Hava efektleri (yağmur/kar/şimşek) | `flightsim3d.js` | Ultra Dynamic Weather (pakette varsa; içerik ajanı) | ◐ | — |
| GPWS uyarıları + MASTER lambaları + ton | `flightsim_gpws*.js` | ✓ + sesli uyarı (Windows SAPI konuşma; havacılık İngilizcesi ya da arayüz dili, onaylayınca susar; Ayarlar → Uyarılar) | ✓ | P2 |
| Grafik ▾ (ön ayarlar, FPS, kare sınırı, özel kaydırıcılar) | `flightsim_gfx.js` | Ayarlar → Görüntü: 5 ön ayar kartı + otomatik algılama + çözünürlük, pencere kipi, VSync, kare sınırı, ölçek, 8 ayrıntı kaydırıcısı | ✓ | — |
| Servo tetik "Dinle" | `flightsim_servotrig.js` | Duraklatma → Gelişmiş | ✓ | — |
| Hedef sembolojisi / kilit (yalnız HUD + puan) | `flightsim_targets.js` | ✓ | ✓ | — |
| Yer kontrol radar/EO pencereleri | `flightsim_gcs_ui.js` | Sahada istasyon + kafes kule (EO tareti, 4 sn'de dönen radar anteni); uçuşta sol altta RADAR PPI (tarama izi, menzil halkaları, temaslar, tıkla-seç → EO izler / hedefse kilit) ve EO penceresi (taret kamerası, otomatik yakınlaştırma, gündüz/termal/gece, AZ/EL/MZL OSD); Y / U ile aç-kapa | ✓ | P2 |
| Çarpışma efekti (patlama, sarsıntı) | `flightsim3d.js` | Çarpma: darbe sesi, ekran parlaması, kamera sarsıntısı, savrulan enkaz parçaları | ✓ | P2 |
| Giriş sekansı 3-2-1-BAŞLA | `flightsim_intro.js` | Sayaç + halka + parkur adı + bip; kamera 100 m / 80 m sarmalla takip konumuna iner (yarış dışı) | ✓ | P2 |
| Esc → çıkış onayı / duraklatma | `session.js` | Duraklatma menüsü (SITL sürer): devam, RTL, sıfırla, yardım, hızlı ayarlar (Mode 1–4, ses, kokpit), gelişmiş (servo, MP), uçuşu bitir | ✓ | — |

## 12. Uçuş sonu raporu

| Özellik | Web | UE | Durum | Plan |
|---|---|---|---|---|
| Uçuş özeti: sonuç, süre, mesafe, azami hız, disiplin notu, halkalar, en iyi tur, yakıt/enerji, hedef puanı, rozetler, servo | `showFlightReport`, `flightsim_report.js` | Sonuç başlığı + karo ızgarası + rozet çipleri; Tekrar uç / Ayarları değiştir / Kayıtlar / Menü | ✓ | — |
| 2B mini harita (rota), PDF | `flightsim_report.js` | Uçuş özetinde 2B rota haritası (irtifaya göre renk, başlangıç/bitiş) + yazdırılabilir rapor (rota SVG, irtifa profili, özet tablosu; tarayıcıda Yazdır → PDF) | ✓ | P2 |

## 13. TOP 10

| Özellik | Web | UE | Durum | Plan |
|---|---|---|---|---|
| 30 gün / tüm zamanlar, sıra madalyası, puan, galibiyet | `menu/top10.js` | ✓ (madalya daireleri, puan çubuğu) | ✓ | — |
| 3B avatar, puan bileşen çipleri, maskeli e-posta | `menu/top10.js` | TOP10: menü sahnesinde 3B baş-omuz avatar anlık görüntüsü (yoksa çizilmiş portre), maskeli e-posta, puan bileşen çipleri (karşılaşma/tempo/disiplin/önleme/FPV) | ✓ | P2 |

## 14. Başarımlar

| Özellik | Web | UE | Durum | Plan |
|---|---|---|---|---|
| Hedef rozetleri, savunma rütbeleri, FPV kişisel rekor | `flightsim_targets.puan` | Seyir defteri → Başarımlar: 12 Steam başarımı (açık/kilitli), rozet kataloğu ve kazanılma sayıları (ilk/hızlı/as/yakın/avcı/yarış/it dalaşı/FPV rekor), savunma rütbe merdiveni; özet rozetleri 7 dilde | ✓ | P2 |

## 15. Bayram temaları

| Özellik | Web | UE | Durum | Plan |
|---|---|---|---|---|
| 20 tema, ülke tespiti, 3B süsler | `holiday_fx.js`, `flightsim_holiday3d.js` | TR takvimi (resmî + dini bayramlar 2026–2028, yaklaşık): ana menüde kutlama şeridi + arka plan rengi; Ayarlar → Otomatik/Kapalı. 3B süs yok | ◐ | P2 |

## 16. Müzik ve ses mikseri

| Özellik | Web | UE | Durum | Plan |
|---|---|---|---|---|
| Sentez müzik (Am7 → Fmaj7 → Cmaj7 → G6, 76 BPM; pad, bas, arpej, çan, yankı) | `ui/music.js` | `USmvMusicSynth` (aynı akorlar/tempo); menü 1, sihirbaz/salon 0,7, uçuş 0 (1,2 sn söner) | ✓ | — |
| Kanallar: ana, müzik, efekt/arayüz, uyarı, motor | `ui/audiomix.js` | Ayarlar → Ses (canlı önizleme; tercihler GameUserSettings.ini) | ✓ | — |

## 17. Ayarlar

| Özellik | Web | UE | Durum | Plan |
|---|---|---|---|---|
| Dil, birim, ses, hareketli arka plan, bayram, FPV analog, liderlikte gizlen | `settings/index.js` | Sekmeli: Genel · Görüntü · Ses · Kumanda · Tuşlar · Uyarılar · Bağlantı | ✓ | — |
| Tuş atama (klavye + gamepad) | — | Ayarlar → Tuşlar (25 eylem, çakışmada yer değiştirme, varsayılana dön) | ✓ | — |
| Mission Planner UDP, Cesium belirteci | — | ✓ | ✓ | — |

## 18. Operasyon / kayıttan oynatma

| Özellik | Web | UE | Durum | Plan |
|---|---|---|---|---|
| Uçuş kayıtları listesi | `ops/index.js` | Kayıtlar ekranı (sonuç rozeti, süre, mesafe, not) | ✓ | — |
| Kayıttan oynatma (3B, hız 1–8×) | `ops/replay.js` | Her uçuş 5 Hz kaydedilir (Saved/Simumav/Replays, son 30); Seyir defteri → Kayıtlar: liste + rota haritası; 3B sahnede oynatma ½–8× (şeritte hız, ilerleme, baştan), rapor, silme | ✓ | P2 |

## Eklentiler (Fab) — kullanım durumu

| Eklenti | Durum | Not |
|---|---|---|
| Main Menu & Settings System (CommonUI, tuş atama, gamepad) | Yeteneklerin karşılığı Slate ile yazıldı | Eklenti yalnız runner diskinde, içerik UMG/Blueprint varlıkları; kod tabanı tamamen C++/Slate (UMG .uasset yok). `ui-envanter` iş akışı API'yi listeler; uygun çıkarsa ayarlar ekranı ona taşınabilir. |
| Effect Forge (buff/debuff) | Kullanılmadı | Oyun türüyle ilgisi zayıf; envanterden sonra değerlendirilecek. |
| Procedural Radial Wheel Menu | Kullanılmadı | Slate eşdeğeri `SSmvRadial` (fare + gamepad sağ çubuk). |
| Airplane Controller & Instruments | Kullanılmadı | Slate eşdeğeri: kokpit altılısı (`SSmvFlightUi::Cockpit`). 3B kokpit modelleri içerik ajanının staging'i bitince bağlanabilir. |
