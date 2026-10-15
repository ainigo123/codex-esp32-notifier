[CmdletBinding()]
param()

# A Codex command hook receives event JSON on stdin. The first version does not
# depend on its schema, but consuming stdin prevents a broken pipe in the caller.
$null = [Console]::In.ReadToEnd()

$configPath = Join-Path $PSScriptRoot 'esp32-notify.json'

try {
    if (-not (Test-Path -LiteralPath $configPath)) {
        throw "Missing hook config: $configPath"
    }

    $config = Get-Content -Raw -LiteralPath $configPath | ConvertFrom-Json
    $hostName = [string]$config.host
    $port = [int]$config.port
    $token = [string]$config.token
    $message = [string]$config.message

    if ([string]::IsNullOrWhiteSpace($hostName)) {
        throw 'The notification host is empty.'
    }
    if ($port -lt 1 -or $port -gt 65535) {
        throw "Invalid UDP port: $port"
    }
    if ([string]::IsNullOrWhiteSpace($token) -or $token -eq 'change-me') {
        throw 'Set the same non-default token on the PC and ESP32.'
    }

    $payload = "CODEX_DONE|$token|$message"
    $bytes = [Text.Encoding]::UTF8.GetBytes($payload)
    $client = [Net.Sockets.UdpClient]::new()

    try {
        $client.EnableBroadcast = $true
        $client.Client.SendTimeout = 1500
        $sent = $client.Send($bytes, $bytes.Length, $hostName, $port)
        if ($sent -ne $bytes.Length) {
            throw "Only $sent of $($bytes.Length) bytes were sent."
        }
    }
    finally {
        $client.Dispose()
    }
}
catch {
    # Notification failures must not prevent Codex from completing. Keep stdout
    # machine-readable and put diagnostics on stderr/the Hooks output channel.
    [Console]::Error.WriteLine("ESP32 notification failed: $($_.Exception.Message)")
}

# The stop hook is observational and requests no follow-up loop.
Write-Output '{}'

