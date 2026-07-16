@echo off
setlocal
pushd "%~dp0"

where cl.exe >nul 2>nul
if errorlevel 1 (
    echo Run this script from an MSVC Native Tools Command Prompt.
    popd
    exit /b 1
)

cl.exe /nologo /W4 /EHsc /MT /DUNICODE /D_UNICODE ^
    remove_net_device.cpp network_device_remover.cpp ^
    /Fe:remove-net-device.exe /link setupapi.lib advapi32.lib

set "result=%errorlevel%"
popd
exit /b %result%
