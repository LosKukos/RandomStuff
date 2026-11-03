@echo off
:: ============================================================
:: Dashboard Remote Access - ZeroTier Port Forward Setup
:: Spustit jako Administrator
:: ============================================================

net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Tento skript vyzaduje admin prava.
    echo Klikni pravy tlacitko a zvol "Spustit jako spravce".
    pause
    exit /b 1
)

echo Nastavuji port forwarding na ZeroTier adapteru...
echo.

:: Smazat stare pravidla pokud existuji
netsh interface portproxy delete v4tov4 listenaddress=172.27.167.45 listenport=8080 >nul 2>&1
netsh interface portproxy delete v4tov4 listenaddress=172.27.167.45 listenport=7070 >nul 2>&1
netsh interface portproxy delete v4tov4 listenaddress=172.27.167.45 listenport=9090 >nul 2>&1

:: Smazat stara firewall pravidla
netsh advfirewall firewall delete rule name="Dash Remote 8080" >nul 2>&1
netsh advfirewall firewall delete rule name="Dash Remote 7070" >nul 2>&1
netsh advfirewall firewall delete rule name="Dash Remote 9090" >nul 2>&1

:: Port proxy pravidla
netsh interface portproxy add v4tov4 listenaddress=172.27.167.45 listenport=8080 connectaddress=10.0.1.24 connectport=80
netsh interface portproxy add v4tov4 listenaddress=172.27.167.45 listenport=7070 connectaddress=10.0.1.21 connectport=80
netsh interface portproxy add v4tov4 listenaddress=172.27.167.45 listenport=9090 connectaddress=10.0.1.98 connectport=80

:: Firewall inbound pravidla
netsh advfirewall firewall add rule name="Dash Remote 8080" dir=in action=allow protocol=TCP localport=8080
netsh advfirewall firewall add rule name="Dash Remote 7070" dir=in action=allow protocol=TCP localport=7070
netsh advfirewall firewall add rule name="Dash Remote 9090" dir=in action=allow protocol=TCP localport=9090

echo.
echo Hotovo. Aktivni pravidla:
echo.
netsh interface portproxy show all
echo.
echo Pristup:
echo   Dashboard : http://172.27.167.45:8080
echo   WLED      : http://172.27.167.45:7070
echo   MC ESP    : http://172.27.167.45:9090
echo.
pause
