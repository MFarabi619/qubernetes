#include "Module_WiFi.h"
#include "Module_Serial_Logger.h"

#define MDNS_HOSTNAME "qubernetes"

#include <ESPmDNS.h>
#include <WiFi.h>

// WiFiServer server(80);

// String header;

// String output26State = "off";
// String output27State = "off";

// const int output26 = 26;
// const int output27 = 27;

// void begin_wifi_ap() {
//   // pinMode(output26, OUTPUT);
//   // pinMode(output27, OUTPUT);
//   // digitalWrite(output26, LOW);
//   // digitalWrite(output27, LOW);

//   Serial.print("Setting AP (Access Point)…");
//   // Remove the password parameter, if you want the AP (Access Point) to be
//   open WiFi.softAP("thepromisedlan", "thepromisedlan");

//   IPAddress IP = WiFi.softAPIP();
//   Serial.print("AP IP address: ");
//   Serial.println(IP);

//   server.begin();
// }

// void loop_wifi_ap() {
//   WiFiClient client = server.available();

//   if (client) {
//     Serial.println("New Client.");
//     String currentLine = "";

//     while (client.connected()) {
//       if (client.available()) {
//         char c = client.read();
//         Serial.write(c);
//         header += c;

//         if (c == '\n') {
//           if (currentLine.length() == 0) {
//             client.println("HTTP/1.1 200 OK");
//             client.println("Content-type:text/html");
//             client.println("Connection: close");
//             client.println();

//             if (header.indexOf("GET /26/on") >= 0) {
//               Serial.println("GPIO 26 on");
//               output26State = "on";
//               digitalWrite(output26, HIGH);
//             } else if (header.indexOf("GET /26/off") >= 0) {
//               Serial.println("GPIO 26 off");
//               output26State = "off";
//               digitalWrite(output26, LOW);
//             } else if (header.indexOf("GET /27/on") >= 0) {
//               Serial.println("GPIO 27 on");
//               output27State = "on";
//               digitalWrite(output27, HIGH);
//             } else if (header.indexOf("GET /27/off") >= 0) {
//               Serial.println("GPIO 27 off");
//               output27State = "off";
//               digitalWrite(output27, LOW);
//             }

//             client.println("<!DOCTYPE html><html>");
//             client.println("<head><meta name=\"viewport\" "
//                            "content=\"width=device-width,
//                            initial-scale=1\">");
//             client.println("<link rel=\"icon\" href=\"data:,\">");
//             client.println(
//                 "<style>html { font-family: Helvetica; display: inline-block;
//                 " "margin: 0px auto; text-align: center;}");
//             client.println(".button { background-color: #4CAF50; border:
//             none; "
//                            "color: white; padding: 16px 40px;");
//             client.println("text-decoration: none; font-size: 30px; margin: "
//                            "2px; cursor: pointer;}");
//             client.println(
//                 ".button2 {background-color: #555555;}</style></head>");

//             client.println("<body><h1>ESP32 Web Server</h1>");

//             client.println("<p>GPIO 26 - State " + output26State + "</p>");
//             if (output26State == "off") {
//               client.println("<p><a href=\"/26/on\"><button "
//                              "class=\"button\">ON</button></a></p>");
//             } else {
//               client.println("<p><a href=\"/26/off\"><button class=\"button "
//                              "button2\">OFF</button></a></p>");
//             }

//             client.println("<p>GPIO 27 - State " + output27State + "</p>");
//             if (output27State == "off") {
//               client.println("<p><a href=\"/27/on\"><button "
//                              "class=\"button\">ON</button></a></p>");
//             } else {
//               client.println("<p><a href=\"/27/off\"><button class=\"button "
//                              "button2\">OFF</button></a></p>");
//             }
//             client.println("</body></html>");

//             client.println();
//             break;
//           } else {
//             currentLine = "";
//           }
//         } else if (c != '\r') {

//           currentLine += c;
//         }
//       }
//     }

//     header = "";
//     client.stop();
//     Serial.println("Client disconnected.");
//     Serial.println("");
//   }
// }

void begin_wifi() {
  Serial.println(CLR_BLUE_B "\n=== NETWORK BRING-UP ===" CLR_RESET);
  Serial.printf(CLR_YELLOW "\n🛜[WiFi] Connecting to SSID: %s\n" CLR_RESET,
                NETWORK_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(NETWORK_SSID, NETWORK_PSK);

  Serial.print(CLR_YELLOW "\n🛜[WiFi] Connecting" CLR_RESET);

  uint32_t t0 = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    Serial.print(CLR_YELLOW "." CLR_RESET);
    delay(300);
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(CLR_GREEN "\n🛜[WiFi] Connected" CLR_RESET);
    Serial.print(CLR_MAGENTA_B "\n🛜[WiFi] IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(CLR_RED "🛜[WiFi] ERROR: connect timeout. Check 2.4GHz/WPA2 "
                           "and password." CLR_RESET);
  }

  if (MDNS.begin(MDNS_HOSTNAME)) {
    Serial.printf(CLR_GREEN "📢[mDNS] Responder started (%s.local)\n" CLR_RESET,
                  MDNS_HOSTNAME);
  } else {
    Serial.println(CLR_RED
                   "📢[mDNS] ERROR: Failed to start responder" CLR_RESET);
  }
}
