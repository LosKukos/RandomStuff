# AXIS — TODO

## Git cleanup ✓
- [x] Smazat staré soubory
- [x] Přidat runtime soubory do .gitignore

---

## ESP — refactor / streamline (pending)
- [ ] Rozdělit web.cpp
- [ ] parseBody() helper
- [ ] Sjednotit JsonDocument typy
- [ ] Přesunout emit funkce
- [ ] Zobecnit dirty flag pattern

---

## Remote vrstva ✓ (in progress)

- [x] MQTT bridge — mqtt_bridge.h/cpp, PubSubClient
- [x] UserRecord — registrace, login, token, LittleFS
- [x] Auth endpointy — register, login, verify, logout
- [x] /api/users/list, /api/players/online
- [x] players.lua v CC master
- [x] MQTT auth middleware — axis/auth topic, token verify v axis/cmd
- [x] Remote dashboard — remote.html pro GitHub Pages
- [ ] ESP: get_orders_by_user command handler (vrátit orders filtrované dle userId/mcName)
- [ ] ESP: get_packages_by_user command handler
- [ ] ESP: get_me command handler (broadcast ME snapshot na request)
- [ ] README update pro remote vrstvu

---

## Logistický systém

- [ ] /api/orders/list endpoint
- [ ] /api/packages/list endpoint
- [ ] Delivery flow — CC na destinaci → status delivered
- [ ] Player detector peripheral v CC masteru

