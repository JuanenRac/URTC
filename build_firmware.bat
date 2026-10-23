@echo off
REM HYDRA_UMC_SCRIPT_STANDARD_HEADER_BEGIN
REM *****************************************************************************
REM Project   : URTC
REM Script    : build_firmware.bat
REM Purpose   : Incremental firmware build and versioned artifact packaging workflow.
REM Author    : JuanenRac (Electro Hobby 3D)
REM Email     : electrohobby3d@gmail.com
REM Copyright : (C) 2026 JuanenRac
REM License   : GPL-3.0-or-later - see LICENSE
REM *****************************************************************************
REM HYDRA_UMC_SCRIPT_STANDARD_HEADER_END
REM HYDRA_UMC_SCRIPT_STANDARD_BANNER_BEGIN
echo.
echo *****************************************************************************
echo * URTC - build_firmware.bat
echo * Mode      : BUILD (no version change)
echo * Author    : JuanenRac (Electro Hobby 3D)
echo * Email     : electrohobby3d@gmail.com
echo * Copyright : (C) 2026 JuanenRac
echo * License   : GPL-3.0-or-later - see LICENSE
echo * ------------------------------------------------------------------------- *
echo * 1. Read the project version (compiling never changes it; run bump.py to raise it).
echo * 2. Run this project's declared build, verification and packaging commands.
echo * 3. Report the result and keep an interactive terminal open.
echo *****************************************************************************
echo.
REM HYDRA_UMC_SCRIPT_STANDARD_BANNER_END
setlocal enabledelayedexpansion
rem Keep the window open for people, never in automation (CI / URTC_CI).
set "KEEP_OPEN=pause"
if defined CI set "KEEP_OPEN=ver >nul"
if "%URTC_CI%"=="1" set "KEEP_OPEN=ver >nul"
REM HYDRA_UMC_SCRIPT_STANDARD_VERSION_STEP
echo [1/3] Reading the component versions - compiling never changes them, run bump.py first...
REM HYDRA_UMC_SCRIPT_STANDARD_VERSION_CAPTURE_BEFORE
for /f "usebackq delims=" %%V in (`python -c "import json; print(json.load(open(r'%~dp0urtc.project.json', encoding='utf-8'))['version'])"`) do set "HYDRA_UMC_VERSION_BEFORE=%%V"
echo.
set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "BUILD=%ROOT%\build"
set "FIRMWARE_OUT=%ROOT%\firmware"
REM Release mode writes to firmware\ (git-ignored, published only as release assets); CI mode writes to build\firmware.
if "%URTC_CI%"=="1" set "FIRMWARE_OUT=%BUILD%\firmware"
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
echo === 1b. Host logic tests (same suites as build_firmware.sh) ===
set "COVERAGE_ARGS="
if defined URTC_COVERAGE_FLOOR set "COVERAGE_ARGS=--coverage-floor %URTC_COVERAGE_FLOOR%"
python "%ROOT%\tools\run_host_tests.py" --build-dir "%BUILD%" %COVERAGE_ARGS%
if errorlevel 2 (
    echo   WARN host logic tests were not run: they need WSL or a Linux host
    set /a WARN+=1
) else if errorlevel 1 (
    echo   FAIL host logic tests FAILED
    set /a FAIL+=1
    goto :summary
) else (
    echo   OK   host logic tests passed
    set /a PASS+=1
)

REM -----------------------------------------------------------------------
echo.
echo === 1c. Shared Melexis drivers identical on master and slave ===
python "%ROOT%\check_shared_drivers.py"
if errorlevel 1 (
    echo   FAIL vendored MLX9064x drivers differ between src\F303-master and src\F303-slave
    set /a FAIL+=1
    goto :summary
) else (
    echo   OK   vendored MLX9064x drivers identical on both chips
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
set "CFLAGS=-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -DSTM32F303xC -DUSE_HAL_DRIVER -I%BUILD%\common\CMSIS_Include -I%BUILD%\common\HAL_Include -I%ROOT%\src\common -O2 -Wall -ffunction-sections -fdata-sections"
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

REM The firmware directory is one coherent build set. Remove only generated
REM URTC artifacts and its generated manifest; preserve any other files.
echo.
echo === Firmware output cleanup ===
del /q "%FIRMWARE_OUT%\URTC_*.bin" 2>nul
del /q "%FIRMWARE_OUT%\URTC_*.elf" 2>nul
del /q "%FIRMWARE_OUT%\URTC_*.hex" 2>nul
del /q "%FIRMWARE_OUT%\firmware_manifest.json" 2>nul
del /q "%FIRMWARE_OUT%\URTC_*.bin.sha256" 2>nul
del /q "%FIRMWARE_OUT%\URTC_*.bin.manifest.json" 2>nul
echo   OK   old generated firmware artifacts removed from firmware\

echo.
echo === 3b. Firmware signing keys ^(docs\FIRMWARE_SIGNING.TXT^) ===
REM The bootloaders get their HMAC key injected from the PRIVATE file named
REM by URTC_SIGNING_KEY_FILE - never committed. Without it: the PUBLIC
REM development key (bench boards only). URTC_RELEASE=1 refuses that.
python "%ROOT%\firmware_signing.py" check
if errorlevel 1 (
    echo   FAIL signing keys unavailable - see the message above, nothing built
    set /a FAIL+=1
    goto :summary
)
set "MASTER_KEY_FLAGS="
set "SLAVE_KEY_FLAGS="
for /f "usebackq delims=" %%k in (`python "%ROOT%\firmware_signing.py" cflags master`) do set "MASTER_KEY_FLAGS=%%k"
for /f "usebackq delims=" %%k in (`python "%ROOT%\firmware_signing.py" cflags slave`) do set "SLAVE_KEY_FLAGS=%%k"

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
REM The bootloader version is read from its header (raise it first with
REM bump.py). The command's own stdout IS the current "MAJOR.MINOR.PATCH" -
REM captured below via a for /f on its own stdout, directly into the
REM output filename, so the header is never re-read a second time just to
REM get the same number back. Filename convention
REM URTC_MAIN_BOOTLOADER_v{MAJOR}.{MINOR}.{PATCH} matches HYDRA-UMC's own
REM <PREFIX>_v<MAJOR>.<MINOR>.<PATCH> pattern - see CHANGELOG.md for why
REM this replaced the old unversioned URTC_BOOTLOADER name.
set "BOOT_VER="
for /f "usebackq delims=" %%v in (`python "%ROOT%\bump_version.py" --current "!SRC!\bootloader_common.h" BOOTLOADER_VERSION`) do set "BOOT_VER=%%v"
if not defined BOOT_VER (
    echo   FAIL bootloader version lookup failed - see traceback above
    set /a FAIL+=1
    goto :summary
)
set "BOOT_NAME=URTC_MAIN_BOOTLOADER_v!BOOT_VER!"
set "SECTION_FAIL=0"
for %%f in ("!SRC!\*.c") do (
    arm-none-eabi-gcc %CFLAGS% !MASTER_KEY_FLAGS! -I"!SRC!" -x c -c "%%f" -o "!OUT!\%%~nf.o"
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
REM The application version is read from its header (bump.py also mirrors a
REM new one into the bootloader copy). Same stdout-capture
REM pattern as the bootloader above - filename convention
REM URTC_MAIN_FIRMWARE_v{MAJOR}.{MINOR}.{PATCH} replaces the old
REM URTC_V{MAJOR}.{MINOR}_F303CC (no PATCH) name.
set "APP_VER="
for /f "usebackq delims=" %%v in (`python "%ROOT%\bump_version.py" --current "!SRC!\firmware_common.h" FIRMWARE_VERSION`) do set "APP_VER=%%v"
if not defined APP_VER (
    echo   FAIL application version lookup failed - see traceback above
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
REM Same read-only version lookup as the main board bootloader above. Same stdout-capture
REM pattern too - filename convention URTC_SLAVE_BOOTLOADER_v{MAJOR}.
REM {MINOR}.{PATCH} replaces the old unversioned URTC_SLAVE_BOOTLOADER name.
set "SLAVE_BOOT_VER="
for /f "usebackq delims=" %%v in (`python "%ROOT%\bump_version.py" --current "!SRC!\slaveboot_common.h" BOOTLOADER_VERSION`) do set "SLAVE_BOOT_VER=%%v"
if not defined SLAVE_BOOT_VER (
    echo   FAIL bootloader version lookup failed - see traceback above
    set /a FAIL+=1
    goto :summary
)
set "SLAVE_BOOT_NAME=URTC_SLAVE_BOOTLOADER_v!SLAVE_BOOT_VER!"
set "SECTION_FAIL=0"
for %%f in ("!SRC!\*.c") do (
    arm-none-eabi-gcc %CFLAGS% !SLAVE_KEY_FLAGS! -I"!SRC!" -x c -c "%%f" -o "!OUT!\%%~nf.o"
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
REM Same read-only version lookup as the main board application above. Same stdout-capture pattern - filename convention
REM URTC_SLAVE_FIRMWARE_v{MAJOR}.{MINOR}.{PATCH} replaces the old
REM unversioned URTC_SLAVE_APP name.
set "SLAVE_APP_VER="
for /f "usebackq delims=" %%v in (`python "%ROOT%\bump_version.py" --current "!SRC!\slave_common.h" FIRMWARE_VERSION`) do set "SLAVE_APP_VER=%%v"
if not defined SLAVE_APP_VER (
    echo   FAIL application version lookup failed - see traceback above
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
python "%ROOT%\generate_manifest.py" "%ROOT%" "%FIRMWARE_OUT%"
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
echo Versioning: this build did not change any version. Raise the version of
echo a component explicitly with 'python bump.py' BEFORE building ^(an
echo application bump also mirrors the new version into its bootloader's
echo FIRMWARE_VERSION_* copy^) - see VERSION_CHECKLIST.txt and CHANGELOG.md.

REM Keeps the window open when this script is double-clicked from Explorer,
REM on success AND on failure - every FAIL path above reaches this same
REM :summary label via `goto :summary`, so one `pause` here covers all of
REM them (same convention as sibling repo HYDRA-UMC's own build_firmware.bat).
REM `pause` itself already no-ops instead of hanging when stdin isn't a real
REM console (e.g. redirected from NUL or a pipe).
echo.
%KEEP_OPEN%

endlocal
if %FAIL% GTR 0 exit /b 1
exit /b 0
