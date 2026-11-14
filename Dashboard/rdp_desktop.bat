@echo off
:: Remote Desktop na desktop PC.
:: rdp_desktop.bat        -> pres ZeroTier (172.27.167.45)
:: rdp_desktop.bat lan    -> domaci sit (10.0.1.50)
set TARGET=172.27.167.45
if /i "%~1"=="lan" set TARGET=10.0.1.50
start "" mstsc /v:%TARGET%
