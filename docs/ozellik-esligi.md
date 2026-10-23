# Simumav (Steam / UE5) ↔ SIMUMAV (simumav.com) özellik eşliği

Durum: **yapıldı** = kodu yazıldı (derlenmedi — şirket bilgisayarında ilk derleme bekliyor), **iskelet** = akış/arayüz var, ayrıntı eksik,
**planlandı** = henüz kod yok. Fizik her iki kipte de **gerçek ArduPilot SITL**'dir; istemci görselleştirir ve kumanda eder (Chaos fiziği kullanılmaz).

**Kipler.** Giriş zorunlu değildir. Varsayılan **misafir kip**: oyun doğrudan açılır, profil Steam kullanıcı adıyla yerel tutulur
(`Saved/Simumav/Profiles/<steam_id>/guest/*.json`), fizik oyunla dağıtılan **yerel ArduPilot SITL 4.7.2** (Windows/cygwin ikilileri) ile çalışır.
**simumav.com hesabıyla** giriş yapılınca: bulut eşitleme, çevrim içi karşılaşma, TOP 10 puanı, isteğe bağlı "Bulutta uç" (sunucu SITL'i) açılır.

## Ana tablo

| Özellik | SIMUMAV web kaynağı | Simumav UE karşılığı | Misafir (yerel) | Hesapla | Durum | Gerçekçi grafik notu | Öncelik |
|---|---|---|---|---|---|---|---|
| Giriş / hesap | `site/js/auth.js`, `/api/session`, mastertool `sv` | `USimumavClient::Login` (PBKDF2 auth_hash, smsid çerezi, Windows Kimlik Bilgisi Yöneticisi), Hoş geldin ekranı "Misafir olarak devam et / Giriş yap" | — (misafir varsayılan) | ✓ | yapıldı | — | P0 |
| Misafir verisini hesaba aktarma | — (yeni) | `USmvDataService::ImportGuestToAccount` (hangar → crew → tracks → missions, parkur kimlik eşlemesi) | — | ✓ | yapıldı | — | P1 |
| Hangar (dron CRUD, tip, model, renk, düzen, sürüş, parametreler) | `/api/hangar`, `hangar/index.js` | `SSmvShell::Hangar` + `USmvDataService` (yerel JSON / REST); katalog sunucuyla aynı anahtarlar (sözleşme testi denetler) | ✓ | ✓ (eşitleme) | yapıldı | GLB modeller simumav.com statiklerinden çalışma anında (glTFRuntime varsa); yoksa prosedürel gövde | P0 |
| Güç / yakıt (azami hız, yakıt, batarya, Wh, kalan süre) | `flightsim_power.py`, `tele.fuel/elec` | `SmvCore::FPowerState` (birebir; Python'a karşı sınandı) + HUD çubukları | ✓ | ✓ (sunucu) | yapıldı | — | P0 |
| Azami hız → itki ölçeği | `SIM_PLN_THR_MUL` (HEPGO yamalı SITL) | Yerel resmi ikili bu parametreyi tanımaz → yalnız `AIRSPEED_MAX/CRUISE, THR_MAX` | kısmi | ✓ (sunucu) | yapıldı (kısmi) | — | P2 |
| Sanal servo montajı | `SIM_SRV_REV` (yamalı SITL) | Yerelde yok (resmi ikili); sunucuda tam | ✗ | ✓ | yerelde yapılamaz | — | P3 |
| Mürettebat + 3B avatar/karakter yaratma | `/api/crew`, `avatar.py`, `crew/avatar*.js` | Pilot CRUD + avatar v1 alanları (kask/vizör/ayna/tulum/ten/arma/aksesuar) | ✓ | ✓ | iskelet (3B karakter görünümü planlandı) | MetaHuman değil: sade stilize karakter (Fab, uyumlu lisans) planlandı | P2 |
| Parkurlar (konum, yarıçap, rüzgâr/esinti, saat, hava, dünya kipi, kapılar) | `/api/tracks` | `SSmvShell::Tracks`, kapı oluşturucu (8 halkalık tur) | ✓ | ✓ | yapıldı (harita üzerinde düzenleme planlandı) | — | P0 |
| Gerçek arazi | Google/terrarium + flightsim3d | Cesium for Unreal (World Terrain + Bing uydu) — belirteç Ayarlar'dan ya da paketten | ✓ (belirteçle) | ✓ | yapıldı (CAYSIM WorldSetup) | Nanite yok (3D Tiles), Lumen GI, SkyAtmosphere, VolumetricCloud, ExponentialHeightFog | P0 |
| Google 3B fotogerçekçi | `/api/drone/sim/g3d-key` | Cesium ion üzerinden Google Photorealistic 3D Tiles (ion varlığı) | planlandı | planlandı | planlandı | En gerçekçi kip; Cesium ion lisans/ücret koşulları | P1 |
| Prosedürel dünya | `world{seed, …}` | `ASmvProcTerrain`: tohumlu ısı haritası, dağlar, ağaç/bina örnekleri, pist, su; ev çevresi 300 m düz (SITL düz zemini) | ✓ | ✓ | yapıldı | Malzeme Editor Python'la (M_SmvTerrain); PCG/Landscape + Nanite foliage planlandı | P0 |
| Hava / rüzgâr | `SIM_WIND_*`, esinti `g(t)` | Esinti 1 Hz `SIM_WIND_SPD` (birebir formül), sis yoğunluğu hava durumuna göre | ✓ | ✓ | yapıldı | Yağmur/kar Niagara sistemleri planlandı (içerik varlığı gerektirir) | P1 |
| Görevler (koridor/ilaçlama/tarama/FPV/koru) | `/api/missions`, plan JSON | Görev CRUD (plan JSON); uçuşta Mission Planner ile görev yükleme | ✓ (MP ile) | ✓ (sunucu yürütür) | iskelet (görev düzenleyici + yerel yükleme planlandı) | — | P1 |
| Uç sihirbazı | `fly/wizard.js` | `SSmvShell::Wizard` (araç, pilot, parkur, görev, kalkış, güç, hedefler, yarış, görüntü, hız, fizik) | ✓ | ✓ | yapıldı | — | P0 |
| Tek oyunculu uçuş | `/api/fly/start` + WS | Yerel: `USmvLocalFlightLink` (SITL alt süreci + MAVLink); bulut: `USmvCloudFlightLink` | ✓ | ✓ | yapıldı | — | P0 |
| Çok oyunculu: salon / davet / şifre | `/api/match`, `/ws/match` | `USmvMatchClient` + Karşılaşma ekranı | ✗ (giriş gerekir) | ✓ | iskelet | — | P1 |
| Halka yarışı | `flightsim_match_core.py` | `SmvCore` halka üretimi/geçiş (Python ile aynı tohum → aynı halkalar), torus kapılar, sıralama | ✓ (yerel YZ) | ✓ (sunucu hakem) | yapıldı | Halka malzemesi M_SmvGlow (emissive + bloom) | P0 |
| İt dalaşı | `ItDalasi` | `SmvCore::FDogfight` (birebir), HUD tutma çubuğu | ✓ (1 YZ) | ✓ | yapıldı | — | P1 |
| Yapay zekâ pilot | `flightsim_match_bot.py` | `SmvCore::FBotPilot` (seviye sabitleri aynı); **yerelde kinematik** (ikinci SITL yok) | ✓ (kinematik) | ✓ (sunucu SITL'li bot) | yapıldı (fark belgelendi) | — | P1 |
| Kamera kipleri | `flightsim_camera.js` | Takip, FPV, Gimbal, Yörünge, Yer, Serbest, Tepeden | ✓ | ✓ | yapıldı | Sinema kamerası (CineCamera, DoF) planlandı | P0 |
| PiP / gimbal | `camera_pick.js` | SceneCapture → render hedefi → HUD köşesi; PgUp/PgDn gimbal | ✓ | ✓ | yapıldı | — | P1 |
| EO / termal / gece | `cmd camera` | Son işlem ayarlarıyla yaklaşık (gri tonlama, kontrast, yeşil kazanç, gren) | ✓ | ✓ | iskelet (gerçek termal malzeme planlandı) | Termal: Custom Depth + malzeme; gece: NVG malzemesi | P2 |
| HUD / OSD / kadranlar / gösterge temaları | `flightsim_hud.js`, `flightsim_fpv_osd.js`, `gtheme.js` | CAYSIM askerî HUD (Slate) + Simumav katmanı; temalar: askerî / FPV OSD / sade | ✓ | ✓ | yapıldı (klasik kadranlı tema planlandı) | — | P0 |
| Uçuş uyarıları §42 (GPWS, stall, bank angle, IMU…) | `flightsim_gpws.js` / `.py` | `SmvCore::FGpws` (JS değerlendiriciyle birebir; Node'a karşı sınandı), MASTER WARNING/CAUTION şeridi, B ile onay, bulutta `cmd gpws` | ✓ | ✓ | yapıldı | Sesli ifade ("Pull up") yerine ton — konuşma sentezi planlandı | P0 |
| Uçuş disiplini notu | `ozetle`, `not_harfi` | `SmvCore::SummarizeAlerts` (Python ile birebir) | ✓ | ✓ | yapıldı | — | P0 |
| Sesler / müzik / efektler | `sfx.js`, `music.js`, `audiomix.js` | Prosedürel motor sesi (USynthComponent: elektrikli çok rotor/pervane/içten yanmalı/jet/rover, Doppler), uyarı tonları | ✓ | ✓ | iskelet (müzik ve efekt kütüphanesi planlandı) | MetaSounds geçişi planlandı | P1 |
| Kayıt / kayıttan oynatma | `/api/fly/rec/*`, `/api/ops/{id}/replay` | Yerel .BIN logları `Saved/Simumav/Logs`; uçuş kayıtları ekranı; CAYSIM ReplayPlayer (HEPGO biçimi) | kısmi | kısmi | iskelet (yerel .BIN oynatma + video kaydı planlandı) | — | P2 |
| Servo tetik | `flightsim_servotrig.py` | `SmvCore::FServoTrigger` (kenar tetik, sim saatiyle gecikme, bekçi) + duraklatma menüsünde ayar | ✓ | ✓ | yapıldı | Çarpma efekti (Niagara) planlandı | P1 |
| Hedefler / kilit / angajman HUD | `flightsim_targets.py`, §40 | `SmvCore::FTargetTracker` (önleme, angajman, puan, rozetler; Python ile birebir), hedef aktörleri, nişangâh, Boşluk ile kilit | ✓ | ✓ | yapıldı | Hedef GLB'leri simumav.com'dan | P1 |
| Koru görevi / yer kontrol / radar-EO | `flightsim_gcs.py`, `defend` | — | ✗ | ✓ (sunucu) | planlandı | — | P3 |
| Fırlatıcılar / rampa | `launch_parametreleri`, `flightsim_launcher.py` | Sabit kanat: pist / pnömatik rampa (catapult servo 7) / araç; INTERCEPTOR: dikey + pod/tüp (dikey gibi) | ✓ | ✓ | yapıldı (el fırlatıcısı yön hizalama + elden atış planlandı) | CAYSIM LaunchRig görseli (rampa/araç) bağlanacak | P1 |
| Rover (kara aracı) | `rover.parm`, `drive` | ArduRover, Ackermann / skid, rover RC eşlemesi | ✓ | ✓ | yapıldı | — | P2 |
| Kumanda: klavye / gamepad / RC verici | `rc.js`, `flightsim_gamepad.js` | Enhanced Input (kodla), RawInput HID RC verici eksen eşlemesi, Mod 1/2, expo; sanal çubuklar (dokunmatik) yok | ✓ | ✓ | yapıldı | — | P0 |
| Mission Planner bağlantısı | MP TCP portu | Yerel: ham MAVLink UDP köprüsü (127.0.0.1:14550 çıkışı, GCS kilidi, PARAM_SET yan dinleme); bulut: sunucu TCP portu | ✓ | ✓ | yapıldı | — | P0 |
| Sıfırla korunumu, ağır çekim (0,25×–8×) | §17, `SIM_SPEEDUP` | Backspace×2 / menü; +/−; oturum parametreleri yeniden yazılır | ✓ | ✓ | yapıldı | — | P1 |
| Arazi sunumu (TERRAIN_DATA) | `_terrain_istek` | Yerelde yok (SITL düz zemin); Mission Planner bağlıysa MP yanıtlar | ✗ | ✓ | yerelde planlandı | — | P3 |
| TOP 10 | `/api/leaderboard` | TOP 10 ekranı (oturumsuz görüntüleme); puan yalnız sunucuda doğrulanan uçuşlardan | görüntüle | ✓ | yapıldı | — | P1 |
| Ayarlar | `/api/settings` | Dil, birimler, ses, uyarılar, gösterge teması, grafik kalitesi, kumanda, MP UDP, Cesium belirteci (yerel alanlar + sunucu eşitleme) | ✓ | ✓ | yapıldı | Ölçeklenebilirlik 0–4 | P0 |
| 7 dil (tr, en, de, fr, es, ru, ar) | `app/i18n` | `Content/Simumav/Lang/*.json` (372 anahtar, eksiksiz; SIMUMAV web çevirileri + elle) | ✓ | ✓ | yapıldı (Arapça sağdan sola yerleşim gözden geçirilecek) | Arapça yazı tipi yedeği doğrulanacak | P1 |
| Bayram temaları | (SIMUMAV'da karşılığı bulunamadı) | — | — | — | planlandı | — | P3 |
| Steam başarımları | — | 12 başarım (ilk uçuş, güvenli iniş, halka/it dalaşı galibiyeti, disiplin A, tüm halkalar, önleme, ace, rampa, rover, 10 saat, bulut uçuşu); misafirde de çalışır | ✓ | ✓ | yapıldı | — | P0 |
| Steam ↔ simumav.com hesap bağlama | — | Oyun içi e-posta+şifre girişi; Steam kimliği yerel profil anahtarı. Steam oturum biletiyle sunucu tarafı bağlama sunucuda uç gerektirir | — | kısmi | planlandı (sunucu değişikliği gerekir) | — | P2 |
| Kapsüller, mobil, sanal çubuk | §19 | — (masaüstü Steam) | — | — | kapsam dışı | — | — |

## Politika / yapılamayan adımlar

- **HEPGO yamalı SITL ikilileri** (`SIM_SRV_REV`, `SIM_PLN_THR_MUL`) Windows için derlenmiş hâlde mevcut değil; yerelde resmi 4.7.2 kullanılır (ilgili
  satırlar "kısmi"). Yamalar `fabrika-yonetim/tools/sitl` altında; cygwin derlemesi ayrı bir iş.
- **Steam oturum biletiyle hesap bağlama** sunucuda yeni uç gerektirir (`fabrika-yonetim` bu işte değiştirilmedi).
- **TOP 10'a yerel (misafir) uçuş puanı gönderme** yapılmadı: sunucu yalnız kendi SITL'inde doğruladığı uçuşları sayar; istemci beyanı hileye açık.

## Grafik kalite notları (UE 5.8)

Lumen GI + yansıma (donanım ışın izleme açık), Nanite (prosedürel ağlar için planlandı), Sanal Gölge Haritaları, SkyAtmosphere + VolumetricCloud +
ExponentialHeightFog + DirectionalLight + SkyLight (gerçek zamanlı yakalama) — `Config/DefaultEngine.ini` ve CAYSIM `ACaySimWorldSetup`. TSR
(r.AntiAliasingMethod=4). Chaos fiziği kullanılmaz; çarpışma yalnız ışın izleme ile algılanır ve SITL'e `crash` olarak bildirilir.
