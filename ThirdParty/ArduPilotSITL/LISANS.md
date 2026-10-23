# ArduPilot SITL (Simumav ile dağıtılan üçüncü taraf yazılım)

Simumav'ın misafir kipteki uçuş fiziği, oyunla birlikte dağıtılan **ArduPilot SITL** (Software-In-The-Loop) ikilileridir.
Oyun bu programları ayrı süreç olarak çalıştırır ve MAVLink protokolüyle (TCP) konuşur; ArduPilot kodu Simumav'a bağlanmaz/gömülmez.

| Bileşen | Sürüm | Lisans | Kaynak |
|---|---|---|---|
| ArduCopter.exe / ArduHeli.exe / ArduPlane.exe / ArduRover.exe | 4.7.2 (git `fbd0a3160c90a557c24ce4c38c16975532652536`) | GPL-3.0-or-later | https://github.com/ArduPilot/ardupilot/tree/fbd0a3160c90a557c24ce4c38c16975532652536 |
| cygwin1.dll | Cygwin çalışma zamanı | LGPL-3.0 (Cygwin lisans istisnası) | https://cygwin.com/git/?p=newlib-cygwin.git |
| cyggcc_s-*.dll, cygstdc++-6.dll, cyggomp-1.dll, cygatomic-1.dll, cygquadmath-0.dll, cygssp-0.dll | GCC çalışma zamanı | GPL-3.0 + GCC Runtime Library Exception | https://gcc.gnu.org/ |
| cygiconv-2.dll, cygintl-8.dll | GNU libiconv / gettext | LGPL | https://www.gnu.org/software/libiconv/ · https://www.gnu.org/software/gettext/ |

İkililer, Mission Planner'ın kullandığı resmi derleme sunucusundan (https://firmware.ardupilot.org/Tools/MissionPlanner/sitl/) sha256
doğrulamasıyla alınır (Tools/sitl/sitl-manifest.json). **Kaynak kodu teklifi:** Yukarıdaki bağlantılar bu ikililerin tam karşılık gelen
kaynak kodudur; ayrıca talep halinde (simumav.com → Destek) en az 3 yıl süreyle aynı kaynak kodu fiziksel/elektronik ortamda sağlanır.
GPL-3.0 metni: https://www.gnu.org/licenses/gpl-3.0.txt · LGPL-3.0: https://www.gnu.org/licenses/lgpl-3.0.txt

ArduPilot, ArduPilot geliştirici topluluğunun tescilli adıdır; Simumav ArduPilot projesiyle bağlantılı değildir ve onun tarafından onaylanmamıştır.
