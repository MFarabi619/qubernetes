#include "Driver_Spiffs.h"
#include "Module_Serial_Logger.h"

void setup_spiffs() {
  if (SPIFFS.begin(true)) {
    Serial.println(CLR_GREEN "\n[SPIFFS] Mounted" CLR_RESET);
  } else {
    Serial.println(CLR_RED "\n[SPIFFS] ERROR: mount failed" CLR_RESET);
  }
}
