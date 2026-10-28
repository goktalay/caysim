# Simumav - son derlenen oyunu (runner diskindeki paket) bir klasore kopyalar; kurulum gerekmez.
# Kullanim (normal PowerShell yeterli):
#   D:\Simumav\oyun-kopyala.ps1                              -> Shipping, D:\Simumav\Oyun
#   D:\Simumav\oyun-kopyala.ps1 -Surum Development           -> gunluk / hata ayiklama acik surum, D:\Simumav\Oyun-Dev
#   D:\Simumav\oyun-kopyala.ps1 -Hedef E:\Oyunlar\Simumav    -> baska klasor
# Hedef her seferinde pakete esitlenir (robocopy /MIR); oyunun kendi kayitlari (Saved: bolge haritalari, ayarlar, gunlukler) korunur.
# Masaustune "Simumav" (Development icin "Simumav (Dev)") kisayolu olusturur; -KisayolYok ile atlanir.
param([ValidateSet('Shipping', 'Development')][string]$Surum = 'Shipping', [string]$Hedef = '', [switch]$KisayolYok)
$ErrorActionPreference = 'Stop'
if (-not $Hedef) { $Hedef = if ($Surum -eq 'Shipping') { 'D:\Simumav\Oyun' } else { 'D:\Simumav\Oyun-Dev' } }
$kaynak = "C:\claude\actions-runner\_work\caysim\caysim\Paketler\$Surum\Windows"
if (-not (Test-Path -LiteralPath (Join-Path $kaynak 'Simumav.exe'))) { throw "Paket bulunamadi: $kaynak (once derleme calismali)" }
if (Get-Process -Name 'Runner.Worker' -ErrorAction SilentlyContinue) {
	Write-Host 'UYARI: su anda bir derleme / test calisiyor; paket yazilmakta olabilir.' -ForegroundColor Yellow
	if ((Read-Host 'Yine de kopyalansin mi? (e/h)') -ne 'e') { return }
}
if (Get-Process -Name 'Simumav*' -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($Hedef, [StringComparison]::OrdinalIgnoreCase) }) { throw "Hedefteki oyun acik; once kapatin: $Hedef" }
$exe = Get-Item -LiteralPath (Join-Path $kaynak 'Simumav.exe')
$boyut = (Get-ChildItem -LiteralPath $kaynak -Recurse -File | Measure-Object Length -Sum).Sum
Write-Host ("Kaynak: {0} ({1}, {2:N1} GB, derleme {3})" -f $kaynak, $Surum, ($boyut / 1GB), $exe.LastWriteTime)
Write-Host ("Hedef:  {0}" -f $Hedef)
New-Item -ItemType Directory -Force -Path $Hedef | Out-Null
# Saved (Development'ta paket icinde: bolge haritalari, ayarlar, gunlukler) korunur
& robocopy.exe $kaynak $Hedef /MIR /XD "$Hedef\Simumav\Saved" Saved /R:2 /W:2 /MT:16 /NP /NFL /NDL /NJH | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy hata kodu $LASTEXITCODE" }
if (-not $KisayolYok) {
	$ad = if ($Surum -eq 'Shipping') { 'Simumav' } else { 'Simumav (Dev)' }
	$lnk = Join-Path ([Environment]::GetFolderPath('Desktop')) "$ad.lnk"
	$s = (New-Object -ComObject WScript.Shell).CreateShortcut($lnk)
	$s.TargetPath = Join-Path $Hedef 'Simumav.exe'
	$s.WorkingDirectory = $Hedef
	$s.Save()
	Write-Host "Kisayol: $lnk"
}
Write-Host ("Tamam: {0}\Simumav.exe" -f $Hedef) -ForegroundColor Green
