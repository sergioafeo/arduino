#include <NTPClient.h>
#include <Scheduler.h>
#include <Task.h>
#include <ESP8266WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WEMOS_SHT3X.h>
#define DEBUG_ESP_HTTP_CLIENT 1
#define DEBUG_ESP_PORT Serial
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>

// CONFIG
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

#define OLED_RESET          0  // GPIO0
#define MAX_SAMPLES         60
#define DEFAULT_SETPOINT    23.f
#define OVERSHOOT           0.1f
#define UNDERSHOOT          0.05f
#define MIN_TEMP            10.0f
#define MAX_TEMP            30.0f
const char* host          = "ESP-THERMOSTAT";
const char* ssid          = "POLOSUR";
const char* password      = "pinguinita";
#define relayPin            D0

// GLOBALS

SHT3X sht30(0x45);
Adafruit_SSD1306 display(OLED_RESET);
float setPoint;
ESP8266WebServer server(80);

// IMPLEMENTATIONS

class TempTask : public Task {
  protected:
    void setup() {
      http.setReuse(true);
      for (int i = 0; i < MAX_SAMPLES; i++) tempSamples[i] = 0.f;
      // Initialize temperature setpoint
      EEPROM.begin(sizeof(float));
      float eepromData;
      EEPROM.get(0x0, eepromData);
      DEBUG("Read from EEPROM: ");
      DEBUGLN(eepromData);
      if (eepromData > 10.0f && eepromData < 40.0f) //appears to be a valid value
        setPoint = eepromData;
      else
        setPoint = DEFAULT_SETPOINT;
      DEBUG("New setpoint: ");
      DEBUGLN(setPoint);
    }

    void loop() {
      String result;
      delay(1000);
      http.begin("192.168.178.20", 80, "/data");
      DEBUG("[HTTP] GET...\n");
      int httpCode = http.GET();
      float temp;
      float hum;
      // httpCode will be negative on error
      if (httpCode > 0) {
        // HTTP header has been sent and Server response header has been handled
        DEBUGF("[HTTP] GET... code: %d\n", httpCode);
        // file found at server
        if (httpCode == HTTP_CODE_OK) {
          result = http.getString();
          DEBUG(result);
          int sepIndex = result.indexOf(';');
          temp = result.substring(0, sepIndex).toFloat();
          hum = result.substring(sepIndex + 1).toFloat();
          updateAverage(temp);
          controlTemp();
        }
      } else {
        DEBUGF("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
        temp = hum = -1.f;
      }
      http.end();
      sht30.get();
      printData(temp, hum, sht30.cTemp, sht30.humidity);
    }
  private:
    HTTPClient http;
    float tempSamples[MAX_SAMPLES];
    int currSample = -1, nSamples = 0;
    float sumT;
    float avgT;


    void controlTemp() {
      static int state = LOW;
      int newState = state;
      switch (state) {
        case LOW:
          if (avgT < setPoint - UNDERSHOOT)
            newState = HIGH;
          break;
        case HIGH:
          if (avgT > setPoint + OVERSHOOT)
            newState = LOW;
          break;
        default:
          DEBUGLN("PANIC: unknown state. Resetting to LOW!");
          newState = LOW;
      }
      if (newState != state)
        digitalWrite(relayPin, newState);
      state = newState;
    }

    void updateAverage(float temp) {
      if (temp >= 0 && temp <= 100) {
        currSample = (currSample + 1) % MAX_SAMPLES;
        sumT -= tempSamples[currSample];  // This works bc the array is initialized with 0s
        sumT += temp;
        tempSamples[currSample] = temp;
        if (nSamples < MAX_SAMPLES) nSamples++;
        avgT = sumT / nSamples;
        DEBUG("temp: "); DEBUG(temp);
        DEBUG("sumT: "); DEBUG(sumT);
        DEBUG("nSamples: "); DEBUG(nSamples);
        DEBUG("currSample: "); DEBUGLN(currSample);
      }
    }

    void printData(float temp, float humidity, float localtemp, float localhumidity) {
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println(" -REMOTE-");
      if (temp >= 0 && temp <= 100) {
        display.print(temp);
        display.print("/");
        display.print(avgT);
        //display.println("C");
      }
      else {
        display.println("--.--");
      }
      if (humidity >= 0 && humidity <= 100) {
        display.print(" ");
        display.print(humidity);
        display.println("\%");
      }
      else {
        display.println("--.--");
      }
      display.println("  LOCAL ");
      display.print(localtemp);
      display.println("C");
      //    display.print(localhumidity);
      //    display.println("\%");
      display.print("Set: ");
      display.println(setPoint);
      display.display();
    }
} tempTask;

class WebTask : public Task {
  protected:
    void setup() {
      if (!MDNS.begin(host)) {
        DEBUGLN("Error setting up MDNS responder!");
      } else {
        DEBUGLN("mDNS responder started.");
      }
      server.on("/setPoint", handleSetPoint);
      updateServer.setup(&server);
      server.begin();
      MDNS.addService("http", "tcp", 80);
    }
    void loop() {
      server.handleClient();
    }

  private:

    ESP8266HTTPUpdateServer updateServer;

    static void handleSetPoint() {
      if (server.hasArg("setPoint")) {
        String setPointString = server.arg("setPoint");
        float newSetPoint = setPointString.toFloat();

        if (newSetPoint >= MIN_TEMP && newSetPoint <= MAX_TEMP) {
          setPoint = newSetPoint;
          EEPROM.put(0x0, setPoint);
          EEPROM.commit();
          server.send(200, "text/html", "OK!");
        }
        else {
          server.send(200, "text/html", "Range exceeded!");
        }
      }
      else
        server.send(200, "text/html", "Argument setPoint required!");
    }
} webTask;

// SETUP
void setup() {
#ifdef SERIAL_DEBUG
  Serial.begin(115200);
#endif
  // by default, we'll generate the high voltage from the 3.3v line internally! (neat!)
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3C (for the 64x48)
  // init done

  // Show image buffer on the display hardware.
  // Since the buffer is intialized with an Adafruit splashscreen
  // internally, this will display the splashscreen.
  display.display();
  pinMode(relayPin, OUTPUT);
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(WHITE);

  DEBUG("Connecting to ");
  DEBUGLN(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(host);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    DEBUG(".");
    display.print(".");
    display.display();
  }
  DEBUGLN("");
  DEBUGLN("WiFi connected");
  DEBUGLN("IP address: ");
  DEBUGLN(WiFi.localIP());
  display.println("");
  display.println(WiFi.localIP());
  display.display();

  Scheduler.start(&webTask);
  Scheduler.start(&tempTask);
  Scheduler.begin();
}
void loop() {}
