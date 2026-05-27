# AXIS CC Loader Gate

Flow:
- claimne další `packed` order
- accept/reject třídí balíky
- každý accepted balík potvrdí na ESP
- po všech loaded zavolá load-complete
- nastaví schedule a pulzne vlak

ESP endpointy:
- POST /api/orders/claim-next-load
- POST /api/package/loaded
- POST /api/orders/load-complete
