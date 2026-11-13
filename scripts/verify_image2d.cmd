@echo off
setlocal enabledelayedexpansion
rem ============================================================
rem  Image2DAlg one-shot verification (read-only).
rem  Usage:  scripts\verify_image2d.cmd
rem  Checks: test exe / dumpbin exports / dumpbin dependents /
rem          ctest / delivery checklist
rem  ASCII-only output on purpose (avoid console codepage issues).
rem ============================================================

for %%I in ("%~dp0..") do set "ROOT=%%~fI"
cd /d "%ROOT%"
set "OUT=%ROOT%\build\output\Release"
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
    echo     SKIP (missing exe)
    set "FAILED=1"
    goto :exports
)
pushd "%OUT%"
Image2DAlg_test.exe > "%TMPD%\test.log" 2>&1
set "TEXIT=%ERRORLEVEL%"
popd
for /f %%c in ('findstr /c:"[PASS]" "%TMPD%\test.log" ^| find /c /v ""') do set "NPASS=%%c"
for /f %%c in ('findstr /c:"[FAIL]" "%TMPD%\test.log" ^| find /c /v ""') do set "NFAIL=%%c"
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
set "DUMPBIN="
where dumpbin >nul 2>&1 && for /f "delims=" %%i in ('where dumpbin') do if not defined DUMPBIN set "DUMPBIN=%%i"
if not defined DUMPBIN (
    for /f "usebackq delims=" %%i in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-ChildItem -Path 'C:\Program Files\Microsoft Visual Studio','C:\Program Files (x86)\Microsoft Visual Studio' -Recurse -Filter dumpbin.exe -ErrorAction SilentlyContinue ^| Select-Object -First 1 -ExpandProperty FullName"`) do set "DUMPBIN=%%i"
)
if not defined DUMPBIN (
    echo     SKIP (dumpbin not found)
    set "FAILED=1"
    goto :dependents
)
echo     dumpbin = !DUMPBIN!
"!DUMPBIN!" /nologo /exports "%OUT%\Image2DAlg.dll" > "%TMPD%\exports.txt" 2>&1
set "MISSING="
for %%s in (Image2DAlg_ToGray Image2DAlg_Gamma Image2DAlg_Threshold Image2DAlg_Blur Image2DAlg_Morphology Image2DAlg_Canny Image2DAlg_Resize Image2DAlg_Crop Image2DAlg_Rotate Image2DAlg_Enhance Image2DAlg_ConnectedComponents Image2DAlg_AnalyzeContours Image2DAlg_MatchTemplate Image2DAlg_FreeBlobs Image2DAlg_FreeContours) do (
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
    echo     SKIP (dumpbin not found)
    goto :ctest
)
"!DUMPBIN!" /nologo /dependents "%OUT%\Image2DAlg.dll" > "%TMPD%\dependents.txt" 2>&1
for %%d in (ShapelyAlg.dll CommonAlg.dll opencv_world440.dll) do (
    findstr /i /c:"%%d" "%TMPD%\dependents.txt" >nul 2>&1
    if errorlevel 1 (echo     MISS %%d & set "FAILED=1") else (echo     OK   %%d)
)

rem ------------------------------------------------------------
:ctest
echo.
echo [4] ctest
ctest --test-dir "%ROOT%\build" -C Release --output-on-failure > "%TMPD%\ctest.log" 2>&1
set "CEXIT=%ERRORLEVEL%"
echo     exit = !CEXIT!
findstr /c:"tests passed" "%TMPD%\ctest.log"
echo     ---- per test ----
findstr /r /c:"Passed" /c:"Failed" /c:"Not Run" "%TMPD%\ctest.log"
if not "!CEXIT!"=="0" set "FAILED=1"

rem ------------------------------------------------------------
echo.
echo [5] delivery checklist
python -I "%ROOT%\scripts\make_delivery_checklist.py" > "%TMPD%\check.txt" 2>&1
set "PEXIT=%ERRORLEVEL%"
echo     exit = !PEXIT!
if not "!PEXIT!"=="0" set "FAILED=1"
powershell -NoProfile -Command "Get-Content -LiteralPath '%TMPD%\check.txt' -Tail 12"

rem ------------------------------------------------------------
echo.
echo ============================================================
if "!FAILED!"=="0" (echo  RESULT: ALL CHECKS PASSED) else (echo  RESULT: SOMETHING FAILED - see above)
echo  logs: %TMPD%
echo ============================================================
endlocal & if "%FAILED%"=="0" (exit /b 0) else (exit /b 1)
