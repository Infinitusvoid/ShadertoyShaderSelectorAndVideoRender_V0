param(
    [string]$Configuration = "Debug",
    [string]$Platform = "x64",
    [string]$Target = "Build"
)

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildCmd = Join-Path $scriptDir "build.cmd"

if (-not (Test-Path $buildCmd)) {
    throw "Missing build.cmd at $buildCmd"
}

& $buildCmd $Configuration $Platform $Target
exit $LASTEXITCODE
