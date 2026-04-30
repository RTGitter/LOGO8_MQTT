#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <picoMQTT.h>

// ESP32 MQTT/Setup-Programm für LOGO8 mit erstem Webserver-Zugang
// Benötigt die Bibliothek picoMQTT im Arduino IDE Bibliotheksmanager.

const char* apSSID = "LOGO8-SETUP";
const char* apPassword = "logo8setup";
const int apChannel = 1;

WebServer server(80);
Preferences prefs;
WiFiClient wifiClient;
PicoMQTT mqtt(wifiClient);

struct Config {
  char sta_ssid[32];
  char sta_pass[64];
  bool useStatic;
  char static_ip[16];
  char gateway[16];
  char subnet[16];
  char mqtt_server[40];
  int mqtt_port;
  char mqtt_client_id[32];
  char mqtt_user[32];
  char mqtt_pass[64];
  char mqtt_topic[48];
  bool mqtt_enabled;
};

Config config;
bool configValid = false;
bool webMode = false;
unsigned long lastWifiAttempt = 0;
unsigned long lastMqttAttempt = 0;
const unsigned long wifiRetryInterval = 5000;
const unsigned long mqttRetryInterval = 10000;

void setup() {
  Serial.begin(115200);
  delay(100);
  loadConfig();

  if (!configValid) {
    Serial.println("Keine gültige Konfiguration gefunden. Starte Setup-Hotspot.");
    startSetupPortal();
    return;
  }

  if (!connectStation()) {
    Serial.println("Station-Verbindung fehlgeschlagen. Starte Setup-Hotspot.");
    startSetupPortal();
    return;
  }

  if (config.mqtt_enabled) {
    setupMQTT();
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound(handleNotFound);
  server.begin();
}

void loop() {
  if (webMode) {
    server.handleClient();
    return;
  }

  server.handleClient();

  if (WiFi.status() != WL_CONNECTED && millis() - lastWifiAttempt > wifiRetryInterval) {
    Serial.println("Wiederholter Verbindungsversuch zum WLAN...");
    connectStation();
  }

  if (config.mqtt_enabled) {
    if (!mqtt.isConnected() && millis() - lastMqttAttempt > mqttRetryInterval) {
      Serial.println("Versuche MQTT-Verbindung...");
      mqttReconnect();
    }
    if (mqtt.isConnected()) {
      mqtt.loop();
    }
  }
}

void loadConfig() {
  prefs.begin("logo8", true);
  if (!prefs.isKey("configured")) {
    prefs.end();
    configValid = false;
    return;
  }

  String s;

  s = prefs.getString("sta_ssid", "");
  strncpy(config.sta_ssid, s.c_str(), sizeof(config.sta_ssid) - 1);
  config.sta_ssid[sizeof(config.sta_ssid) - 1] = '\0';

  s = prefs.getString("sta_pass", "");
  strncpy(config.sta_pass, s.c_str(), sizeof(config.sta_pass) - 1);
  config.sta_pass[sizeof(config.sta_pass) - 1] = '\0';

  config.useStatic = prefs.getBool("useStatic", false);

  s = prefs.getString("static_ip", "");
  strncpy(config.static_ip, s.c_str(), sizeof(config.static_ip) - 1);
  config.static_ip[sizeof(config.static_ip) - 1] = '\0';

  s = prefs.getString("gateway", "");
  strncpy(config.gateway, s.c_str(), sizeof(config.gateway) - 1);
  config.gateway[sizeof(config.gateway) - 1] = '\0';

  s = prefs.getString("subnet", "255.255.255.0");
  strncpy(config.subnet, s.c_str(), sizeof(config.subnet) - 1);
  config.subnet[sizeof(config.subnet) - 1] = '\0';

  s = prefs.getString("mqtt_server", "");
  strncpy(config.mqtt_server, s.c_str(), sizeof(config.mqtt_server) - 1);
  config.mqtt_server[sizeof(config.mqtt_server) - 1] = '\0';

  config.mqtt_port = prefs.getInt("mqtt_port", 1883);

  s = prefs.getString("mqtt_client_id", "esp32-logo8");
  strncpy(config.mqtt_client_id, s.c_str(), sizeof(config.mqtt_client_id) - 1);
  config.mqtt_client_id[sizeof(config.mqtt_client_id) - 1] = '\0';

  s = prefs.getString("mqtt_user", "");
  strncpy(config.mqtt_user, s.c_str(), sizeof(config.mqtt_user) - 1);
  config.mqtt_user[sizeof(config.mqtt_user) - 1] = '\0';

  s = prefs.getString("mqtt_pass", "");
  strncpy(config.mqtt_pass, s.c_str(), sizeof(config.mqtt_pass) - 1);
  config.mqtt_pass[sizeof(config.mqtt_pass) - 1] = '\0';

  s = prefs.getString("mqtt_topic", "logo8/status");
  strncpy(config.mqtt_topic, s.c_str(), sizeof(config.mqtt_topic) - 1);
  config.mqtt_topic[sizeof(config.mqtt_topic) - 1] = '\0';

  config.mqtt_enabled = prefs.getBool("mqtt_enabled", false);

  configValid = strlen(config.sta_ssid) > 0 && strlen(config.mqtt_server) > 0;
  prefs.end();
}

void saveConfig() {
  prefs.begin("logo8", false);
  prefs.putString("sta_ssid", String(config.sta_ssid));
  prefs.putString("sta_pass", String(config.sta_pass));
  prefs.putBool("useStatic", config.useStatic);
  prefs.putString("static_ip", String(config.static_ip));
  prefs.putString("gateway", String(config.gateway));
  prefs.putString("subnet", String(config.subnet));
  prefs.putString("mqtt_server", String(config.mqtt_server));
  prefs.putInt("mqtt_port", config.mqtt_port);
  prefs.putString("mqtt_client_id", String(config.mqtt_client_id));
  prefs.putString("mqtt_user", String(config.mqtt_user));
  prefs.putString("mqtt_pass", String(config.mqtt_pass));
  prefs.putString("mqtt_topic", String(config.mqtt_topic));
  prefs.putBool("mqtt_enabled", config.mqtt_enabled);
  prefs.putBool("configured", true);
  prefs.end();
}

bool connectStation() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(100);

  if (config.useStatic) {
    IPAddress ip, gw, sn;
    ip.fromString(config.static_ip);
    gw.fromString(config.gateway);
    sn.fromString(config.subnet);
    WiFi.config(ip, gw, sn);
  }

  WiFi.begin(config.sta_ssid, config.sta_pass);
  Serial.printf("Verbinde mit WLAN %s...\n", config.sta_ssid);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < wifiTimeout) {
    delay(200);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WLAN verbunden.");
    Serial.print("IP-Adresse: ");
    Serial.println(WiFi.localIP());
    return true;
  }

  Serial.println("WLAN-Verbindung fehlgeschlagen.");
  lastWifiAttempt = millis();
  return false;
}

void startSetupPortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSSID, apPassword, apChannel);
  delay(500);
  IPAddress apIP = WiFi.softAPIP();
  Serial.printf("Setup-Portal gestartet: %s (%s)\n", apSSID, apPassword);
  Serial.printf("Webserver erreichbar unter http://%s/\n", apIP.toString().c_str());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound(handleNotFound);
  server.begin();
  webMode = true;
}

void setupMQTT() {
  mqtt.setServer(config.mqtt_server, config.mqtt_port);
  mqtt.setCallback(mqttCallback);
  mqttReconnect();
}

void mqttReconnect() {
  if (mqtt.isConnected()) {
    return;
  }

  lastMqttAttempt = millis();
  if (mqtt.connect(config.mqtt_client_id, config.mqtt_user, config.mqtt_pass)) {
    Serial.println("MQTT verbunden.");
    mqtt.subscribe(config.mqtt_topic);
    mqtt.publish(config.mqtt_topic, "ESP32 online", true);
  } else {
    Serial.printf("MQTT-Verbindung fehlgeschlagen: %d\n", mqtt.state());
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("MQTT Nachricht empfangen: ");
  Serial.println(topic);
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.printf("Payload: %s\n", message.c_str());
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>LOGO8 ESP32 Setup</title></head><body>";
  html += "<h1>LOGO8 ESP32 Setup</h1>";
  html += "<form method='POST' action='/save'>";
  html += "<h2>WLAN</h2>";
  html += "SSID:<br><input name='sta_ssid' value='" + String(config.sta_ssid) + "' maxlength='31'><br>";
  html += "Passwort:<br><input type='password' name='sta_pass' value='" + String(config.sta_pass) + "' maxlength='63'><br>";
  html += "<label><input type='checkbox' name='useStatic'" + String(config.useStatic ? " checked" : "") + "> Statische IP verwenden</label><br>";
  html += "IP-Adresse:<br><input name='static_ip' value='" + String(config.static_ip) + "' maxlength='15'><br>";
  html += "Gateway:<br><input name='gateway' value='" + String(config.gateway) + "' maxlength='15'><br>";
  html += "Subnetz:<br><input name='subnet' value='" + String(config.subnet) + "' maxlength='15'><br>";
  html += "<h2>MQTT</h2>";
  html += "Server:<br><input name='mqtt_server' value='" + String(config.mqtt_server) + "' maxlength='39'><br>";
  html += "Port:<br><input name='mqtt_port' type='number' value='" + String(config.mqtt_port) + "' min='1' max='65535'><br>";
  html += "Client-ID:<br><input name='mqtt_client_id' value='" + String(config.mqtt_client_id) + "' maxlength='31'><br>";
  html += "Benutzer:<br><input name='mqtt_user' value='" + String(config.mqtt_user) + "' maxlength='31'><br>";
  html += "Passwort:<br><input type='password' name='mqtt_pass' value='" + String(config.mqtt_pass) + "' maxlength='63'><br>";
  html += "Topic:<br><input name='mqtt_topic' value='" + String(config.mqtt_topic) + "' maxlength='47'><br>";
  html += "<label><input type='checkbox' name='mqtt_enabled'" + String(config.mqtt_enabled ? " checked" : "") + "> MQTT aktivieren</label><br>";
  html += "<br><button type='submit'>Speichern und Neustarten</button>";
  html += "</form>";

  if (!webMode) {
    html += "<h2>Status</h2>";
    html += "<p>WLAN: " + String(WiFi.status() == WL_CONNECTED ? "verbunden" : "nicht verbunden") + "</p>";
    if (config.mqtt_enabled) {
      html += "<p>MQTT: " + String(mqtt.isConnected() ? "verbunden" : "nicht verbunden") + "</p>";
    }
    html += "<p>IP: " + WiFi.localIP().toString() + "</p>";
  }

  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleSave() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  String value;
  value = server.arg("sta_ssid");
  value.toCharArray(config.sta_ssid, sizeof(config.sta_ssid));
  value = server.arg("sta_pass");
  value.toCharArray(config.sta_pass, sizeof(config.sta_pass));
  config.useStatic = server.hasArg("useStatic");
  value = server.arg("static_ip");
  value.toCharArray(config.static_ip, sizeof(config.static_ip));
  value = server.arg("gateway");
  value.toCharArray(config.gateway, sizeof(config.gateway));
  value = server.arg("subnet");
  value.toCharArray(config.subnet, sizeof(config.subnet));
  value = server.arg("mqtt_server");
  value.toCharArray(config.mqtt_server, sizeof(config.mqtt_server));
  config.mqtt_port = server.arg("mqtt_port").toInt();
  value = server.arg("mqtt_client_id");
  value.toCharArray(config.mqtt_client_id, sizeof(config.mqtt_client_id));
  value = server.arg("mqtt_user");
  value.toCharArray(config.mqtt_user, sizeof(config.mqtt_user));
  value = server.arg("mqtt_pass");
  value.toCharArray(config.mqtt_pass, sizeof(config.mqtt_pass));
  value = server.arg("mqtt_topic");
  value.toCharArray(config.mqtt_topic, sizeof(config.mqtt_topic));
  config.mqtt_enabled = server.hasArg("mqtt_enabled");

  saveConfig();
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Gespeichert</title></head><body>";
  html += "<h1>Konfiguration gespeichert</h1>";
  html += "<p>Das Gerät startet neu...</p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
  delay(1000);
  ESP.restart();
}

void handleNotFound() {
  server.send(404, "text/plain", "404: Nicht gefunden");
}
