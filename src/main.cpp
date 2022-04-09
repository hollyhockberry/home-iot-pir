// Copyright (c) 2022 Inaba
// This software is released under the MIT License.
// http://opensource.org/licenses/mit-license.php

#include <Arduino.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

static String SSID(""), PSK("");
static String INFLUX_MDNS(""), INFLUX_IP("");
static int INFLUX_PORT = 8086;
static String DB_NAME(""), MEASUREMENT("");

constexpr static const int PIR_PORT = 1;
static RTC_DATA_ATTR int l_exist = -1;

template<typename T>
T Key(const char* key, const T initial, const DynamicJsonDocument& json) {
  return json.containsKey(key) ? static_cast<T>(json[key]) : initial;
}

void load() {
  if (!SPIFFS.begin(true)) {
    return;
  }
  auto file = SPIFFS.open("/setting.json", "r");
  if (!file || file.size() == 0) {
    return;
  }
  DynamicJsonDocument json(1300);
  if (::deserializeJson(json, file)) {
    return;
  }
  SSID = Key<const char*>("SSID", "", json);
  PSK = Key<const char*>("PSK", "", json);
  INFLUX_MDNS = Key<const char*>("influx_mdns_addr", "", json);
  INFLUX_IP = Key<const char*>("influx_ip_addr", "", json);
  INFLUX_PORT = Key<int>("influx_port", 8086, json);
  DB_NAME = Key<const char*>("db_name", "", json);
  MEASUREMENT = Key<const char*>("measurement", "", json);
}

bool connectWiFi() {
  if (SSID.equals("") || PSK.equals("")) {
    return false;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID.c_str(), PSK.c_str());
  while (WiFi.status() != WL_CONNECTED) {
    ::delay(500);
  }
  return true;
}

void disconnectWiFi() {
  MDNS.end();
  WiFi.disconnect();
}

String address() {
  if (!INFLUX_MDNS.equals("")) {
    while (!MDNS.begin(WiFi.macAddress().c_str())) {
      ::delay(100);
    }
    return MDNS.queryHost(INFLUX_MDNS).toString();
  } else if (!INFLUX_IP.equals("")) {
    return INFLUX_IP;
  } else {
    return "";
  }
}

bool post(bool exist) {
  if (!::connectWiFi()) {
    return false;
  }
  const auto addr = ::address();
  if (addr.equals("") || addr.equals("0.0.0.0")) {
    ::disconnectWiFi();
    return false;
  }
  const String url = "http://" + ::address() + ":" + String(INFLUX_PORT, 10)
      + "/write?db=" + DB_NAME;
Serial.println(url);
  HTTPClient http;
  http.begin(url.c_str());

  const String payload = MEASUREMENT + ",id=" + WiFi.macAddress().c_str()
    + " exist=" + (exist ? "1" : "0");
Serial.println(payload);
  http.POST(payload);
  ::disconnectWiFi();
  return true;
}

#include <Adafruit_NeoPixel.h>
static Adafruit_NeoPixel led(1, 2, NEO_GRB + NEO_KHZ800);

void setup() {
  Serial.begin(115200);
  ::load();
  ::pinMode(PIR_PORT, INPUT);
}

void loop() {
  auto exist = ::digitalRead(PIR_PORT) == HIGH;
  if (exist) {
    ::esp_sleep_enable_timer_wakeup(20 * 1000000);
  } else {
    ::esp_deep_sleep_enable_gpio_wakeup(
      BIT(PIR_PORT), ESP_GPIO_WAKEUP_GPIO_HIGH);
  }
  auto curr = exist ? 1 : 0;
  if (curr != l_exist) {
led.begin();
led.setPixelColor(0, exist ? 0xff : 0);
led.show();
Serial.println(exist ? "OCCUPIED *******" : "VACANT *******");
    while (!::post(exist)) {
      ::delay(500);
    }
    l_exist = curr;
  }
Serial.println("Enter sleep");
  ::esp_deep_sleep_start();
  for (;;) {}
  // never reach...
}