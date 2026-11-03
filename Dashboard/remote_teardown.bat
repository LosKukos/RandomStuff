@echo off
:: ============================================================
:: Dashboard Remote Access - ZeroTier Port Forward Teardown
:: Spustit jako Administrator
:: ============================================================

net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Tento skript vyzaduje admin prava.
    echo Klikni pravy tlacitko a zvol "Spustit jako spravce".
    pause
    exit /b 1
)

echo Odstranuji port forwarding pravidla...
echo.

netsh interface portproxy delete v4tov4 listenaddress=172.27.167.50 listenport=8080
netsh interface portproxy delete v4tov4 listenaddress=172.27.167.50 listenport=7070
netsh interface portproxy delete v4tov4 listenaddress=172.27.167.50 listenport=9090

netsh advfirewall firewall delete rule name="Dash Remote 8080"
netsh advfirewall firewall delete rule name="Dash Remote 7070"
netsh advfirewall firewall delete rule name="Dash Remote 9090"

echo.
echo Hotovo. Zbyvajici pravidla:
echo.
netsh interface portproxy show all
echo.
pause
