#ifndef MODULE_ASYNC_WEB_SERVER_H
#define MODULE_ASYNC_WEB_SERVER_H

#include "Module_WiFi.h"

#include <ArduinoOTA.h>
#include <WebSerial.h>

static int led_toggle_state = 0;

void initialize_led_pins();
String formatUptime(unsigned long ms);
void begin_Module_Async_Web_Server();

#endif
