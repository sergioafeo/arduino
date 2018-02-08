#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
//#include <SPI.h>
//#include <Wire.h>
#include <WEMOS_SHT3X.h>
#include <ESP8266HTTPUpdateServer.h>

#undef SERIAL_DEBUG

#ifdef SERIAL_DEBUG
#define DEBUG(arg)   Serial.print(arg)
#define DEBUGLN(arg) Serial.print(arg)
#define DEBUGF(...)  Serial.printf(__VA_ARGS__)
#else
#define DEBUG(arg)
#define DEBUGLN(arg)
#define DEBUGF(...)
#endif

SHT3X sht30(0x45);
const char* ssid     = "POLOSUR";
const char* password = "pinguinita";
const char* host     = "ESP-TEMPSENSOR";
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer updateServer;

String currTemp;
#define OLED

#ifdef OLED
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#define OLED_RESET 0  // GPIO0
Adafruit_SSD1306 display(OLED_RESET);
#endif

void setup() {
  Serial.begin(115200);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize witµh the I2C addr 0x3C (for the 64x48)
  display.display();
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(WHITE);

  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(host);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    display.print(".");
    display.display();
  }

  DEBUG("");
  DEBUGLN("WiFi connected");
  DEBUG("IP address: ");
  DEBUGLN(WiFi.localIP());
  display.println("");
  display.println(WiFi.localIP());
  display.display();

  if (!MDNS.begin(host)) {
    DEBUGLN("Error setting up MDNS responder!");
  } else {
    DEBUGLN("mDNS responder started.");
  }

  server.on("/temp", []() {
    sht30.get();
    float temp = sht30.cTemp;
    float humidity = sht30.humidity;
    currTemp = String(temp);
    server.send(200, "text/html", currTemp);
    printData(temp, humidity);
  });
  server.on("/humidity", []() {
    sht30.get();
    float temp = sht30.cTemp;
    float humidity = sht30.humidity;
    currTemp = String(humidity);
    server.send(200, "text/html", currTemp);
    printData(temp, humidity);
  });
  server.on("/data", []() {
    sht30.get();
    float temp = sht30.cTemp;
    float humidity = sht30.humidity;
    currTemp = String(String(temp) + ";" + String(humidity));
    server.send(200, "text/html", currTemp);
    printData(temp, humidity);
  });
  updateServer.setup(&server);
  server.begin();
  MDNS.addService("http", "tcp", 80);
}

void printData(float temp, float humidity) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("   LOCAL");
  display.println(" ");
  display.print("  ");
  display.print(temp);
  display.println("C");
  display.print("  ");
  display.print(humidity);
  display.println("\%");
  display.display();
  DEBUG("Temperature: ");
  DEBUGLN(temp);
  DEBUG("Humidity: ");
  DEBUGLN(humidity);
}
