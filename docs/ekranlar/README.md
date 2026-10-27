# Simumav (UE5) — ekran görüntüleri

Şirket bilgisayarında otomatik duman testiyle (ekransız çizim, `-RenderOffScreen`, Türkçe) çekildi; build 48928fd. Yol haritası ekranları (docs/yol-haritasi.md): sade ana menü, Hangar sihirbazı, Parkur, Mürettebat, Uçuş akışı, deneyim ekranı. Fab haritaları gerçek konumlarına demirli; harita dışı Google fotogerçekçi 3B (yoksa Cesium World Terrain + Bing uydu; docs/fab-icerik.md). Çarpma testleri: docs/fizik-carpisma.md. Gök/bulut/yansımalar gerçek pencerede daha canlı olabilir.

## 2026-10-26 — bölge haritası turu

### Bölge haritası — Manhattan çevrimdışı paketi (Terrarium arazi + OSM binaları, yollar, ağaçlar; M_SmvFacade)
![Bölge haritası — Manhattan çevrimdışı paketi (Terrarium arazi + OSM binaları, yollar, ağaçlar; M_SmvFacade)](B1_bolge_manhattan.jpg)

### Bölge haritası — paket binasına bilerek çarpma (ÇARPIŞMA → disarm)
![Bölge haritası — paket binasına bilerek çarpma (ÇARPIŞMA → disarm)](B2_bolge_carpma.jpg)

### Uçuş › Parkur — Bölge haritası kartı (HAZIR, boyut, bina / yol sayısı, Google kapsaması, Güncelle)
![Uçuş › Parkur — Bölge haritası kartı (HAZIR, boyut, bina / yol sayısı, Google kapsaması, Güncelle)](B3_bolge_karti.jpg)

### Taksim — Google 3B akışı + paketten görünmez bina çarpışması
![Taksim — Google 3B akışı + paketten görünmez bina çarpışması](B4_taksim_google.jpg)

### Fırlatma göstergesi HUD'da (pist kalkışı satırı)
![Fırlatma göstergesi HUD'da (pist kalkışı satırı)](B5_firlatma_hud.jpg)

### Yükleme ekranı (Türkçe, daha açık arka plan)
![Yükleme ekranı (Türkçe, daha açık arka plan)](B6_yukleme_tr.jpg)

### Şehir — City Sample Crowds yakın plan MetaHuman yayalar (öndeki grup; arkada OverCrowd kalabalığı)
![City Sample yayaları](B7_citysample_yayalar.jpg)

### Mürettebat — pilot Workbrand Techwear kıyafetiyle
![Pilot Techwear](B8_pilot_techwear.jpg)


## 1. Açılış — logo + menü sahnesi hazırlık ilerlemesi (atmosfer hazır olmadan menü açılmaz)

![Açılış — logo + menü sahnesi hazırlık ilerlemesi (atmosfer hazır olmadan menü açılmaz)](01_00_boot.jpg)

## 2. Ana menü (Hangar · Parkur · Mürettebat · Uçuş)

![Ana menü (Hangar · Parkur · Mürettebat · Uçuş)](02_01_menu.jpg)

## 3. Pilot menüsü (sağ üst)

![Pilot menüsü (sağ üst)](03_01_pilotmenu.jpg)

## 4. Hangar — kayıtlı dronlar, gerçek sahnede vitrin

![Hangar — kayıtlı dronlar, gerçek sahnede vitrin](04_02_hangar.jpg)

## 5. Yeni dron 1 — tip

![Yeni dron 1 — tip](05_03_hw0_type.jpg)

## 6. Yeni dron 2 — model (3B)

![Yeni dron 2 — model (3B)](06_03_hw1_model.jpg)

## 7. Yeni dron 3 — parametreler ve Lua betiği

![Yeni dron 3 — parametreler ve Lua betiği](07_03_hw2_params.jpg)

## 8. Yeni dron 4 — kamera (gece görüşü önizlemesi)

![Yeni dron 4 — kamera (gece görüşü önizlemesi)](08_03_hw3_camera.jpg)

## 9. Yeni dron 5 — eklentiler

![Yeni dron 5 — eklentiler](09_03_hw4_addons.jpg)

## 10. Yeni dron 6 — görünüm (renk, logo, çıkartma)

![Yeni dron 6 — görünüm (renk, logo, çıkartma)](10_03_hw5_look.jpg)

## 11. Parkur — üç seçenek

![Parkur — üç seçenek](11_04_parkur.jpg)

## 12. Özel parkurlar

![Özel parkurlar](12_05_custom_list.jpg)

## 13. Karşılaşma parkurları

![Karşılaşma parkurları](13_05_enc_list.jpg)

## 14. Karşılaşma parkuru — harita içi kamera turu

![Karşılaşma parkuru — harita içi kamera turu](14_05_enc_tour.jpg)

## 15. Karşılaşma parkuru turu — 2. harita

![Karşılaşma parkuru turu — 2. harita](15_05_enc_tour2.jpg)

## 16. Harita turu — yükleme ekranı (Yıkık İnşaat, aşamalar)

![Harita turu — yükleme ekranı (Yıkık İnşaat, aşamalar)](16_05_enc_tour2_load.jpg)

## 17. Karşılaşma parkuru turu — 3. harita

![Karşılaşma parkuru turu — 3. harita](17_05_enc_tour3.jpg)

## 18. Karşılaşma parkuru turu — 4. harita

![Karşılaşma parkuru turu — 4. harita](18_05_enc_tour4.jpg)

## 19. Harita turu — yükleme katmanı (1. harita zaten yüklü)

![Harita turu — yükleme katmanı (1. harita zaten yüklü)](19_05_enc_tour_load.jpg)

## 20. Gerçek dünya parkurları

![Gerçek dünya parkurları](20_05_real_list.jpg)

## 21. Yeni özel parkur 1 — biyom

![Yeni özel parkur 1 — biyom](21_06_pw0_biome.jpg)

## 22. Yeni özel parkur 2 — dünya (canlı 3B önizleme)

![Yeni özel parkur 2 — dünya (canlı 3B önizleme)](22_06_pw1_world.jpg)

## 23. Yeni özel parkur 3 — hava durumu

![Yeni özel parkur 3 — hava durumu](23_06_pw2_weather.jpg)

## 24. Yeni özel parkur 4 — ad ve deniz seviyesi

![Yeni özel parkur 4 — ad ve deniz seviyesi](24_06_pw3_name.jpg)

## 25. Yeni gerçek dünya 1 — harita (adres ara, işaret, boyut)

![Yeni gerçek dünya 1 — harita (adres ara, işaret, boyut)](25_07_rw0_map.jpg)

## 26. Yeni gerçek dünya 2 — hava

![Yeni gerçek dünya 2 — hava](26_07_rw1_weather.jpg)

## 27. Yeni gerçek dünya 3 — ad

![Yeni gerçek dünya 3 — ad](27_07_rw2_name.jpg)

## 28. Mürettebat — pilot değiştir, seviye, XP, madalyalar

![Mürettebat — pilot değiştir, seviye, XP, madalyalar](28_08_crew.jpg)

## 29. Yeni pilot 1 — cinsiyet

![Yeni pilot 1 — cinsiyet](29_09_cw0_gender.jpg)

## 30. Yeni pilot 2 — vücut

![Yeni pilot 2 — vücut](30_09_cw1_body.jpg)

## 31. Yeni pilot 3 — yüz

![Yeni pilot 3 — yüz](31_09_cw2_face.jpg)

## 32. Yeni pilot 4 — aksesuar

![Yeni pilot 4 — aksesuar](32_09_cw3_acc.jpg)

## 33. Yeni pilot 5 — ad soyad

![Yeni pilot 5 — ad soyad](33_09_cw4_name.jpg)

## 34. Pilot menüsü › Özelleştir

![Pilot menüsü › Özelleştir](34_10_customize.jpg)

## 35. Uçuş 1 — dron

![Uçuş 1 — dron](35_11_fly0_drone.jpg)

## 36. Uçuş 2 — parkur

![Uçuş 2 — parkur](36_11_fly2_track.jpg)

## 37. Uçuş 3 — oyun türü

![Uçuş 3 — oyun türü](37_11_fly3_mode.jpg)

## 38. Uçuş 4 — tek / çok oyunculu

![Uçuş 4 — tek / çok oyunculu](38_11_fly4_players.jpg)

## 39. Uçuş 5 — özet ve UÇ

![Uçuş 5 — özet ve UÇ](39_11_fly5_go.jpg)

## 40. Arkadaşlarım

![Arkadaşlarım](40_12_friends.jpg)

## 41. Başarımlar

![Başarımlar](41_13_achievements.jpg)

## 42. Önceki uçuşlarım

![Önceki uçuşlarım](42_13_flights.jpg)

## 43. Ayarlar — genel

![Ayarlar — genel](43_14_set0.jpg)

## 44. Ayarlar — görüntü

![Ayarlar — görüntü](44_14_set1.jpg)

## 45. Ayarlar — ses

![Ayarlar — ses](45_14_set2.jpg)

## 46. Ayarlar — kumanda

![Ayarlar — kumanda](46_14_set3.jpg)

## 47. Ayarlar — tuşlar

![Ayarlar — tuşlar](47_14_set4.jpg)

## 48. Ayarlar — uyarılar

![Ayarlar — uyarılar](48_14_set5.jpg)

## 49. Ayarlar — bağlantı

![Ayarlar — bağlantı](49_14_set6.jpg)

## 50. Kayıttan oynatma

![Kayıttan oynatma](50_15_replay.jpg)

## 51. TOP 10

![TOP 10](51_16_top10.jpg)

## 52. Hesap

![Hesap](52_17_account.jpg)

## 53. Destek

![Destek](53_18_support.jpg)

## 54. FPV parkurları

![FPV parkurları](54_19_fpv.jpg)

## 55. Karşılaşma salonu

![Karşılaşma salonu](55_19_lobby.jpg)

## 56. Görevler

![Görevler](56_19_missions.jpg)

## 57. Gelişmiş uçuş ayarları

![Gelişmiş uçuş ayarları](57_19_wizard.jpg)

## 58. Uçuşa giriş karesi — Orman (tüm aşamalar bitti; 5 sn sonraki kareyle aynı ışık)

![Uçuşa giriş karesi — Orman (tüm aşamalar bitti; 5 sn sonraki kareyle aynı ışık)](58_20_f1_entry.jpg)

## 59. Uçuş — FPV kamerası

![Uçuş — FPV kamerası](59_20_f1_fpv.jpg)

## 60. Uçuş — Orman, alacakaranlık, yağmur (yerde)

![Uçuş — Orman, alacakaranlık, yağmur (yerde)](60_20_f1_ground.jpg)

## 61. Uçuş — Orman, askı

![Uçuş — Orman, askı](61_20_f1_hover.jpg)

## 62. Yükleme ekranı — Orman (~3 sn)

![Yükleme ekranı — Orman (~3 sn)](62_20_f1_load.jpg)

## 63. Yükleme ekranı — Orman (ilerleme ≥ %60)

![Yükleme ekranı — Orman (ilerleme ≥ %60)](63_20_f1_load2.jpg)

## 64. Duraklatma

![Duraklatma](64_23_pause.jpg)

## 65. Uçuş sonrası deneyim (XP, seviye)

![Uçuş sonrası deneyim (XP, seviye)](65_24_progress.jpg)

## 66. Uçuş özeti

![Uçuş özeti](66_24_summary.jpg)

## 67. Uçuşa giriş karesi — Şehir (tüm aşamalar bitti; 5 sn sonraki kareyle aynı ışık)

![Uçuşa giriş karesi — Şehir (tüm aşamalar bitti; 5 sn sonraki kareyle aynı ışık)](67_30_f3_entry.jpg)

## 68. Uçuş — Şehir (yerde)

![Uçuş — Şehir (yerde)](68_30_f3_ground.jpg)

## 69. Uçuş — Şehir, askı (City Traffic Pro trafiği, yaya kalabalığı)

![Uçuş — Şehir, askı (City Traffic Pro trafiği, yaya kalabalığı)](69_30_f3_hover.jpg)

## 70. Yükleme ekranı — Şehir (~3 sn)

![Yükleme ekranı — Şehir (~3 sn)](70_30_f3_load.jpg)

## 71. Yükleme ekranı — Şehir (ilerleme ≥ %60)

![Yükleme ekranı — Şehir (ilerleme ≥ %60)](71_30_f3_load2.jpg)

## 72. Uçuşa giriş karesi — Askerî Üs / rampa (tüm aşamalar bitti; 5 sn sonraki kareyle aynı ışık)

![Uçuşa giriş karesi — Askerî Üs / rampa (tüm aşamalar bitti; 5 sn sonraki kareyle aynı ışık)](72_45_f6_entry.jpg)

## 73. Uçuş — Askerî Üs (Konya Karapınar) — sabit kanat (yerde)

![Uçuş — Askerî Üs (Konya Karapınar) — sabit kanat (yerde)](73_45_f6_ground.jpg)

## 74. Askerî Üs (Konya Karapınar) — sabit kanat — yüksek irtifada FPV / jet HUD

![Askerî Üs (Konya Karapınar) — sabit kanat — yüksek irtifada FPV / jet HUD](74_45_f6_hfpv.jpg)

## 75. Askerî Üs (Konya Karapınar) — sabit kanat — ~110 m: harita dışı gerçek dünya (Google fotogerçekçi 3B)

![Askerî Üs (Konya Karapınar) — sabit kanat — ~110 m: harita dışı gerçek dünya (Google fotogerçekçi 3B)](75_45_f6_high.jpg)

## 76. Askerî Üs (Konya Karapınar) — sabit kanat — hedef irtifa (150–220 m): gerçek ufuk

![Askerî Üs (Konya Karapınar) — sabit kanat — hedef irtifa (150–220 m): gerçek ufuk](76_45_f6_high2.jpg)

## 77. Kalkış yöntemi — pnömatik rampa (fırlatma öncesi; HUD durumu)

![Kalkış yöntemi — pnömatik rampa (fırlatma öncesi; HUD durumu)](77_45_f6_launch0.jpg)

## 78. Kalkış yöntemi — pnömatik rampa (bırakmadan ~1 sn sonra)

![Kalkış yöntemi — pnömatik rampa (bırakmadan ~1 sn sonra)](78_45_f6_launch1.jpg)

## 79. Yükleme ekranı — Askerî Üs / rampa (~3 sn)

![Yükleme ekranı — Askerî Üs / rampa (~3 sn)](79_45_f6_load.jpg)

## 80. Yükleme ekranı — Askerî Üs / rampa (ilerleme ≥ %60)

![Yükleme ekranı — Askerî Üs / rampa (ilerleme ≥ %60)](80_45_f6_load2.jpg)

## 81. Askerî Üs — 216 m, 3440×1440 ultra geniş (Google fotogerçekçi 3B; kenarda delik yok)

![Askerî Üs — 216 m, 3440×1440 ultra geniş (Google fotogerçekçi 3B; kenarda delik yok)](81_46_f6_uw_high2.jpg)

## 82. Uçuşa giriş karesi — Şehir / VTOL (tüm aşamalar bitti; 5 sn sonraki kareyle aynı ışık)

![Uçuşa giriş karesi — Şehir / VTOL (tüm aşamalar bitti; 5 sn sonraki kareyle aynı ışık)](82_65_f10_entry.jpg)

## 83. Uçuş — Şehir (İstanbul Taksim, Google fotogerçekçi 3B) — VTOL (yerde)

![Uçuş — Şehir (İstanbul Taksim, Google fotogerçekçi 3B) — VTOL (yerde)](83_65_f10_ground.jpg)

## 84. Şehir (İstanbul Taksim, Google fotogerçekçi 3B) — VTOL — yüksek irtifada FPV / jet HUD

![Şehir (İstanbul Taksim, Google fotogerçekçi 3B) — VTOL — yüksek irtifada FPV / jet HUD](84_65_f10_hfpv.jpg)

## 85. Şehir (İstanbul Taksim, Google fotogerçekçi 3B) — VTOL — ~110 m: harita dışı gerçek dünya (Google fotogerçekçi 3B)

![Şehir (İstanbul Taksim, Google fotogerçekçi 3B) — VTOL — ~110 m: harita dışı gerçek dünya (Google fotogerçekçi 3B)](85_65_f10_high.jpg)

## 86. Şehir (İstanbul Taksim, Google fotogerçekçi 3B) — VTOL — hedef irtifa (150–220 m): gerçek ufuk

![Şehir (İstanbul Taksim, Google fotogerçekçi 3B) — VTOL — hedef irtifa (150–220 m): gerçek ufuk](86_65_f10_high2.jpg)

## 87. Kalkış yöntemi — dikey kalkış (fırlatma öncesi; HUD durumu)

![Kalkış yöntemi — dikey kalkış (fırlatma öncesi; HUD durumu)](87_65_f10_launch0.jpg)

## 88. Kalkış yöntemi — dikey kalkış (bırakmadan ~1 sn sonra)

![Kalkış yöntemi — dikey kalkış (bırakmadan ~1 sn sonra)](88_65_f10_launch1.jpg)

## 89. Yükleme ekranı — Şehir / VTOL (~3 sn)

![Yükleme ekranı — Şehir / VTOL (~3 sn)](89_65_f10_load.jpg)

## 90. Yükleme ekranı — Şehir / VTOL (ilerleme ≥ %60)

![Yükleme ekranı — Şehir / VTOL (ilerleme ≥ %60)](90_65_f10_load2.jpg)

## 91. Şehir (Taksim) — 150 m, 3440×1440 ultra geniş (Google İstanbul)

![Şehir (Taksim) — 150 m, 3440×1440 ultra geniş (Google İstanbul)](91_66_f10_uw_high2.jpg)

## 92. Uçuşa giriş — Şehir (Google 3B, Taksim)

![Uçuşa giriş — Şehir (Google 3B, Taksim)](92_70_f11_entry.jpg)

## 93. 70_f11_ground

![70_f11_ground](93_70_f11_ground.jpg)

## 94. Çarpma testi — Şehir (Google 3B, Taksim): 10 m askı

![Çarpma testi — Şehir (Google 3B, Taksim): 10 m askı](94_70_f11_hover.jpg)

## 95. 70_f11_load

![70_f11_load](95_70_f11_load.jpg)

## 96. 70_f11_load2

![70_f11_load2](96_70_f11_load2.jpg)

## 97. Çarpma testi — Şehir (Google 3B, Taksim): burun hedefe, koşu öncesi

![Çarpma testi — Şehir (Google 3B, Taksim): burun hedefe, koşu öncesi](97_70_f11_ram0.jpg)

## 98. Çarpma testi — Şehir (Google 3B, Taksim): ÇARPIŞMA anı (gövde kutuları kırmızı, disarm)

![Çarpma testi — Şehir (Google 3B, Taksim): ÇARPIŞMA anı (gövde kutuları kırmızı, disarm)](98_70_f11_ram1.jpg)

## 99. Çarpma testi — Şehir (Google 3B, Taksim): +3 sn (araç yüzey boyunca düştü)

![Çarpma testi — Şehir (Google 3B, Taksim): +3 sn (araç yüzey boyunca düştü)](99_70_f11_ram2.jpg)

## 100. Çarpma testi — Şehir (Google 3B, Taksim): +7 sn, gövde yüzeyde duruyor

![Çarpma testi — Şehir (Google 3B, Taksim): +7 sn, gövde yüzeyde duruyor](100_70_f11_ram3.jpg)

## 101. Uçuşa giriş — Askerî Üs — Fab binaları

![Uçuşa giriş — Askerî Üs — Fab binaları](101_75_f12_entry.jpg)

## 102. 75_f12_ground

![75_f12_ground](102_75_f12_ground.jpg)

## 103. Çarpma testi — Askerî Üs — Fab binaları: 10 m askı

![Çarpma testi — Askerî Üs — Fab binaları: 10 m askı](103_75_f12_hover.jpg)

## 104. 75_f12_load

![75_f12_load](104_75_f12_load.jpg)

## 105. 75_f12_load2

![75_f12_load2](105_75_f12_load2.jpg)

## 106. Çarpma testi — Askerî Üs — Fab binaları: burun hedefe, koşu öncesi

![Çarpma testi — Askerî Üs — Fab binaları: burun hedefe, koşu öncesi](106_75_f12_ram0.jpg)

## 107. Çarpma testi — Askerî Üs — Fab binaları: ÇARPIŞMA anı (gövde kutuları kırmızı, disarm)

![Çarpma testi — Askerî Üs — Fab binaları: ÇARPIŞMA anı (gövde kutuları kırmızı, disarm)](107_75_f12_ram1.jpg)

## 108. Çarpma testi — Askerî Üs — Fab binaları: +3 sn (araç yüzey boyunca düştü)

![Çarpma testi — Askerî Üs — Fab binaları: +3 sn (araç yüzey boyunca düştü)](108_75_f12_ram2.jpg)

## 109. Çarpma testi — Askerî Üs — Fab binaları: +7 sn, gövde yüzeyde duruyor

![Çarpma testi — Askerî Üs — Fab binaları: +7 sn, gövde yüzeyde duruyor](109_75_f12_ram3.jpg)

## 110. Uçuşa giriş — Google fotogerçekçi 3B (Taksim)

![Uçuşa giriş — Google fotogerçekçi 3B (Taksim)](110_80_f13_entry.jpg)

## 111. 80_f13_ground

![80_f13_ground](111_80_f13_ground.jpg)

## 112. Çarpma testi — Google fotogerçekçi 3B (Taksim): 10 m askı

![Çarpma testi — Google fotogerçekçi 3B (Taksim): 10 m askı](112_80_f13_hover.jpg)

## 113. 80_f13_load

![80_f13_load](113_80_f13_load.jpg)

## 114. 80_f13_load2

![80_f13_load2](114_80_f13_load2.jpg)

## 115. Çarpma testi — Google fotogerçekçi 3B (Taksim): burun hedefe, koşu öncesi

![Çarpma testi — Google fotogerçekçi 3B (Taksim): burun hedefe, koşu öncesi](115_80_f13_ram0.jpg)

## 116. Çarpma testi — Google fotogerçekçi 3B (Taksim): ÇARPIŞMA anı (gövde kutuları kırmızı, disarm)

![Çarpma testi — Google fotogerçekçi 3B (Taksim): ÇARPIŞMA anı (gövde kutuları kırmızı, disarm)](116_80_f13_ram1.jpg)

## 117. Çarpma testi — Google fotogerçekçi 3B (Taksim): +3 sn (araç yüzey boyunca düştü)

![Çarpma testi — Google fotogerçekçi 3B (Taksim): +3 sn (araç yüzey boyunca düştü)](117_80_f13_ram2.jpg)

## 118. Çarpma testi — Google fotogerçekçi 3B (Taksim): +7 sn, gövde yüzeyde duruyor

![Çarpma testi — Google fotogerçekçi 3B (Taksim): +7 sn, gövde yüzeyde duruyor](118_80_f13_ram3.jpg)

## 119. Çarpma testi — Orman — dağ yamacı: burun hedefe, koşu öncesi

![Çarpma testi — Orman — dağ yamacı: burun hedefe, koşu öncesi](119_85_f14_ram0.jpg)

## 120. Çarpma testi — Orman — dağ yamacı: ÇARPIŞMA anı (gövde kutuları kırmızı, disarm)

![Çarpma testi — Orman — dağ yamacı: ÇARPIŞMA anı (gövde kutuları kırmızı, disarm)](120_85_f14_ram1.jpg)

## 121. Çarpma testi — Orman — dağ yamacı: +3 sn (araç yüzey boyunca düştü)

![Çarpma testi — Orman — dağ yamacı: +3 sn (araç yüzey boyunca düştü)](121_85_f14_ram2.jpg)

## 122. Çarpma testi — Orman — dağ yamacı: +7 sn, gövde yüzeyde duruyor

![Çarpma testi — Orman — dağ yamacı: +7 sn, gövde yüzeyde duruyor](122_85_f14_ram3.jpg)
