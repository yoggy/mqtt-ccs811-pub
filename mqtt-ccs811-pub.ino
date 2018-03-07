#include <ESP8266WiFi.h>
#include <PubSubClient.h>   // https://github.com/knolleary/pubsubclient/
#include <time.h> // for ntp
#include <Wire.h>

// https://github.com/AmbientDataInc/Ambient_AirQuality/tree/master/examples/CCS811_test
#include "SparkFunCCS811.h"

#define PIN_SDA 14
#define PIN_SCL 12
#define PIN_CCS811_RST 13

void reboot()
{
  Serial.println("reboot() !!!!");
  while (1);
}

unsigned long last_updated_t;

void check() {
  last_updated_t = millis();
}

unsigned long diff() {
  return millis() - last_updated_t;
}

//////////////////////////////////////////////////////////////////////

#define CCS811_ADDR 0x5B
CCS811 ccs811(CCS811_ADDR);

void reset_ccs811() {
  pinMode(PIN_CCS811_RST, OUTPUT);
  digitalWrite(PIN_CCS811_RST, LOW);
  delay(100);
  digitalWrite(PIN_CCS811_RST, HIGH);
  delay(100);
}

void setup_ccs811() {
  Serial.println("setup_ccs811() : start");
  Wire.begin(PIN_SDA, PIN_SCL); //SDA, SCL
  reset_ccs811();

  CCS811Core::status rv = ccs811.begin();
  if (rv != CCS811Core::SENSOR_SUCCESS) {
    Serial.println("setup_ccs811() : ccs811.begin() failed...");
    reboot();
  }

  ccs811.setDriveMode(1); // read every 1sec
  check();
  Serial.println("setup_ccs811() : success");
}

//////////////////////////////////////////////////////////////////////

// Wif config (from config.ino)
extern char *wifi_ssid;
extern char *wifi_password;
extern char *mqtt_server;
extern int  mqtt_port;

extern char *mqtt_client_id;
extern bool mqtt_use_auth;
extern char *mqtt_username;
extern char *mqtt_password;

extern char *mqtt_publish_topic;

WiFiClient wifi_client;
PubSubClient mqtt_client(mqtt_server, mqtt_port, NULL, wifi_client);

void setup_wifi()
{
  Serial.println("setup_wifi() : start");

  WiFi.begin(wifi_ssid, wifi_password);
  WiFi.mode(WIFI_STA);

  int wifi_count = 0;
  while (WiFi.status() != WL_CONNECTED) {
    wifi_count ++;
    delay(300);
    if (wifi_count > 100) reboot();
  }

  Serial.println("setup_wifi() : wifi connected");

  Serial.println("setup_wifi() : mqtt connecting");
  bool rv = false;
  if (mqtt_use_auth == true) {
    rv = mqtt_client.connect(mqtt_client_id, mqtt_username, mqtt_password);
  }
  else {
    rv = mqtt_client.connect(mqtt_client_id);
  }
  if (rv == false) {
    Serial.println("setup_wifi() : mqtt connecting failed...");
    reboot();
  }
  Serial.println("setup_wifi() : mqtt connected");

  Serial.println("setup_wifi() : success");
}

/////////////////////////////////////////////////////////////////////////////

void setup() {
  Serial.begin(115200);

  setup_wifi();
  setup_ccs811();

  configTime(9 * 3600, 0, "ntp.nict.jp");
}

void loop() {
  // every 1sec
  if (diff() > 1000) {
    if (ccs811.dataAvailable()) {
      ccs811.readAlgorithmResults();
      publish_message(ccs811.getCO2(), ccs811.getTVOC());
    }
    check();
  }

  if (diff() > 7000) {
    reboot();
  }
}

void publish_message(uint16_t co2, uint16_t tvoc) {
  char t_str[32];
  time_t t;
  struct tm *tm;
  t = time(NULL);
  tm = localtime(&t);
  snprintf(
    t_str,
    32,
    "%d-%02d-%02dT%02d:%02d:%02d+09:00",
    tm->tm_year+1900,
    tm->tm_mon+1,
    tm->tm_mday,
    tm->tm_hour,
    tm->tm_min,
    tm->tm_sec
    );
  
  char msg[128];
  snprintf(msg, 128, "{\"co2\":%d,\"tvos\":%d,\"last_update_t\":\"%s\"}", co2, tvoc, t_str);
  Serial.println(msg);

  mqtt_client.publish(mqtt_publish_topic, msg);
}

