param(
    [Parameter(Mandatory = $true)]
    [string]$FirmwareBin,

    [string]$MqttHost = "",
    [string]$MqttUsername = ""
)

$ErrorActionPreference = "Stop"

function ConvertTo-CString([string]$Value) {
    if ($null -eq $Value) { return "" }
    return $Value.Replace('\', '\\').Replace('"', '\"').Replace("`r", '\r').Replace("`n", '\n')
}

function ConvertFrom-Secure([Security.SecureString]$Secure) {
    if ($null -eq $Secure -or $Secure.Length -eq 0) { return "" }
    $ptr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($Secure)
    try {
        return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($ptr)
    }
    finally {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($ptr)
    }
}

$binPath = (Resolve-Path $FirmwareBin).Path
$repoRoot = Split-Path $PSScriptRoot -Parent
$fwDir = Join-Path $repoRoot "MAYAP_INDUSTRIAL_v3_4_0"
$outPath = Join-Path $fwDir "build_secrets.h"

if (-not $MqttHost) {
    $MqttHost = Read-Host "MQTT host (vd broker.example.com)"
}
if (-not $MqttUsername) {
    $MqttUsername = Read-Host "MQTT username"
}
$secureMqttPassword = Read-Host "MQTT password (an tren man hinh)" -AsSecureString
$MqttPassword = ConvertFrom-Secure $secureMqttPassword

if (-not $MqttHost -or -not $MqttUsername -or -not $MqttPassword) {
    throw "MQTT host/username/password la bat buoc cho local production build."
}

$secureOtaPassword = Read-Host "Arduino OTA LAN password (Enter = tat OTA LAN)" -AsSecureString
$OtaPassword = ConvertFrom-Secure $secureOtaPassword

# PEM duoc nhung trong .rodata o dang ASCII. Chi trich PUBLIC material:
# TLS CA certificates va OTA verification PUBLIC KEY. Khong trich/copy secret.
$bytes = [IO.File]::ReadAllBytes($binPath)
$ascii = [Text.Encoding]::ASCII.GetString($bytes)

$certMatches = [regex]::Matches(
    $ascii,
    '-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----',
    [Text.RegularExpressions.RegexOptions]::Singleline
)
$pubMatches = [regex]::Matches(
    $ascii,
    '-----BEGIN PUBLIC KEY-----.*?-----END PUBLIC KEY-----',
    [Text.RegularExpressions.RegexOptions]::Singleline
)

if ($certMatches.Count -lt 1) {
    throw "Khong tim thay PEM CERTIFICATE trong .bin. Hay dung .bin GitHub Actions da build thanh cong voi MAYAP_TLS_ROOT_CA."
}
if ($pubMatches.Count -lt 1) {
    throw "Khong tim thay PEM PUBLIC KEY trong .bin. Hay dung .bin GitHub Actions da build thanh cong voi MAYAP_OTA_SIGNING_PUBLIC_KEY."
}

# Loai trung, giu nguyen thu tu trong binary.
$certs = @()
foreach ($m in $certMatches) {
    $v = $m.Value.Trim()
    if ($certs -notcontains $v) { $certs += $v }
}
$publicKeys = @()
foreach ($m in $pubMatches) {
    $v = $m.Value.Trim()
    if ($publicKeys -notcontains $v) { $publicKeys += $v }
}

if ($publicKeys.Count -ne 1) {
    throw "Tim thay $($publicKeys.Count) PUBLIC KEY trong .bin; khong tu dong doan key OTA. Can kiem tra thu cong."
}

$tlsBundle = ($certs -join "`n")
$otaPublicKey = $publicKeys[0]

$hostEsc = ConvertTo-CString $MqttHost
$userEsc = ConvertTo-CString $MqttUsername
$passEsc = ConvertTo-CString $MqttPassword
$otaPassEsc = ConvertTo-CString $OtaPassword

$content = @"
#pragma once

// AUTO-GENERATED LOCAL FILE - DO NOT COMMIT.
// Source public material: $([IO.Path]::GetFileName($binPath))

#define MAYAP_WIFI_SSID ""
#define MAYAP_WIFI_PASSWORD ""

#define MAYAP_MQTT_HOST "$hostEsc"
#define MAYAP_MQTT_PORT 8883
#define MAYAP_MQTT_USE_TLS 1
#define MAYAP_MQTT_USERNAME "$userEsc"
#define MAYAP_MQTT_PASSWORD "$passEsc"
#define MAYAP_MQTT_TOPIC_ROOT "mayap/v1"

// Device key hien tai nam trong NVS sau migration. Khong nhung legacy secret.
#define MAYAP_DEVICE_SECRET ""
#define MAYAP_ENABLE_LEGACY_DEVICE_MIGRATION 0

#define MAYAP_CLOUD_API_HOST "mayap-push-worker.vietk-mayaptrung.workers.dev"
#define MAYAP_OTA_PASSWORD "$otaPassEsc"

#define MAYAP_TLS_ROOT_CA R"PEM(
$tlsBundle
)PEM"

#define MAYAP_OTA_SIGNING_PUBLIC_KEY R"PEM(
$otaPublicKey
)PEM"
"@

[IO.File]::WriteAllText($outPath, $content, [Text.UTF8Encoding]::new($false))

Write-Host ""
Write-Host "OK: da tao local secrets file:" -ForegroundColor Green
Write-Host "  $outPath"
Write-Host "TLS certificates extracted: $($certs.Count)"
Write-Host "OTA public key extracted: 1"
Write-Host ""
Write-Host "File nay da nam trong .gitignore. Khong git add -f / khong gui file cho nguoi khac." -ForegroundColor Yellow
Write-Host "Mo MAYAP_INDUSTRIAL_v3_4_0.ino bang Arduino IDE va Upload binh thuong."
