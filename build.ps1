param(
    [string]$Configuration = "Debug",
    [string]$Platform = "x64",
    [string]$Target = "Build"
)

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$solution = Join-Path $scriptDir "ShadertoyShaderSelectorAndVideoRender_V0.sln"

function Resolve-MsBuild {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $found = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\amd64\MSBuild.exe | Select-Object -First 1
        if ($found) {
            return $found
        }
    }

    $candidates = @(
        (Join-Path $env:ProgramFiles "Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe"),
        (Join-Path $env:ProgramFiles "Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\amd64\MSBuild.exe"),
        (Join-Path $env:ProgramFiles "Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\amd64\MSBuild.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\amd64\MSBuild.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\2019\Professional\MSBuild\Current\Bin\amd64\MSBuild.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\2019\Enterprise\MSBuild\Current\Bin\amd64\MSBuild.exe")
    )

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path $candidate)) {
            return $candidate
        }
    }

    throw "Failed to locate MSBuild.exe"
}

function Add-EnvValue {
    param(
        [hashtable]$Map,
        [string]$Name
    )

    $value = [System.Environment]::GetEnvironmentVariable($Name, "Process")
    if (-not [string]::IsNullOrEmpty($value) -and -not $Map.ContainsKey($Name)) {
        $Map[$Name] = $value
    }
}

$msbuild = Resolve-MsBuild

Write-Host "Using MSBuild: $msbuild"
Write-Host "Building $solution [$Configuration|$Platform] target=$Target"

$normalizedPath = [System.Environment]::GetEnvironmentVariable("Path", "Process")
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:Path = $normalizedPath

Push-Location $scriptDir
try {
    & $msbuild $solution "/t:$Target" "/p:Configuration=$Configuration" "/p:Platform=$Platform"
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
