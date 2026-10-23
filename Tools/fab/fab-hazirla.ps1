# Simumav — Fab içeriklerini derleme anında projeye hazırlar (şirket bilgisayarı / self-hosted runner).
# Lisanslı dosyalar (uasset, fbx, doku, ses) DEPOYA GİRMEZ: her derlemede runner diskinden çalışma kopyasına alınır,
# kopyalanan klasörler .git/info/exclude ile git dışında tutulur. Hiçbir şey bulunamazsa uyarı verir ve 0 ile çıkar
# (oyun BasicShapes gövdeleri ve prosedürel dünya ile çalışır). Kullanıcı rehberi: docs/fab-icerik.md
#
#  a) Hazırlama projesi (varsayılan D:\SimumavFab): Launcher → Fab Kitaplığı → "Projeye Ekle" ile eklenen paketler.
#     Content\<paket> → proje Content\<paket> (robocopy /MIR; yalnız değişenler). Paketler /Game/<paket>/... yollarıyla
#     birbirine başvurduğu için klasör adı KORUNUR (Content\Fab\<paket> altına taşımak başvuruları kırar).
#  b) VaultCache\FabLibrary\<ürün>\fbx|obj → Editor Python (Scripts/Python/fab_import.py) ile /Game/Fab/<hedef>.
#     Dron ürünleri /Game/Fab/Drone (oyun çalışma anında buradan yükler). Kaynak imzası değişmedikçe yeniden aktarılmaz.
#  c) VaultCache\<paket>\data\Content (Launcher'ın indirdiği UE paketleri) → proje Content\<paket> (a ile aynı kural).
#  e) Fizik çarpışması: uçuş haritalarının (ve şehir ağ klasörlerinin) statik ağlarında basit çarpışma yoksa Editor Python
#     (Scripts/Python/fab_carpisma.py) CollisionTraceFlag=UseComplexAsSimple yazar; paket başına "carpismasiz ag sayisi" günlüğe.
#     Düzeltilen dosyalar robocopy /XO ile sonraki kopyalamada ezilmez; harita/paket imzası değişmedikçe adım atlanır.
#  d) Config/DefaultGame.ini (yalnız çalışma kopyası; checkout her derlemede sıfırlar):
#       DirectoriesToAlwaysCook "Fab" (içe aktarılan gövdeler + motor sesi kopyaları; yalnız yumuşak yolla yüklenir), "UltraDynamicSky"
#       MapsToCook: YALNIZ uçuş haritaları KAYIT TABLOSUNDAKİ (Source/Simumav/Private/SmvMapRegistry.cpp) haritalar + alt seviyeleri
#       ve şehir bloğunun City Sample bina seviyeleri; şehir bina ağı klasörleri DirectoriesToAlwaysCook. Paketlerin geri kalanı
#       (demo/showcase haritaları, kullanılmayan varlıklar) pakete GİRMEZ — pişirme süresi ve paket boyutu bunun için sınırlı.
param(
	[string]$Proje = (Get-Location).Path,
	[string]$UeRoot = $env:UE_ROOT,
	[string]$FabProje = $(if ($env:FAB_PROJE) { $env:FAB_PROJE } else { 'D:\SimumavFab' }),
	[string]$Vault = $(if ($env:FAB_VAULT) { $env:FAB_VAULT } else { 'C:\ProgramData\Epic\EpicGamesLauncher\VaultCache' })
)
$ErrorActionPreference = 'Continue'
function Bilgi($s) { Write-Host "[fab] $s" }
function Uyari($s) { Write-Host "::warning::[fab] $s" }

$content = Join-Path $Proje 'Content'
$uproject = Join-Path $Proje 'Simumav.uproject'
$iniYol = Join-Path $Proje 'Config\DefaultGame.ini'
# projenin kendi Content klasörleri ve UE özel klasörleri — üzerine yazılmaz
$korunan = @('Drones', 'Input', 'Launch', 'Maps', 'Simumav', 'UI', 'Fab', 'Collections', 'Developers', 'StarterContent')
$kopyalanan = New-Object System.Collections.ArrayList   # Content altı göreli yollar (ör. RuinedConstruction, __ExternalActors__\RuinedConstruction)
$paketler = New-Object System.Collections.ArrayList     # üst düzey paket klasörleri (harita aramak için)

function Kopyala([string]$kaynak, [string]$hedef) {
	# /XO: hedefte daha yeni olan dosya (fab_carpisma.py'nin duzelttigi ag) kaynaktan ezilmez; kaynak guncellenirse kopyalanir
	robocopy $kaynak $hedef /MIR /XO /R:1 /W:1 /NP /NFL /NDL /NJH /XJ | Select-Object -Last 6 | ForEach-Object { if ("$_".Trim()) { Write-Host "    $_" } }
	$rc = $LASTEXITCODE
	if ($rc -ge 8) { Uyari "robocopy hata kodu $rc : $kaynak"; return $false }
	return $true
}

function PaketKopyala([string]$srcContent, [string]$etiket) {
	# Content kökündeki harita/varlık dosyaları (ör. KitBash3D Refineries: Content\Main.umap -> /Game/Main)
	foreach ($f in (Get-ChildItem -LiteralPath $srcContent -File -ErrorAction SilentlyContinue | Where-Object { $_.Extension -in '.umap', '.uasset' })) {
		if ($kopyalanan -contains $f.Name) { continue }
		$hedefDosya = Join-Path $content $f.Name
		if ((Test-Path -LiteralPath $hedefDosya) -and -not ($oncekiListe -contains $f.Name)) { Uyari "atlandi (projede ayni adli dosya var): $etiket\$($f.Name)"; continue }
		$h = Get-Item -LiteralPath $hedefDosya -ErrorAction SilentlyContinue
		if (-not $h -or $h.Length -ne $f.Length -or $h.LastWriteTimeUtc -ne $f.LastWriteTimeUtc) {
			Copy-Item -LiteralPath $f.FullName -Destination $hedefDosya -Force
			Bilgi "kok dosyasi kopyalandi: $etiket\$($f.Name)"
		}
		[void]$kopyalanan.Add($f.Name)
	}
	foreach ($d in (Get-ChildItem -LiteralPath $srcContent -Directory -ErrorAction SilentlyContinue)) {
		$ad = $d.Name
		if ($ad -like '__External*') {
			# World Partition (OFPA) aktörleri: __ExternalActors__\<paket>\... — alt klasör bazında
			foreach ($s in (Get-ChildItem -LiteralPath $d.FullName -Directory -ErrorAction SilentlyContinue)) {
				if ($korunan -contains $s.Name) { continue }
				$rel = "$ad\$($s.Name)"
				if ($kopyalanan -contains $rel) { continue }
				if (Kopyala $s.FullName (Join-Path $content $rel)) { [void]$kopyalanan.Add($rel) }
			}
			continue
		}
		if ($korunan -contains $ad) { Uyari "atlandi (proje klasoruyle cakisiyor): $etiket\$ad"; continue }
		if ($kopyalanan -contains $ad) { Bilgi "zaten alindi (ilk kaynak gecerli): $ad"; continue }
		Bilgi "kopyalaniyor: $etiket\$ad"
		if (Kopyala $d.FullName (Join-Path $content $ad)) { [void]$kopyalanan.Add($ad); [void]$paketler.Add($ad) }
	}
}

$oncekiListe = @()
$listeYolu = Join-Path $content '.fab-kopyalar.txt'
if (Test-Path -LiteralPath $listeYolu) { $oncekiListe = @(Get-Content -LiteralPath $listeYolu | Where-Object { $_ }) }

try {
	# ---------------- a) hazırlama projesi ----------------
	$fabContent = Join-Path $FabProje 'Content'
	if (Test-Path -LiteralPath $fabContent) {
		Bilgi "hazirlama projesi: $FabProje"
		PaketKopyala $fabContent 'SimumavFab'
	} else {
		Uyari "hazirlama projesi yok: $fabContent (docs/fab-icerik.md - bos UE 5.8 projesi olusturup Fab paketlerini 'Projeye Ekle' ile ekleyin)"
	}

	# ---------------- c) VaultCache UE paketleri ----------------
	if (Test-Path -LiteralPath $Vault) {
		foreach ($v in (Get-ChildItem -LiteralPath $Vault -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -ne 'FabLibrary' })) {
			$dc = Join-Path $v.FullName 'data\Content'
			if (Test-Path -LiteralPath $dc) {
				Bilgi "VaultCache paketi: $($v.Name)"
				PaketKopyala $dc $v.Name
			} else {
				$stage = Join-Path $v.FullName 'stage'
				$durum = if (Test-Path -LiteralPath $stage) { 'indirme tamamlanmamis (stage var, data\Content yok)' } else { 'data\Content yok' }
				Uyari "VaultCache paketi kullanilamadi: $($v.Name) - $durum"
			}
		}
	} else {
		Uyari "VaultCache yok: $Vault"
	}

	# önceki derlemede kopyalanıp artık kaynağı olmayan klasörleri kaldır (yalnız kendi listemizdekiler)
	$liste = Join-Path $content '.fab-kopyalar.txt'
	if (Test-Path -LiteralPath $liste) {
		foreach ($eski in (Get-Content -LiteralPath $liste | Where-Object { $_ })) {
			if (-not ($kopyalanan -contains $eski) -and -not ($korunan -contains $eski)) {
				$p = Join-Path $content $eski
				if (Test-Path -LiteralPath $p) { Bilgi "kaynagi kalmayan kopya kaldiriliyor: $eski"; Remove-Item -LiteralPath $p -Recurse -Force -ErrorAction SilentlyContinue }
			}
		}
	}
	Set-Content -LiteralPath $liste -Value ($kopyalanan -join "`r`n") -Encoding UTF8

	# git dışında tut (lisanslı içerik depoya girmez)
	$exclude = Join-Path $Proje '.git\info\exclude'
	if (Test-Path -LiteralPath (Split-Path $exclude)) {
		$mevcut = if (Test-Path -LiteralPath $exclude) { Get-Content -LiteralPath $exclude } else { @() }
		$ekle = @('/Content/Fab/', '/Content/.fab-kopyalar.txt') + ($kopyalanan | ForEach-Object { '/Content/' + ($_ -replace '\\', '/') + $(if ($_ -match '\.(umap|uasset)$') { '' } else { '/' }) })
		$yeni = $ekle | Where-Object { $mevcut -notcontains $_ }
		if ($yeni) { Add-Content -LiteralPath $exclude -Value $yeni -Encoding ASCII }
	}

	# ---------------- b) FabLibrary FBX/OBJ ürünleri ----------------
	$isler = @()
	$imzalar = @{}
	$lib = Join-Path $Vault 'FabLibrary'
	$droneAlindi = $false
	if (Test-Path -LiteralPath $lib) {
		foreach ($u in (Get-ChildItem -LiteralPath $lib -Directory -ErrorAction SilentlyContinue)) {
			$dosya = Get-ChildItem -LiteralPath (Join-Path $u.FullName 'fbx') -Filter *.fbx -File -Recurse -ErrorAction SilentlyContinue | Sort-Object Length -Descending | Select-Object -First 1
			if (-not $dosya) { $dosya = Get-ChildItem -LiteralPath (Join-Path $u.FullName 'obj') -Filter *.obj -File -Recurse -ErrorAction SilentlyContinue | Sort-Object Length -Descending | Select-Object -First 1 }
			if (-not $dosya) { continue }
			$urun = ($u.Name -replace '-[0-9a-f]{6,}$', '')
			$temiz = ($urun -replace '[^A-Za-z0-9]', '')
			$statik = $false
			# sahne dekoru (yalniz gorsel statik ag): kara araclari / hava savunma / anti-dron sistemleri — once bunlar ("Anti-Drone" dron degil)
			if ($urun -match '(?i)tank|patriot|mim-?104|sam_|_sam|anti-?drone|microwave|launcher|truck|vehicle') { $hedefAd = "Props\$temiz"; $ad = "SM_$temiz"; $statik = $true }
			elseif ($urun -match '(?i)fpv|interceptor|racer') { $hedefAd = 'DroneFpv'; $ad = 'SK_FabDroneFpv' }
			elseif (-not $droneAlindi -and $urun -match '(?i)drone|quad|copter|uav') { $hedefAd = 'Drone'; $ad = 'SK_FabDrone'; $droneAlindi = $true }
			else { $hedefAd = $temiz; $ad = "SK_$temiz" }
			$hedefDisk = Join-Path $content "Fab\$hedefAd"
			$imza = "$($dosya.FullName)|$($dosya.Length)|$($dosya.LastWriteTimeUtc.Ticks)"
			$imzaDosya = Join-Path $hedefDisk '.kaynak-imza'
			$var = (Test-Path -LiteralPath $imzaDosya) -and ((Get-Content -LiteralPath $imzaDosya -Raw).Trim() -eq $imza) -and (Get-ChildItem -LiteralPath $hedefDisk -Filter *.uasset -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1)
			$hedefYol = '/Game/Fab/' + ($hedefAd -replace '\\', '/')
			Bilgi "FabLibrary urunu: $urun ($($dosya.Name), $([math]::Round($dosya.Length / 1KB)) KB) -> $hedefYol$(if ($statik) { ' (statik dekor)' })"
			Get-ChildItem -LiteralPath $u.FullName -Recurse -File -ErrorAction SilentlyContinue | ForEach-Object { Write-Host ("    " + $_.FullName.Substring($u.FullName.Length) + "  " + $_.Length) }
			if ($var) { Bilgi "  degismemis, ice aktarma atlandi"; continue }
			$meta = Join-Path $dosya.DirectoryName 'metadata'
			$isler += [pscustomobject]@{ kaynak = $dosya.FullName; hedef = $hedefYol; ad = $ad; meta = $meta; statik = $statik }
			$imzalar[$hedefYol] = @($imzaDosya, $imza)
		}
	}
	# Ultra Dynamic Sky: degisken adlari ve hava on ayarlari bir kez gunluge yazilir (oyun bunlari adla baglar)
	$udsDir = Join-Path $content 'UltraDynamicSky'
	$udsDamga = Join-Path $content 'Fab\.uds-incelendi'
	if ((Test-Path -LiteralPath $udsDir) -and -not (Test-Path -LiteralPath $udsDamga)) {
		$isler += [pscustomobject]@{ tur = 'uds'; hedef = '/Game/UltraDynamicSky' }
		$imzalar['/Game/UltraDynamicSky'] = @($udsDamga, 'tamam')
	}
	# Aircraft Engine Sounds: secilen motor dongulerinin kopyasi /Game/Fab/Sounds (oyun ucak/VTOL sesi icin buradan yukler)
	$sesDir = Join-Path $content 'cplomedia_AircraftEngineSounds'
	$sesDamga = Join-Path $content 'Fab\Sounds\.ses-v1'
	if ((Test-Path -LiteralPath $sesDir) -and -not (Test-Path -LiteralPath $sesDamga)) {
		$isler += [pscustomobject]@{ tur = 'ses'; hedef = '/Game/Fab/Sounds'; kok = '/Game/cplomedia_AircraftEngineSounds' }
		$imzalar['/Game/Fab/Sounds'] = @($sesDamga, 'tamam')
	}
	if ($isler.Count -gt 0) {
		$editor = Join-Path $UeRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
		if (-not (Test-Path -LiteralPath $editor)) { Uyari "editor yok: $editor - ice aktarma atlandi" }
		else {
			$isYol = Join-Path $env:TEMP 'fab-isler.json'
			$sonucYol = Join-Path $env:TEMP 'fab-sonuc.json'
			Remove-Item -LiteralPath $sonucYol -ErrorAction SilentlyContinue
			ConvertTo-Json -InputObject @($isler) -Depth 3 | Set-Content -LiteralPath $isYol -Encoding UTF8
			$env:FAB_IS_LISTESI = $isYol
			$env:FAB_SONUC = $sonucYol
			$env:SIMUMAV_EDITOR_ACIK = '0'
			$betik = Join-Path $Proje 'Scripts\Python\fab_import.py'
			Bilgi "Editor Python ile ice aktariliyor ($($isler.Count) is)"
			$ddc = @($env:UE_ZEN_ARGS -split ' ' | Where-Object { $_ })   # CI'nin kendi Zen DDC sunucusu (build-windows.yml)
			& $editor $uproject "-ExecutePythonScript=$betik" -unattended -nop4 -nosplash -NullRHI -stdout -FullStdOutLogOutput @ddc 2>&1 | ForEach-Object {
				$l = "$_"
				if ($l -match 'fab_import|LogInterchange.*(Error|Warning)|LogFbx.*(Error|Warning)') { Write-Host "    $l" }
			}
			Bilgi "editor cikis kodu: $LASTEXITCODE"
			if (Test-Path -LiteralPath $sonucYol) {
				$sonuc = Get-Content -LiteralPath $sonucYol -Raw -Encoding UTF8 | ConvertFrom-Json
				foreach ($s in @($sonuc)) {
					if ($s.ok -and $imzalar.ContainsKey($s.hedef)) {
						$i = $imzalar[$s.hedef]
						New-Item -ItemType Directory -Force -Path (Split-Path $i[0]) | Out-Null
						Set-Content -LiteralPath $i[0] -Value $i[1] -Encoding UTF8
						Bilgi "ice aktarildi: $($s.hedef) (iskelet: $($s.iskelet); kemik: $(@($s.kemikler).Count))"
					} else { Uyari "ice aktarilamadi: $($s.hedef)" }
				}
			} else { Uyari "ice aktarma sonucu yok (editor betigi calismadi?)" }
		}
	}

	# ---------------- d) paketleme ayarları ----------------
	$satirlar = @()
	if (Test-Path -LiteralPath (Join-Path $content 'Fab')) { $satirlar += '+DirectoriesToAlwaysCook=(Path="Fab")' }
	# Ultra Dynamic Sky calisma aninda yumusak yolla yuklenir (SmvFabContent::SpawnSky)
	if (Test-Path -LiteralPath $udsDir) { $satirlar += '+DirectoriesToAlwaysCook=(Path="UltraDynamicSky")'; Bilgi "Ultra Dynamic Sky bulundu - pakete pisirilecek" }
	# ucus haritalari: KAYIT TABLOSU (oyun kodu) - yalniz oradaki haritalar pisirilir
	$reg = Join-Path $Proje 'Source\Simumav\Private\SmvMapRegistry.cpp'
	$regMetin = if (Test-Path -LiteralPath $reg) { Get-Content -LiteralPath $reg -Raw -Encoding UTF8 } else { '' }
	function Blok([string]$ad) { if ($regMetin -match "(?s)// <$ad>(.*?)// </$ad>") { return $Matches[1] } else { Uyari "kayit tablosunda blok yok: $ad"; return '' } }
	function DiskYolu([string]$oyunYolu) { return (Join-Path $content (($oyunYolu.Substring(6)) -replace '/', '\')) }
	function HaritaVar([string]$oyunYolu) { return ($oyunYolu.StartsWith('/Game/') -and (Test-Path -LiteralPath ((DiskYolu $oyunYolu) + '.umap'))) }
	function Diskten([string]$umap) { return '/Game/' + (($umap.Substring($content.Length).TrimStart('\') -replace '\.umap$', '') -replace '\\', '/') }
	$pisirilecek = New-Object System.Collections.ArrayList
	foreach ($ln in ((Blok 'HARITA-TABLOSU') -split "`n")) {
		$m = [regex]::Matches($ln, 'TEXT\("([^"]*)"\)')
		if ($m.Count -lt 5) { continue }
		$anahtar = $m[0].Groups[1].Value
		$adaylar = $m[4].Groups[1].Value
		if (-not $adaylar) { Bilgi "harita $anahtar : kodla kurulur (sehir blogu)"; continue }
		$secilen = $null
		foreach ($c in ($adaylar -split ';')) { $c = $c.Trim(); if ($c -and (HaritaVar $c)) { $secilen = $c; break } }
		if (-not $secilen) { Uyari "harita $anahtar : pakette yok ($adaylar) - menude 'yok' gorunur"; continue }
		Bilgi "harita $anahtar : $secilen"
		[void]$pisirilecek.Add($secilen)
		# alt seviyeler (Sub_Levels / SubLevels klasorleri: haritanin klasoru ve bir ustu) - calisma aninda kalici seviyeyle yuklenir
		$hDir = Split-Path ((DiskYolu $secilen) + '.umap')
		foreach ($kok in @($hDir, (Split-Path $hDir))) {
			foreach ($sd in (Get-ChildItem -LiteralPath $kok -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '(?i)^sub_?levels?$' })) {
				foreach ($u in (Get-ChildItem -LiteralPath $sd.FullName -Filter *.umap -File -Recurse -ErrorAction SilentlyContinue)) {
					$r2 = Diskten $u.FullName
					if (-not ($pisirilecek -contains $r2)) { [void]$pisirilecek.Add($r2) }
				}
			}
		}
	}
	# sehir blogu: bina agi klasorleri ve City Sample bina seviyeleri
	foreach ($y in [regex]::Matches((Blok 'SEHIR-AG-KLASORLERI'), '"(/Game/[^"]+)"')) {
		$g = $y.Groups[1].Value
		if (Test-Path -LiteralPath (DiskYolu $g)) { $satirlar += "+DirectoriesToAlwaysCook=(Path=`"$($g.Substring(6))`")"; Bilgi "sehir agi klasoru: $g" } else { Uyari "sehir agi klasoru yok: $g" }
	}
	foreach ($y in [regex]::Matches((Blok 'SEHIR-KULELERI'), '"(/Game/[^"]+)"')) {
		$g = $y.Groups[1].Value
		if (HaritaVar $g) { [void]$pisirilecek.Add($g); Bilgi "sehir bina seviyesi: $g" } else { Uyari "sehir bina seviyesi yok: $g" }
	}
	foreach ($h in $pisirilecek) { $satirlar += "+MapsToCook=(FilePath=`"$h`")" }
	# ---------------- e) fizik carpismasi (statik aglar) ----------------
	$carpKlasor = @()
	foreach ($y in [regex]::Matches((Blok 'SEHIR-AG-KLASORLERI'), '"(/Game/[^"]+)"')) { $g = $y.Groups[1].Value; if (Test-Path -LiteralPath (DiskYolu $g)) { $carpKlasor += $g } }
	if (Test-Path -LiteralPath (Join-Path $content 'Fab\Props')) { $carpKlasor += '/Game/Fab/Props' }
	if ($pisirilecek.Count -gt 0 -or $carpKlasor.Count -gt 0) {
		function CarpImza {
			$n = 0; $t = [int64]0
			foreach ($k in $kopyalanan) {
				$d = Join-Path $content $k
				if (Test-Path -LiteralPath $d -PathType Container) {
					foreach ($f in (Get-ChildItem -LiteralPath $d -Recurse -File -Filter *.uasset -ErrorAction SilentlyContinue)) { $n++; $t += $f.Length + $f.LastWriteTimeUtc.Ticks % 1000003 }
				}
			}
			return "v1|$($pisirilecek -join ';')|$($carpKlasor -join ';')|$n|$t"
		}
		$carpDamga = Join-Path $content 'Fab\.carpisma-imza'
		$imzaOnce = CarpImza
		$eskiImza = if (Test-Path -LiteralPath $carpDamga) { (Get-Content -LiteralPath $carpDamga -Raw).Trim() } else { '' }
		if ($eskiImza -eq $imzaOnce) {
			Bilgi "fizik carpismasi: haritalar ve paketler degismemis - atlandi (onceki sonuc: Content\Fab\.carpisma-sonuc.json)"
			$eskiSonuc = Join-Path $content 'Fab\.carpisma-sonuc.json'
			if (Test-Path -LiteralPath $eskiSonuc) { Get-Content -LiteralPath $eskiSonuc -Raw -Encoding UTF8 | Write-Host }
		} else {
			$editor = Join-Path $UeRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
			if (-not (Test-Path -LiteralPath $editor)) { Uyari "editor yok: $editor - carpisma hazirligi atlandi" }
			else {
				$gYol = Join-Path $env:TEMP 'fab-carpisma-girdi.json'
				$sYol = Join-Path $env:TEMP 'fab-carpisma-sonuc.json'
				Remove-Item -LiteralPath $sYol -ErrorAction SilentlyContinue
				ConvertTo-Json -InputObject @{ haritalar = @($pisirilecek); klasorler = @($carpKlasor) } -Depth 3 | Set-Content -LiteralPath $gYol -Encoding UTF8
				$env:FAB_CARPISMA_GIRDI = $gYol
				$env:FAB_CARPISMA_SONUC = $sYol
				$env:SIMUMAV_EDITOR_ACIK = '0'
				$betik = Join-Path $Proje 'Scripts\Python\fab_carpisma.py'
				Bilgi "fizik carpismasi: $($pisirilecek.Count) harita + $($carpKlasor.Count) klasor statik aglari denetleniyor (Editor Python)"
				$ddc = @($env:UE_ZEN_ARGS -split ' ' | Where-Object { $_ })
				& $editor $uproject "-ExecutePythonScript=$betik" -unattended -nop4 -nosplash -NullRHI -stdout -FullStdOutLogOutput @ddc 2>&1 | ForEach-Object {
					$l = "$_"
					if ($l -match 'fab_carpisma') { Write-Host "    $l" }
				}
				Bilgi "editor cikis kodu: $LASTEXITCODE"
				if (Test-Path -LiteralPath $sYol) {
					New-Item -ItemType Directory -Force -Path (Split-Path $carpDamga) | Out-Null
					Copy-Item -LiteralPath $sYol -Destination (Join-Path $content 'Fab\.carpisma-sonuc.json') -Force
					Set-Content -LiteralPath $carpDamga -Value (CarpImza) -Encoding UTF8
				} else { Uyari "carpisma hazirligi sonucu yok (editor betigi calismadi?)" }
			}
		}
	}
	$haritalar = @()
	if ($satirlar.Count -gt 0 -or $haritalar.Count -gt 0) {
		$ek = @('', '; ---- fab-hazirla.ps1 (derleme aninda; depoda yok) ----', '[/Script/UnrealEd.ProjectPackagingSettings]') + $satirlar
		if ($haritalar.Count -gt 0) { $ek += @('', '[/Script/Simumav.SmvFabContent]') + $haritalar }
		Add-Content -LiteralPath $iniYol -Value $ek -Encoding UTF8
		Bilgi "DefaultGame.ini eklendi:"
		$ek | ForEach-Object { Write-Host "    $_" }
	} else {
		Bilgi "Fab icerigi yok - paketleme ayari degismedi (BasicShapes yedekleri kullanilir)"
	}
} catch {
	Uyari "fab-hazirla beklenmeyen hata: $($_.Exception.Message)"
}
exit 0
