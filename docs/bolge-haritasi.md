# Bölge haritası (gerçek dünya parkuru)

Gerçek dünya parkuruna (Parkur › Gerçek Dünya) yalnız **bölge haritası inmiş** bölgede girilir. Bölge = parkur noktası çevresinde
**azami uçuş mesafesi** yarıçaplı daire (sihirbazdaki kaydırıcı; 250 m – 5 km) + 600 m kenar payı.

## İki parça

| Parça | Ne yapar | Diskte |
|---|---|---|
| Çevrimdışı paket (açık veri) | Arazi yükseklikleri + OpenStreetMap binaları, yolları, su, orman / park alanları, tek ağaçlar indirilir. İnternetsiz uçulur. | `Saved/Simumav/Regions/<enlem>_<boylam>_<yarıçap>/` |
| Google 3B kapsama denetimi | Google fotogerçekçi 3B karo ağacının **yalnız JSON düğümleri** okunur; daire içindeki örnek noktaların üstündeki en ince karo boyu ölçülür → "tam 3B" ve "kapsama" oranları. | yalnız sonuç (`region.json`) |

**Google karoları indirilmez / saklanmaz.** Google Map Tiles API politikası önceden getirme, önbellekleme, saklama ve çevrimdışı
kullanımı yasaklar ("you must not pre-fetch, index, store, or cache any Content", "Offline uses"). Bu yüzden Google 3B uçuşta canlı akıtılır;
denetim yalnız hangi kaynağın kullanılacağına karar verir.

## Uçuşta kaynak seçimi

| Ayarlar › Görüntü › Gerçek dünya | Google denetimi geçti (kapsama ≥ %90, ≤ 30 gün) | Geçmedi / yapılmadı |
|---|---|---|
| Google 3B (varsayılan) | Google fotogerçekçi 3B (canlı) | çevrimdışı paket |
| Arazi | Cesium World Terrain + Bing uydu (canlı) | aynı |
| Çevrimdışı | çevrimdışı paket | çevrimdışı paket |

Kalkış zemini (SITL ev irtifası) her durumda paketin arazisinden alınır.

**Google akışında bina çarpışması paketten:** Türkiye'de Google karoları 2,5B (bina ağı yok; dron binanın içinden geçiyordu — duman f13 Taksim:
72 yönde 0 isabet). Google ile uçarken paketteki OSM binaları **görünmez, BlockAll çarpışmalı** kurulur (yer ofseti ölçülünce birlikte kayar).
Duman 40409620782: Taksim'de görünmez binaya çarpma → disarm → yüzeyde 0,18 m.

**3B / 2,5B ayrımı ölçülemiyor:** Google karo ağacı Taksim ve Manhattan'da aynı (en küçük geometricError 2,01 m, derinlik 25, ortanca karo 38 m);
arayüz yalnız kapsamayı gösterir.

## Veri kaynakları ve lisans

- **Arazi:** AWS Terrain Tiles (Terrarium PNG; `s3.amazonaws.com/elevation-tiles-prod`), z14 (~9 m/px) uçuş alanı, z10 ufuk (30 km).
  Kaynaklar: SRTM, GMTED2010, ETOPO1, EU-DEM (Copernicus), NED … — atıf gerekli, ticari kullanım serbest.
  Deniz: Terrarium batimetrisi ≤ 0 m → taban −3 m'ye kırpılır, 0 m'de deniz düzlemi.
- **Binalar / yollar / su / orman:** OpenStreetMap verisi, **OpenFreeMap** vektör karolarından (OpenMapTiles şeması, z14; Cloudflare önbelleği,
  anahtar / hız sınırı yok; ~0,4 sn ve ~400 KB / karo; TileJSON `https://tiles.openfreemap.org/planet`). Katmanlar: building (render_height),
  transportation, water, landcover, landuse (mezarlık), park. Çözücü `Core/SmvMvt` (tests/cpp/test_mvt.py, mapbox_vector_tile'a karşı).
  © OpenStreetMap katkıcıları (ODbL), © OpenMapTiles, OpenFreeMap — atıf gerekli.
  Yedek: Overpass API (`[/Script/Simumav.SmvRegion] OsmSource=overpass`; z15, tek sorgu, 9 deneme) — kamu sunucusu yoğunlukta 502 / 504 / 429
  veriyordu, 2 km bölge saatlerce sürüp "takılı" görünüyordu (kullanıcı, 2026-10-28).
- Bina yüksekliği: `height` → `building:levels` (+ `roof:levels`) → türe ve taban alanına göre sezgisel kat sayısı (OSM kimliğine bağlı, her seferinde aynı).
- Uydu görüntüsü YOK: serbest lisanslı yüksek çözünürlüklü küresel görüntü bulunmadığından arazi Hyper malzemesiyle (eğim → kaya,
  yükseklik → kar) çizilir. Sentinel-2 cloudless 2016 (CC BY 4.0, 10 m) eklenebilir ama EOX hizmetinin ticari kullanımı abonelik ister.

## Sahne (ASmvRegionWorld)

- Arazi: kare ızgara (adım 5–14 m, yarıçapa göre; `[/Script/Simumav.SmvRegion] TerrainStepM`), 96 hücrelik parçalar, eşzamansız çarpışma pişirme.
- Ufuk halkası: paket kenarından 30 km'ye, 4 m alçakta.
- Binalar: taban izi yükseltilir (duvar + çatı), 400 m hücre başına bir bileşen, BlockAll çarpışma, `building` etiketi.
  Malzeme `M_SmvFacade` (Scripts/Python/simumav_varliklar.py): UV metre, kat / pencere ızgarası, zemin katta vitrin, gece rastgele yanan pencereler.
  Ev noktasını içeren / 8 m yakınındaki bina kurulmaz.
- Yollar: araziye serilen şeritler (asfalt / kaldırım / toprak), çarpışmasız.
- Ağaçlar: orman 1/90 m², çalılık 1/160 m², park 1/450 m², mezarlık 1/260 m²; bütçe 45 000 (`TreeBudget`), Brushify ağları.

## Ayarlar (`DefaultGame.ini`, `[/Script/Simumav.SmvRegion]`)

`GoogleTargetM` (25; bu boya inen örnek için ağaçta daha derine inilmez), `GoogleFullM` (60; "tam 3B" eşiği), `GoogleMaxRequests` (900),
`TerrainStepM`, `TreeBudget`.

## Duman testi

`-SmvSmokeOnly=15,16` (Taksim 1,2 km bölge paketi: indirme + Google denetimi + çevrimdışı paketle binaya çarpma ve 120 m görünüm),
`-SmvSmokeRegion=enlem,boylam,yarıçap` başka bölge. Günlük satırları `Bölge …`, `SMOKE region …`. `-SmvNoRegionGate`: giriş kapısı kapalı (eski davranış).
