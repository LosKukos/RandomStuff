# AXIS CC Loader Gate

Instalace na CC počítač:

1. Vlož `loader_gate.lua`.
2. Uprav config nahoře:
   - `espBase`
   - `scanDepot`
   - `confirmDepot` volitelně
   - `trainStation`
   - `homeStation`
   - redstone sides
3. Spusť:

```lua
loader_gate
```

Vyžaduje ESP endpointy:
- `POST /api/orders/claim-next-load`
- `POST /api/package/loaded`
- `POST /api/orders/load-complete`

Flow:
- claimne další `packed` order
- accept/reject třídí balíky podle packageId
- každý accepted balík potvrdí na ESP
- po všech loaded zavolá load-complete
- až potom nastaví schedule a pulzne vlak

`confirmDepot`:
- `nil` = bere se jako loaded po vyprázdnění scan depotu
- `"create:depot_X"` = čeká se na potvrzení packageId za accept větví
