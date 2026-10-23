# Simumav — Steam'e yükleme (steamcmd). Oturum: STEAM_USERNAME + STEAM_CONFIG_VDF (base64 config.vdf; Steam Guard tek seferlik
# girişten sonra alınır — docs/steam-yayin.md). Şifre ve oturum dosyası günlüğe yazılmaz.
param(
    [Parameter(Mandatory = $true)][string]$AppId,
    [Parameter(Mandatory = $true)][string]$DepotId,
    [Parameter(Mandatory = $true)][string]$Content,
    [string]$Branch = "beta",
    [string]$Desc = "Simumav"
)
# Windows PowerShell 5.1 uyumu: TLS 1.2 zorunlu (varsayılan TLS 1.0 olabilir), ilerleme çubuğu indirmeyi çok yavaşlatır
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
$ProgressPreference = "SilentlyContinue"
$ErrorActionPreference = "Stop"
$kok = Join-Path $env:RUNNER_TEMP "steamcmd"
if (-not (Test-Path "$kok\steamcmd.exe")) {
    New-Item -ItemType Directory -Force -Path $kok | Out-Null
    Invoke-WebRequest -Uri "https://steamcdn-a.akamaihd.net/client/installer/steamcmd.zip" -OutFile "$kok\steamcmd.zip" -UseBasicParsing
    Expand-Archive -Force "$kok\steamcmd.zip" $kok
}
New-Item -ItemType Directory -Force -Path "$kok\config" | Out-Null
[IO.File]::WriteAllBytes("$kok\config\config.vdf", [Convert]::FromBase64String($env:STEAM_CONFIG_VDF))
$build = Join-Path $env:RUNNER_TEMP "steam-build"
New-Item -ItemType Directory -Force -Path $build | Out-Null
$appVdf = @"
"AppBuild"
{
    "AppID" "$AppId"
    "Desc" "$Desc"
    "BuildOutput" "$build\output"
    "ContentRoot" "$Content"
    "SetLive" "$Branch"
    "Depots"
    {
        "$DepotId"
        {
            "FileMapping"
            {
                "LocalPath" "*"
                "DepotPath" "."
                "recursive" "1"
            }
            "FileExclusion" "*.pdb"
        }
    }
}
"@
Set-Content -Encoding UTF8 -Path "$build\app_build.vdf" -Value $appVdf
& "$kok\steamcmd.exe" +login $env:STEAM_USERNAME +run_app_build "$build\app_build.vdf" +quit
if ($LASTEXITCODE -ne 0) { throw "steamcmd başarısız ($LASTEXITCODE)" }
Remove-Item -Force "$kok\config\config.vdf"
Write-Host "Steam yükleme tamam: app $AppId depot $DepotId dal '$Branch'"
