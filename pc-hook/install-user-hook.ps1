[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$cursorDir = Join-Path $env:USERPROFILE '.cursor'
$hooksDir = Join-Path $cursorDir 'hooks'
$hooksFile = Join-Path $cursorDir 'hooks.json'
$sourceHook = Join-Path $PSScriptRoot 'esp32-notify.ps1'
$sourceConfig = Join-Path $PSScriptRoot 'esp32-notify.json'
$destinationHook = Join-Path $hooksDir 'esp32-notify.ps1'
$destinationConfig = Join-Path $hooksDir 'esp32-notify.json'
$hookCommand = 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\hooks\esp32-notify.ps1'

if (-not (Test-Path -LiteralPath $sourceConfig)) {
    throw 'Create pc-hook\esp32-notify.json from the example and test it before installing.'
}

New-Item -ItemType Directory -Force -Path $hooksDir | Out-Null
Copy-Item -LiteralPath $sourceHook -Destination $destinationHook -Force
Copy-Item -LiteralPath $sourceConfig -Destination $destinationConfig -Force

if (Test-Path -LiteralPath $hooksFile) {
    $document = Get-Content -Raw -LiteralPath $hooksFile | ConvertFrom-Json
}
else {
    $document = [pscustomobject]@{
        version = 1
        hooks = [pscustomobject]@{}
    }
}

if (-not $document.PSObject.Properties['version']) {
    $document | Add-Member -NotePropertyName version -NotePropertyValue 1
}
if (-not $document.PSObject.Properties['hooks']) {
    $document | Add-Member -NotePropertyName hooks -NotePropertyValue ([pscustomobject]@{})
}
if (-not $document.hooks.PSObject.Properties['stop']) {
    $document.hooks | Add-Member -NotePropertyName stop -NotePropertyValue @()
}

$alreadyInstalled = @($document.hooks.stop) | Where-Object {
    $_.command -eq $hookCommand
}

if (-not $alreadyInstalled) {
    $entry = [pscustomobject]@{
        command = $hookCommand
        timeout = 5
        failClosed = $false
    }
    $document.hooks.stop = @($document.hooks.stop) + $entry
}

$json = $document | ConvertTo-Json -Depth 20
Set-Content -LiteralPath $hooksFile -Value $json -Encoding utf8

Write-Output "Installed ESP32 notification hook in $hooksFile"

