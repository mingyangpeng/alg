@echo off
setlocal enabledelayedexpansion
rem ============================================================
rem  Image2DAlg one-shot verification (read-only).
rem  Usage:  scripts\verify_image2d.cmd [build_dir]   (default: build)
rem  Checks: test exe / dumpbin exports / dumpbin dependents /
rem          ctest / delivery checklist
rem  ASCII-only output on purpose (avoid console codepage issues).
rem ============================================================

for %%I in ("%~dp0..") do set "ROOT=%%~fI"
cd /d "%ROOT%"
set "BUILDDIR=%~1"
if "%BUILDDIR%"=="" set "BUILDDIR=build"
set "OUT=%ROOT%\%BUILDDIR%\output\Release"
set "TMPD=%TEMP%\verify_image2d"
if not exist "%TMPD%" mkdir "%TMPD%" >nul 2>&1

set "FAILED=0"
echo.
echo ============================================================
echo  ROOT = %ROOT%
echo  OUT  = %OUT%
echo  TMP  = %TMPD%
echo ============================================================

echo.
echo [0] artifacts
if exist "%OUT%\Image2DAlg.dll" (echo     OK   Image2DAlg.dll) else (echo     MISS Image2DAlg.dll & set "FAILED=1")
if exist "%OUT%\Image2DAlg_test.exe" (echo     OK   Image2DAlg_test.exe) else (echo     MISS Image2DAlg_test.exe & set "FAILED=1")
if exist "%OUT%\ShapelyAlg.dll" (echo     OK   ShapelyAlg.dll) else (echo     MISS ShapelyAlg.dll & set "FAILED=1")

rem ------------------------------------------------------------
echo.
echo [1] run Image2DAlg_test.exe
if not exist "%OUT%\Image2DAlg_test.exe" (
    echo     SKIP - missing exe
    set "FAILED=1"
    goto :exports
)
pushd "%OUT%"
Image2DAlg_test.exe > "%TMPD%\test.log" 2>&1
set "TEXIT=%ERRORLEVEL%"
popd
rem Count with findstr as the for /f child (one process, terminates on its own).
rem NEVER use plain `find` here: when invoked from Git Bash the PATH contains
rem MSYS find (GNU), which treats /c as a path and recurses the whole drive.
rem Also avoid pipes inside for /f (fragile under this console host).
set "NPASS=0"
set "NFAIL=0"
for /f %%c in ('findstr /c:"[PASS]" "%TMPD%\test.log"') do set /a NPASS+=1
for /f %%c in ('findstr /c:"[FAIL]" "%TMPD%\test.log"') do set /a NFAIL+=1
echo     exit   = !TEXIT!
echo     PASS   = !NPASS!
echo     FAIL   = !NFAIL!
if not "!TEXIT!"=="0" set "FAILED=1"
if not "!NFAIL!"=="0" set "FAILED=1"
echo     ---- last 8 lines ----
powershell -NoProfile -Command "Get-Content -LiteralPath '%TMPD%\test.log' -Tail 8"

rem ------------------------------------------------------------
:exports
echo.
echo [2] dumpbin /exports
rem Locate dumpbin: PATH first, then vswhere -> MSVC toolchain dir (no PATH dependency).
rem NOTE 1: this file must stay ASCII-only (cmd reads it in OEM codepage; UTF-8 bytes
rem         can swallow the following CRLF under GBK and desync the parser).
rem NOTE 2: ProgramFiles(x86) contains a ')' - never expand it inside a ( ) block;
rem         use delayed expansion at statement level only.
set "DUMPBIN="
where dumpbin >nul 2>&1 && for /f "delims=" %%i in ('where dumpbin') do if not defined DUMPBIN set "DUMPBIN=%%i"
if not defined DUMPBIN set "VSWHERE=!ProgramFiles(x86)!\Microsoft Visual Studio\Installer\vswhere.exe"
if not defined DUMPBIN if exist "!VSWHERE!" for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do set "VSPATH=%%i"
if not defined DUMPBIN if defined VSPATH for /d %%d in ("!VSPATH!\VC\Tools\MSVC\*") do if exist "%%d\bin\Hostx64\x64\dumpbin.exe" set "DUMPBIN=%%d\bin\Hostx64\x64\dumpbin.exe"
if not defined DUMPBIN (
    echo     SKIP - dumpbin not found
    set "FAILED=1"
    goto :dependents
)
echo     dumpbin = !DUMPBIN!
rem dumpbin output can come back truncated (missing Summary + tail of the
rem export table under load): retry up to 3 times until the Summary trailer
rem is present. ASCII-only file by design.
set "TRY=0"
:dump_exports
set /a TRY+=1
"!DUMPBIN!" /nologo /exports "%OUT%\Image2DAlg.dll" > "%TMPD%\exports.txt" 2>&1
findstr /c:" Summary" "%TMPD%\exports.txt" >nul 2>&1
if errorlevel 1 if !TRY! LSS 3 (
    ping -n 2 127.0.0.1 >nul
    goto :dump_exports
)
set "MISSING="
for %%s in (Image2DAlg_ToGray Image2DAlg_Gamma Image2DAlg_Threshold Image2DAlg_Blur Image2DAlg_Morphology Image2DAlg_Canny Image2DAlg_Resize Image2DAlg_Crop Image2DAlg_Rotate Image2DAlg_Enhance Image2DAlg_ConnectedComponents Image2DAlg_AnalyzeContours Image2DAlg_MatchTemplate Image2DAlg_FreeBlobs Image2DAlg_FreeContours Image2DAlg_FastFeatures Image2DAlg_ComputeDescriptors Image2DAlg_MatchFeatures Image2DAlg_EstimateAffinePartial Image2DAlg_FindHomography Image2DAlg_SolvePnP Image2DAlg_ComuiFeatureAlign) do (
    findstr /c:"%%s" "%TMPD%\exports.txt" >nul 2>&1
    if errorlevel 1 (set "MISSING=!MISSING! %%s") else (echo     OK   %%s)
)
if defined MISSING (
    echo     MISSING:!MISSING!
    set "FAILED=1"
)

rem ------------------------------------------------------------
:dependents
echo.
echo [3] dumpbin /dependents
if not defined DUMPBIN (
    echo     SKIP - dumpbin not found
    goto :ctest
)
rem /dependents has produced a 0-byte file before: retry while empty (max 3).
set "TRY=0"
:dump_dependents
set /a TRY+=1
"!DUMPBIN!" /nologo /dependents "%OUT%\Image2DAlg.dll" > "%TMPD%\dependents.txt" 2>&1
for %%A in ("%TMPD%\dependents.txt") do set "DSZ=%%~zA"
if "!DSZ!"=="0" if !TRY! LSS 3 (ping -n 2 127.0.0.1 >nul & goto :dump_dependents)
for %%d in (ShapelyAlg.dll CommonAlg.dll opencv_world440.dll) do (
    findstr /i /c:"%%d" "%TMPD%\dependents.txt" >nul 2>&1
    if errorlevel 1 (echo     MISS %%d & set "FAILED=1") else (echo     OK   %%d)
)

rem ------------------------------------------------------------
:ctest
echo.
echo [4] ctest
ctest --test-dir "%ROOT%\%BUILDDIR%" -C Release --output-on-failure > "%TMPD%\ctest.log" 2>&1
set "CEXIT=%ERRORLEVEL%"
echo     exit = !CEXIT!
findstr /c:"tests passed" "%TMPD%\ctest.log"
echo     ---- per test ----
findstr /r /c:"Passed" /c:"Failed" /c:"Not Run" "%TMPD%\ctest.log"
if not "!CEXIT!"=="0" set "FAILED=1"

rem ------------------------------------------------------------
echo.
echo [5] delivery checklist
python -I "%ROOT%\scripts\make_delivery_checklist.py" "%OUT%" > "%TMPD%\check.txt" 2>&1
set "PEXIT=%ERRORLEVEL%"
echo     exit = !PEXIT!
if not "!PEXIT!"=="0" set "FAILED=1"
powershell -NoProfile -Command "Get-Content -LiteralPath '%TMPD%\check.txt' -Tail 12"

rem ------------------------------------------------------------
echo.
echo [6] version sync
python -I "%ROOT%\scripts\check_version_sync.py" Image2DAlg > "%TMPD%\ver.log" 2>&1
set "VEXIT=%ERRORLEVEL%"
echo     exit = !VEXIT!
findstr /c:"PASS" "%TMPD%\ver.log"
findstr /c:"[MISS]" "%TMPD%\ver.log"
if not "!VEXIT!"=="0" set "FAILED=1"

rem ------------------------------------------------------------
echo.
echo ============================================================
if "!FAILED!"=="0" (echo  RESULT: ALL CHECKS PASSED) else (echo  RESULT: SOMETHING FAILED - see above)
echo  logs: %TMPD%
echo ============================================================
endlocal & if "%FAILED%"=="0" (exit /b 0) else (exit /b 1)
