#pragma once

// Jednorazova kontrola noveho prikazu v ntfy topicu NTFY_REPORT_TOPIC
// (GET .../json?poll=1&since=...). Zprava "WOL" odesle magic packet na WOL_TARGET_MAC.
// Volat periodicky z vlastniho tasku (dela TLS spojeni, blokuje par sekund).
void ntfyCmdPoll();
