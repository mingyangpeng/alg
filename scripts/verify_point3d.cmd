@echo off
setlocal enabledelayedexpansion
rem ============================================================
rem  Point3DAlg one-shot verification (read-only).
rem  Usage:  scripts\verify_point3d.cmd [build_dir]   (default: build)
rem  Checks: artifacts / test exe / dumpbin exports / dumpbin
rem          dependents / ctest / delivery checklist
rem  ASCII-only on purpose: cmd reads this file in the OEM codepage,
rem  non-ASCII bytes desync the parser (see verify_image2d.cmd notes).
rem  Rules learned the hard way (keep them):
rem    - no bare ')' inside echo text within ( ) blocks
rem    - never use plain `find` (MSYS GNU find shadows it under Git Bash)
rem    - no pipes inside for /f
rem    - %ProgramFiles(x86)% only via !...! at statement level
rem ============================================================

for %%I in ("%~dp0..") do set "ROOT=%%~fI"
cd /d "%ROOT%"
set "BUILDDIR=%~1"
if "%BUILDDIR%"=="" set "BUILDDIR=build"
set "OUT=%ROOT%\%BUILDDIR%\output\Release"
set "TMPD=%TEMP%\verify_point3d"
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
if exist "%OUT%\Point3DAlg.dll" (echo     OK   Point3DAlg.dll) else (echo     MISS Point3DAlg.dll & set "FAILED=1")
if exist "%OUT%\Point3DAlg_test.exe" (echo     OK   Point3DAlg_test.exe) else (echo     MISS Point3DAlg_test.exe & set "FAILED=1")
if exist "%OUT%\ShapelyAlg.dll" (echo     OK   ShapelyAlg.dll) else (echo     MISS ShapelyAlg.dll & set "FAILED=1")
if exist "%OUT%\CommonAlg.dll" (echo     OK   CommonAlg.dll) else (echo     MISS CommonAlg.dll & set "FAILED=1")
if exist "%OUT%\opencv_world440.dll" (echo     OK   opencv_world440.dll) else (echo     MISS opencv_world440.dll & set "FAILED=1")

rem ------------------------------------------------------------
echo.
echo [1] run Point3DAlg_test.exe
if not exist "%OUT%\Point3DAlg_test.exe" (
    echo     SKIP - missing exe
    set "FAILED=1"
    goto :exports
)
pushd "%OUT%"
Point3DAlg_test.exe > "%TMPD%\test.log" 2>&1
set "TEXIT=%ERRORLEVEL%"
popd
rem The SICK data section needs Z:\ScanImage - without data the test prints
rem SKIP lines and still exits 0; judge by exit code and FAIL count only.
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
"!DUMPBIN!" /nologo /exports "%OUT%\Point3DAlg.dll" > "%TMPD%\exports.txt" 2>&1
set "MISSING="
for %%s in (Point3DAlg_GetVersion Point3DAlg_Init Point3DAlg_Cleanup Point3DAlg_ReadSickCloud Point3DAlg_FreeBuffer Point3DAlg_FreeCloud Point3DAlg_LoadCloud Point3DAlg_SaveCloudEx Point3DAlg_FilterCloud Point3DAlg_ExtractCloud Point3DAlg_TransformCloud Point3DAlg_FitModel Point3DAlg_SegmentCloud Point3DAlg_FreeClouds Point3DAlg_KnnSearch Point3DAlg_RadiusSearch Point3DAlg_MeasureCloud Point3DAlg_EstimateNormals Point3DAlg_DetectKeypoints Point3DAlg_ReconstructSurface Point3DAlg_MeshOp Point3DAlg_CloudToHeightMap Point3DAlg_FreeMesh Point3DAlg_FreeHeightMap Point3DAlg_LoadSickConfig Point3DAlg_StitchQuality Point3DAlg_CreateSickReader Point3DAlg_ComuiReadSick Point3DAlg_ComuiVoxel Point3DAlg_ComuiLoadCloud Point3DAlg_ComuiFilterCloud Point3DAlg_ComuiHeightMap Point3DAlg_ComuiDetectKeypoints Point3DAlg_ComuiFitModel) do (
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
"!DUMPBIN!" /nologo /dependents "%OUT%\Point3DAlg.dll" > "%TMPD%\dependents.txt" 2>&1
for %%d in (CommonAlg.dll ShapelyAlg.dll opencv_world440.dll pcl_common_release.dll) do (
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
echo ============================================================
if "!FAILED!"=="0" (echo  RESULT: ALL CHECKS PASSED) else (echo  RESULT: SOMETHING FAILED - see above)
echo  logs: %TMPD%
echo ============================================================
endlocal & if "%FAILED%"=="0" (exit /b 0) else (exit /b 1)
