```bat
@echo off
setlocal EnableExtensions

title LOADUCK Updater

set "OWNER=slipptech-tech"
set "REPO=LOADUCK"
set "API=https://api.github.com/repos/%OWNER%/%REPO%/releases/latest"

set "APP=%~dp0LOADUCK.exe"
set "TEMP_EXE=%TEMP%\LOADUCK_update_%RANDOM%.exe"
set "TEMP_JSON=%TEMP%\LOADUCK_release_%RANDOM%.json"

echo.
echo ==============================================
echo              LOADUCK UPDATER
echo ==============================================
echo.
echo Checking GitHub for updates...
echo.

:: ------------------------------------------------
:: Check that LOADUCK.exe exists
:: ------------------------------------------------
if not exist "%APP%" (
    echo ERROR: LOADUCK.exe was not found.
    echo.
    echo Put this updater in the same folder as LOADUCK.exe.
    echo.
    pause
    exit /b 1
)

:: ------------------------------------------------
:: Download GitHub release information
:: ------------------------------------------------
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ^
    "$ErrorActionPreference='Stop';" ^
    "$ProgressPreference='SilentlyContinue';" ^
    "$headers=@{'Accept'='application/vnd.github+json';'User-Agent'='LOADUCK-Updater'};" ^
    "Invoke-WebRequest -Uri '%API%' -Headers $headers -OutFile '%TEMP_JSON%'"

if errorlevel 1 (
    echo ERROR: Could not connect to GitHub.
    echo Check your internet connection.
    echo.
    del "%TEMP_JSON%" >nul 2>&1
    pause
    exit /b 1
)

:: ------------------------------------------------
:: Get latest version
:: ------------------------------------------------
set "LATEST="

for /f "delims=" %%A in ('powershell.exe -NoProfile -Command ^
    "$j=Get-Content -Raw '%TEMP_JSON%' | ConvertFrom-Json; $j.tag_name"') do (
    set "LATEST=%%A"
)

if not defined LATEST (
    echo ERROR: Could not read latest GitHub version.
    echo.
    del "%TEMP_JSON%" >nul 2>&1
    pause
    exit /b 1
)

echo Latest version: %LATEST%
echo.

:: ------------------------------------------------
:: Get current EXE SHA256
:: ------------------------------------------------
set "LOCAL_HASH="

for /f "delims=" %%A in ('powershell.exe -NoProfile -Command ^
    "(Get-FileHash -LiteralPath '%APP%' -Algorithm SHA256).Hash"') do (
    set "LOCAL_HASH=%%A"
)

if not defined LOCAL_HASH (
    echo ERROR: Could not calculate local file hash.
    echo.
    del "%TEMP_JSON%" >nul 2>&1
    pause
    exit /b 1
)

echo Current file SHA256:
echo %LOCAL_HASH%
echo.

:: ------------------------------------------------
:: Get GitHub download URL
:: ------------------------------------------------
set "DOWNLOAD_URL="

for /f "delims=" %%A in ('powershell.exe -NoProfile -Command ^
    "$j=Get-Content -Raw '%TEMP_JSON%' | ConvertFrom-Json;" ^
    "$a=$j.assets | Where-Object {$_.name -ieq 'LOADUCK.exe'} | Select-Object -First 1;" ^
    "if($a){$a.browser_download_url}"') do (
    set "DOWNLOAD_URL=%%A"
)

if not defined DOWNLOAD_URL (
    echo ERROR: LOADUCK.exe was not found in the latest Release.
    echo.
    echo Make sure the Release contains an asset named:
    echo LOADUCK.exe
    echo.
    del "%TEMP_JSON%" >nul 2>&1
    pause
    exit /b 1
)

:: ------------------------------------------------
:: Download latest EXE to temporary location
:: ------------------------------------------------
echo Downloading latest LOADUCK.exe...
echo.

powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ^
    "$ErrorActionPreference='Stop';" ^
    "$ProgressPreference='SilentlyContinue';" ^
    "$headers=@{'User-Agent'='LOADUCK-Updater';'Accept'='application/octet-stream'};" ^
    "Invoke-WebRequest -Uri '%DOWNLOAD_URL%' -Headers $headers -OutFile '%TEMP_EXE%'"

if errorlevel 1 (
    echo.
    echo ERROR: Download failed.
    echo.
    del "%TEMP_JSON%" >nul 2>&1
    del "%TEMP_EXE%" >nul 2>&1
    pause
    exit /b 1
)

:: ------------------------------------------------
:: Verify downloaded file
:: ------------------------------------------------
set "NEW_HASH="

for /f "delims=" %%A in ('powershell.exe -NoProfile -Command ^
    "(Get-FileHash -LiteralPath '%TEMP_EXE%' -Algorithm SHA256).Hash"') do (
    set "NEW_HASH=%%A"
)

if not defined NEW_HASH (
    echo ERROR: Could not verify downloaded file.
    del "%TEMP_EXE%" >nul 2>&1
    del "%TEMP_JSON%" >nul 2>&1
    pause
    exit /b 1
)

echo Downloaded SHA256:
echo %NEW_HASH%
echo.

:: ------------------------------------------------
:: Compare hashes
:: ------------------------------------------------
if /I "%LOCAL_HASH%"=="%NEW_HASH%" (
    echo ==============================================
    echo LOADUCK IS ALREADY UP TO DATE
    echo ==============================================
    echo.
    echo Version: %LATEST%
    echo No changes were made.
    echo.

    del "%TEMP_JSON%" >nul 2>&1
    del "%TEMP_EXE%" >nul 2>&1

    pause
    exit /b 0
)

echo New version detected!
echo.
echo Updating LOADUCK...
echo.

:: ------------------------------------------------
:: Close LOADUCK if running
:: ------------------------------------------------
tasklist /FI "IMAGENAME eq LOADUCK.exe" 2>NUL | find /I "LOADUCK.exe" >NUL

if not errorlevel 1 (
    echo LOADUCK is currently running.
    echo Closing LOADUCK...

    taskkill /IM LOADUCK.exe /T /F >nul 2>&1

    timeout /t 2 /nobreak >nul
)

:: ------------------------------------------------
:: Create backup
:: ------------------------------------------------
set "BACKUP=%APP%.backup"

if exist "%BACKUP%" (
    del /F /Q "%BACKUP%" >nul 2>&1
)

echo Creating backup...

copy /Y "%APP%" "%BACKUP%" >nul

if errorlevel 1 (
    echo ERROR: Could not create backup.
    echo.
    del "%TEMP_JSON%" >nul 2>&1
    del "%TEMP_EXE%" >nul 2>&1
    pause
    exit /b 1
)

:: ------------------------------------------------
:: Replace executable
:: ------------------------------------------------
echo Installing new version...

move /Y "%TEMP_EXE%" "%APP%" >nul

if errorlevel 1 (
    echo.
    echo ERROR: Could not replace LOADUCK.exe.
    echo Restoring backup...

    copy /Y "%BACKUP%" "%APP%" >nul

    del "%TEMP_JSON%" >nul 2>&1
    del "%TEMP_EXE%" >nul 2>&1

    pause
    exit /b 1
)

:: ------------------------------------------------
:: Verify installed file
:: ------------------------------------------------
set "FINAL_HASH="

for /f "delims=" %%A in ('powershell.exe -NoProfile -Command ^
    "(Get-FileHash -LiteralPath '%APP%' -Algorithm SHA256).Hash"') do (
    set "FINAL_HASH=%%A"
)

if /I not "%FINAL_HASH%"=="%NEW_HASH%" (
    echo.
    echo ERROR: Installed file verification failed!
    echo Restoring previous version...

    copy /Y "%BACKUP%" "%APP%" >nul

    del "%TEMP_JSON%" >nul 2>&1
    del "%TEMP_EXE%" >nul 2>&1

    pause
    exit /b 1
)

:: ------------------------------------------------
:: Cleanup
:: ------------------------------------------------
del "%BACKUP%" >nul 2>&1
del "%TEMP_JSON%" >nul 2>&1

echo.
echo ==============================================
echo           UPDATE SUCCESSFUL
echo ==============================================
echo.
echo Version: %LATEST%
echo.
echo LOADUCK has been updated successfully.
echo.

:: ------------------------------------------------
:: Start LOADUCK
:: ------------------------------------------------
start "" "%APP%"

echo LOADUCK started.
echo.

timeout /t 2 /nobreak >nul
exit /b 0
```