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
$ilkKaynak = @{}                                         # paket klasörü → ilk kaynak yolu (ortak yayıncı klasörlerinin birleştirilmesi)
$modelKlasorleri = New-Object System.Collections.ArrayList   # model dosyasi iceren (UE paketi olmayan) klasorler -> b) adimi

function Kopyala([string]$kaynak, [string]$hedef) {
	# /XO: hedefte daha yeni olan dosya (fab_carpisma.py'nin duzelttigi ag) kaynaktan ezilmez; kaynak guncellenirse kopyalanir
	# *_BuiltData (harita isik haritalari / onceden hesaplanmis isik verisi) KOPYALANMAZ: proje r.AllowStaticLighting=False
	# (Lumen + Ultra Dynamic Sky) - calisma aninda hic okunmaz. Pisirmede ise yuzlerce lightmap dokusu ayni anda insa
	# ediliyordu (US_Military Showreel_Scene: 541 doku, tahmini ~95 GB) ve bellek tukenip pisirme 3 saat sessiz asili kaldi.
	robocopy $kaynak $hedef /MIR /XO /R:1 /W:1 /NP /NFL /NDL /NJH /XJ /XF $script:builtDataDesen | Select-Object -Last 6 | ForEach-Object { if ("$_".Trim()) { Write-Host "    $_" } }
	$rc = $LASTEXITCODE
	if ($rc -ge 8) { Uyari "robocopy hata kodu $rc : $kaynak"; return $false }
	BuiltDataSil $hedef
	return $true
}

$script:builtDataDesen = @('*_BuiltData.uasset', '*_BuiltData.uexp', '*_BuiltData.ubulk')
$script:builtDataSayi = 0; $script:builtDataBayt = [int64]0
function BuiltDataSil([string]$kok) {
	# /XF dislanan dosyalar /MIR ile hedeften de silinmez: onceki derlemelerden kalanlari kaldir
	foreach ($f in (Get-ChildItem -LiteralPath $kok -Recurse -File -Filter '*_BuiltData.*' -ErrorAction SilentlyContinue)) {
		$script:builtDataSayi++; $script:builtDataBayt += $f.Length
		Remove-Item -LiteralPath $f.FullName -Force -ErrorAction SilentlyContinue
	}
}

function PaketKopyala([string]$srcContent, [string]$etiket) {
	# Content kökündeki harita/varlık dosyaları (ör. KitBash3D Refineries: Content\Main.umap -> /Game/Main)
	foreach ($f in (Get-ChildItem -LiteralPath $srcContent -File -ErrorAction SilentlyContinue | Where-Object { $_.Extension -in '.umap', '.uasset' })) {
		if ($kopyalanan -contains $f.Name) { continue }
		if ($f.Name -like '*_BuiltData.*') {
			# isik verisi kopyalanmaz (Kopyala notu); onceki derlemeden kalan kok kopyasi silinir
			$eski = Join-Path $content $f.Name
			if (Test-Path -LiteralPath $eski) { $script:builtDataSayi++; $script:builtDataBayt += (Get-Item -LiteralPath $eski).Length; Remove-Item -LiteralPath $eski -Force }
			continue
		}
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
		if ($ad -match '[\s\.]') {
			# UE paketi degil (bosluk/nokta): icinde FBX/OBJ/GLB/.blend modeli varsa b) adiminda FabLibrary urunu gibi ice aktarilir
			[void]$modelKlasorleri.Add($d.FullName)
			Uyari "UE paketi degil (bosluk/nokta), model klasoru olarak denenecek: $etiket\$ad"; continue
		}
		if ($korunan -contains $ad) { Uyari "atlandi (proje klasoruyle cakisiyor): $etiket\$ad"; continue }
		if ($kopyalanan -contains $ad) {
			# ayni yayincinin ortak klasoru (or. BendylabStudios: Hexacopter hazirlama projesinde, Interceptor VaultCache'te):
			# ilk kaynakta olmayan alt klasorler eklenir (ilk kaynagin /MIR kopyasi onlari siler; her derlemede yeniden eklenir)
			$ilk = $ilkKaynak[$ad]
			# yalniz urun basina alt klasor kullanan yayinci klasorleri birlestirilir (Hyper gibi ayni adli farkli paketler birlestirilmez)
			if ($ad -notin @('BendylabStudios')) { $ilk = $null }
			$ekAlt = @(Get-ChildItem -LiteralPath $d.FullName -Directory -ErrorAction SilentlyContinue | Where-Object { $ilk -and -not (Test-Path -LiteralPath (Join-Path $ilk $_.Name)) })
			if ($ekAlt.Count -eq 0) { Bilgi "zaten alindi (ilk kaynak gecerli): $ad"; continue }
			foreach ($s in $ekAlt) {
				Bilgi "alt klasor ekleniyor: $etiket\$ad\$($s.Name)"
				[void](Kopyala $s.FullName (Join-Path $content "$ad\$($s.Name)"))
				[void]$paketler.Add("$ad/$($s.Name)")
			}
			continue
		}
		Bilgi "kopyalaniyor: $etiket\$ad"
		if (Kopyala $d.FullName (Join-Path $content $ad)) { [void]$kopyalanan.Add($ad); [void]$paketler.Add($ad); $ilkKaynak[$ad] = $d.FullName }
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

	# ---------------- a2) hazirlama projesi altindaki ALT PROJELER ----------------
	# Bazi Fab urunleri "Projeye Ekle" yerine kendi projesini olusturur (D:\SimumavFab\<Urun>\<Urun>.uproject). Content klasorleri
	# /Game yollari korunarak alinir (VaultCache'ten ONCE: ayni paket iki yerde varsa alt proje - kullanici degisikligi olabilir - gecerli).
	# Config / Plugins / Source yalniz raporlanir: gereken eklenti ve ayarlar Simumav.uproject / Config'e elle (kodla) eklenir.
	if (Test-Path -LiteralPath $FabProje) {
		$anaEklenti = @()
		try { $anaEklenti = @((Get-Content -LiteralPath $uproject -Raw | ConvertFrom-Json).Plugins | ForEach-Object { $_.Name }) } catch { }
		foreach ($alt in (Get-ChildItem -LiteralPath $FabProje -Directory -ErrorAction SilentlyContinue)) {
			$up = Get-ChildItem -LiteralPath $alt.FullName -Filter *.uproject -File -ErrorAction SilentlyContinue | Select-Object -First 1
			if (-not $up) { continue }
			Bilgi "alt proje: $($alt.Name) ($($up.Name))"
			$ac = Join-Path $alt.FullName 'Content'
			if (Test-Path -LiteralPath $ac) { PaketKopyala $ac "alt:$($alt.Name)" }
			try {
				$j = Get-Content -LiteralPath $up.FullName -Raw | ConvertFrom-Json
				foreach ($pl in @($j.Plugins)) { if ($pl.Enabled -and -not ($anaEklenti -contains $pl.Name)) { Uyari "alt proje $($alt.Name) eklentisi projede yok: $($pl.Name)" } }
			} catch { Uyari "alt proje uproject okunamadi: $($up.FullName)" }
			$cfg = Join-Path $alt.FullName 'Config\DefaultEngine.ini'
			if (Test-Path -LiteralPath $cfg) {
				Select-String -LiteralPath $cfg -Pattern '^(r\.GPUSkin|SkeletalMesh\.|r\.SkinCache|r\.VirtualTextures|r\.RayTracing|r\.DynamicGlobalIllumination|r\.Lumen|\+?Profiles=|DefaultGraphicsRHI)' | ForEach-Object { Bilgi "  ayar ($($alt.Name)): $($_.Line.Trim())" }
			}
			foreach ($k in @('Plugins', 'Source')) { if (Test-Path -LiteralPath (Join-Path $alt.FullName $k)) { Uyari "alt proje $($alt.Name): $k klasoru var - otomatik eklenmez (kod / eklenti incelemesi gerekir)" } }
		}
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
	if ($script:builtDataSayi -gt 0) { Bilgi ("harita isik verisi (*_BuiltData) disarida birakildi: {0} dosya, {1:N0} MB (r.AllowStaticLighting=False)" -f $script:builtDataSayi, ($script:builtDataBayt / 1MB)) }
	else { Bilgi "harita isik verisi (*_BuiltData): projede yok (kopyalanmaz)" }
	$isler = @()
	$imzalar = @{}
	$lib = Join-Path $Vault 'FabLibrary'
	$droneAlindi = $false
	# Blender (yalniz .blend olarak inen urunler icin: arka planda GLB'ye donusturulur)
	# sira: BLENDER_EXE (vars) > kayit defteri (kaldirma kaydi InstallLocation, .blend dosya iliskisi, App Paths) > bilinen klasorler > PATH.
	# Not: Microsoft Store (WindowsApps) ve kullanici profiline (AppData\Local\Programs) kurulan Blender'i runner hesabi
	# (NETWORK SERVICE) cogu zaman okuyamaz - bu durumda bulunan ama erisilemeyen yol uyari olarak yazilir.
	$blender = $null
	$blenderAday = New-Object System.Collections.ArrayList
	if ($env:BLENDER_EXE) { [void]$blenderAday.Add($env:BLENDER_EXE) }
	foreach ($rk in @('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*', 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*', 'Registry::HKEY_USERS\*\Software\Microsoft\Windows\CurrentVersion\Uninstall\*')) {
		foreach ($k in (Get-ItemProperty -Path $rk -ErrorAction SilentlyContinue | Where-Object { "$($_.DisplayName)" -match '(?i)^blender' })) {
			if ($k.InstallLocation) { [void]$blenderAday.Add((Join-Path $k.InstallLocation 'blender.exe')) }
			if ($k.DisplayIcon) { [void]$blenderAday.Add(("$($k.DisplayIcon)" -replace '^"|",?\d*$|,\d+$', '')) }
		}
	}
	foreach ($rk in @('Registry::HKEY_CLASSES_ROOT\blendfile\shell\open\command', 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\blender.exe')) {
		$v = (Get-ItemProperty -Path $rk -ErrorAction SilentlyContinue).'(default)'
		if ($v -and ("$v" -match '"?([^"]*blender[^"]*\.exe)"?')) { [void]$blenderAday.Add($Matches[1]) }
	}
	foreach ($a in $blenderAday) {
		if ($a -and $a -like '*.exe') {
			if (Test-Path -LiteralPath $a -PathType Leaf) { $blender = $a; break }
			Uyari "Blender kaydi var ama runner hesabi dosyaya erisemiyor: $a (C:\Program Files altina 'tum kullanicilar' kurulumu ya da vars.BLENDER_EXE)"
		}
	}
	if (-not $blender) {
		foreach ($bk in @('C:\Program Files\Blender Foundation', 'C:\Program Files (x86)\Blender Foundation', 'D:\Program Files\Blender Foundation', 'C:\Blender', 'D:\Blender', 'C:\claude', 'C:\Program Files\Steam\steamapps\common', 'C:\Program Files (x86)\Steam\steamapps\common')) {
			$bf = Get-ChildItem -Path $bk -Filter blender.exe -File -Recurse -Depth 3 -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1
			if ($bf) { $blender = $bf.FullName; break }
		}
	}
	if (-not $blender) {
		foreach ($u in (Get-ChildItem -Path 'C:\Users' -Directory -ErrorAction SilentlyContinue)) {
			$bf = Get-ChildItem -Path (Join-Path $u.FullName 'AppData\Local\Programs') -Filter blender.exe -File -Recurse -Depth 3 -ErrorAction SilentlyContinue | Select-Object -First 1
			if ($bf) { $blender = $bf.FullName; break }
		}
	}
	if (-not $blender) { $bc = Get-Command blender.exe -ErrorAction SilentlyContinue; if ($bc) { $blender = $bc.Source } }
	if (-not $blender -and (Test-Path -LiteralPath 'C:\Program Files\WindowsApps' -ErrorAction SilentlyContinue)) {
		Bilgi "Blender Microsoft Store'dan kurulduysa runner hesabi calistiramaz: blender.org MSI (tum kullanicilar) kurun ya da vars.BLENDER_EXE verin"
	}
	Bilgi "Blender: $(if ($blender) { $blender } else { 'yok (.blend urunleri atlanir)' })"
	if ($blender) { & $blender --version 2>&1 | Select-Object -First 1 | ForEach-Object { Bilgi "Blender surumu: $_" } }
	$donusum = Join-Path $Proje 'Intermediate\FabModel'
	function EnBuyuk([string]$kok, [string[]]$uzantilar) {
		return Get-ChildItem -LiteralPath $kok -File -Recurse -ErrorAction SilentlyContinue | Where-Object { $uzantilar -contains $_.Extension.ToLower() } | Sort-Object Length -Descending | Select-Object -First 1
	}
	function ModelBul([string]$kok, [string]$temizAd, [int]$derinlik = 0) {
		# oncelik: FBX > OBJ > GLB/glTF > .blend (Blender ile GLB'ye) > .zip (acilip yeniden aranir)
		$f = EnBuyuk $kok @('.fbx'); if ($f) { return $f }
		$f = EnBuyuk $kok @('.obj'); if ($f) { return $f }
		$f = EnBuyuk $kok @('.glb', '.gltf'); if ($f) { return $f }
		$b = EnBuyuk $kok @('.blend')
		if ($b) {
			if (-not $blender) { Uyari "yalniz .blend var, Blender yok: $($b.FullName)"; return $null }
			$cikti = Join-Path $donusum "$temizAd.glb"
			$imzaB = "$($b.FullName)|$($b.Length)|$($b.LastWriteTimeUtc.Ticks)"
			$imzaBDosya = "$cikti.kaynak"
			if ((Test-Path -LiteralPath $cikti) -and (Test-Path -LiteralPath $imzaBDosya) -and ((Get-Content -LiteralPath $imzaBDosya -Raw).Trim() -eq $imzaB)) { return (Get-Item -LiteralPath $cikti) }
			New-Item -ItemType Directory -Force -Path $donusum | Out-Null
			$py = Join-Path $donusum 'blend2glb.py'
			$pyMetin = @(
				'import bpy, sys',
				'out = sys.argv[sys.argv.index("--") + 1]',
				'try:',
				'    bpy.ops.file.find_missing_files(directory=bpy.path.abspath("//"))',
				'except Exception as e:',
				'    print("blend2glb: find_missing_files", e)',
				'n = len([o for o in bpy.data.objects if o.type == "MESH"])',
				'print("blend2glb: mesh nesnesi", n)',
				'try:',
				'    bpy.ops.object.mode_set(mode="OBJECT")',
				'except Exception:',
				'    pass',
				'try:',
				'    ms = [o for o in bpy.context.view_layer.objects if o.type in ("MESH", "CURVE", "SURFACE", "META", "FONT") and o.visible_get()]',
				'    bpy.ops.object.select_all(action="DESELECT")',
				'    for o in ms: o.select_set(True)',
				'    if ms:',
				'        bpy.context.view_layer.objects.active = ms[0]',
				'        bpy.ops.object.convert(target="MESH")',
				'        bpy.ops.object.join()',
				'        print("blend2glb: tek aga birlestirildi", len(ms))',
				'except Exception as e:',
				'    print("blend2glb: birlestirme yapilamadi", e)',
				'bpy.ops.export_scene.gltf(filepath=out, export_format="GLB", use_visible=True, export_apply=True, export_yup=True, export_animations=False)',
				'print("blend2glb: tamam", out)'
			)
			Set-Content -LiteralPath $py -Value $pyMetin -Encoding ASCII
			Bilgi "Blender donusumu: $($b.FullName) -> $cikti"
			Remove-Item -LiteralPath $cikti -ErrorAction SilentlyContinue
			& $blender --factory-startup -b $b.FullName --python $py -- $cikti 2>&1 | ForEach-Object { $l = "$_"; if ($l -match 'blend2glb|Error|Traceback') { Write-Host "    $l" } }
			if (Test-Path -LiteralPath $cikti) {
				Set-Content -LiteralPath $imzaBDosya -Value $imzaB -Encoding UTF8
				Bilgi "  GLB: $([math]::Round((Get-Item -LiteralPath $cikti).Length / 1KB)) KB"
				return (Get-Item -LiteralPath $cikti)
			}
			Uyari "Blender donusumu basarisiz: $($b.FullName)"; return $null
		}
		if ($derinlik -eq 0) {
			$z = EnBuyuk $kok @('.zip')
			if ($z) {
				$acik = Join-Path $donusum "zip_$temizAd"
				if (-not (Test-Path -LiteralPath $acik)) {
					Bilgi "zip aciliyor: $($z.FullName)"
					try { Expand-Archive -LiteralPath $z.FullName -DestinationPath $acik -Force } catch { Uyari "zip acilamadi: $($z.FullName) - $($_.Exception.Message)"; return $null }
				}
				return (ModelBul $acik $temizAd 1)
			}
		}
		return $null
	}
	$urunKlasorleri = @()
	if (Test-Path -LiteralPath $lib) { $urunKlasorleri += @(Get-ChildItem -LiteralPath $lib -Directory -ErrorAction SilentlyContinue) }
	foreach ($mk in $modelKlasorleri) { $urunKlasorleri += @(Get-Item -LiteralPath $mk) }
	if ($urunKlasorleri.Count -gt 0) {
		foreach ($u in $urunKlasorleri) {
			$temizOn = (($u.Name -replace '-[0-9a-f]{6,}$', '') -replace '[^A-Za-z0-9]', '')
			$dosya = ModelBul $u.FullName $temizOn
			if (-not $dosya) { continue }
			$urun = ($u.Name -replace '-[0-9a-f]{6,}$', '')
			$temiz = ($urun -replace '[^A-Za-z0-9]', '')
			$statik = $false
			# sahne dekoru (yalniz gorsel statik ag): kara araclari / hava savunma / anti-dron sistemleri — once bunlar ("Anti-Drone" dron degil)
			# gemi (korunacak nesne, yalniz gorsel): Interchange OBJ/glTF ice aktarimi parca basina ayri statik ag uretir; oyun klasordeki tum aglari birlikte kurar
			if ($urun -match '(?i)mogami|frigate|ship|destroyer|corvette') { $hedefAd = "Ships\$temiz"; $ad = "SM_$temiz"; $statik = $true }
			# us / tesis dekoru (kisla, radar, gozetleme kulesi, anten): statik
			elseif ($urun -match '(?i)barrack|radar|watch[\s_-]?tower|dish|antenna|command[\s_-]?center|radio[\s_-]?tower') { $hedefAd = "Base\$temiz"; $ad = "SM_$temiz"; $statik = $true }
			elseif ($urun -match '(?i)agras|drone[\s_-]?196|armou?red') { $hedefAd = "Props\$temiz"; $ad = "SM_$temiz"; $statik = $true }
			elseif ($urun -match '(?i)tank|patriot|mim-?104|sam_|_sam|anti-?drone|microwave|launcher|truck|vehicle') { $hedefAd = "Props\$temiz"; $ad = "SM_$temiz"; $statik = $true }
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
	# Tus / kumanda simge paketi (Joystix): FabLibrary urun klasorundeki PNG'ler ya da .unitypackage icindeki PNG'ler /Game/Fab/Joystix'e
	if (Test-Path -LiteralPath $lib) {
		foreach ($u in (Get-ChildItem -LiteralPath $lib -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '(?i)joystix|button_?overlay|button_?prompt|input_?prompt' })) {
			$dosyalar = @(Get-ChildItem -LiteralPath $u.FullName -Recurse -File -ErrorAction SilentlyContinue | Where-Object { ($_.Extension -eq '.png' -and $_.Name -ne 'thumbnail.png') -or $_.Extension -eq '.unitypackage' })
			$boyut = ($dosyalar | Measure-Object Length -Sum).Sum
			Bilgi "simge paketi: $($u.Name) - $($dosyalar.Count) dosya, $([math]::Round($boyut / 1KB)) KB"
			if ($boyut -lt 50KB) { Uyari "simge paketi bos ya da indirme eksik ($($u.Name)): Fab'den PNG / Unreal bicimini indirin"; continue }
			$hedefDisk = Join-Path $content 'Fab\Joystix'
			$imza = "$($u.FullName)|$($dosyalar.Count)|$boyut"
			$imzaDosya = Join-Path $hedefDisk '.kaynak-imza'
			if ((Test-Path -LiteralPath $imzaDosya) -and ((Get-Content -LiteralPath $imzaDosya -Raw).Trim() -eq $imza)) { Bilgi "  degismemis, ice aktarma atlandi"; continue }
			$isler += [pscustomobject]@{ tur = 'simge'; kaynak = $u.FullName; hedef = '/Game/Fab/Joystix' }
			$imzalar['/Game/Fab/Joystix'] = @($imzaDosya, $imza)
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
	# Yeni paketlerin varlik envanteri (yalniz gunluk, paket basina bir kez; "INCELE" satirlari): kalabalik / trafik / dron / gemi paketleri
	foreach ($pk in $paketler) {
		if ($pk -notmatch '(?i)crowd|traffic|hexa|drone|uav|mogami|frigate|ffm|agras|ship|vehicle|interceptor|bendylab') { continue }
		$damga = Join-Path $content ("Fab\.incele-" + ($pk -replace '[\\/ ]', '_'))
		if (Test-Path -LiteralPath $damga) { continue }
		$isler += [pscustomobject]@{ tur = 'incele'; hedef = "/Game/$pk"; sinir = 500 }
		$imzalar["/Game/$pk"] = @($damga, 'tamam')
	}
	# OverCrowd (motor eklentisi; Simumav.uproject'te etkin): icerik ve Python API envanteri bir kez
	$ocDamga = Join-Path $content 'Fab\.incele-OverCrowd-v1'
	if (((Get-Content -LiteralPath $uproject -Raw) -match '"OverCrowd"') -and -not (Test-Path -LiteralPath $ocDamga)) {
		$isler += [pscustomobject]@{ tur = 'incele'; hedef = '/OverCrowd'; sinir = 600; api = @('OverCrowd', 'OverNiagara', 'OverVat', 'VertexAnim', 'Vat') }
		$imzalar['/OverCrowd'] = @($ocDamga, 'tamam')
	}
	# OverCrowd VAT pisirme (kalabalik): UE5 Manny/Quinn + yurume/bekleme -> /Game/Fab/Crowd (surum damgasi basina bir kez; sonuc gunlukte "VAT").
	# v3: klip kare araliklari (start/end) atanir; pisirme kaynagi ve klip alanlari gunluge (malzeme kodlamasi icin)
	# v4: yerinde yurume + 3 sn bekleme @ 20 Hz, kose dizini UV1, VAT malzemesi (M_SmvCrowdVat) ve ag basina ornekler
	$vatDamga = Join-Path $content 'Fab\.vat-v4'
	if (((Get-Content -LiteralPath $uproject -Raw) -match '"OverCrowd"') -and (Test-Path -LiteralPath (Join-Path $content 'AT_Drone')) -and -not (Test-Path -LiteralPath $vatDamga)) {
		$isler += [pscustomobject]@{ tur = 'vat'; hedef = '/Game/Fab/Crowd'; agler = @('/Game/AT_Drone/Demo/Character/Mannequins/Meshes/SKM_Manny_Simple', '/Game/AT_Drone/Demo/Character/Mannequins/Meshes/SKM_Quinn_Simple') }
		$imzalar['/Game/Fab/Crowd'] = @($vatDamga, 'tamam')
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
			# VAT pisirme cizim aygiti ister (iskeletli ag -> statik ag): bu durumda NullRHI yerine ekransiz cizim
			$rhi = if (@($isler | Where-Object { $_.tur -eq 'vat' }).Count -gt 0) { '-RenderOffScreen' } else { '-NullRHI' }
			& $editor $uproject "-ExecutePythonScript=$betik" -unattended -nop4 -nosplash $rhi -stdout -FullStdOutLogOutput @ddc 2>&1 | ForEach-Object {
				$l = "$_"
				if ($l -match 'fab_import|OverCrowd|LogInterchange.*(Error|Warning)|LogFbx.*(Error|Warning)') { Write-Host "    $l" }
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
	# manzara (yalniz gorsel): agac / kaya / ot / uzak dag / ucan arac / ic mekan malzemesi klasorleri
	# (Source/Simumav/Private/SmvScenery.cpp MANZARA-KLASORLERI; oyun bunlari yumusak yolla yukler)
	$scn = Join-Path $Proje 'Source\Simumav\Private\SmvScenery.cpp'
	$scnMetin = if (Test-Path -LiteralPath $scn) { Get-Content -LiteralPath $scn -Raw -Encoding UTF8 } else { '' }
	if ($scnMetin -match "(?s)// <MANZARA-KLASORLERI>(.*?)// </MANZARA-KLASORLERI>") {
		foreach ($y in [regex]::Matches($Matches[1], '"(/Game/[^"]+)"')) {
			$g = $y.Groups[1].Value
			if (Test-Path -LiteralPath (DiskYolu $g)) { $satirlar += "+DirectoriesToAlwaysCook=(Path=`"$($g.Substring(6))`")"; Bilgi "manzara klasoru: $g" } else { Bilgi "manzara klasoru yok (paket inmemis): $g" }
		}
	}
	# sehir hayati (trafik araclari, yayalar) ve yeni dron govdeleri (Source/Simumav/Private/SmvCityLife.cpp PISIR-KLASORLERI)
	$clf = Join-Path $Proje 'Source\Simumav\Private\SmvCityLife.cpp'
	$clMetin = if (Test-Path -LiteralPath $clf) { Get-Content -LiteralPath $clf -Raw -Encoding UTF8 } else { '' }
	if ($clMetin -match "(?s)// <PISIR-KLASORLERI>(.*?)// </PISIR-KLASORLERI>") {
		foreach ($y in [regex]::Matches($Matches[1], '"(/Game/[^"]+)"')) {
			$g = $y.Groups[1].Value
			if (Test-Path -LiteralPath (DiskYolu $g)) { $satirlar += "+DirectoriesToAlwaysCook=(Path=`"$($g.Substring(6))`")"; Bilgi "pisirilecek klasor: $g" } else { Bilgi "klasor yok (paket inmemis): $g" }
		}
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
