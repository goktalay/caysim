# Simumav (UE5) — kullanıcı yol haritası (arayüz ve oyun mantığı)

Kaynak: kullanıcının 2026-10-24 tarihli "Simumav.docx" belgesi (metin aynen aşağıda). Genel ilkeler:
- Menü SADE ve ŞIK; ana menüde yalnız beş giriş: **Hangar · Parkur · Mürettebat · Uçuş** + sağ üstte **Pilot** menüsü.
- Arayüzlerde GERÇEKÇİ SAHNELER (Fab haritaları, UDS atmosferi, gerçek 3B modeller) — düz degrade/çizim yerine.
- Oyun modlarındaki angajman/savunma mekanikleri yalnız oyuncu kontrollü + HUD/puan; otonom güdüm yok.

---

Menü çok karışık öncelikle aşağıdaki gibi düzenleyelim
## Hangar
Bu modülde yarattığımız dronlar birebir mission planner’dan bağlanabileceğimiz dronlar olacak ve biz parametrelerini ve script’lerini girdiğimizde kalıcı olarak kayıtlı olacak
- Bu modüle girdiğimizde öncelikle 1 tane kayıtlı FPV dron olacak
- Yeni düğmesi artı işareti ile sağında olacak
  - Bu düğmeye bastığımızda öncelikle dron tipini soracak
    - ÇOK MOTORLU (MULTIKOPTER)
    - SABİT KANAT (PLANE)
    - VTOL
    - ROVER
  - Dron tipini seçtikten sonra dron tipine göre kullanabileceği dron çeşitleri 3d olarak görünecek
    - ÇOK MOTORLU
      - 4 PERVANELİ (BULABİLDİĞİN TÜM MODELLERİ KOY)
      - 6 PERVANELİ (BULABİLDİĞİN TÜM MODELLERİ KOY)
      - 8 PERVANELİ 4 MOTORLU (BULABİLDİĞİN TÜM MODELLERİ KOY)
    - SABİT KANAT
      - UÇAK ÇEŞİTLERİ (BULABİLDİĞİN TÜM MODELLERİ KOY)
      - CAYMAZ V3
    - VTOL
      - VTOL ÇEŞİTLERİ  (BULABİLDİĞİN TÜM MODELLERİ KOY)
    - ROVER
      - BULABİLDİĞİN TÜM MODELLERİ KOY
- Dron modelini seçtikten sonra 
  - Maximum Hızı
  - Maximum İrtifası
  - Ve dron tipi ve modeline göre parametreler gelsin
- Kamera seçsin
  - Gündüz Kamerası
  - Gece Görüşü
  - Termal
  - Multispektral
- Eklentiler
  - İlaçlama aparatları
  - Paket bırakma aparatı
  - Paraşüt ve benzeri ekipmanlar
- En son sayfada
  - Dronun ismi
  - Rengi
  - Ve benzeri özellkleri seçsin mesela üzerine logo eklemek gibi yada sticker gibi özelleştirilir şeyler
- Bu sayede yeni oluşturulan dron hangara eklenecek 3 boyutlu olarak dönecek
- Aslında ilk eklenen FPV’de bir multikopter
- Kayıtlı dronu seçer yada tıklarsa sistem sorsun Düzenle yada Sil
## Parkur
Bu modüle girince bizi 3 seçenek karşılasın bu seçenekleri ifade eden çok şık animasyonlar dönsün
- Karşılaşma Parkurları
  - Fabrika, orman, askeri üs gibi parkurlar burada görünsün ve parkurun içinden görüntüler göstersin tıklayınca bir şey yapmasın sadece görünsün
- Özel Parkurlar
  - Burada Yeni diye bir düğme olsun tıklayınca
    - Orman
      - Bu seçeneği seçince 
        - Ağaç sıklığı,
        - Su sıklığı
    - Şehir
      - Bina sayısı
      - Araç sayısı
      - İnsan sayısı
    - Dağlık alan
      - Dağ sayısı
      - Su sıklığı
      - Ağaç sıklığı
  - Hava durumu seçenekleri
  - Parkurun ismi, deniz seviyesi yüksekliği
- Gerçek Dünya
  - Lokasyonunu seçmek için Google map açılsın
    - Adres arayabilsin
    - İkonu kaydırsın
    - Harita boyutunu girsin
  - Hava durumu seçenekleri
  - Parkurun ismi
## Mürettebat
- Bu modülde varsayılan bir pilot olacak yeni pilot dediğimizde sihirbaz şeklinde
  - Cinsiyet seçimi
  - Vücüt tipi seçimi
  - Yüz seçimi
  - Aksesuar Seçimi
  - İsim soyisim
- Bu modülde yaratacağımız pilotlar en üstte pilot değiştir ile değiştirilebilecek
- Her pilotun leveli 1 ile başlayacak
- Yapılan uçuşlarda ki manevralara göre uçuş sonrasında deneyim puanı kazanacak ve level artacak
- 100 levele kadar artabilecek levele göre madalya alabilecek
## Uçuş
- Bu modülde girdiğimizde
  - Hangarda oluşturduğumuz dronlar gelsin seçtiğimizde
  - Parkur seçimi gelsin
    - Karşılaşma Parkurları (diğer parkur tiplerinde hiç parkur yoksa bu seçenek gelmeden direkt Karşılaşma Parkurlarında ki parkurlar gelsin direkt)
    - Özel Parkurlar (daha önce oluşturduysak bu seçenek gelsin)
    - Gerçek Dünyalar (daha önce oluşturduysak bu seçenek gelsin)
  - Oyun türü
    - Serbest uçuş
    - Halka toplama
    - Yük bırakma
    - Tarla ilaçlama
    - İt dalaşı
    - Kritik Tesis Savunma
    - Gemi savunma
  - Oyun türünü seçtikten sonra
    - Tek oyunculu
      - Bu seçeneği seçtiğimde oyun türüne göre yapayzeka oyuncusu otomatik eklensin tabii gerekiyorsa ve gerektiği sayıda
    - Multiplayer
      - Bu seçeneği seçtiğimde oyuncu davet etsin email adresi ile
      - İstediği sayıda oyuncu davet edebilsin
      - Yapa zeyka oyuncusu istediği kadar eklesin
  - En son uç desin
## Pilot
En üstte sağda bulunan bu pilot menüsünde
- Özelleştir
- Önceki uçuşlarım
- Arkadaşlarım (burada daha önce beraber oyuna girdiği kişiler)
