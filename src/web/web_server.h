// web/web_server.h
// HTTP server using built-in WebServer.h (no external libs).
// No TLS because (1) C3 RAM is tight (2) in AP mode the user is already on
// the local network, and mDNS http://on3pork.local works in AP+STA.
#pragma once

#include <Arduino.h>

namespace Web {

void begin();
void loop();           // call from main loop()

} // namespace Web