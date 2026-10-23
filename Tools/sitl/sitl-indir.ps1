# Simumav — yerel SITL ikililerini sabit sürüm + sha256 doğrulamasıyla indirir (CI ve geliştirici makinesi).
# Kullanım:  pwsh -File Tools/sitl/sitl-indir.ps1 [-Hedef ThirdParty/ArduPilotSITL/Win64] [-BaseUrl <ayna>]
param(
    [string]$Hedef = (Join-Path $PSScriptRoot "..\..\ThirdParty\ArduPilotSITL\Win64"),
    [string]$BaseUrl = ""
)
# Windows PowerShell 5.1 uyumu: TLS 1.2 zorunlu (varsayılan TLS 1.0 olabilir), ilerleme çubuğu indirmeyi çok yavaşlatır
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
$ProgressPreference = "SilentlyContinue"
$ErrorActionPreference = "Stop"
$manifest = Get-Content -Raw -Encoding UTF8 (Join-Path $PSScriptRoot "sitl-manifest.json") | ConvertFrom-Json
if (-not $BaseUrl) { $BaseUrl = $manifest.base_url }
New-Item -ItemType Directory -Force -Path $Hedef | Out-Null
$gecici = Join-Path ([System.IO.Path]::GetTempPath()) ("smv-sitl-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $gecici | Out-Null
try {
    foreach ($d in $manifest.dosyalar) {
        $son = Join-Path $Hedef $d.hedef
        if (Test-Path $son) {
            $h = (Get-FileHash -Algorithm SHA256 $son).Hash.ToLower()
            if ($h -eq $d.sha256) { Write-Host "hazir  $($d.hedef)"; continue }
        }
        $tmp = Join-Path $gecici $d.ad
        Write-Host "indir  $($d.ad)"
        Invoke-WebRequest -Uri ($BaseUrl + $d.ad) -OutFile $tmp -UseBasicParsing
        $h = (Get-FileHash -Algorithm SHA256 $tmp).Hash.ToLower()
        if ($h -ne $d.sha256) {
            throw "SHA256 UYUSMAZ: $($d.ad) beklenen $($d.sha256) gelen $h — kaynak güncellenmiş olabilir; Tools/sitl/sitl-manifest.json'u bilinçli olarak güncelleyin ya da aynayı kullanın (-BaseUrl)."
        }
        Move-Item -Force $tmp $son
    }
    Copy-Item -Force (Join-Path $PSScriptRoot "..\..\ThirdParty\ArduPilotSITL\LISANS.md") (Join-Path $Hedef "LISANS-ArduPilot-SITL.md")
    Write-Host "SITL $($manifest.surum) hazır: $Hedef"
}
finally {
    Remove-Item -Recurse -Force $gecici -ErrorAction SilentlyContinue
}
