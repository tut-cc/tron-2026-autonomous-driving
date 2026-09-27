param([string]$MsysRoot = 'C:\msys64')
$ErrorActionPreference = 'Stop'
$applicationRoot = Split-Path $PSScriptRoot -Parent
$workspaceRoot = Split-Path $applicationRoot -Parent
$frontend = Join-Path $applicationRoot 'Application/mini-4wd-webapp'
$kernelRoot = Join-Path $workspaceRoot 'CPU0/mtk3_bsp2'
$stageRoot = Join-Path ([IO.Path]::GetTempPath()) 'vehicleoutput-webgen'
$stage = Join-Path $stageRoot ([Guid]::NewGuid().ToString('N'))
$stageRootFull = [IO.Path]::GetFullPath($stageRoot).TrimEnd('\')
$stageFull = [IO.Path]::GetFullPath($stage)
if (!$stageFull.StartsWith($stageRootFull + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Unsafe web generator staging directory'
}
$bash = Join-Path $MsysRoot 'usr/bin/bash.exe'
if (!(Test-Path -LiteralPath $bash) -and (Test-Path -LiteralPath 'C:\Program Files\Git\usr\bin\bash.exe')) {
    $MsysRoot = 'C:\Program Files\Git'
    $bash = Join-Path $MsysRoot 'usr/bin/bash.exe'
}
if (!(Test-Path -LiteralPath $bash)) { throw "bash.exe not found under $MsysRoot; install MSYS2 or Git Bash" }
$previousPath = $env:PATH
try {
    New-Item -ItemType Directory -Force (Join-Path $stage 'fs') | Out-Null
    foreach ($name in @('index.html','style.css')) {
        Copy-Item -LiteralPath (Join-Path $frontend $name) -Destination (Join-Path $stage "fs/$name")
    }
    Copy-Item -LiteralPath (Join-Path $frontend 'js') -Destination (Join-Path $stage 'fs/js') -Recurse
    if (Test-Path -LiteralPath (Join-Path $frontend 'assets')) {
        Copy-Item -LiteralPath (Join-Path $frontend 'assets') -Destination (Join-Path $stage 'fs/assets') -Recurse
    }
    Copy-Item -LiteralPath (Join-Path $applicationRoot 'Application/web/404.html') -Destination (Join-Path $stage 'fs/404.html')
    $generator = Get-Content -LiteralPath (Join-Path $kernelRoot 'uct/lwip/src/lwip/src/apps/http/makefsdata/makefsdata') -Raw
    $mime = @"
} elsif(`$file =~ /\.js`$/) {
 print(HEADER "Content-type: application/javascript\r\n");
} elsif(`$file =~ /\.css`$/) {
 print(HEADER "Content-type: text/css\r\n");
} elsif(`$file =~ /\.gif`$/) {
"@
    $generator = $generator.Replace('} elsif($file =~ /\.gif$/) {', $mime)
    $generator = $generator.Replace('/tmp/','../')
    $generator = $generator.Replace('$fvar =~ s-\.-_-g;', '$fvar =~ s-\.-_-g; $fvar =~ s/[^A-Za-z0-9_]/_/g;')
    [IO.File]::WriteAllText((Join-Path $stage 'makefsdata'), $generator.Replace("`r`n","`n"), [Text.UTF8Encoding]::new($false))
    $unixStage = '/' + $stage.Substring(0,1).ToLower() + $stage.Substring(2).Replace('\','/')
    $env:PATH = "$MsysRoot\usr\bin;$previousPath"
    & $bash '-c' 'cd "$1" && perl makefsdata' '--' $unixStage
    if ($LASTEXITCODE -ne 0) { throw 'makefsdata failed' }
    Copy-Item -LiteralPath (Join-Path $stage 'fsdata.c') -Destination (Join-Path $applicationRoot 'Application/web/fsdata.h')
    Write-Output 'Generated Application/web/fsdata.h; staging was outside the deliverable and is removed below.'
} finally {
    $env:PATH = $previousPath
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
}
