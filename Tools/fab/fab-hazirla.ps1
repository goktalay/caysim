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
#  d) Config/DefaultGame.ini (yalnız çalışma kopyası; checkout her derlemede sıfırlar):
#       DirectoriesToAlwaysCook "Fab" (içe aktarılan gövdeler yalnız yumuşak yolla yüklenir)
#       MapsToCook + [/Script/Simumav.SmvFabContent] +Maps=<anahtar>|<harita>  (her paketten bir gösterim haritası)
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
	robocopy $kaynak $hedef /MIR /R:1 /W:1 /NP /NFL /NDL /NJH /XJ | Select-Object -Last 6 | ForEach-Object { if ("$_".Trim()) { Write-Host "    $_" } }
	$rc = $LASTEXITCODE
	if ($rc -ge 8) { Uyari "robocopy hata kodu $rc : $kaynak"; return $false }
	return $true
}

function PaketKopyala([string]$srcContent, [string]$etiket) {
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
		$ekle = @('/Content/Fab/', '/Content/.fab-kopyalar.txt') + ($kopyalanan | ForEach-Object { '/Content/' + ($_ -replace '\\', '/') + '/' })
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
			$dosya = Get-ChildItem -LiteralPath (Join-Path $u.FullName 'fbx') -Filter *.fbx -File -ErrorAction SilentlyContinue | Select-Object -First 1
			if (-not $dosya) { $dosya = Get-ChildItem -LiteralPath (Join-Path $u.FullName 'obj') -Filter *.obj -File -ErrorAction SilentlyContinue | Select-Object -First 1 }
			if (-not $dosya) { continue }
			$urun = ($u.Name -replace '-[0-9a-f]{6,}$', '')
			$temiz = ($urun -replace '[^A-Za-z0-9]', '')
			if (-not $droneAlindi -and $urun -match '(?i)drone|quad|copter|uav') { $hedefAd = 'Drone'; $ad = 'SK_FabDrone'; $droneAlindi = $true }
			else { $hedefAd = $temiz; $ad = "SK_$temiz" }
			$hedefDisk = Join-Path $content "Fab\$hedefAd"
			$imza = "$($dosya.FullName)|$($dosya.Length)|$($dosya.LastWriteTimeUtc.Ticks)"
			$imzaDosya = Join-Path $hedefDisk '.kaynak-imza'
			$var = (Test-Path -LiteralPath $imzaDosya) -and ((Get-Content -LiteralPath $imzaDosya -Raw).Trim() -eq $imza) -and (Get-ChildItem -LiteralPath $hedefDisk -Filter *.uasset -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1)
			Bilgi "FabLibrary urunu: $urun ($($dosya.Name), $([math]::Round($dosya.Length / 1KB)) KB) -> /Game/Fab/$hedefAd"
			Get-ChildItem -LiteralPath $u.FullName -Recurse -File -ErrorAction SilentlyContinue | ForEach-Object { Write-Host ("    " + $_.FullName.Substring($u.FullName.Length) + "  " + $_.Length) }
			if ($var) { Bilgi "  degismemis, ice aktarma atlandi"; continue }
			$meta = Join-Path $dosya.DirectoryName 'metadata'
			$isler += [pscustomobject]@{ kaynak = $dosya.FullName; hedef = "/Game/Fab/$hedefAd"; ad = $ad; meta = $meta }
			$imzalar["/Game/Fab/$hedefAd"] = @($imzaDosya, $imza)
		}
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
	$haritalar = @()
	foreach ($p in $paketler) {
		$kok = Join-Path $content $p
		$umaps = @(Get-ChildItem -LiteralPath $kok -Filter *.umap -Recurse -File -ErrorAction SilentlyContinue)
		if ($umaps.Count -eq 0) { Bilgi "paket: $p (harita yok)"; continue }
		$tercih = $umaps | Where-Object { $_.BaseName -match '(?i)demo|showcase|overview|example|preview|level|map' -and $_.BaseName -notmatch '(?i)test|template|empty|_BuiltData' } | Sort-Object Length -Descending | Select-Object -First 1
		if (-not $tercih) { $tercih = $umaps | Where-Object { $_.BaseName -notmatch '_BuiltData' } | Sort-Object Length -Descending | Select-Object -First 1 }
		if (-not $tercih) { continue }
		$umaps | ForEach-Object { Write-Host ("    harita: " + $_.FullName.Substring($content.Length) + "  " + [math]::Round($_.Length / 1KB) + " KB") }
		$rel = $tercih.FullName.Substring($content.Length).TrimStart('\') -replace '\.umap$', ''
		$paket = '/Game/' + ($rel -replace '\\', '/')
		$anahtar = ($p -replace '[^A-Za-z0-9]', '').ToLower()
		Bilgi "paket: $p - harita $paket (anahtar $anahtar; $($umaps.Count) harita)"
		# secilen haritanin klasorundeki diger haritalar (kalici seviyenin alt seviyeleri) de pisirilir
		foreach ($u in ($umaps | Where-Object { $_.DirectoryName -eq $tercih.DirectoryName -and $_.BaseName -notmatch '_BuiltData' })) {
			$r2 = '/Game/' + (($u.FullName.Substring($content.Length).TrimStart('\') -replace '\.umap$', '') -replace '\\', '/')
			$satirlar += "+MapsToCook=(FilePath=`"$r2`")"
		}
		$haritalar += "+Maps=$anahtar|$paket"
	}
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
