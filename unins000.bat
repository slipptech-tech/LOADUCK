
@echo off
setlocal EnableExtensions EnableDelayedExpansion
title LOADUCK Uninstaller
mode con: cols=80 lines=30
color 0F
cd /d "%~dp0"

:MENU
cls
echo.
echo                  ====================================
echo                           LOADUCK
echo                         UNINSTALLER
echo                  ====================================
echo.
echo.
echo                This will delete all files and folders
echo                in the current directory, except this BAT.
echo.
echo                Directory:
echo                %~dp0
echo.
echo.
echo                         Are you sure?
echo.
echo                         [Y] Yes
echo                         [N] No
echo.

choice /c YN /n /m "                       Select Y or N: "

if errorlevel 2 goto CANCEL
if errorlevel 1 goto REMOVE

:CANCEL
cls
echo.
echo                  ====================================
echo                    UNINSTALLATION CANCELLED
echo                  ====================================
echo.
timeout /t 2 /nobreak >nul
exit /b

:REMOVE
cls
echo.
echo                  ====================================
echo                      REMOVING LOADUCK FILES
echo                  ====================================
echo.

set /a COUNT=0
for /f "delims=" %%A in ('dir /a /b 2^>nul') do (
    if /i not "%%~fA"=="%~f0" set /a COUNT+=1
)

if !COUNT! EQU 0 goto DONE

set /a CURRENT=0

for /f "delims=" %%A in ('dir /a /b 2^>nul') do (
    if /i not "%%~fA"=="%~f0" (
        set /a CURRENT+=1
        set /a PERCENT=CURRENT*100/COUNT
        set /a BARS=PERCENT/2

        if !PERCENT! LSS 25 (
            color 0C
        ) else if !PERCENT! LSS 50 (
            color 06
        ) else if !PERCENT! LSS 75 (
            color 0E
        ) else (
            color 0A
        )

        if exist "%%A\*" (
            rd /s /q "%%A" 2>nul
        ) else (
            del /f /q "%%A" 2>nul
        )

        cls
        echo.
        echo                  ====================================
        echo                      REMOVING LOADUCK FILES
        echo                  ====================================
        echo.
        echo.
        echo                         Progress: !PERCENT!%%
        echo.
        <nul set /p "=                         ["
        for /l %%B in (1,1,!BARS!) do <nul set /p "=#"
        for /l %%B in (!BARS!,1,49) do <nul set /p "=-"
        echo ] 
        echo.
        echo                         !CURRENT! / !COUNT! items
        echo.
    )
)

:DONE
color 0A
cls
echo.
echo                  ====================================
echo                    UNINSTALLATION COMPLETED
echo                  ====================================
echo.
echo                  The uninstaller BAT was preserved.
echo.
pause
exit /b
