@echo off
setlocal

set "worker=%~dp0out\build\win-msvc-debug\sledgehammer_editor.exe"
if /i "%~1"=="--help" goto launch
if not exist "%worker%" (
    >&2 echo Sledgehammer Editor worker is missing at "%worker%".
    >&2 echo From the checkout root, run these commands in an x64 Visual Studio developer prompt:
    >&2 echo   cmake --preset win-msvc-debug -DCATACLYSMX_BUILD_SLEDGEHAMMER_EDITOR=ON
    >&2 echo   cmake --build --preset win-msvc-debug --target sledgehammer_editor
    exit /b 1
)

:launch
py -3 --version >nul 2>&1
if errorlevel 1 goto python
py -3 "%~dp0tools\sledgehammer_editor\serve.py" --worker "%worker%" %*
exit /b %errorlevel%

:python
python "%~dp0tools\sledgehammer_editor\serve.py" --worker "%worker%" %*
exit /b %errorlevel%
