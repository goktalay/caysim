# Simumav - C: diskindeki buyuk klasorleri D:\Simumav'a tasir; eski yerlerine baglanti (junction) birakir, boylece Epic Launcher,
# Unreal, GitHub runner ve oyun eski yollari kullanmaya devam eder (ayar degismez).
# YONETICI PowerShell'de calistirin. Once prova (yalniz plan): .\d-tasima.ps1     Uygulamak icin: .\d-tasima.ps1 -Uygula
# Calistirmadan once: Epic Games Launcher, Unreal Editor ve Simumav kapali olsun. Runner hizmeti betik tarafindan durdurulup baslatilir.
param([switch]$Uygula, [string]$Hedef = 'D:\Simumav')
$ErrorActionPreference = 'Stop'
$ben = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $ben.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Yonetici PowerShell gerekli (sag tik > Yonetici olarak calistir)' }
$kalemler = @(
	@{ Ad = 'GitHub runner calisma alani'; Kaynak = 'C:\claude\actions-runner\_work'; Yer = 'actions-runner-work' },
	@{ Ad = 'Unreal Engine 5.8'; Kaynak = 'C:\claude\epic'; Yer = 'epic' },
	@{ Ad = 'SimumavFab projesi'; Kaynak = 'C:\claude\SimumavFab'; Yer = 'SimumavFab' },
	@{ Ad = 'Epic VaultCache (Fab onbellegi)'; Kaynak = 'C:\ProgramData\Epic\EpicGamesLauncher\VaultCache'; Yer = 'VaultCache' },
	@{ Ad = 'Derleme onbellegi (runner / NETWORK SERVICE)'; Kaynak = 'C:\Windows\ServiceProfiles\NetworkService\AppData\Local\UnrealEngine'; Yer = 'NetworkService-UnrealEngine' },
	@{ Ad = 'Kurulum dosyalari'; Kaynak = 'C:\claude\Kurulum'; Yer = 'Kurulum' },
	@{ Ad = 'Simumav-dev (oyun)'; Kaynak = 'C:\claude\Simumav-dev'; Yer = 'Simumav-dev' },
	@{ Ad = 'Simumav-deneme (oyun)'; Kaynak = 'C:\claude\Simumav-deneme'; Yer = 'Simumav-deneme' }
)
function Boyut([string]$p) { $s = (Get-ChildItem -LiteralPath $p -Recurse -File -Force -ErrorAction SilentlyContinue | Measure-Object Length -Sum); return @([int64]$s.Sum, [int64]$s.Count) }
function BaglantiMi([string]$p) { $i = Get-Item -LiteralPath $p -Force -ErrorAction SilentlyContinue; return $i -and ($i.Attributes -band [IO.FileAttributes]::ReparsePoint) }

# ---- plan ----
$toplam = [int64]0; $plan = @()
foreach ($k in $kalemler) {
	$hedefYol = Join-Path $Hedef $k.Yer
	if (-not (Test-Path -LiteralPath $k.Kaynak)) { Write-Host ("  [yok]        {0}  ({1})" -f $k.Kaynak, $k.Ad); continue }
	if (BaglantiMi $k.Kaynak) { Write-Host ("  [tasinmis]   {0} -> {1}" -f $k.Kaynak, (Get-Item -LiteralPath $k.Kaynak -Force).Target); continue }
	$b = Boyut $k.Kaynak
	$toplam += $b[0]
	$plan += @{ Ad = $k.Ad; Kaynak = $k.Kaynak; HedefYol = $hedefYol; Bayt = $b[0]; Sayi = $b[1] }
	Write-Host ("  [tasinacak]  {0,8:N1} GB  {1,9} dosya  {2} -> {3}" -f ($b[0] / 1GB), $b[1], $k.Kaynak, $hedefYol)
}
$d = Get-PSDrive -Name ($Hedef.Substring(0, 1))
Write-Host ("Toplam {0:N1} GB; {1}: bos {2:N1} GB" -f ($toplam / 1GB), $d.Name, ($d.Free / 1GB))
if ($toplam -gt $d.Free * 0.95) { throw 'Hedef surucude yeterli yer yok' }
if (-not $Uygula) { Write-Host 'PROVA: hicbir sey degismedi. Uygulamak icin: .\d-tasima.ps1 -Uygula'; return }

# ---- on kosullar ----
$acik = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -match '^(EpicGamesLauncher|UnrealEditor.*|Simumav.*|ShaderCompileWorker|UnrealPak)$' }
if ($acik) { $acik | ForEach-Object { Write-Host ("  acik: {0} (pid {1})" -f $_.ProcessName, $_.Id) }; throw 'Once Epic Launcher / Unreal / Simumav kapatilmali' }
$hizmet = Get-Service -Name 'actions.runner.*' -ErrorAction SilentlyContinue | Select-Object -First 1
if ($hizmet) {
	if ($hizmet.Status -eq 'Running') { Write-Host ("Runner hizmeti durduruluyor: " + $hizmet.Name); Stop-Service -Name $hizmet.Name -Force; Start-Sleep -Seconds 5 }
}
# derleme onbellegi sunucusu (Zen) is bittikten sonra da acik kalabilir: onbellek klasoru tasinacagi icin kapatilir
Get-Process -Name 'zenserver' -ErrorAction SilentlyContinue | ForEach-Object { Write-Host ("Zen sunucusu kapatiliyor (pid " + $_.Id + ")"); Stop-Process -Id $_.Id -Force }
Start-Sleep -Seconds 2
Get-Process -Name 'Runner.Listener', 'Runner.Worker' -ErrorAction SilentlyContinue | ForEach-Object { throw ("Runner hala calisiyor (" + $_.ProcessName + ") - calisan bir is varsa bitmesini bekleyin") }
New-Item -ItemType Directory -Force -Path $Hedef | Out-Null
$gunluk = Join-Path $Hedef ('tasima-' + (Get-Date -Format 'yyyyMMdd-HHmm') + '.log')

# ---- tasima ----
try {
	foreach ($p in $plan) {
		Write-Host ("== {0}: {1} -> {2} ({3:N1} GB)" -f $p.Ad, $p.Kaynak, $p.HedefYol, ($p.Bayt / 1GB))
		$t0 = Get-Date
		& robocopy.exe $p.Kaynak $p.HedefYol /E /COPYALL /DCOPY:DAT /XJ /R:2 /W:2 /MT:16 /NP /NFL /NDL "/UNILOG+:$gunluk" | Out-Null
		$rc = $LASTEXITCODE
		if ($rc -ge 8) { throw ("robocopy hata kodu $rc - gunluk: $gunluk (kaynak yerinde, degismedi)") }
		$b = Boyut $p.HedefYol
		if ($b[1] -lt $p.Sayi -or $b[0] -lt $p.Bayt) { throw ("dogrulama tutmadi: hedef {0} dosya / {1:N1} GB, kaynak {2} / {3:N1} GB (kaynak yerinde)" -f $b[1], ($b[0] / 1GB), $p.Sayi, ($p.Bayt / 1GB)) }
		Write-Host ("   kopyalandi ve dogrulandi ({0:N0} dk); kaynak siliniyor, baglanti kuruluyor" -f ((Get-Date) - $t0).TotalMinutes)
		& cmd.exe /c rmdir /s /q "`"$($p.Kaynak)`""
		if (Test-Path -LiteralPath $p.Kaynak) { throw ("kaynak silinemedi: " + $p.Kaynak + " (hedefte tam kopyasi var; elle silip su komutu calistirin: New-Item -ItemType Junction -Path '" + $p.Kaynak + "' -Target '" + $p.HedefYol + "')") }
		New-Item -ItemType Junction -Path $p.Kaynak -Target $p.HedefYol | Out-Null
		Write-Host ("   tamam: {0} -> {1}" -f $p.Kaynak, $p.HedefYol)
	}
}
finally {
	if ($hizmet) { Write-Host ("Runner hizmeti baslatiliyor: " + $hizmet.Name); Start-Service -Name $hizmet.Name }
}
$c = Get-PSDrive -Name C
Write-Host ("Bitti. C: bos {0:N1} GB. Gunluk: {1}" -f ($c.Free / 1GB), $gunluk)
