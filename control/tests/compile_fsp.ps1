param(
 [Parameter(Mandatory=$true)][string]$ReferenceProject,
 [string]$Gcc='C:\Renesas\RA\e2studio_v2026-04.2_fsp_v6.5.0\toolchains\gcc_arm\13.2.rel1\bin\arm-none-eabi-gcc.exe'
)
$ErrorActionPreference='Stop'
$ref=(Resolve-Path -LiteralPath $ReferenceProject).Path
Push-Location (Join-Path $PSScriptRoot '..')
try {
 New-Item -ItemType Directory build -Force | Out-Null
 $flags=@('-std=c11','-mthumb','-mcpu=cortex-m85+nopacbti','-mfloat-abi=hard','-O0','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-D_RENESAS_RA_','-D_RA_CORE=CPU0','-D_RA_ORDINAL=1','-D_RAFSP_EK_RA8P1_','-DCONTROL_SINGLE_CORE_VALIDATION=1','-Iinclude','-Iports')
 foreach($i in @('Debug','ra_cfg/fsp_cfg/bsp','ra_cfg/fsp_cfg','ra_gen','ra/fsp/inc','ra/fsp/inc/api','ra/fsp/inc/instances','ra/arm/CMSIS_6/CMSIS/Core/Include','mtk3_bsp2','mtk3_bsp2/config','mtk3_bsp2/include','mtk3_bsp2/mtkernel/kernel/knlinc')) { $flags+=('-I'+(Join-Path $ref $i)) }
 foreach($src in @('src/control_motor.c','src/vehicle_control.c','src/control_ipc.c','src/producer_api.c','src/motor_output_backend.c','src/motor_output_drv8833.c','src/control_runtime.c','ports/control_hw_ra8p1.c','ports/ipc_ra8p1.c')) {
   $out='build/'+[IO.Path]::GetFileNameWithoutExtension($src)+'.arm.o'
   & $Gcc @flags -c $src -o $out
   if($LASTEXITCODE -ne 0){throw "ARM compile failed: $src"}
 }
 Write-Output 'PASS ARM compile against existing M85 FSP/BSP headers. NOT an M33 boot/link verification.'
} finally {Pop-Location}
