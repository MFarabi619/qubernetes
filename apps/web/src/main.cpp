#include "Module_Async_Web_Server.h"
#include "Module_Serial_Logger.h"

void setup() {
  begin_serial_logger();

  begin_Module_Async_Web_Server();
}

void loop() {
  ArduinoOTA.handle();

  static unsigned long last_status_print_time = millis();

  if ((unsigned long)(millis() - last_status_print_time) > 2000) {
    WebSerial.print(F("IP address: "));
    WebSerial.println(WiFi.localIP());
    WebSerial.printf("Uptime: %lu ms\n", millis());
    WebSerial.printf("Free heap: %u\n", ESP.getFreeHeap());
    last_status_print_time = millis();
  }

  WebSerial.loop();
}
