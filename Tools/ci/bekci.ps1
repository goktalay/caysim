# Simumav CI - pisirme / paketleme ilerleme bekcisi.
# Neden: 9cada62 derlemesinde (run 40160928646) "Paketle - Development" pisirmesi 8732/9938 pakette, US_Military Showreel_Scene
# haritasinin 541 lightmap dokusu ayni anda insa edilirken (tahmini ~95 GB; bilgisayarda bos sanal bellek < 1 GB) sustu ve
# 3 saat hic satir gelmedi; is 12 saat sinirina kadar bekleyecekti. Kalici onlem fab-hazirla.ps1'de (*_BuiltData kopyalanmaz);
# bu bekci her durumda ikinci savunma hattidir:
#   - komut ayri surecte calisir, ciktisi dosyaya yazilir ve satir satir gunluge aktarilir;
#   - $SessizDk dakika boyunca HIC yeni satir gelmezse: bellek ozeti + en buyuk surecler + Unreal surecleri dokulur,
#     surec agaci (UAT -> dotnet -> UnrealEditor-Cmd / ShaderCompileWorker ...) sonlandirilir;
#   - bir kez yeniden denenir: doku derleme eszamanliligi sinirlanir (Editor.AsyncTextureCompilationMaxConcurrency) ve
#     pisirici daha erken cop toplar (CookSettings.MemoryMinFreePhysical/Virtual). Ikinci kez de susarsa adim duser.
# Ayrica UBT/LiveCoding mutex "Access denied" durumunda (kilit.ps1 ile ayni kural) 60 sn arayla yeniden denenir.
# Kullanim (workflow run blogu):
#   . .\Tools\ci\bekci.ps1
#   $rc = Invoke-Bekcili -Ad 'Development' -Bat "$env:UE_ROOT\Engine\Build\BatchFiles\RunUAT.bat" -Arglar @('BuildCookRun', ...) -EkCook "$env:UE_ZEN_ARGS"
#   if ($rc -ne 0) { throw ... }
# -EkCook: -AdditionalCookerOptions degeri (yeniden denemede sinirlama secenekleri buna eklenir).

function Write-BekciDokum([string]$Neden) {
	Write-Host "::group::Bekci dokumu - $Neden"
	try {
		$os = Get-CimInstance Win32_OperatingSystem
		Write-Host ("Fiziksel bellek: toplam {0:N1} GB, bos {1:N1} GB | Sanal (commit): toplam {2:N1} GB, bos {3:N1} GB" -f `
			($os.TotalVisibleMemorySize / 1MB), ($os.FreePhysicalMemory / 1MB), ($os.TotalVirtualMemorySize / 1MB), ($os.FreeVirtualMemory / 1MB))
		Get-CimInstance Win32_PageFileUsage -ErrorAction SilentlyContinue | ForEach-Object { Write-Host ("Sayfa dosyasi {0}: {1} MB, kullanilan {2} MB, tepe {3} MB" -f $_.Name, $_.AllocatedBaseSize, $_.CurrentUsage, $_.PeakUsage) }
	} catch { Write-Host "bellek bilgisi alinamadi: $_" }
	Write-Host "En cok ozel bellek kullanan 15 surec:"
	Get-Process -ErrorAction SilentlyContinue | Sort-Object PrivateMemorySize64 -Descending | Select-Object -First 15 | ForEach-Object {
		Write-Host ("  {0,-28} pid {1,6}  ozel {2,7:N0} MB  calisma {3,7:N0} MB  cpu {4,8:N0} sn  is parcacigi {5}" -f $_.ProcessName, $_.Id, ($_.PrivateMemorySize64 / 1MB), ($_.WorkingSet64 / 1MB), $_.CPU, $_.Threads.Count)
	}
	Write-Host "Unreal / derleme surecleri:"
	Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -match '(?i)^(UnrealEditor|ShaderCompileWorker|UnrealPak|zenserver|dotnet|AutomationTool|UnrealBuildTool|Interchange|TextureBuildWorker|cmd)' } | ForEach-Object {
		Write-Host ("  {0,-28} pid {1,6}  ozel {2,7:N0} MB  cpu {3,8:N0} sn  baslama {4}" -f $_.ProcessName, $_.Id, ($_.PrivateMemorySize64 / 1MB), $_.CPU, $_.StartTime)
	}
	Write-Host "::endgroup::"
}

function Stop-BekciAgac([int]$KokPid) {
	# taskkill /T tum alt surecleri (UAT -> dotnet -> UnrealEditor-Cmd -> ShaderCompileWorker) sonlandirir; ust surec once
	# olduyse kalan cocuklar komut satirindaki proje yoluyla bulunur. Zen sunucusu (DDC) dokunulmaz: sonraki deneme kullanir.
	& taskkill.exe /PID $KokPid /T /F 2>&1 | Select-Object -First 20 | ForEach-Object { Write-Host "  $_" }
	$proje = (Get-Location).Path
	Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
		$_.Name -match '(?i)^(UnrealEditor-Cmd|UnrealEditor|ShaderCompileWorker|UnrealPak|dotnet)\.exe$' -and "$($_.CommandLine)" -like "*$proje*"
	} | ForEach-Object { Write-Host "  kalan surec sonlandiriliyor: $($_.Name) pid $($_.ProcessId)"; Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
	Get-Process -Name ShaderCompileWorker -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
}

function Invoke-BekciTek([string]$Bat, [string[]]$Arglar, [int]$SessizDk, [string]$Ad) {
	# tek calistirma; donus: @{ rc; sessiz; mutex }
	$argMetin = ($Arglar | ForEach-Object { $a = "$_"; if ($a -match '[\s"]') { '"' + ($a -replace '"', '\"') + '"' } else { $a } }) -join ' '
	$kok = Join-Path $env:RUNNER_TEMP ('bekci-' + [guid]::NewGuid().ToString('N'))
	$out = "$kok.out.log"; $err = "$kok.err.log"
	New-Item -ItemType File -Force -Path $out, $err | Out-Null
	Write-Host "[bekci] $Ad baslatiliyor (sessizlik siniri $SessizDk dk)"
	$p = Start-Process -FilePath $Bat -ArgumentList $argMetin -NoNewWindow -PassThru -RedirectStandardOutput $out -RedirectStandardError $err
	$null = $p.Handle   # PS 5.1: ExitCode icin tutamac erken alinmali
	$okuyucular = @{}
	foreach ($f in @($out, $err)) {
		for ($d = 1; $d -le 10 -and -not $okuyucular[$f]; $d++) {
			try {
				$fs = [System.IO.File]::Open($f, 'Open', 'Read', 'ReadWrite,Delete')
				$okuyucular[$f] = New-Object System.IO.StreamReader($fs, [System.Text.Encoding]::UTF8)
			} catch { Start-Sleep -Milliseconds 500 }
		}
		if (-not $okuyucular[$f]) { throw "[bekci] gunluk dosyasi okunamiyor: $f" }
	}
	$son = Get-Date
	$sessiz = $false; $mutex = $false
	$sonSatir = ''
	while ($true) {
		$bitti = $p.HasExited
		$yeni = $false
		foreach ($f in @($out, $err)) {
			$r = $okuyucular[$f]
			while ($null -ne ($l = $r.ReadLine())) {
				Write-Host $l
				$yeni = $true; $sonSatir = $l
				if ($l -match 'UnauthorizedAccessException: Access to the path .Global\\') { $mutex = $true }
			}
		}
		if ($yeni) { $son = Get-Date }
		if ($bitti) { break }
		$dk = ((Get-Date) - $son).TotalMinutes
		if ($dk -ge $SessizDk) {
			Write-Host "::warning::[bekci] $Ad : $SessizDk dakikadir yeni gunluk satiri yok (son satir: $($sonSatir.Substring(0, [Math]::Min(200, $sonSatir.Length)))) - surec agaci sonlandiriliyor"
			Write-BekciDokum "$Ad sessiz $SessizDk dk"
			Stop-BekciAgac $p.Id
			Start-Sleep -Seconds 5
			$sessiz = $true
			break
		}
		Start-Sleep -Seconds 2
	}
	if (-not $sessiz) { $p.WaitForExit() }
	foreach ($r in $okuyucular.Values) { while ($null -ne ($l = $r.ReadLine())) { Write-Host $l }; $r.Close() }
	$rc = if ($sessiz) { 124 } else { $p.ExitCode }
	if ($null -eq $rc) { $rc = 1 }
	return @{ rc = [int]$rc; sessiz = $sessiz; mutex = $mutex }
}

function Invoke-Bekcili {
	param(
		[string]$Ad,
		[string]$Bat,
		[string[]]$Arglar,
		[string]$EkCook = '',
		[int]$SessizDk = $(if ($env:SMV_BEKCI_DK) { [int]$env:SMV_BEKCI_DK } else { 30 }),
		[int]$MutexDeneme = 20
	)
	$ErrorActionPreference = 'Continue'
	$sinirli = $false
	$mutexSay = 0
	while ($true) {
		$ek = $EkCook
		if ($sinirli) {
			# yeniden deneme: doku insasi en cok 4 es zamanli, pisirici 6 GB bos bellek altinda cop toplar
			$ek = ("$EkCook -ini:Engine:[ConsoleVariables]:Editor.AsyncTextureCompilationMaxConcurrency=4" +
				" -ini:Editor:[CookSettings]:MemoryMinFreePhysical=6144 -ini:Editor:[CookSettings]:MemoryMinFreeVirtual=6144").Trim()
		}
		$tam = @($Arglar)
		if ($ek) { $tam += "-AdditionalCookerOptions=$ek" }
		$s = Invoke-BekciTek -Bat $Bat -Arglar $tam -SessizDk $SessizDk -Ad $Ad
		if ($s.rc -eq 0) { return 0 }
		if ($s.mutex -and -not $s.sessiz -and $mutexSay -lt $MutexDeneme) {
			$mutexSay++
			Write-Host "::warning::UBT/LiveCoding mutex baska bir oturumda (etkilesimli editor ya da UBT acik); deneme $mutexSay/$MutexDeneme - 60 sn sonra yeniden"
			Start-Sleep -Seconds 60
			continue
		}
		if ($s.sessiz -and -not $sinirli) {
			Write-Host "::warning::[bekci] $Ad asili kaldi; doku derleme eszamanliligi sinirlanarak BIR KEZ yeniden deneniyor"
			$sinirli = $true
			continue
		}
		if ($s.sessiz) { Write-Host "::error::[bekci] $Ad sinirli yeniden denemede de asili kaldi (yukaridaki bekci dokumune bakin)" }
		return $s.rc
	}
}
