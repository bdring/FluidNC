[CmdletBinding()]
param(
    [Parameter()]
    [ValidatePattern('^https?://')]
    [string]$BaseUri = 'http://192.0.2.2',

    [Parameter()]
    [string]$OutputDirectory = (Join-Path $PWD ('fluidnc-evidence-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))),

    [Parameter()]
    [ValidateRange(1, 60)]
    [int]$TimeoutSeconds = 8,

    [Parameter()]
    [switch]$IncludeSensitiveSettings
)

$ErrorActionPreference = 'Stop'
$base = $BaseUri.TrimEnd('/')
$null = New-Item -ItemType Directory -Path $OutputDirectory -Force
$manifest = [ordered]@{
    schema = 1
    capturedUtc = [DateTime]::UtcNow.ToString('o')
    baseUri = $base
    transport = 'HTTP GET only; no WebSocket, G-code, reset, upload, or write request'
    files = @()
    errors = @()
}

function Get-SafeName {
    param([Parameter(Mandatory)][string]$Name)
    return ($Name -replace '[^A-Za-z0-9._-]', '_')
}

function Add-ManifestFile {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Source
    )

    $item = Get-Item -LiteralPath $Path
    $hash = Get-FileHash -LiteralPath $Path -Algorithm SHA256
    $manifest.files += [ordered]@{
        name = $item.Name
        source = $Source
        bytes = $item.Length
        sha256 = $hash.Hash
    }
}

function Invoke-ReadOnlyGet {
    param(
        [Parameter(Mandatory)][string]$RelativeUri,
        [Parameter(Mandatory)][string]$OutputName,
        [switch]$AllowNotFound
    )

    $uri = $base + $RelativeUri
    $path = Join-Path $OutputDirectory (Get-SafeName $OutputName)
    try {
        Invoke-WebRequest -UseBasicParsing -Method Get -Uri $uri -TimeoutSec $TimeoutSeconds -OutFile $path | Out-Null
        Add-ManifestFile -Path $path -Source $RelativeUri
        return $path
    } catch {
        $status = $null
        if ($_.Exception.Response) {
            $status = [int]$_.Exception.Response.StatusCode
        }
        if ($AllowNotFound -and $status -eq 404) {
            $manifest.errors += [ordered]@{ source = $RelativeUri; status = 404; message = 'not present' }
            return $null
        }
        $manifest.errors += [ordered]@{ source = $RelativeUri; status = $status; message = $_.Exception.Message }
        throw
    }
}

function Get-CommandUri {
    param([Parameter(Mandatory)][string]$Command)
    return '/command?plain=' + [Uri]::EscapeDataString($Command)
}

function Get-EspCommandUri {
    param([Parameter(Mandatory)][string]$Command)
    return '/command?commandText=' + [Uri]::EscapeDataString($Command)
}

function ConvertTo-WebDavPath {
    param([Parameter(Mandatory)][string]$ConfigFilename)
    $segments = $ConfigFilename.TrimStart('/').Split('/') | ForEach-Object { [Uri]::EscapeDataString($_) }
    return '/flash/' + ($segments -join '/')
}

try {
    # Query the state first. FluidNC can block this request during motion; in that
    # case the collector fails closed instead of adding filesystem load.
    $statePath = Invoke-ReadOnlyGet -RelativeUri (Get-CommandUri '$State') -OutputName 'state.txt'
    $stateText = Get-Content -LiteralPath $statePath -Raw
    if ($stateText -notmatch 'State\s+\d+\s+\((Idle|Alarm|ConfigAlarm|Critical)\)') {
        throw "Unsafe or unknown machine state; refusing further reads. Raw response: $stateText"
    }

    $commands = @(
        @{ Name = 'firmware-info.json'; Uri = (Get-EspCommandUri '[ESP800]json=yes') },
        @{ Name = 'system-stats.json'; Uri = (Get-EspCommandUri '[ESP420]json=yes') },
        @{ Name = 'build-info.txt'; Uri = (Get-CommandUri '$Build/Info') },
        @{ Name = 'config-filename.txt'; Uri = (Get-CommandUri '$Config/Filename') },
        @{ Name = 'runtime-config.yaml'; Uri = (Get-CommandUri '$Config/Dump') },
        @{ Name = 'channel-info.txt'; Uri = (Get-CommandUri '$Channel/Info') },
        @{ Name = 'startup-log.txt'; Uri = (Get-CommandUri '$Startup/Show') },
        @{ Name = 'backtrace.txt'; Uri = (Get-CommandUri '$Backtrace/Show') },
        @{ Name = 'nvs-stats.txt'; Uri = (Get-CommandUri '$Settings/Stats') }
    )

    foreach ($command in $commands) {
        Invoke-ReadOnlyGet -RelativeUri $command.Uri -OutputName $command.Name | Out-Null
    }

    $configFilenameResponse = Get-Content -LiteralPath (Join-Path $OutputDirectory 'config-filename.txt') -Raw
    if ($configFilenameResponse -notmatch '(?m)^\$Config/Filename=([^\r\n]+)') {
        throw 'Could not parse Config/Filename from the raw FluidNC response.'
    }
    $configFilename = [Uri]::UnescapeDataString($Matches[1].Trim())
    $manifest.configFilename = $configFilename
    Invoke-ReadOnlyGet -RelativeUri (ConvertTo-WebDavPath $configFilename) -OutputName 'configured-file.yaml' | Out-Null

    # The journal exists only after this diagnostic firmware has persisted a
    # qualifying prior reset. A missing file is an expected, recorded result.
    Invoke-ReadOnlyGet -RelativeUri '/flash/debug-crash-journal.jsonl' -OutputName 'debug-crash-journal.jsonl' -AllowNotFound | Out-Null

    if ($IncludeSensitiveSettings) {
        Write-Warning 'The ESP400 snapshot can contain Wi-Fi/authentication secrets. Keep the evidence directory private.'
        Invoke-ReadOnlyGet -RelativeUri (Get-EspCommandUri '[ESP400]json=yes') -OutputName 'settings-and-runtime-config-sensitive.json' | Out-Null
    }
} finally {
    $manifestPath = Join-Path $OutputDirectory 'manifest.json'
    $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
}

Write-Output (Resolve-Path -LiteralPath $OutputDirectory).Path
