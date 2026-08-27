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
python "%~dp0bump_manifest_version.py"
if errorlevel 1 ( echo VERSION BUMP FAILED. & pause & exit /b 1 )

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "BUILD=%ROOT%\build"
set "FIRMWARE_OUT=%ROOT%\firmware"

REM -----------------------------------------------------------------------
REM Banner - printed on every run (not just a comment at the top of this
REM file), so anyone double-clicking this from Explorer or launching it
REM from a fresh cmd window sees what project/script/author/license they're
REM looking at before any output scrolls past.
REM -----------------------------------------------------------------------
echo =============================================================================
echo  URTC - firmware build
echo.
echo  Installs tools, verifies everything, and compiles all 4 URTC firmware
echo  binaries from a clean checkout: main board application + bootloader,
echo  expansion slave application + bootloader (STM32F303).
echo.
echo  Author:  JuanenRac (Electro Hobby 3D^) - electrohobby3d@gmail.com
echo  License: GPL-3.0 - see LICENSE at repo root
echo =============================================================================

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
REM Bootloader version is INCREMENTAL - bump PATCH (odometer-carry into
REM MINOR/MAJOR) before the compiler reads this header, so the .bin built
REM below already embeds the new version - see bump_version.py.
REM bump_version.py's own stdout IS the freshly-bumped "MAJOR.MINOR.PATCH" -
REM captured below via a for /f on its own stdout, directly into the
REM output filename, so the header is never re-read a second time just to
REM get the same number back. Filename convention
REM URTC_MAIN_BOOTLOADER_v{MAJOR}.{MINOR}.{PATCH} matches HYDRA-UMC's own
REM <PREFIX>_v<MAJOR>.<MINOR>.<PATCH> pattern - see CHANGELOG.md for why
REM this replaced the old unversioned URTC_BOOTLOADER name.
set "BOOT_VER="
for /f "usebackq delims=" %%v in (`python "%ROOT%\bump_version.py" "!SRC!\bootloader_common.h" BOOTLOADER_VERSION`) do set "BOOT_VER=%%v"
if not defined BOOT_VER (
    echo   FAIL bootloader version bump script failed - see traceback above
    set /a FAIL+=1
    goto :summary
)
set "BOOT_NAME=URTC_MAIN_BOOTLOADER_v!BOOT_VER!"
set "SECTION_FAIL=0"
for %%f in ("!SRC!\*.c") do (
    arm-none-eabi-gcc %CFLAGS% -I"!SRC!" -x c -c "%%f" -o "!OUT!\%%~nf.o"
    if errorlevel 1 (
        echo   FAIL %%~nf.c failed to compile
        set "SECTION_FAIL=1"
    )
)
if "!SECTION_FAIL!"=="1" (
    echo   FAIL main board bootloader: one or more source files failed to compile - see errors above
    set /a FAIL+=1
    goto :summary
)
arm-none-eabi-gcc %LDCOMMON% -T"!SRC!\STM32F303CCTx_BOOTLOADER.ld" "%BUILD%\app_master\startup.o" "%BUILD%\app_master\system_stm32f3xx.o" "!OUT!\*.o" "%BUILD%\hal_obj\*.o" -o "!OUT!\!BOOT_NAME!.elf"
if errorlevel 1 (
    echo   FAIL main board bootloader: link failed
    set /a FAIL+=1
    goto :summary
)
arm-none-eabi-objcopy -O binary "!OUT!\!BOOT_NAME!.elf" "!OUT!\!BOOT_NAME!.bin"
if errorlevel 1 (
    echo   FAIL main board bootloader: objcopy ^(.bin^) failed
    set /a FAIL+=1
    goto :summary
)
arm-none-eabi-objcopy -O ihex "!OUT!\!BOOT_NAME!.elf" "!OUT!\!BOOT_NAME!.hex"
if errorlevel 1 (
    echo   FAIL main board bootloader: objcopy ^(.hex^) failed
    set /a FAIL+=1
    goto :summary
)
copy /y "!OUT!\!BOOT_NAME!.elf" "%FIRMWARE_OUT%\" >nul
copy /y "!OUT!\!BOOT_NAME!.bin" "%FIRMWARE_OUT%\" >nul
copy /y "!OUT!\!BOOT_NAME!.hex" "%FIRMWARE_OUT%\" >nul
echo   OK   !BOOT_NAME!.bin/.hex/.elf built
set /a PASS+=1

echo.
echo === 5. Main board application ^(src\F303-master\^) ===
set "OUT=%BUILD%\master_app"
rmdir /s /q "!OUT!" 2>nul
mkdir "!OUT!"
set "SRC=%ROOT%\src\F303-master"
REM Application firmware is INCREMENTAL too - same odometer-carry bump as
REM the bootloader above, plus mirroring the new version into
REM bootloader_common.h's own FIRMWARE_VERSION_* copy so the two can never
REM drift apart (see bump_version.py's own header). Same stdout-capture
REM pattern as the bootloader above - filename convention
REM URTC_MAIN_FIRMWARE_v{MAJOR}.{MINOR}.{PATCH} replaces the old
REM URTC_V{MAJOR}.{MINOR}_F303CC (no PATCH) name.
set "APP_VER="
for /f "usebackq delims=" %%v in (`python "%ROOT%\bump_version.py" "!SRC!\firmware_common.h" FIRMWARE_VERSION "!SRC!\boot\bootloader_common.h"`) do set "APP_VER=%%v"
if not defined APP_VER (
    echo   FAIL application version bump script failed - see traceback above
    set /a FAIL+=1
    goto :summary
)
set "APP_NAME=URTC_MAIN_FIRMWARE_v!APP_VER!"
set "MASTER_INC=-I!SRC!\melexis_mlx90640 -I!SRC!\melexis_mlx90641 -I!SRC!\melexis_mlx90642"
set "SECTION_FAIL=0"
for %%f in ("!SRC!\*.c") do (
    arm-none-eabi-gcc %CFLAGS% -I"!SRC!" !MASTER_INC! -x c -c "%%f" -o "!OUT!\%%~nf.o"
    if errorlevel 1 (echo   FAIL %%~nf.c failed to compile & set "SECTION_FAIL=1")
)
for %%f in ("!SRC!\melexis_mlx90640\*.c") do (
    arm-none-eabi-gcc %CFLAGS% -I"!SRC!" -I"!SRC!\melexis_mlx90640" -x c -c "%%f" -o "!OUT!\%%~nf.o"
    if errorlevel 1 (echo   FAIL %%~nf.c failed to compile & set "SECTION_FAIL=1")
)
for %%f in ("!SRC!\melexis_mlx90641\*.c") do (
    arm-none-eabi-gcc %CFLAGS% -I"!SRC!" -I"!SRC!\melexis_mlx90641" -x c -c "%%f" -o "!OUT!\%%~nf.o"
    if errorlevel 1 (echo   FAIL %%~nf.c failed to compile & set "SECTION_FAIL=1")
)
for %%f in ("!SRC!\melexis_mlx90642\*.c") do (
    arm-none-eabi-gcc %CFLAGS% -I"!SRC!" -I"!SRC!\melexis_mlx90642" -x c -c "%%f" -o "!OUT!\%%~nf.o"
    if errorlevel 1 (echo   FAIL %%~nf.c failed to compile & set "SECTION_FAIL=1")
)
REM The one C++ file in the whole project - see docs\COMPILE_STM32F303.TXT section 6
arm-none-eabi-g++ %CXXFLAGS% -I"!SRC!\melexis_mlx90641" -c "!SRC!\melexis_mlx90641\MLX90641_API.cpp" -o "!OUT!\MLX90641_API.o"
if errorlevel 1 (echo   FAIL MLX90641_API.cpp failed to compile & set "SECTION_FAIL=1")
if "!SECTION_FAIL!"=="1" (
    echo   FAIL main board application: one or more source files failed to compile - see errors above
    set /a FAIL+=1
    goto :summary
)
arm-none-eabi-g++ %LDCOMMON% -fno-exceptions -fno-rtti -fno-unwind-tables -fno-threadsafe-statics -T"!SRC!\STM32F303CCTx_APP.ld" "%BUILD%\app_master\startup.o" "%BUILD%\app_master\system_stm32f3xx.o" "!OUT!\*.o" "%BUILD%\hal_obj\*.o" -o "!OUT!\!APP_NAME!.elf"
if errorlevel 1 (
    echo   FAIL main board application: link failed
    set /a FAIL+=1
    goto :summary
)
arm-none-eabi-objcopy -O binary "!OUT!\!APP_NAME!.elf" "!OUT!\!APP_NAME!.bin"
if errorlevel 1 (
    echo   FAIL main board application: objcopy ^(.bin^) failed
    set /a FAIL+=1
    goto :summary
)
arm-none-eabi-objcopy -O ihex "!OUT!\!APP_NAME!.elf" "!OUT!\!APP_NAME!.hex"
if errorlevel 1 (
    echo   FAIL main board application: objcopy ^(.hex^) failed
    set /a FAIL+=1
    goto :summary
)
copy /y "!OUT!\!APP_NAME!.elf" "%FIRMWARE_OUT%\!APP_NAME!.elf" >nul
copy /y "!OUT!\!APP_NAME!.bin" "%FIRMWARE_OUT%\!APP_NAME!.bin" >nul
copy /y "!OUT!\!APP_NAME!.hex" "%FIRMWARE_OUT%\!APP_NAME!.hex" >nul
echo   OK   !APP_NAME!.bin/.hex/.elf built
echo        Verify MLX90641's own public C API is unmangled with:
echo        arm-none-eabi-nm "!OUT!\!APP_NAME!.elf" ^| findstr "MLX90641_DumpEE MLX90641_ExtractParameters MLX90641_GetFrameData MLX90641_GetTa MLX90641_CalculateTo"
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
REM Same incremental-version bump as the main board bootloader above - see
REM bump_version.py's own header for the full policy. Same stdout-capture
REM pattern too - filename convention URTC_SLAVE_BOOTLOADER_v{MAJOR}.
REM {MINOR}.{PATCH} replaces the old unversioned URTC_SLAVE_BOOTLOADER name.
set "SLAVE_BOOT_VER="
for /f "usebackq delims=" %%v in (`python "%ROOT%\bump_version.py" "!SRC!\slaveboot_common.h" BOOTLOADER_VERSION`) do set "SLAVE_BOOT_VER=%%v"
if not defined SLAVE_BOOT_VER (
    echo   FAIL bootloader version bump script failed - see traceback above
    set /a FAIL+=1
    goto :summary
)
set "SLAVE_BOOT_NAME=URTC_SLAVE_BOOTLOADER_v!SLAVE_BOOT_VER!"
set "SECTION_FAIL=0"
for %%f in ("!SRC!\*.c") do (
    arm-none-eabi-gcc %CFLAGS% -I"!SRC!" -x c -c "%%f" -o "!OUT!\%%~nf.o"
    if errorlevel 1 (echo   FAIL %%~nf.c failed to compile & set "SECTION_FAIL=1")
)
if "!SECTION_FAIL!"=="1" (
    echo   FAIL expansion slave bootloader: one or more source files failed to compile - see errors above
    set /a FAIL+=1
    goto :summary
)
arm-none-eabi-gcc %LDCOMMON% -T"!SRC!\STM32F303CBTx_SLAVEBOOT.ld" "%BUILD%\app_slave\startup.o" "%BUILD%\app_slave\system_stm32f3xx.o" "!OUT!\*.o" "%BUILD%\hal_obj\*.o" -o "!OUT!\!SLAVE_BOOT_NAME!.elf"
if errorlevel 1 (
    echo   FAIL expansion slave bootloader: link failed
    set /a FAIL+=1
    goto :summary
)
arm-none-eabi-objcopy -O binary "!OUT!\!SLAVE_BOOT_NAME!.elf" "!OUT!\!SLAVE_BOOT_NAME!.bin"
if errorlevel 1 (
    echo   FAIL expansion slave bootloader: objcopy ^(.bin^) failed
    set /a FAIL+=1
    goto :summary
)
arm-none-eabi-objcopy -O ihex "!OUT!\!SLAVE_BOOT_NAME!.elf" "!OUT!\!SLAVE_BOOT_NAME!.hex"
if errorlevel 1 (
    echo   FAIL expansion slave bootloader: objcopy ^(.hex^) failed
    set /a FAIL+=1
    goto :summary
)
copy /y "!OUT!\!SLAVE_BOOT_NAME!.elf" "%FIRMWARE_OUT%\" >nul
copy /y "!OUT!\!SLAVE_BOOT_NAME!.bin" "%FIRMWARE_OUT%\" >nul
copy /y "!OUT!\!SLAVE_BOOT_NAME!.hex" "%FIRMWARE_OUT%\" >nul
echo   OK   !SLAVE_BOOT_NAME!.bin/.hex/.elf built
set /a PASS+=1

echo.
echo === 7. Expansion slave application ^(src\F303-slave\^) ===
set "OUT=%BUILD%\slave_app"
rmdir /s /q "!OUT!" 2>nul
mkdir "!OUT!"
set "SRC=%ROOT%\src\F303-slave"
REM Same incremental bump + bootloader-mirror as the main board application
REM above. Same stdout-capture pattern - filename convention
REM URTC_SLAVE_FIRMWARE_v{MAJOR}.{MINOR}.{PATCH} replaces the old
REM unversioned URTC_SLAVE_APP name.
set "SLAVE_APP_VER="
for /f "usebackq delims=" %%v in (`python "%ROOT%\bump_version.py" "!SRC!\slave_common.h" FIRMWARE_VERSION "!SRC!\boot\slaveboot_common.h"`) do set "SLAVE_APP_VER=%%v"
if not defined SLAVE_APP_VER (
    echo   FAIL application version bump script failed - see traceback above
    set /a FAIL+=1
    goto :summary
)
set "SLAVE_APP_NAME=URTC_SLAVE_FIRMWARE_v!SLAVE_APP_VER!"
set "SLAVE_INC=-I!SRC!\melexis_mlx90640 -I!SRC!\melexis_mlx90641 -I!SRC!\melexis_mlx90642"
set "SECTION_FAIL=0"
for %%f in ("!SRC!\*.c") do (
    arm-none-eabi-gcc %CFLAGS% -I"!SRC!" !SLAVE_INC! -x c -c "%%f" -o "!OUT!\%%~nf.o"
    if errorlevel 1 (echo   FAIL %%~nf.c failed to compile & set "SECTION_FAIL=1")
)
for %%f in ("!SRC!\melexis_mlx90640\*.c") do (
    arm-none-eabi-gcc %CFLAGS% -I"!SRC!" !SLAVE_INC! -x c -c "%%f" -o "!OUT!\%%~nf.o"
    if errorlevel 1 (echo   FAIL %%~nf.c failed to compile & set "SECTION_FAIL=1")
)
arm-none-eabi-g++ %CXXFLAGS% -I"!SRC!\melexis_mlx90641" -c "!SRC!\melexis_mlx90641\MLX90641_API.cpp" -o "!OUT!\MLX90641_API.o"
if errorlevel 1 (echo   FAIL MLX90641_API.cpp failed to compile & set "SECTION_FAIL=1")
for %%f in ("!SRC!\melexis_mlx90642\*.c") do (
    arm-none-eabi-gcc %CFLAGS% -I"!SRC!" !SLAVE_INC! -x c -c "%%f" -o "!OUT!\%%~nf.o"
    if errorlevel 1 (echo   FAIL %%~nf.c failed to compile & set "SECTION_FAIL=1")
)
if "!SECTION_FAIL!"=="1" (
    echo   FAIL expansion slave application: one or more source files failed to compile - see errors above
    set /a FAIL+=1
    goto :summary
)
arm-none-eabi-g++ %LDCOMMON% -fno-exceptions -fno-rtti -fno-unwind-tables -fno-threadsafe-statics -T"!SRC!\STM32F303CBTx_SLAVEAPP.ld" "%BUILD%\app_slave\startup.o" "%BUILD%\app_slave\system_stm32f3xx.o" "!OUT!\*.o" "%BUILD%\hal_obj\*.o" -lm -o "!OUT!\!SLAVE_APP_NAME!.elf"
if errorlevel 1 (
    echo   FAIL expansion slave application: link failed
    set /a FAIL+=1
    goto :summary
)
arm-none-eabi-objcopy -O binary "!OUT!\!SLAVE_APP_NAME!.elf" "!OUT!\!SLAVE_APP_NAME!.bin"
if errorlevel 1 (
    echo   FAIL expansion slave application: objcopy ^(.bin^) failed
    set /a FAIL+=1
    goto :summary
)
arm-none-eabi-objcopy -O ihex "!OUT!\!SLAVE_APP_NAME!.elf" "!OUT!\!SLAVE_APP_NAME!.hex"
if errorlevel 1 (
    echo   FAIL expansion slave application: objcopy ^(.hex^) failed
    set /a FAIL+=1
    goto :summary
)
copy /y "!OUT!\!SLAVE_APP_NAME!.elf" "%FIRMWARE_OUT%\" >nul
copy /y "!OUT!\!SLAVE_APP_NAME!.bin" "%FIRMWARE_OUT%\" >nul
copy /y "!OUT!\!SLAVE_APP_NAME!.hex" "%FIRMWARE_OUT%\" >nul
echo   OK   !SLAVE_APP_NAME!.bin/.hex/.elf built
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
echo Versioning: all 4 components built above ^(2 applications, 2
echo bootloaders^) are incremental - each had its own PATCH auto-bumped
echo ^(odometer-carry into MINOR/MAJOR^) by bump_version.py before
echo compiling, since every real build increments it automatically now.
echo Each application's own bump also mirrored the new version into its
echo bootloader's FIRMWARE_VERSION_* copy - see VERSION_CHECKLIST.txt
echo and CHANGELOG.md.

REM Keeps the window open when this script is double-clicked from Explorer,
REM on success AND on failure - every FAIL path above reaches this same
REM :summary label via `goto :summary`, so one `pause` here covers all of
REM them (same convention as sibling repo HYDRA-UMC's own build_firmware.bat).
REM `pause` itself already no-ops instead of hanging when stdin isn't a real
REM console (e.g. redirected from NUL or a pipe).
echo.
pause

endlocal
if %FAIL% GTR 0 exit /b 1
exit /b 0
