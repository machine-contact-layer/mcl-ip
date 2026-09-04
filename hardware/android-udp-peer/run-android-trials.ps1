<#
    Run the MCL-IP over-air campaign against an Android handset, both
    directions, at both Wire/Link majors, and write the evidence record.

    THE RIG

    The phone is the access point and one of the two MCL peers. The laptop
    joins that access point on a named interface and is the other peer. Every
    MCL datagram crosses the phone's 2.4 GHz SoftAP; nothing crosses USB.

    adb is used for exactly three things: pushing the binaries, starting a
    process, and capturing its stdout. It is never the transport. If adb were
    carrying the frames this would be a test of a USB cable.

    -BindIp is not optional. This laptop has two wireless interfaces on the
    same subnet, so without it the routing table picks one and the result
    describes a different radio from the one being measured.

    WHAT FOUR CELLS BUY

    Direction alone is not enough and neither is major alone.

      phone responds  x  major 0   the experimental path, phone decoding
      phone responds  x  major 1   the Stable path, phone decoding
      phone initiates x  major 0   the experimental path, phone encoding
      phone initiates x  major 1   the Stable path, phone encoding

    An encoder defect and a decoder defect look identical from one direction.
    Running both is what separates them.
#>
param(
    [string]$PeerIp,                       # the phone's address on the SoftAP
    [string]$BindIp,                       # this laptop's address on that SoftAP
    [string]$WindowsBin,                   # dir holding the MSVC-built tools
    [int]$Count = 100,
    [int]$BasePort = 5555,
    [string]$Adb = 'C:\Users\marsm\rdb\adb.exe',
    [string]$EvidenceRoot = ''
)

$ErrorActionPreference = 'Stop'

foreach ($n in @('PeerIp', 'BindIp', 'WindowsBin')) {
    if ([string]::IsNullOrWhiteSpace((Get-Variable $n).Value)) {
        throw "-$n is required. Run 'ipconfig' and 'adb shell ip -o -4 addr' to find both addresses."
    }
}
if (-not (Test-Path -LiteralPath $Adb)) { throw "adb not found at $Adb" }

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$outDir    = Join-Path $scriptDir 'build\out'
$stamp     = Get-Date -Format 'yyyyMMdd'
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = Join-Path $scriptDir ("..\..\evidence\e4-android-udp-$stamp")
}
New-Item -ItemType Directory -Force -Path $EvidenceRoot | Out-Null
$EvidenceRoot = (Resolve-Path $EvidenceRoot).Path

$peerExe = Join-Path $WindowsBin 'mcl_ip_udp_over_air_peer.exe'
$respExe = Join-Path $WindowsBin 'mcl_ip_udp_over_air_responder.exe'
foreach ($e in @($peerExe, $respExe)) {
    if (-not (Test-Path -LiteralPath $e)) {
        throw "$e not found. Build mcl-ip with -DMCL_IP_BUILD_TOOLS=ON."
    }
}

# Deploy. /data/local/tmp is where an adb shell may execute, and the binaries
# are static so nothing on the phone has to supply a loader.
$remote = '/data/local/tmp/mcl'
# Windows PowerShell wraps a native program's stderr in an ErrorRecord, and
# under 'Stop' that aborts the script even when the program succeeded. adb
# writes its transfer progress to stderr, so the deploy below would abort on a
# successful push. Exit codes are checked explicitly instead.
$ErrorActionPreference = 'Continue'

& $Adb shell "rm -rf $remote; mkdir -p $remote" 2>&1 | Out-Null
foreach ($b in @('mcl_ip_peer_arm64', 'mcl_ip_responder_arm64')) {
    $local = Join-Path $outDir $b
    if (-not (Test-Path -LiteralPath $local)) {
        throw "$local not found. Run build-android.ps1 first."
    }
    & $Adb push $local "$remote/" 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "adb push of $b failed (exit $LASTEXITCODE)" }
}
& $Adb shell "chmod 755 $remote/*" 2>&1 | Out-Null

# Record what the phone actually is. A result without the platform it ran on
# is not evidence about a platform.
$props = @('ro.product.manufacturer', 'ro.product.model', 'ro.build.version.release',
           'ro.build.version.sdk', 'ro.build.version.security_patch',
           'ro.product.cpu.abi', 'ro.soc.model', 'ro.build.display.id')
$deviceLines = @('DEVICE UNDER TEST', '')
foreach ($p in $props) {
    $v = (& $Adb shell "getprop $p").Trim()
    $deviceLines += ("  {0,-34} {1}" -f $p, $v)
}
$deviceLines += ('  {0,-34} {1}' -f 'kernel', (& $Adb shell 'uname -sr').Trim())
$deviceLines += ('  {0,-34} {1}' -f 'selinux', (& $Adb shell 'getenforce').Trim())
$deviceLines += ('  {0,-34} {1}' -f 'phone address', $PeerIp)
$deviceLines += ('  {0,-34} {1}' -f 'laptop address', $BindIp)
[System.IO.File]::WriteAllLines((Join-Path $EvidenceRoot 'device.txt'), $deviceLines,
                                (New-Object System.Text.UTF8Encoding($false)))

$results = @()
$port = $BasePort

function Invoke-Cell {
    param([string]$Who, [int]$Major, [int]$Port)

    $tag  = "$Who-major$Major"
    $rLog = Join-Path $EvidenceRoot "$tag-responder.log"
    $pLog = Join-Path $EvidenceRoot "$tag-initiator.log"

    if ($Who -eq 'phone-responds') {
        $proc = Start-Process -FilePath $Adb -NoNewWindow -PassThru `
            -ArgumentList @('shell', "$remote/mcl_ip_responder_arm64 --port $Port --major $Major --count 4000") `
            -RedirectStandardOutput $rLog -RedirectStandardError "$rLog.err"
        Start-Sleep -Seconds 2
        & $peerExe --peer $PeerIp --bind $BindIp --port $Port --major $Major --count $Count |
            Tee-Object -FilePath $pLog | Out-Host
        $rc = $LASTEXITCODE
        Start-Sleep -Seconds 1
        if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
        & $Adb shell "pkill -f mcl_ip_responder_arm64" 2>&1 | Out-Null
    } else {
        $proc = Start-Process -FilePath $respExe -NoNewWindow -PassThru `
            -ArgumentList @('--port', "$Port", '--bind', $BindIp, '--major', "$Major", '--count', '4000') `
            -RedirectStandardOutput $rLog -RedirectStandardError "$rLog.err"
        Start-Sleep -Seconds 2
        & $Adb shell "$remote/mcl_ip_peer_arm64 --peer $BindIp --bind $PeerIp --port $Port --major $Major --count $Count" |
            Tee-Object -FilePath $pLog | Out-Host
        $rc = $LASTEXITCODE
        Start-Sleep -Seconds 1
        if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
    }

    $summary = (Get-Content $pLog | Select-String 'checks, .* failed').Line
    if ($null -eq $summary) { $summary = '(no summary line)' }
    return [pscustomobject]@{ Cell = $tag; Exit = $rc; Summary = $summary.Trim() }
}

foreach ($who in @('phone-responds', 'phone-initiates')) {
    foreach ($major in @(0, 1)) {
        Write-Host ''
        Write-Host "=== $who, major $major, port $port ===" -ForegroundColor Cyan
        $results += (Invoke-Cell -Who $who -Major $major -Port $port)
        $port++
    }
}

Write-Host ''
Write-Host '=== SUMMARY ===' -ForegroundColor Cyan
$failed = 0
foreach ($r in $results) {
    $mark = if ($r.Exit -eq 0) { 'PASS' } else { 'FAIL'; }
    if ($r.Exit -ne 0) { $failed++ }
    Write-Host ("  {0,-28} {1}  {2}" -f $r.Cell, $mark, $r.Summary)
}

$sumLines = @('MCL-IP over-air campaign against an Android handset', "run $stamp", '')
foreach ($r in $results) {
    $sumLines += ("  {0,-28} exit={1}  {2}" -f $r.Cell, $r.Exit, $r.Summary)
}
[System.IO.File]::WriteAllLines((Join-Path $EvidenceRoot 'summary.txt'), $sumLines,
                                (New-Object System.Text.UTF8Encoding($false)))

# Hash every artifact, including the build manifest, so a reader can tie the
# logs to the exact sources that produced them.
Copy-Item (Join-Path $outDir 'SOURCES.txt') $EvidenceRoot -Force
$hashLines = @()
Get-ChildItem -File $EvidenceRoot | Where-Object { $_.Name -ne 'SHA256SUMS.txt' } | Sort-Object Name | ForEach-Object {
    $hashLines += ("{0}  {1}" -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash, $_.Name)
}
[System.IO.File]::WriteAllLines((Join-Path $EvidenceRoot 'SHA256SUMS.txt'), $hashLines,
                                (New-Object System.Text.UTF8Encoding($false)))

Write-Host ''
Write-Host "evidence: $EvidenceRoot"
exit $failed
