#include "Module_Async_Web_Server.h"

#include "Driver_Spiffs.h"
#include "Module_WiFi.h"

#include "Module_Serial_Logger.h"
#include <ESPAsyncWebServer.h>

#ifdef ARDUINO_XIAO_ESP32S3
#define TOGGLE_LED_PIN LED_BUILTIN
#define REQUEST_INDICATOR_LED_PIN LED_BUILTIN
#endif

#ifdef ARDUINO_ESP32S3_DEV
#define LED_RED 39
#define LED_GREEN 37
#define LED_YELLOW 38
#define TOGGLE_LED_PIN 38
#define REQUEST_INDICATOR_LED_PIN 37
#endif

AsyncWebServer server(80);
const long interval = 500;
unsigned long previousMillis = 0;
unsigned long ota_progress_millis = 0;

// ESPDash dashboard(server, "/dashboard", true);

// // Cards
// dash::GenericCard genericString(dashboard, "Generic String");
// dash::GenericCard<float> genericFloat(dashboard, "Generic Float");
// dash::GenericCard<int> genericInt(dashboard, "Generic Int");
// dash::TemperatureCard temp(dashboard, "Temperature"); // default precision is
// 2 dash::HumidityCard<float, 3> hum(dashboard,
//                                  "Humidity"); // set decimal precision is 3
// dash::FeedbackCard feedback(dashboard, "Status", dash::Status::SUCCESS);
// dash::ProgressCard<float, 4> progressFloat(dashboard, "Progress Float", 0, 1,
//                                            "kWh");
// dash::ProgressCard progressInt(dashboard, "Progress Int", 0, 100, "%");

// // Interactives
// dash::SeparatorCard cardSeparator(
//     dashboard, "Interactives",
//     "Below you will find all interactive cards available inside ESP-DASH
//     Lite");
// dash::SliderCard<float, 4> sliderFloatP4(dashboard, "Float Slider (4)", 0, 1,
//                                          0.0001f);
// dash::SliderCard<float> sliderFloatP2(dashboard, "Float Slider (2)", 0, 1,
//                                       0.01f);
// dash::SliderCard sliderInt(dashboard, "Int Slider", 0, 255, 1, "bits");
// dash::SliderCard<uint32_t> updateDelay(dashboard, "Update Delay", 1000,
// 20000,
//                                        1000, "ms");
// dash::ToggleButtonCard button(dashboard, "Button");

// // Charts
// dash::SeparatorCard chartSeparator(
//     dashboard, "Charts",
//     "Below you will find all charts available inside ESP-DASH Lite");
// dash::BarChart<const char *, int> bar(dashboard, "Power Usage (kWh)");

// // Custom Statistics
// dash::StatisticValue stat1(dashboard, "Statistic 1");
// dash::StatisticValue<float, 4> stat2(dashboard, "Statistic 2");
// dash::StatisticProvider<uint32_t> statProvider(dashboard, "Statistic
// Provider");

// uint8_t test_status = 0;

// /**
//  * Note how we are keeping all the chart data in global scope.
//  */
// // Bar Chart Data
// const char *BarXAxis[] = {
//     "1/4/22",  "2/4/22",  "3/4/22",  "4/4/22",  "5/4/22",  "6/4/22",
//     "7/4/22",  "8/4/22",  "9/4/22",  "10/4/22", "11/4/22", "12/4/22",
//     "13/4/22", "14/4/22", "15/4/22", "16/4/22", "17/4/22", "18/4/22",
//     "19/4/22", "20/4/22", "21/4/22", "22/4/22", "23/4/22", "24/4/22",
//     "25/4/22", "26/4/22", "27/4/22", "28/4/22", "29/4/22", "30/4/22"};
// int BarYAxis[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
//                   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

unsigned long last_update_millis = 0;
uint32_t update_delay = 2000;

String formatUptime(unsigned long ms) {
  unsigned long s = ms / 1000UL;
  unsigned long d = s / 86400UL;
  s %= 86400UL;
  unsigned long h = s / 3600UL;
  s %= 3600UL;
  unsigned long m = s / 60UL;
  s %= 60UL;

  char buf[48];
  if (d)
    snprintf(buf, sizeof(buf), "%lud %luh %lum %lus", d, h, m, s);
  else if (h)
    snprintf(buf, sizeof(buf), "%luh %lum %lus", h, m, s);
  else if (m)
    snprintf(buf, sizeof(buf), "%lum %lus", m, s);
  else
    snprintf(buf, sizeof(buf), "%lus", s);
  return String(buf);
}

void begin_Module_Async_Web_Server() {
  pinMode(REQUEST_INDICATOR_LED_PIN, OUTPUT);
  digitalWrite(REQUEST_INDICATOR_LED_PIN, LOW);
  pinMode(TOGGLE_LED_PIN, OUTPUT);
  digitalWrite(TOGGLE_LED_PIN, LOW);

  setup_spiffs();
  begin_wifi();

  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

  server.onNotFound([](AsyncWebServerRequest *request) {
    digitalWrite(REQUEST_INDICATOR_LED_PIN, HIGH);
    String message = "404 — Nothing here\n\nURI: " + request->url();
    request->send(404, "text/plain; charset=utf-8", message);
    digitalWrite(REQUEST_INDICATOR_LED_PIN, LOW);
  });

  server.on("/on", HTTP_GET, [](AsyncWebServerRequest *request) {
    digitalWrite(TOGGLE_LED_PIN, HIGH);
    led_toggle_state = 1;
    request->send(200, "text/plain; charset=utf-8", "ON");
  });

  server.on("/off", HTTP_GET, [](AsyncWebServerRequest *request) {
    digitalWrite(TOGGLE_LED_PIN, LOW);
    led_toggle_state = 0;
    request->send(200, "text/plain; charset=utf-8", "OFF");
  });

  server.on("/toggle", HTTP_GET, [](AsyncWebServerRequest *request) {
    digitalWrite(TOGGLE_LED_PIN, !digitalRead(TOGGLE_LED_PIN));
    request->send(200, "text/plain; charset=utf-8", "OFF");
  });

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    digitalWrite(REQUEST_INDICATOR_LED_PIN, HIGH);

    String json = "{";
    json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
    json += "\"uptime\":\"" + formatUptime(millis()) + "\",";
    json += "\"uptime_seconds\":" + String(millis() / 1000) + ",";
    json += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
    json += "\"gpio_state\":" + String(digitalRead(TOGGLE_LED_PIN));
    json += "}";

    request->send(200, "application/json; charset=utf-8", json);

    digitalWrite(REQUEST_INDICATOR_LED_PIN, LOW);
  });

  server.on("/metrics", HTTP_GET, [](AsyncWebServerRequest *req) {
    String response;
    response.reserve(512);

    response += "wifi_rssi_dbm " + String(WiFi.RSSI()) + "\n";
    response += "heap_free_bytes " + String(ESP.getFreeHeap()) + "\n";
    response += "heap_largest_block_bytes " +
                String(heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT)) +
                "\n";
    response +=
        "cpu_temperature_celsius " + String(temperatureRead(), 2) + "\n";
    response += "uptime_seconds " + String(millis() / 1000) + "\n";
    response += "uptime_millis " + String(millis()) + "\n";
    response += "gpio_state " + String(digitalRead(TOGGLE_LED_PIN)) + "\n";
    response += "reset_reason " + String((int)esp_reset_reason()) + "\n";

    req->send(200, "text/plain; charset=utf-8", response);
  });

  ArduinoOTA.begin();

  // WebSerial.setAuthentication("qubernetes", "qubernetes");
  WebSerial.begin(&server);

  WebSerial.onMessage([](uint8_t *data, size_t len) {
    Serial.printf("Received %lu bytes from WebSerial: ", len);
    Serial.write(data, len);
    Serial.println();
    WebSerial.println("Received Data...");
    String d = "";
    for (size_t i = 0; i < len; i++) {
      d += char(data[i]);
    }
    WebSerial.println(d);
  });

  server.begin();
  Serial.println(CLR_GREEN "[HTTP] Async server started on port 80" CLR_RESET);
}
