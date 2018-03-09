#include <ESP8266WiFi.h>
#include <PubSubClient.h>   // https://github.com/knolleary/pubsubclient/
#include <time.h> // for ntp
#include <Wire.h>

// https://github.com/AmbientDataInc/Ambient_AirQuality/tree/master/examples/CCS811_test
#include "SparkFunCCS811.h"

#define PIN_SDA 14
#define PIN_SCL 12
#define PIN_CCS811_RST 13

void reboot() {
  Serial.println("REBOOT!!!!!");
  delay(1000);

  ESP.reset();

  while (true) {
    Serial.println("REBOOT!!!!!");
    delay(500);
  };
}

unsigned long last_updated_t;

void clear_time() {
  last_updated_t = millis();
}

unsigned long diff_time() {
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
  Wire.setClockStretchLimit(30000);  // !!!! ATTENTION : The CCS811 clock stretch is very short time. !!!!
  reset_ccs811();

  CCS811Core::status rv = ccs811.begin();
  if (rv != CCS811Core::SENSOR_SUCCESS) {
    Serial.println("setup_ccs811() : ccs811.begin() failed...");
    reboot();
  }

  ccs811.setDriveMode(1); // read every 1sec
  clear_time();
  Serial.println("setup_ccs811() : success");
}

void cc811_check() {
  uint8_t value;
  CCS811Core::status returnCode = ccs811.readRegister( CSS811_STATUS, &value );
  Serial.print("cc811_check() : returnCode=");
  switch ( returnCode )
  {
    case CCS811Core::SENSOR_SUCCESS:
      Serial.println("SUCCESS");
      break;
    case CCS811Core::SENSOR_ID_ERROR:
      Serial.println("ID_ERROR");
      break;
    case CCS811Core::SENSOR_I2C_ERROR:
      Serial.println("I2C_ERROR");
      break;
    case CCS811Core::SENSOR_INTERNAL_ERROR:
      Serial.println("INTERNAL_ERROR");
      break;
    case CCS811Core::SENSOR_GENERIC_ERROR:
      Serial.println("GENERIC_ERROR");
      break;
    default:
      Serial.println("Unspecified error.");
  }
  delay(100);
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
  if (diff_time() > 1000) {
    cc811_check();
    if (ccs811.dataAvailable()) {
      ccs811.readAlgorithmResults();
      process_data(ccs811.getCO2(), ccs811.getTVOC());
      clear_time();
    }
  }

  if (diff_time() > 7000) {
    reboot();
  }
}

uint32_t total_co2 = 0;
uint32_t total_tvoc = 0;
int total_count = 0;

void process_data(uint16_t co2, uint16_t tvoc)
{
  if (400 <= co2 && co2 < 8192) {
    total_co2 += co2;
    total_tvoc += tvoc;
    total_count ++;

    if (total_count == 10) {
      uint32_t avg_co2 = total_co2 / total_count;
      uint32_t avg_tvos = total_tvoc / total_count;

      publish_message(avg_co2, avg_tvos);

      total_co2 = 0;
      total_tvoc = 0;
      total_count = 0;
    }
  }
}

void publish_message(uint32_t co2, uint32_t tvoc) {
  char t_str[32];
  time_t t;
  struct tm *tm;
  t = time(NULL);
  tm = localtime(&t);
  snprintf(
    t_str,
    32,
    "%d-%02d-%02dT%02d:%02d:%02d+09:00",
    tm->tm_year + 1900,
    tm->tm_mon + 1,
    tm->tm_mday,
    tm->tm_hour,
    tm->tm_min,
    tm->tm_sec
  );

  char msg[128];
  snprintf(msg, 128, "{\"co2\":%d,\"tvos\":%d,\"last_update_t\":\"%s\"}", co2, tvoc, t_str);
  Serial.println(msg);

  mqtt_client.publish(mqtt_publish_topic, msg, true);
}

