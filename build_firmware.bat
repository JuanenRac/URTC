@echo off
REM =============================================================================
REM build_firmware.bat - Install tools, verify everything, compile all 4 URTC
REM firmware binaries (main board app/bootloader, expansion slave app/bootloader)
REM from a clean checkout.
REM
REM PROJECT: URTC (Universal Robot Tool Controller)
REM AUTHOR: JuanenRac (Electro Hobby 3D) - electrohobby3d@gmail.com
REM LICENSE: GPL-3.0 (same as the firmware this builds - see LICENSE at repo root)
REM
REM Every step this script performs is documented in full, with the reasoning
REM behind each choice, in docs\COMPILE_STM32F303.TXT - read that first if
REM anything here needs adjusting. This script automates that document; it
REM doesn't replace it.
REM
REM HONESTY NOTE: this .bat was written by carefully translating
REM build_firmware.sh - the Linux version - which WAS actually run end to
REM end against this project's own real source and verified working. This
REM Windows version could not be executed in the environment that wrote it
REM (no Windows machine available there) - the logic mirrors the verified
REM Linux script exactly, but hasn't itself been run start to finish on a
REM real Windows machine. If something here doesn't work as written, trust
REM build_firmware.sh's own logic as the source of truth and adjust this
REM file's own syntax to match its intent.
REM
REM Usage:
REM   build_firmware.bat              build all 4 binaries
REM   build_firmware.bat --clean      wipe the local build\ cache first
REM   build_firmware.bat master       build only the main board (app+bootloader)
REM   build_firmware.bat slave        build only the expansion slave (app+bootloader)
REM =============================================================================
setlocal enabledelayedexpansion

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "BUILD=%ROOT%\build"
set "FIRMWARE_OUT=%ROOT%\firmware"

REM Pinned to STM32CubeF3 v1.11.6's own known-good submodule combination -
REM see docs\COMPILE_STM32F303.TXT for why these are pinned rather than
REM tracking each repo's own latest master.
set "HAL_REPO=https://github.com/STMicroelectronics/stm32f3xx_hal_driver.git"
set "HAL_COMMIT=953955afe65f89e60e556bbcdba752597f5da65d"
set "CMSIS_DEVICE_REPO=https://github.com/STMicroelectronics/cmsis_device_f3.git"
set "CMSIS_DEVICE_TAG=v2.3.8"
set "CMSIS_CORE_REPO=https://github.com/STMicroelectronics/cmsis_core.git"

set /a PASS=0
set /a WARN=0
set /a FAIL=0

set "TARGET=all"
if "%~1"=="--clean" (
    echo Removing %BUILD% ...
    rmdir /s /q "%BUILD%" 2>nul
    if "%~2" NEQ "" set "TARGET=%~2"
) else if "%~1" NEQ "" (
    set "TARGET=%~1"
)

REM -----------------------------------------------------------------------
echo.
echo === 1. Toolchain ===
REM -----------------------------------------------------------------------
where arm-none-eabi-gcc >nul 2>&1
if errorlevel 1 (
    echo arm-none-eabi-gcc not found on PATH.
    echo.
    echo Install the official Arm GNU Toolchain for Windows from:
    echo   https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
    echo Pick the "arm-none-eabi" AArch32 bare-metal target, Windows installer.
    echo During install, check "Add path to environment variable" when offered.
    echo.
    echo If winget is available, you can also try:
    echo   winget install --id Arm.GnuArmEmbeddedToolchain
    echo.
    echo Re-run this script after installing.
    set /a FAIL+=1
    goto :summary
)
for /f "delims=" %%v in ('arm-none-eabi-gcc --version ^| findstr /r "^arm-none-eabi-gcc"') do echo   OK   arm-none-eabi-gcc found: %%v
set /a PASS+=1

for %%T in (arm-none-eabi-g++ arm-none-eabi-objcopy arm-none-eabi-size arm-none-eabi-nm) do (
    where %%T >nul 2>&1
    if errorlevel 1 (
        echo   FAIL %%T not found - the Arm GNU Toolchain install should provide this; check your install
        set /a FAIL+=1
    ) else (
        echo   OK   %%T found
        set /a PASS+=1
    )
)

where git >nul 2>&1
if errorlevel 1 (
    echo   FAIL git not found - needed to fetch ST's own HAL/CMSIS sources.
    echo        Install from https://git-scm.com/download/win and re-run.
    set /a FAIL+=1
    goto :summary
) else (
    for /f "delims=" %%v in ('git --version') do echo   OK   git found: %%v
    set /a PASS+=1
)

REM -----------------------------------------------------------------------
echo.
echo === 2. ST HAL/CMSIS sources (cached locally under build\vendor\ after first run) ===
REM -----------------------------------------------------------------------
if not exist "%BUILD%\common\HAL_Include" mkdir "%BUILD%\common\HAL_Include"
if not exist "%BUILD%\common\CMSIS_Include" mkdir "%BUILD%\common\CMSIS_Include"
if not exist "%BUILD%\hal_src" mkdir "%BUILD%\hal_src"
if not exist "%BUILD%\hal_obj" mkdir "%BUILD%\hal_obj"
if not exist "%BUILD%\vendor" mkdir "%BUILD%\vendor"

if not exist "%BUILD%\vendor\hal" (
    echo Fetching STM32F3xx HAL driver ^(pinned commit^)...
    mkdir "%BUILD%\vendor\hal"
    pushd "%BUILD%\vendor\hal"
    git -c safe.directory=* init -q
    git -c safe.directory=* remote add origin "%HAL_REPO%"
    git -c safe.directory=* fetch --depth 1 origin %HAL_COMMIT% -q
    git -c safe.directory=* checkout -q FETCH_HEAD
    popd
    echo   OK   HAL driver fetched
) else (
    echo   OK   HAL driver already cached at build\vendor\hal
)
set /a PASS+=1

if not exist "%BUILD%\vendor\cmsis_device_f3" (
    echo Fetching CMSIS device headers for F3 ^(pinned tag %CMSIS_DEVICE_TAG%^)...
    git -c safe.directory=* clone --depth 1 --branch %CMSIS_DEVICE_TAG% -q "%CMSIS_DEVICE_REPO%" "%BUILD%\vendor\cmsis_device_f3"
    echo   OK   CMSIS device ^(F3^) fetched
) else (
    echo   OK   CMSIS device ^(F3^) already cached at build\vendor\cmsis_device_f3
)
set /a PASS+=1

if not exist "%BUILD%\vendor\cmsis_core" (
    echo Fetching generic ARM CMSIS Core headers ^(Include\ only, sparse^)...
    git -c safe.directory=* clone --depth 1 --filter=blob:none --no-checkout -q "%CMSIS_CORE_REPO%" "%BUILD%\vendor\cmsis_core"
    pushd "%BUILD%\vendor\cmsis_core"
    git -c safe.directory=* sparse-checkout init --no-cone
    echo /CMSIS/Core/Include/** > .git\info\sparse-checkout
    git -c safe.directory=* checkout -q
    popd
    echo   OK   CMSIS core fetched ^(Include\ only^)
) else (
    echo   OK   CMSIS core already cached at build\vendor\cmsis_core
)
set /a PASS+=1

REM Assemble the flat include/source tree every build below compiles against.
copy /y "%BUILD%\vendor\hal\Inc\*.h" "%BUILD%\common\HAL_Include\" >nul
xcopy /y /i /q "%BUILD%\vendor\hal\Inc\Legacy" "%BUILD%\common\HAL_Include\Legacy\" >nul
copy /y "%BUILD%\common\HAL_Include\stm32f3xx_hal_conf_template.h" "%BUILD%\common\HAL_Include\stm32f3xx_hal_conf.h" >nul
copy /y "%BUILD%\vendor\hal\Src\*.c" "%BUILD%\hal_src\" >nul
copy /y "%BUILD%\vendor\cmsis_device_f3\Include\*.h" "%BUILD%\common\CMSIS_Include\" >nul
xcopy /y /i /q /e "%BUILD%\vendor\cmsis_core\CMSIS\Core\Include\*" "%BUILD%\common\CMSIS_Include\" >nul
if exist "%BUILD%\common\CMSIS_Include\core_cm4.h" if exist "%BUILD%\common\HAL_Include\stm32f3xx_hal.h" (
    echo   OK   HAL/CMSIS include tree assembled
    set /a PASS+=1
) else (
    echo   FAIL HAL/CMSIS include tree incomplete - check build\common\ manually
    set /a FAIL+=1
)

REM -----------------------------------------------------------------------
echo.
echo === 3. Common compiler flags and shared HAL objects ^(21 modules, shared by all 4 builds^) ===
REM -----------------------------------------------------------------------
set "CFLAGS=-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -DSTM32F303xC -DUSE_HAL_DRIVER -I%BUILD%\common\CMSIS_Include -I%BUILD%\common\HAL_Include -O2 -Wall -ffunction-sections -fdata-sections"
set "CXXFLAGS=-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -fno-exceptions -fno-rtti -fno-unwind-tables -fno-threadsafe-statics -specs=nano.specs -specs=nosys.specs -O2 -Wall"
set "LDCOMMON=-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -specs=nano.specs -specs=nosys.specs -Wl,--gc-sections"

set "HAL_MODULES=stm32f3xx_hal stm32f3xx_hal_adc stm32f3xx_hal_adc_ex stm32f3xx_hal_can stm32f3xx_hal_cortex stm32f3xx_hal_dac stm32f3xx_hal_dac_ex stm32f3xx_hal_dma stm32f3xx_hal_flash stm32f3xx_hal_flash_ex stm32f3xx_hal_gpio stm32f3xx_hal_i2c stm32f3xx_hal_i2c_ex stm32f3xx_hal_iwdg stm32f3xx_hal_pwr stm32f3xx_hal_pwr_ex stm32f3xx_hal_rcc stm32f3xx_hal_rcc_ex stm32f3xx_hal_spi stm32f3xx_hal_tim stm32f3xx_hal_tim_ex"

set /a HAL_COUNT=0
for %%f in (%HAL_MODULES%) do (
    arm-none-eabi-gcc %CFLAGS% -x c -c "%BUILD%\hal_src\%%f.c" -o "%BUILD%\hal_obj\%%f.o"
    if errorlevel 1 (
        echo   FAIL %%f failed to compile
    ) else (
        set /a HAL_COUNT+=1
    )
)
if !HAL_COUNT! EQU 21 (
    echo   OK   21/21 HAL modules compiled
    set /a PASS+=1
) else (
    echo   FAIL only !HAL_COUNT!/21 HAL modules compiled - see errors above
    set /a FAIL+=1
    goto :summary
)

for %%C in (app_master app_slave) do (
    if not exist "%BUILD%\%%C" mkdir "%BUILD%\%%C"
    copy /y "%BUILD%\vendor\cmsis_device_f3\Source\Templates\gcc\startup_stm32f303xc.s" "%BUILD%\%%C\" >nul
    copy /y "%BUILD%\vendor\cmsis_device_f3\Source\Templates\system_stm32f3xx.c" "%BUILD%\%%C\" >nul
    arm-none-eabi-gcc %CFLAGS% -x assembler-with-cpp -c "%BUILD%\%%C\startup_stm32f303xc.s" -o "%BUILD%\%%C\startup.o"
    arm-none-eabi-gcc %CFLAGS% -x c -c "%BUILD%\%%C\system_stm32f3xx.c" -o "%BUILD%\%%C\system_stm32f3xx.o"
)
echo   OK   startup + system files compiled for both chips
set /a PASS+=1

if not exist "%FIRMWARE_OUT%" mkdir "%FIRMWARE_OUT%"

REM -----------------------------------------------------------------------
if "%TARGET%"=="all" set "DO_MASTER=1" & set "DO_SLAVE=1"
if "%TARGET%"=="master" set "DO_MASTER=1"
if "%TARGET%"=="slave" set "DO_SLAVE=1"

if defined DO_MASTER (
echo.
echo === 4. Main board bootloader ^(src\F303-master\boot\^) ===
set "OUT=%BUILD%\master_boot"
rmdir /s /q "!OUT!" 2>nul
mkdir "!OUT!"
set "SRC=%ROOT%\src\F303-master\boot"
for %%f in ("!SRC!\*.c") do arm-none-eabi-gcc %CFLAGS% -I"!SRC!" -x c -c "%%f" -o "!OUT!\%%~nf.o"
arm-none-eabi-gcc %LDCOMMON% -T"!SRC!\STM32F303CCTx_BOOTLOADER.ld" "%BUILD%\app_master\startup.o" "%BUILD%\app_master\system_stm32f3xx.o" "!OUT!\*.o" "%BUILD%\hal_obj\*.o" -o "!OUT!\URTC_BOOTLOADER.elf"
arm-none-eabi-objcopy -O binary "!OUT!\URTC_BOOTLOADER.elf" "!OUT!\URTC_BOOTLOADER.bin"
arm-none-eabi-objcopy -O ihex "!OUT!\URTC_BOOTLOADER.elf" "!OUT!\URTC_BOOTLOADER.hex"
copy /y "!OUT!\URTC_BOOTLOADER.elf" "%FIRMWARE_OUT%\" >nul
copy /y "!OUT!\URTC_BOOTLOADER.bin" "%FIRMWARE_OUT%\" >nul
copy /y "!OUT!\URTC_BOOTLOADER.hex" "%FIRMWARE_OUT%\" >nul
echo   OK   URTC_BOOTLOADER.bin/.hex/.elf built
set /a PASS+=1

echo.
echo === 5. Main board application ^(src\F303-master\^) ===
set "OUT=%BUILD%\master_app"
rmdir /s /q "!OUT!" 2>nul
mkdir "!OUT!"
set "SRC=%ROOT%\src\F303-master"
for %%f in ("!SRC!\*.c") do arm-none-eabi-gcc %CFLAGS% -I"!SRC!" -x c -c "%%f" -o "!OUT!\%%~nf.o"
for %%f in ("!SRC!\melexis_mlx90640\*.c") do arm-none-eabi-gcc %CFLAGS% -I"!SRC!" -I"!SRC!\melexis_mlx90640" -x c -c "%%f" -o "!OUT!\%%~nf.o"
for %%f in ("!SRC!\melexis_mlx90641\*.c") do arm-none-eabi-gcc %CFLAGS% -I"!SRC!" -I"!SRC!\melexis_mlx90641" -x c -c "%%f" -o "!OUT!\%%~nf.o"
for %%f in ("!SRC!\melexis_mlx90642\*.c") do arm-none-eabi-gcc %CFLAGS% -I"!SRC!" -I"!SRC!\melexis_mlx90642" -x c -c "%%f" -o "!OUT!\%%~nf.o"
REM The one C++ file in the whole project - see docs\COMPILE_STM32F303.TXT section 6
arm-none-eabi-g++ %CXXFLAGS% -I"!SRC!\melexis_mlx90641" -c "!SRC!\melexis_mlx90641\MLX90641_API.cpp" -o "!OUT!\MLX90641_API.o"
arm-none-eabi-g++ %LDCOMMON% -fno-exceptions -fno-rtti -fno-unwind-tables -fno-threadsafe-statics -T"!SRC!\STM32F303CCTx_APP.ld" "%BUILD%\app_master\startup.o" "%BUILD%\app_master\system_stm32f3xx.o" "!OUT!\*.o" "%BUILD%\hal_obj\*.o" -o "!OUT!\URTC_APP.elf"
arm-none-eabi-objcopy -O binary "!OUT!\URTC_APP.elf" "!OUT!\URTC_APP.bin"
arm-none-eabi-objcopy -O ihex "!OUT!\URTC_APP.elf" "!OUT!\URTC_APP.hex"
REM Filename encodes the firmware version - read it from firmware_common.h
set "FW_VER=?" & set "FW_MIN=?"
for /f "tokens=3" %%v in ('findstr /c:"#define FIRMWARE_VERSION_MAJOR" "!SRC!\firmware_common.h"') do set "FW_VER=%%v"
for /f "tokens=3" %%v in ('findstr /c:"#define FIRMWARE_VERSION_MINOR" "!SRC!\firmware_common.h"') do set "FW_MIN=%%v"
set "APP_NAME=URTC_V!FW_VER!.!FW_MIN!_F303CC"
copy /y "!OUT!\URTC_APP.elf" "%FIRMWARE_OUT%\!APP_NAME!.elf" >nul
copy /y "!OUT!\URTC_APP.bin" "%FIRMWARE_OUT%\!APP_NAME!.bin" >nul
copy /y "!OUT!\URTC_APP.hex" "%FIRMWARE_OUT%\!APP_NAME!.hex" >nul
echo   OK   !APP_NAME!.bin/.hex/.elf built
echo        Verify MLX90641's own public C API is unmangled with:
echo        arm-none-eabi-nm "!OUT!\URTC_APP.elf" ^| findstr "MLX90641_DumpEE MLX90641_ExtractParameters MLX90641_GetFrameData MLX90641_GetTa MLX90641_CalculateTo"
echo        ^(all 5 should show plain names, no leading _Z - see docs\COMPILE_STM32F303.TXT section 6^)
set /a PASS+=1
)

REM -----------------------------------------------------------------------
if defined DO_SLAVE (
echo.
echo === 6. Expansion slave bootloader ^(src\F303-slave\boot\^) ===
set "OUT=%BUILD%\slave_boot"
rmdir /s /q "!OUT!" 2>nul
mkdir "!OUT!"
set "SRC=%ROOT%\src\F303-slave\boot"
for %%f in ("!SRC!\*.c") do arm-none-eabi-gcc %CFLAGS% -I"!SRC!" -x c -c "%%f" -o "!OUT!\%%~nf.o"
arm-none-eabi-gcc %LDCOMMON% -T"!SRC!\STM32F303CBTx_SLAVEBOOT.ld" "%BUILD%\app_slave\startup.o" "%BUILD%\app_slave\system_stm32f3xx.o" "!OUT!\*.o" "%BUILD%\hal_obj\*.o" -o "!OUT!\URTC_SLAVE_BOOTLOADER.elf"
arm-none-eabi-objcopy -O binary "!OUT!\URTC_SLAVE_BOOTLOADER.elf" "!OUT!\URTC_SLAVE_BOOTLOADER.bin"
arm-none-eabi-objcopy -O ihex "!OUT!\URTC_SLAVE_BOOTLOADER.elf" "!OUT!\URTC_SLAVE_BOOTLOADER.hex"
copy /y "!OUT!\URTC_SLAVE_BOOTLOADER.elf" "%FIRMWARE_OUT%\" >nul
copy /y "!OUT!\URTC_SLAVE_BOOTLOADER.bin" "%FIRMWARE_OUT%\" >nul
copy /y "!OUT!\URTC_SLAVE_BOOTLOADER.hex" "%FIRMWARE_OUT%\" >nul
echo   OK   URTC_SLAVE_BOOTLOADER.bin/.hex/.elf built
set /a PASS+=1

echo.
echo === 7. Expansion slave application ^(src\F303-slave\^) ===
set "OUT=%BUILD%\slave_app"
rmdir /s /q "!OUT!" 2>nul
mkdir "!OUT!"
set "SRC=%ROOT%\src\F303-slave"
set "SLAVE_INC=-I!SRC!\melexis_mlx90640 -I!SRC!\melexis_mlx90641 -I!SRC!\melexis_mlx90642"
for %%f in ("!SRC!\*.c") do arm-none-eabi-gcc %CFLAGS% -I"!SRC!" !SLAVE_INC! -x c -c "%%f" -o "!OUT!\%%~nf.o"
for %%f in ("!SRC!\melexis_mlx90640\*.c") do arm-none-eabi-gcc %CFLAGS% -I"!SRC!" !SLAVE_INC! -x c -c "%%f" -o "!OUT!\%%~nf.o"
arm-none-eabi-g++ %CXXFLAGS% -I"!SRC!\melexis_mlx90641" -c "!SRC!\melexis_mlx90641\MLX90641_API.cpp" -o "!OUT!\MLX90641_API.o"
for %%f in ("!SRC!\melexis_mlx90642\*.c") do arm-none-eabi-gcc %CFLAGS% -I"!SRC!" !SLAVE_INC! -x c -c "%%f" -o "!OUT!\%%~nf.o"
arm-none-eabi-g++ %LDCOMMON% -fno-exceptions -fno-rtti -fno-unwind-tables -fno-threadsafe-statics -T"!SRC!\STM32F303CBTx_SLAVEAPP.ld" "%BUILD%\app_slave\startup.o" "%BUILD%\app_slave\system_stm32f3xx.o" "!OUT!\*.o" "%BUILD%\hal_obj\*.o" -lm -o "!OUT!\URTC_SLAVE_APP.elf"
arm-none-eabi-objcopy -O binary "!OUT!\URTC_SLAVE_APP.elf" "!OUT!\URTC_SLAVE_APP.bin"
arm-none-eabi-objcopy -O ihex "!OUT!\URTC_SLAVE_APP.elf" "!OUT!\URTC_SLAVE_APP.hex"
copy /y "!OUT!\URTC_SLAVE_APP.elf" "%FIRMWARE_OUT%\" >nul
copy /y "!OUT!\URTC_SLAVE_APP.bin" "%FIRMWARE_OUT%\" >nul
copy /y "!OUT!\URTC_SLAVE_APP.hex" "%FIRMWARE_OUT%\" >nul
echo   OK   URTC_SLAVE_APP.bin/.hex/.elf built
set /a PASS+=1
)

REM -----------------------------------------------------------------------
if "%TARGET%"=="all" if %FAIL% EQU 0 (
echo.
echo === 8. Firmware manifest ^(firmware_manifest.json^) ===
python "%ROOT%\generate_manifest.py" "%ROOT%"
if errorlevel 1 (
    echo   WARN firmware_manifest.json regeneration failed - see the traceback above
) else (
    echo   OK   firmware_manifest.json regenerated
    set /a PASS+=1
)
)
if NOT "%TARGET%"=="all" (
    echo   WARN firmware_manifest.json NOT regenerated - only ran a partial ^(%TARGET%^) build. Run without a target argument ^(or with 'all'^) to refresh it.
)

:summary
echo.
echo === Summary ===
echo !PASS! passed, !WARN! warnings, !FAIL! failed
echo.
echo Output binaries are in: %FIRMWARE_OUT%\
echo.
echo Note on reproducibility: this script pins a specific, known-good ST
echo HAL/CMSIS version combination ^(see the top of this file^) so repeated
echo runs of THIS script produce consistent results. That pinned version
echo is not guaranteed to be byte-for-byte identical to whatever HAL
echo version firmware\*.bin was originally built against, if those files
echo already existed before this run - a same-size, different-bytes result
echo is expected and not a sign anything is wrong ^(see
echo docs\COMPILE_STM32F303.TXT for the full reasoning^). A DIFFERENT file
echo size, or a build failure, is the real thing worth investigating.
echo.
echo Remember this project's own standing rules before committing any
echo change that triggered this rebuild: a bootloader source edit needs
echo its own version bump ^(BOOTLOADER_VERSION_MAJOR/MINOR/PATCH^) before
echo this rebuild counts as final - see VERSION_CHECKLIST.txt.

endlocal
if %FAIL% GTR 0 exit /b 1
exit /b 0
