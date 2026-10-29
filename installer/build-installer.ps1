param(
    [string]$BuildDir = (Join-Path $PSScriptRoot '..\build'),
    [string]$QtBin = 'C:\Qt\6.8.3\msvc2022_64\bin',
    [string]$RedistPath = '',
    [string]$IsccPath = ''
)

$ErrorActionPreference = 'Stop'
$BuildDir = (Resolve-Path $BuildDir).Path
$exe = Join-Path $BuildDir 'RBMK-Control-Room.exe'
$deploy = Join-Path $QtBin 'windeployqt.exe'
if (-not $RedistPath) { $RedistPath = Join-Path $BuildDir 'vc_redist.x64.exe' }
if (-not $IsccPath) {
    $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($command) { $IsccPath = $command.Source }
    else {
        $candidates = @(
            (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
            (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
            (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe')
        )
        $IsccPath = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    }
}
foreach ($path in @($exe, $deploy, $RedistPath, $IsccPath)) {
    if (-not $path -or -not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required file not found: '$path'. See the installer instructions in README.md."
    }
}

# The Inno script uses this fixed, ignored staging directory; never package the build tree.
$stage = Join-Path $PSScriptRoot '..\build\installer-stage'
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
$app = Join-Path $stage 'app'
New-Item -ItemType Directory -Path $app -Force | Out-Null
Copy-Item $exe $app
Copy-Item $RedistPath (Join-Path $stage 'vc_redist.x64.exe')
& $deploy --release --no-translations --no-system-d3d-compiler --no-opengl-sw --no-compiler-runtime (Join-Path $app 'RBMK-Control-Room.exe')
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }
if (-not (Test-Path (Join-Path $app 'platforms\qwindows.dll'))) { throw 'Qt Windows platform plugin was not deployed.' }
& $IsccPath (Join-Path $PSScriptRoot 'RBMK-Control-Room.iss')
if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed with exit code $LASTEXITCODE" }