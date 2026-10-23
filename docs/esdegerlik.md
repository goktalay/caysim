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
| Sahne geçişleri (FLIP, ışık süpürmesi) | `ui/transition.js` | Kart giriş animasyonu + açılma sesi; tam FLIP yok | ◐ | P2 |
| Bölüme özel hareketli arka planlar | `ui/backdrop.js` | `SSmvBackdrop` (degrade, ışık lekeleri, perspektif ızgara, parçacık, süzülen dron siluetleri, tarama çizgisi) — tüm bölümlerde ortak | ◐ (bölüme özel sahne yok) | P2 |
| Menü canlı uçuş sahnesi (3B, sinematik kamera) | `menu/flight.js`, `flightcore.js` | `ASmvMenuScene`: prosedürel arazi (TR TEST Kalecik), sekizli tur atan sabit kanat, kamera döngüsü takip → yan → yörünge → yerden; Ayarlar'dan kapatılır | ✓ | — |
| Klavye / gamepad tam gezinme, odak halkası | `menu/index.js` | Slate odak gezinmesi; odak halkası yalnız klavye/gamepad'de; A/Enter seç, B/Esc geri, LB/RB-Q/E sekme, Y/F1 yardım | ✓ | — |
| Arayüz sesleri (hover, tık, geri, hata, süpürme) | `ui/sfx.js` | `FSmvUiSound` → `USmvToneSynth` (ayrı kazanç; ses dosyası yok) | ✓ | — |
| Bildirimler (toast; boş metin bastırılır) | `ui/components.js` | Sağ üst yığın (en çok 4), önem rengi + simge, kayarak girer, 5 sn | ✓ | — |
| Onay / soru diyalogları | `modal`, `confirmDlg` | Tehlikeli işlemler kırmızı düğme; ayrı diyalog bileşeni yok (Sıfırla çift basma, DISARM çift basma) | ◐ | P2 |
| Tema koyu / açık / otomatik | `sm:theme` | Yalnız koyu | ✗ | P2 |
| 7 dil, Arapça sağdan sola | `app/i18n` | `Content/Simumav/Lang/*.json` (668 anahtar, eksiksiz); RTL yerleşimi gözden geçirilmedi | ◐ | P1 |
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
| Destek: sorun bildir / taleplerim | `support/index.js` | — | ✗ | P2 |

## 3. Ana menü

| Özellik | Web | UE | Durum | Plan |
|---|---|---|---|---|
| Logo SIMU**MAV** | `menu/index.js` | ✓ (parıltılı) | ✓ | — |
| 6 kart: Hangar, Mürettebat, Parkur, Görevler, Uç, Operasyon (renkleri) | `menu/index.js` | "UÇ" büyük kahraman kartı + 6 kart (Hangar #38bdf8, Mürettebat #f472b6, Parkur #34d399, Görevler #a78bfa, Karşılaşma, Kayıtlar #fb923c) | ✓ | — |
| Kart 3B eğilme (±9°) + ışık noktası | `menu/index.js` | Büyüme + parıltı + süpürme ışığı; 3B eğilme yok | ◐ | P2 |
| TOP 10 düğmesi, canlı uçuş rozeti, ipucu satırı | `menu/index.js` | ✓ (+ pilot kartı, SITL/Steam/bulut/harita durumu) | ✓ | — |

## 4. Hangar

| Özellik | Web | UE | Durum | Plan |
|---|---|---|---|---|
| Seçili dronun sürekli dönen 3B modeli | `hangar/index.js` | `ASmvMenuScene` önizleme yakalaması (render hedefi; yalnız araç, kendi ışıkları) + döner platform | ✓ | — |
| Dron şeridi (mini 3B kartlar) | `hangar/index.js` | Liste kartları (tür simgesi, renk vurgusu, ★ varsayılan) | ◐ | P2 |
| Tür seçimi + 4 stat çubuğu (hız/menzil/dayanıklılık/çeviklik) | `hangar/index.js` | Aynı değerler, animasyonlu çubuklar | ✓ | — |
| Model kartları (GLB / PROC), kontrol düzeni, sürüş | `bridge.py GLB_MODELS` | Seçici + çipler (katalog sunucuyla aynı anahtarlar) | ✓ | — |
| 10 renk örneği + ad | `hangar/index.js` | ✓ (+ #rrggbb) | ✓ | — |
| "Hangara indi" animasyonu | `hangar/index.js` | Kaydedildi bildirimi | ◐ | P2 |
| Güç ve yakıt düzenleyici (aralıklar) | `fly/power.js` | Kaydırıcılar (−1 = varsayılan) | ✓ | — |
| "★ Önerilen" (son uçuşta ölçülen) | `fly/power.js` | — | ✗ | P2 |

## 5. Mürettebat / pilot oluşturucu

| Özellik | Web | UE | Durum | Plan |
|---|---|---|---|---|
| Pilot kartları, ★ varsayılan, sil | `crew/index.js` | ✓ | ✓ | — |
| Canlı 3B tam boy önizleme, sekmeler (Beden/Yüz/Saç/Kıyafet/Aksesuar) | `avatar*.js` | Çizilmiş stilize portre (kask, vizör + ayna, tulum, ten, arma, aksesuar; nefes animasyonu); avatar v1 alanları | ◐ (beden/yüz/saç alanları yok; 3B karakter yok) | P1 |
| Rastgele, hazır pilotlar (p01–p10) | `crew/index.js` | Rastgele görünüm + karakter seçici | ✓ | — |
| "Hoş geldin, {ad}!" | `crew/index.js` | ✓ | ✓ | — |
| Sahnede 3B pilot (RC tutan) | `flightsim_pilot*.js` | — | ✗ | P2 |

## 6. Parkur / harita

| Özellik | Web | UE | Durum | Plan |
|---|---|---|---|---|
| Parkurlarım / hazır FPV parkurları sekmeleri | `tracks/index.js`, `fly/fpv.js` | Parkurlar ekranı + Uç sihirbazında "Hazır haritalar" (Fab kayıt tablosu `SmvMapRegistry`: ad, açıklama, önizleme PNG, pakette yoksa pasif) + "Parkurlarım" | ◐ (8 FPV parkuru yok) | P1 |
| Konum (harita tıkla / ara) | `gmap.js`, `satmap.js` | Enlem/boylam kutuları | ◐ | P2 |
| Dünya kipi gerçek / Google 3B / prosedürel + ayarlar | `tracks/index.js` | ✓ (prosedürel ayarları kaydırıcı) | ✓ | — |
| Rüzgâr kadranı, saat, hava | `tracks/index.js` | Kaydırıcılar + simgeli çipler; sihirbazda pusula | ✓ | — |
| Halka editörü (haritada) | `tracks/index.js` | 8 halkalık tur üretici | ◐ | P2 |

## 7. Görevler

| Özellik | Web | UE | Durum | Plan |
|---|---|---|---|---|
| Koridor / serbest / ilaçlama / FPV bayrak / tarama / FPV halka / yer kontrolünü koru | `missions/editor.js` | Görev listesi, tür çipleri (simgeli), plan JSON | ◐ (harita üzerinde tasarım yok) | P1 |

## 8. Uç — seçim ve sihirbazlar

| Özellik | Web | UE | Durum | Plan |
|---|---|---|---|---|
| "Nasıl uçmak istersin?" (tek / FPV / çok oyunculu) | `fly/index.js` | Ana menü UÇ + Karşılaşma ekranı | ◐ | P1 |
| Adımlı tek oyunculu sihirbaz | `fly/wizard.js` | 7 adım: Araç (3B önizleme + statlar + pilot) → Harita (kartlar, önizleme görseli) → Ortam (saat, hava, rüzgâr pusulası; UDS'ye gider) → Kumanda (Mode 1–4 şemalı kartlar, cihaz, expo) → Görev ve kurallar (görev, yarış/it dalaşı, hedefler) → Gelişmiş (kalkış, güç, kamera, hız, arka uç, .BIN) → Özet + BAŞLAT; LB/RB / Q-E adım; "Hemen başlat" | ✓ | — |
| Kalkış yöntemleri + rüzgâr önerisi | `wizard.js` | Yöntemler ve aralıklar ✓; rüzgâr önerisi yok | ◐ | P2 |
| Yer kontrol (radar/EO) adımı | `wizard.js` | — | ✗ | P2 |
| Kamera türü + termal paleti, gösterge tasarımı, grafik ön ayarı | `wizard.js` | Kamera türü ✓ (palet yok); gösterge teması Ayarlar'da; grafik Ayarlar'da | ◐ | P2 |
| Video kaydı (WebM) | `wizard.js` | — | ✗ | P2 |

## 9. Karşılaşma / lobi / yapay zekâ

| Özellik | Web | UE | Durum | Plan |
|---|---|---|---|---|
| Halka yarışı / it dalaşı kurucu, davet + şifre | `fly/match.js` | Oluştur / YZ / Katıl sütunları | ✓ | — |
| Bekleme salonu (oyuncu satırları, hazır, bot ekle, ayrıl) | `fly/match.js` | Renkli oyuncu satırları, hazır rozeti | ✓ | — |
| Bot adları AI-Şahin… | `flightsim_match_bot.py` | Yerel yarışta aynı adlar | ✓ | — |
| Sonuç penceresi, podyum, puan sayacı | `fly/match.js` | Uçuş özeti (rozetler) | ◐ | P1 |
| Halka/it dalaşı ses efektleri | `flightsim_sfx.js` | Halka/kilit/başla tonları | ◐ | P2 |

## 10. FPV parkurları + hayalet

| Özellik | Web | UE | Durum | Plan |
|---|---|---|---|---|
| 8 hazır FPV parkuru, kapı türleri, sektörler, hayalet | `fly/fpv*.js`, `flightsim_fpvcourse*.js` | — (parkur halkaları ve yerel yarış var) | ✗ | P1 |

## 11. Uçuş ekranı

| Özellik | Web | UE | Durum | Plan |
|---|---|---|---|---|
| Bilgi kartı, dron paneli (PFD, telemetri, haberleşme) | `flightsim_panel.js` | `SSmvFlightUi` (cam tema, 7 dil) | ✓ | — |
| Güç kadranları (yakıt/hız/motor/batarya) | `flightsim_power.js` | ✓ | ✓ | — |
| Gösterge temaları (klasik / uçak kokpiti / dijital cam / minimal / retro) | `gtheme.js` | Askerî / FPV OSD / sade + **klasik kokpit altılısı** (sabit kanat/VTOL: hız, yapay ufuk, irtifa, dönüş, yön, dikey hız; G) | ◐ | P2 |
| Alt bant (ARM, Kalkış, İniş, RTL, mod, Sıfırla, kamera, kumanda, panel, menü) | `flightsim_fltbar.js` | ✓ + Kokpit + "?" | ✓ | — |
| Uçuş modu seçici | `flightsim_fltbar.js` | **Radyal tekerlek** (F / gamepad View; fare açısı ya da sağ çubuk; A/F seç, B kapat) | ✓ | — |
| Sanal kumanda + Mode 1–4 + klavye WASD/oklar | `rc.js`, `flightsim_stickmode.js` | ✓ (aynı eşleme; çubuk etiketleri ve ipucu moda göre) | ✓ | — |
| Hassasiyet ⚙ (eksen başına ölü bölge/expo/oran, eğri) | `flightsim_sens*.js` | Tek expo kaydırıcısı | ◐ | P1 |
| Gamepad profili / eksen algıla | `flightsim_gamepad.js` | XInput sabit eşleme + tuş atama (Ayarlar → Tuşlar); RC verici eksen eşlemesi | ◐ | P2 |
| Kamera kipleri 1–7, PiP, FPV FOV | `flightsim_camera.js` | ✓ (PiP sürükleme/boyut yok) | ◐ | P2 |
| Rüzgâr göstergesi (pusula, bileşenler) | `flightsim_wind.js` | — (sihirbazda pusula) | ✗ | P1 |
| Hava efektleri (yağmur/kar/şimşek) | `flightsim3d.js` | Ultra Dynamic Weather (pakette varsa; içerik ajanı) | ◐ | — |
| GPWS uyarıları + MASTER lambaları + ton | `flightsim_gpws*.js` | ✓ (konuşma sentezi yok) | ◐ | P2 |
| Grafik ▾ (ön ayarlar, FPS, kare sınırı, özel kaydırıcılar) | `flightsim_gfx.js` | Ayarlar → Görüntü: 5 ön ayar kartı + otomatik algılama + çözünürlük, pencere kipi, VSync, kare sınırı, ölçek, 8 ayrıntı kaydırıcısı | ✓ | — |
| Servo tetik "Dinle" | `flightsim_servotrig.js` | Duraklatma → Gelişmiş | ✓ | — |
| Hedef sembolojisi / kilit (yalnız HUD + puan) | `flightsim_targets.js` | ✓ | ✓ | — |
| Yer kontrol radar/EO pencereleri | `flightsim_gcs_ui.js` | — | ✗ | P2 |
| Çarpışma efekti (patlama, sarsıntı) | `flightsim3d.js` | Çarpışma yazısı; efekt yok | ◐ | P2 |
| Giriş sekansı 3-2-1-BAŞLA | `flightsim_intro.js` | Ekran üstünde sayaç + halka + parkur adı + bip (yarış dışı); kamera spirali yok | ◐ | P2 |
| Esc → çıkış onayı / duraklatma | `session.js` | Duraklatma menüsü (SITL sürer): devam, RTL, sıfırla, yardım, hızlı ayarlar (Mode 1–4, ses, kokpit), gelişmiş (servo, MP), uçuşu bitir | ✓ | — |

## 12. Uçuş sonu raporu

| Özellik | Web | UE | Durum | Plan |
|---|---|---|---|---|
| Uçuş özeti: sonuç, süre, mesafe, azami hız, disiplin notu, halkalar, en iyi tur, yakıt/enerji, hedef puanı, rozetler, servo | `showFlightReport`, `flightsim_report.js` | Sonuç başlığı + karo ızgarası + rozet çipleri; Tekrar uç / Ayarları değiştir / Kayıtlar / Menü | ✓ | — |
| 2B mini harita (rota), PDF | `flightsim_report.js` | — | ✗ | P2 |

## 13. TOP 10

| Özellik | Web | UE | Durum | Plan |
|---|---|---|---|---|
| 30 gün / tüm zamanlar, sıra madalyası, puan, galibiyet | `menu/top10.js` | ✓ (madalya daireleri, puan çubuğu) | ✓ | — |
| 3B avatar, puan bileşen çipleri, maskeli e-posta | `menu/top10.js` | — | ✗ | P2 |

## 14. Başarımlar

| Özellik | Web | UE | Durum | Plan |
|---|---|---|---|---|
| Hedef rozetleri, savunma rütbeleri, FPV kişisel rekor | `flightsim_targets.puan` | Rozetler özet ekranında; Steam başarımları (12) | ◐ | P2 |

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
| Kayıttan oynatma (3B, hız 1–8×) | `ops/replay.js` | — (CAYSIM `ReplayPlayer` HEPGO biçiminde; yerel .BIN oynatma yok) | ✗ | P2 |

## Eklentiler (Fab) — kullanım durumu

| Eklenti | Durum | Not |
|---|---|---|
| Main Menu & Settings System (CommonUI, tuş atama, gamepad) | Yeteneklerin karşılığı Slate ile yazıldı | Eklenti yalnız runner diskinde, içerik UMG/Blueprint varlıkları; kod tabanı tamamen C++/Slate (UMG .uasset yok). `ui-envanter` iş akışı API'yi listeler; uygun çıkarsa ayarlar ekranı ona taşınabilir. |
| Effect Forge (buff/debuff) | Kullanılmadı | Oyun türüyle ilgisi zayıf; envanterden sonra değerlendirilecek. |
| Procedural Radial Wheel Menu | Kullanılmadı | Slate eşdeğeri `SSmvRadial` (fare + gamepad sağ çubuk). |
| Airplane Controller & Instruments | Kullanılmadı | Slate eşdeğeri: kokpit altılısı (`SSmvFlightUi::Cockpit`). 3B kokpit modelleri içerik ajanının staging'i bitince bağlanabilir. |
