# Simumav CI - UBT/UAT kilit tekrari. Sirket bilgisayarinda etkilesimli bir Unreal Editor ya da UBT ayni motoru kullanirken
# (baska kullanici oturumu) Global\UnrealBuildTool_Mutex / Global\LiveCoding mutex'i NETWORK SERVICE icin "Access denied" verir ve
# -WaitMutex beklemeden dusulur. Bu durumda komut 60 sn arayla yeniden denenir; baska hatalarda hemen doner.
# Kullanim (workflow run blogu):  . .\Tools\ci\kilit.ps1 ; $rc = Invoke-KilitTekrar { & "...\Build.bat" ... } ; if ($rc -ne 0) { throw ... }
function Invoke-KilitTekrar([scriptblock]$Komut, [int]$Deneme = 20) {
	$ErrorActionPreference = 'Continue'
	$rc = 1
	for ($i = 1; $i -le $Deneme; $i++) {
		$log = Join-Path $env:RUNNER_TEMP ('kilit-' + [guid]::NewGuid().ToString('N') + '.log')
		& $Komut 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $log | Out-Host
		$rc = $LASTEXITCODE
		if ($rc -eq 0) { return 0 }
		if (-not (Select-String -LiteralPath $log -Pattern 'UnauthorizedAccessException: Access to the path .Global\\' -Quiet)) { return $rc }
		Write-Host "::warning::UBT/LiveCoding mutex baska bir oturumda (etkilesimli editor ya da UBT acik); deneme $i/$Deneme - 60 sn sonra yeniden"
		Start-Sleep -Seconds 60
	}
	return $rc
}
