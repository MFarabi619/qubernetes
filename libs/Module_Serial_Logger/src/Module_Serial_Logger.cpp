#include "Module_Serial_Logger.h"

void begin_serial_logger() {
  Serial.begin(MONITOR_SPEED);

  Serial.println(CLR_BLUE_B "\n=== BOOT SEQUENCE ===" CLR_RESET);
  Serial.println(CLR_YELLOW "[LOGGER] Initializing..." CLR_RESET);

  Serial.println(CLR_BLUE_B "\n=== HARDWARE BRING-UP SUMMARY ===" CLR_RESET);
  Serial.println(CLR_GREEN "[LOGGER] OK" CLR_RESET);
}
