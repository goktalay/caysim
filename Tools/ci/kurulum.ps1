# Simumav - Windows kurulum dosyasi (Inno Setup 6). Paketlenmis oyunu (Paketler\<cfg>\Windows) tek bir SimumavSetup-<surum>.exe'ye
# cevirir; 2 GB'lik .bin parcalarina bolunur (DiskSpanning). Inno Setup yoksa 7.1.0 (SHA-256 sabit) C:\claude\tools\InnoSetup'a sessizce kurulur.
# Kurulum kullanici klasorune (yonetici gerekmez; istenirse "tum kullanicilar" secilebilir), Baslat menusu + masaustu kisayolu,
# Unreal on kosullari (DirectX / VC++; UEPrereqSetup_x64) ve kaldirici.
# Kullanim: .\Tools\ci\kurulum.ps1 -Paket <klasor> -Cikis <klasor> -Surum <metin>
param([string]$Paket, [string]$Cikis, [string]$Surum)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath (Join-Path $Paket 'Simumav.exe'))) { throw "Paket yok ya da Simumav.exe bulunamadi: $Paket" }

# ---- Inno Setup ----
$adaylar = @('C:\claude\tools\InnoSetup\ISCC.exe', "${env:ProgramFiles}\Inno Setup 7\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe", "$env:LOCALAPPDATA\Programs\Inno Setup 7\ISCC.exe", "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe")
$iscc = $adaylar | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
if (-not $iscc) {
	# sabit surum + SHA-256 (resmi GitHub surumu jrsoftware/issrc is-7_1_0) + Authenticode imzasi
	$hedef = 'C:\claude\tools\InnoSetup'
	$url = 'https://github.com/jrsoftware/issrc/releases/download/is-7_1_0/innosetup-7.1.0-x64.exe'
	$beklenen = '0362a383ed217d4c4239b5933866dd96d3eb2102737da92f80f6057a4b40df2f'
	$kur = Join-Path $env:TEMP 'innosetup-7.1.0-x64.exe'
	[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
	Write-Host "Inno Setup 7.1.0 indiriliyor -> $hedef"
	Invoke-WebRequest -Uri $url -OutFile $kur -UseBasicParsing
	$ozet = (Get-FileHash -LiteralPath $kur -Algorithm SHA256).Hash.ToLower()
	if ($ozet -ne $beklenen) { throw "Inno Setup SHA-256 uyusmuyor: $ozet" }
	$imza = Get-AuthenticodeSignature -FilePath $kur
	Write-Host ("Inno Setup SHA-256 dogru; imza: " + $imza.Status + " / " + $(if ($imza.SignerCertificate) { $imza.SignerCertificate.Subject } else { '-' }))
	$p = Start-Process -FilePath $kur -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER', "/DIR=$hedef", '/NOICONS') -Wait -PassThru
	Write-Host ("Inno Setup kurulumu cikis kodu: " + $p.ExitCode)
	$iscc = Join-Path $hedef 'ISCC.exe'
	if (-not (Test-Path -LiteralPath $iscc)) { throw "ISCC.exe kurulamadi" }
}
Write-Host "ISCC: $iscc"

# ---- betik ----
New-Item -ItemType Directory -Force -Path $Cikis | Out-Null
$ikon = Join-Path $PSScriptRoot '..\kurulum\simumav.ico'
$iss = Join-Path $env:TEMP 'simumav-kurulum.iss'
$turkce = Test-Path -LiteralPath (Join-Path (Split-Path $iscc) 'Languages\Turkish.isl')
$diller = '[Languages]' + "`r`n" + 'Name: "en"; MessagesFile: "compiler:Default.isl"'
if ($turkce) { $diller = '[Languages]' + "`r`n" + 'Name: "tr"; MessagesFile: "compiler:Languages\Turkish.isl"' + "`r`n" + 'Name: "en"; MessagesFile: "compiler:Default.isl"' }
$metin = @"
[Setup]
AppId={{6C1F5E2A-8B47-4D2E-9C1B-53A0D7E4B915}
AppName=Simumav
AppVersion=$Surum
AppVerName=Simumav $Surum
AppPublisher=Simumav
AppPublisherURL=https://simumav.com
DefaultDirName={autopf}\Simumav
DefaultGroupName=Simumav
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=$Cikis
OutputBaseFilename=SimumavSetup-$Surum
SetupIconFile=$ikon
UninstallDisplayIcon={app}\Simumav.exe
Compression=lzma2/fast
SolidCompression=no
DiskSpanning=yes
DiskSliceSize=2100000000
WizardStyle=modern
ExtraDiskSpaceRequired=1073741824

$diller

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "$Paket\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Simumav"; Filename: "{app}\Simumav.exe"
Name: "{group}\{cm:UninstallProgram,Simumav}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Simumav"; Filename: "{app}\Simumav.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Engine\Extras\Redist\en-us\UEPrereqSetup_x64.exe"; Parameters: "/quiet /norestart"; StatusMsg: "DirectX / Visual C++"; Flags: waituntilterminated skipifdoesntexist
Filename: "{app}\Simumav.exe"; Description: "{cm:LaunchProgram,Simumav}"; Flags: nowait postinstall skipifsilent
"@
Set-Content -LiteralPath $iss -Value $metin -Encoding UTF8
$boyut = (Get-ChildItem -LiteralPath $Paket -Recurse -File | Measure-Object Length -Sum).Sum
Write-Host ("Paket: {0} ({1:N2} GB), cikis: {2}, surum {3}" -f $Paket, ($boyut / 1GB), $Cikis, $Surum)
$t0 = Get-Date
& $iscc '/Q' $iss
if ($LASTEXITCODE -ne 0) { throw "ISCC cikis kodu $LASTEXITCODE" }
Write-Host ("Kurulum hazir ({0:N0} dk):" -f ((Get-Date) - $t0).TotalMinutes)
Get-ChildItem -LiteralPath $Cikis -File | Where-Object { $_.Name -like "SimumavSetup-$Surum*" } | ForEach-Object { Write-Host ("  {0}  {1:N2} GB" -f $_.FullName, ($_.Length / 1GB)) }
