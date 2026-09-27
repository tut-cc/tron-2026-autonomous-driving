param([string]$Compiler='C:\Program Files\mingw64\bin\gcc.exe')
$ErrorActionPreference='Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    New-Item -ItemType Directory -Path build -Force | Out-Null
    & $Compiler -std=c11 -O2 -Wall -Wextra -Werror -pedantic -Iinclude src/control_motor.c src/vehicle_control.c src/control_ipc.c src/producer_api.c src/motor_output_backend.c src/motor_output_drv8833.c tests/test_control.c -o build/test_control.exe
    if($LASTEXITCODE -ne 0){throw 'Compile failed'}
    & .\build\test_control.exe
    if($LASTEXITCODE -ne 0){throw 'Tests failed'}
} finally { Pop-Location }
