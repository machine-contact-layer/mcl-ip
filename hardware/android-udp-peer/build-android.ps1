<#
    Build the MCL-IP over-air peer and responder for an Android handset.

    Like the ESP32 build next door, this stages the canonical mcl-link,
    mcl-wire and mcl-ip sources rather than copying any protocol source into
    this directory, and prints the SHA-256 of every staged file. The phone and
    the laptop therefore provably run the same protocol code and cannot drift
    apart.

    WHY A STATIC BINARY AND NOT AN APK

    The MCL traffic is UDP over Wi-Fi. An APK would add an Android UI, a
    Gradle build and a permissions model to a test whose subject is a socket,
    and none of that is under test. A static executable pushed to
    /data/local/tmp and run through `adb shell` adds nothing to the phone,
    leaves nothing behind, and keeps adb where it belongs -- deployment and log
    capture, never the transport.

    WHAT THE LIBC IS, STATED BECAUSE IT MATTERS TO THE CLAIM

    This links statically against aarch64 glibc, not against Android's Bionic.
    The processor, the kernel, the Wi-Fi stack and the radio are the phone's;
    the C library is not. So this run is evidence about a THIRD PLATFORM and a
    third CPU architecture carrying MCL over a real network -- and it is not
    evidence that MCL builds against Bionic, which nothing here claims. The
    "different toolchain, different architecture" requirement of
    mcl-core/governance/V1_SCOPE.md section 5.9(c) is met by the ESP32-S3
    (Xtensa, Arduino/ESP-IDF), not by this.
#>
param(
    [string]$LinkDir = '',
    [string]$WireDir = '',
    [string]$Distro  = 'Ubuntu-24.04'
)

$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ipRoot    = (Resolve-Path (Join-Path $scriptDir '..\..')).Path

if ([string]::IsNullOrWhiteSpace($LinkDir)) { $LinkDir = (Resolve-Path (Join-Path $ipRoot '..\mcl-link')).Path }
if ([string]::IsNullOrWhiteSpace($WireDir)) { $WireDir = (Resolve-Path (Join-Path $ipRoot '..\mcl-wire')).Path }

foreach ($pair in @(@('mcl-link', $LinkDir), @('mcl-wire', $WireDir))) {
    if (-not (Test-Path -LiteralPath $pair[1] -PathType Container)) {
        throw "$($pair[0]) not found at $($pair[1]). Pass -LinkDir / -WireDir explicitly."
    }
}

$staging = Join-Path $scriptDir 'build\staging'
$outDir  = Join-Path $scriptDir 'build\out'
if (Test-Path -LiteralPath (Join-Path $scriptDir 'build')) {
    Remove-Item -Recurse -Force (Join-Path $scriptDir 'build')
}
New-Item -ItemType Directory -Force -Path (Join-Path $staging 'mcl') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $staging 'src') | Out-Null
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$headers = @(
    (Join-Path $LinkDir 'include\mcl\link.h'),
    (Join-Path $LinkDir 'include\mcl\contact.h'),
    (Join-Path $LinkDir 'include\mcl\endpoint_rendezvous.h'),
    (Join-Path $WireDir 'include\mcl\wire.h'),
    (Join-Path $ipRoot  'include\mcl\ip_binding.h')
)
$sources = @(
    (Join-Path $LinkDir 'src\link.c'),
    (Join-Path $LinkDir 'src\contact.c'),
    (Join-Path $LinkDir 'src\endpoint_rendezvous.c'),
    (Join-Path $WireDir 'src\wire.c'),
    (Join-Path $ipRoot  'src\ip_binding.c'),
    (Join-Path $ipRoot  'tools\udp_over_air_peer.c'),
    (Join-Path $ipRoot  'tools\udp_over_air_responder.c')
)

foreach ($h in $headers) {
    if (-not (Test-Path -LiteralPath $h -PathType Leaf)) { throw "Missing header: $h" }
    Copy-Item $h (Join-Path $staging 'mcl')
}
foreach ($s in $sources) {
    if (-not (Test-Path -LiteralPath $s -PathType Leaf)) { throw "Missing source: $s" }
    Copy-Item $s (Join-Path $staging 'src')
}

$manifest = @()
Write-Host 'Staged protocol sources (sha256):' -ForegroundColor Cyan
foreach ($f in ($headers + $sources)) {
    $h = (Get-FileHash -Algorithm SHA256 -LiteralPath $f).Hash
    $rel = $f.Replace((Split-Path -Parent $ipRoot) + '\', '').Replace('\', '/')
    $manifest += [pscustomobject]@{ File = $rel; SHA256 = $h }
    Write-Host ("  {0}  {1}" -f $h.Substring(0, 16), $rel)
}

function ConvertTo-WslPath([string]$p) {
    $full = (Resolve-Path -LiteralPath $p).Path
    $drive = $full.Substring(0, 1).ToLower()
    return '/mnt/' + $drive + $full.Substring(2).Replace('\', '/')
}

$wslStaging = ConvertTo-WslPath $staging
$wslOut     = ConvertTo-WslPath $outDir

# -static because nothing on the phone is going to provide a loader for these
# libraries, and /data/local/tmp is not on any library search path.
# _POSIX_C_SOURCE is requested inside the tools themselves.
$common = '-std=c99 -Wall -Wextra -Werror -pedantic -O2 -static -I' + $wslStaging

$buildScript = @"
set -e
command -v aarch64-linux-gnu-gcc >/dev/null || {
    echo 'aarch64-linux-gnu-gcc not found. Install it with:' >&2
    echo '  sudo apt-get install -y gcc-aarch64-linux-gnu' >&2
    exit 1
}
cd '$wslStaging'
aarch64-linux-gnu-gcc $common -o '$wslOut/mcl_ip_peer_arm64' \
    src/udp_over_air_peer.c src/link.c src/endpoint_rendezvous.c src/wire.c src/ip_binding.c
aarch64-linux-gnu-gcc $common -o '$wslOut/mcl_ip_responder_arm64' \
    src/udp_over_air_responder.c src/link.c src/contact.c src/endpoint_rendezvous.c src/wire.c src/ip_binding.c
aarch64-linux-gnu-strip '$wslOut/mcl_ip_peer_arm64' '$wslOut/mcl_ip_responder_arm64'
file '$wslOut/mcl_ip_peer_arm64'
file '$wslOut/mcl_ip_responder_arm64'
"@

$tmp = Join-Path $env:TEMP ('mcl-android-build-' + [guid]::NewGuid().ToString('N') + '.sh')
[System.IO.File]::WriteAllText($tmp, ($buildScript -replace "`r`n", "`n"), (New-Object System.Text.UTF8Encoding($false)))
try {
    $wslTmp = ConvertTo-WslPath $tmp
    & wsl -d $Distro -u root -- bash $wslTmp
    if ($LASTEXITCODE -ne 0) { throw "cross-compile failed (exit $LASTEXITCODE)" }
} finally {
    Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
}

Write-Host ''
Write-Host 'Built:' -ForegroundColor Green
foreach ($b in @('mcl_ip_peer_arm64', 'mcl_ip_responder_arm64')) {
    $p = Join-Path $outDir $b
    $h = (Get-FileHash -Algorithm SHA256 -LiteralPath $p).Hash
    Write-Host ("  {0}  {1}  {2} bytes" -f $h.Substring(0, 16), $b, (Get-Item $p).Length)
    $manifest += [pscustomobject]@{ File = "build/out/$b"; SHA256 = $h }
}

$lines = @(
    'MCL-IP Android peer build manifest',
    ('built ' + (Get-Date -Format 'yyyy-MM-ddTHH:mm:sszzz')),
    '',
    'The phone runs the same protocol source the laptop runs; these hashes say which.',
    ''
)
foreach ($m in $manifest) { $lines += ("  {0,-46} {1}" -f $m.File, $m.SHA256) }
[System.IO.File]::WriteAllLines(
    (Join-Path $outDir 'SOURCES.txt'), $lines,
    (New-Object System.Text.UTF8Encoding($false)))

Write-Host ''
Write-Host ("manifest: " + (Join-Path $outDir 'SOURCES.txt'))
