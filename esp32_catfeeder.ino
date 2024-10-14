#include <WiFi.h>
#include <ESPmDNS.h>
//https://techtutorialsx.com/2017/12/01/esp32-arduino-asynchronous-http-webserver/
// ESP Async WebServer 3.3.14 Me-No-Dev
// Requires AsyncTCP 3.2.10 Me-No-Dev
#include <ESPAsyncWebServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
// https://github.com/sstaub/TickTwo
#include "TickTwo.h"
#include "time.h"

void setTimezone(String);
void initTime(String);
void printLocalTime();
void setTime(int, int, int, int, int, int, int);
void feedBuster(int);
void onBreakfastTimer();
void onBlinkTimer();
void setBreakfastTimer();
void connectToWiFi(const char * , const char * );
void printLine();

// WiFi network name and password:
const char * networkName = "The One With the Good Signal";
const char * networkPswd = "fivewordslowercasenospaces";

// Time Zone String
// see https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
const char * tzString = "PST8PDT,M3.2.0,M11.1.0";

// Number of limit switch clicks per feeding
const int MEALSIZE_CLICKS_DEFAULT = 5;
const int MEALSIZE_CLICKS_MAX = 20;

typedef struct {
  int hr; 
  int min; 
  int clicks;
} meal;

// {hr, min, clicks}
// Hills prescription diet is about 6.6 grams per "click"
// Buster gets about 50 grams (8 clicks because I'm nice)
// Default feedings at 03:00 (3 clicks) and 05:00 (2 clicks) local
// set hr > 23 to mark an unscheduled slot.
const int NUM_SCHEDULE_SLOTS = 4;
meal schedule[NUM_SCHEDULE_SLOTS] = {
  {3, 0, 3},
  {5, 0, 5},
  {24, 0, 0},
  {24, 0, 0}
};
int meal_idx = 0;

bool led_state;

// Pin assignments
const int BUTTON_PIN = 0;
const int LED_PIN = 5;
const int LIMITSW_PIN = 12;
const int MOTOR_PIN = 13;

// https://github.com/me-no-dev/ESPAsyncWebServer
AsyncWebServer server(80);
const char* PARAM_MESSAGE = "message";

int next_mealsize_clicks = 0;
int seconds_til_breakfast = 100;
// execute once, in seconds_til_breakfast * 1000 milliseconds, use millisecond internal timer 
// timers default to microsecond internal resolution, which limits remaining time to 70 min
TickTwo breakfast_timer(onBreakfastTimer, seconds_til_breakfast * 1000, 1, MILLIS);
// used to wait one minute before scheduling next breakfast, when breakfast happens.
TickTwo nextbreakfast_timer(setBreakfastTimer, 60 * 1000, 1, MILLIS);
TickTwo blink_timer(onBlinkTimer, 1000, 0, MILLIS);

void setTimezone(String timezone){
  Serial.printf("  Setting Timezone to %s\n",timezone.c_str());
  setenv("TZ",timezone.c_str(),1);  //  Now adjust the TZ.  Clock settings are adjusted to show the new local time
  tzset();
}

void initTime(String timezone){
  struct tm timeinfo;

  Serial.println("Setting up time");
  configTime(0, 0, "pool.ntp.org");    // First connect to NTP server, with 0 TZ offset
  if(!getLocalTime(&timeinfo)){
    Serial.println("  Failed to obtain time");
    return;
  }
  Serial.println("  Got the time from NTP");
  // Now we can set the real timezone
  setTimezone(timezone);
}

void printLocalTime(){
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time 1");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S zone %Z %z ");
}

void setTime(int yr, int month, int mday, int hr, int minute, int sec, int isDst) {
  struct tm tm;

  tm.tm_year = yr - 1900;   // Set date
  tm.tm_mon = month-1;
  tm.tm_mday = mday;
  tm.tm_hour = hr;      // Set time
  tm.tm_min = minute;
  tm.tm_sec = sec;
  tm.tm_isdst = isDst;  // 1 or 0
  time_t t = mktime(&tm);
  Serial.printf("Setting time: %s", asctime(&tm));
  struct timeval now = { .tv_sec = t };
  settimeofday(&now, NULL);
}

void feedBuster(int mealsize_clicks=MEALSIZE_CLICKS_DEFAULT) {
  Serial.print("OMG shut up, I'm feeding you ");
  digitalWrite(LED_PIN, HIGH); // turn this on too
  digitalWrite(MOTOR_PIN, HIGH); // enable the feeder motor
  for (int clickCount = 0; clickCount < mealsize_clicks; clickCount++)
  {
    while (digitalRead(LIMITSW_PIN) == LOW)
      ; // make sure we drive at least long enough to release the limit switch once
    // 10 ms debounce
    vTaskDelay(10 / portTICK_RATE_MS);

    while (digitalRead(LIMITSW_PIN) == HIGH)
      ; // drive motor until limit switch is pressed again
    vTaskDelay(10 / portTICK_RATE_MS);

    Serial.print(clickCount + 1);
  }
  Serial.println(" and that's all you get.");
  digitalWrite(MOTOR_PIN, LOW); // disable the feeder motor
  digitalWrite(LED_PIN, LOW);
}

void onBreakfastTimer() {
  breakfast_timer.stop();
  feedBuster(next_mealsize_clicks);

  // wait 1m before scheduling next breakfast
  nextbreakfast_timer.start();
}

void onBlinkTimer() {
  digitalWrite(LED_PIN, led_state);
  led_state = !led_state;
  if (breakfast_timer.state() == RUNNING) {
    Serial.print("Breakfast in ");
    Serial.println(breakfast_timer.remaining() / 1000);
  } else {
    Serial.print("Scheduling in ");
    Serial.println(nextbreakfast_timer.remaining() / 1000);
  }
   
}

// https://github.com/sstaub/Ticker 
void setBreakfastTimer() {
  struct tm timeinfo;
  
  nextbreakfast_timer.stop();

  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time 1");
    return;
  }

  // calculate seconds till feeding
  // BUG: Just starts with the first feeding in the list; should find the _soonest_ instead.
  seconds_til_breakfast = (schedule[meal_idx].hr * 3600 + schedule[meal_idx].min * 60) - (timeinfo.tm_hour * 3600 + timeinfo.tm_min * 60 + timeinfo.tm_sec);
  next_mealsize_clicks = schedule[meal_idx].clicks;
  if (seconds_til_breakfast < 0) {
    seconds_til_breakfast += 86400;
  }
  printLocalTime();
  Serial.printf("%d seconds til breakfast.\n", seconds_til_breakfast);
  breakfast_timer.interval(seconds_til_breakfast * 1000);
  breakfast_timer.start();  

  // advance the mealidx to the next valid meal so we're ready to schedule that one
  do {
    meal_idx++;
    if (meal_idx >= NUM_SCHEDULE_SLOTS) {
      meal_idx = 0;
    }
  } while(schedule[meal_idx].hr > 23); 
  Serial.printf("The following feeding will be %d clicks at %02d:%02d.\n", schedule[meal_idx].clicks, schedule[meal_idx].hr, schedule[meal_idx].min);

}

void connectToWiFi(const char * ssid, const char * pwd) {
  int ledState = 0;

  printLine();
  Serial.println("Connecting to WiFi network: " + String(ssid));

  WiFi.begin(ssid, pwd);

  while (WiFi.status() != WL_CONNECTED) 
  {
    // Blink LED while we're connecting:
    digitalWrite(LED_PIN, ledState);
    ledState = (ledState + 1) % 2; // Flip ledState
    delay(500);
    Serial.print(".");
  }

  if (!MDNS.begin("catfeeder0")) {
    Serial.println("Error setting up MDNS responder!");
    return;
  }

  Serial.println();
  digitalWrite(LED_PIN, LOW);
  Serial.println("WiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  server.on("/hello", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "Hello World");
    Serial.print("serviced hello request.");
  });

  server.on("/feed", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasArg("clicks")) {
      int clicks = request->arg("clicks").toInt();
      if (clicks <= 0) {
        request->send(200, "text/plain", "Got feed request for " + request->arg("clicks") + " clicks. Failed to convert. Feeding default clicks (" + String(MEALSIZE_CLICKS_DEFAULT) + ").");
        feedBuster();
      } else if (clicks > MEALSIZE_CLICKS_MAX) {
        request->send(200, "text/plain", "Got feed request for " + request->arg("clicks") + " clicks. Over limit. Feeding max clicks (" + String(MEALSIZE_CLICKS_MAX) + ").");
        feedBuster(MEALSIZE_CLICKS_MAX);
      } else {
        request->send(200, "text/plain", "Got feed request for " + request->arg("clicks") + " clicks. Feeding.");
        feedBuster(clicks);
      }

    } else {
      request->send(200, "text/plain", "Got feed request without clicks param, feeding default clicks (" + String(MEALSIZE_CLICKS_DEFAULT) + ").");
      feedBuster();
    }
  });

  server.on("/schedule", HTTP_GET, [](AsyncWebServerRequest *request){
    int hour = 0;
    int min = 0;
    int idx = 0;
    int clicks = 0;
    
    if (request->hasArg("idx")) {
      idx = request->arg("idx").toInt();
      if (idx >= NUM_SCHEDULE_SLOTS) {
        request->send(400, "text/plain", "Invalid index: " + request->arg("idx") + ", Valid is 0-" + NUM_SCHEDULE_SLOTS);
      }
    }
    if (request->hasArg("hr")) {
      hour = request->arg("hr").toInt();
      if (hour > 23) {
        request->send(400, "text/plain", "Tried to set feeding hour to " + request->arg("hr"));
        return;
      } else {
        schedule[idx].hr = hour;
      }
    }
    
    if (request->hasArg("min")) {
      min = request->arg("min").toInt();
      if (min > 59) {
        request->send(400, "text/plain", "Tried to set feeding min to " + request->arg("min"));
        return;
      } else {
        schedule[idx].min = min;
      }
    }

    if (request->hasArg("clicks")) {
      clicks = request->arg("clicks").toInt();
      if (clicks > MEALSIZE_CLICKS_MAX) {
        // TODO print allowed values
        request->send(400, "text/plain", "Tried to set number of clicks to " + request->arg("clicks") + ", Max is " + String(MEALSIZE_CLICKS_MAX));
        return;
      } else {
        schedule[idx].clicks = clicks;
      }
    }

    request->send(200, "text/plain", "Set Breakfast Time "+String(idx)+" to "+String(hour)+":"+String(min)+", "+String(clicks)+" clicks.");
    setBreakfastTimer();

    // TODO probably need to sort the feeding times in the array so they don't come out all screwed up

  });

  server.begin();
}

void requestURL(const char * host, uint8_t port) {
  printLine();
  Serial.println("Connecting to domain: " + String(host));

  // Use WiFiClient class to create TCP connections
  WiFiClient client;
  if (!client.connect(host, port))
  {
    Serial.println("connection failed");
    return;
  }
  Serial.println("Connected!");
  printLine();

  // This will send the request to the server
  client.print((String)"GET / HTTP/1.1\r\n" +
               "Host: " + String(host) + "\r\n" +
               "Connection: close\r\n\r\n");
  unsigned long timeout = millis();
  while (client.available() == 0) 
  {
    if (millis() - timeout > 5000) 
    {
      Serial.println(">>> Client Timeout !");
      client.stop();
      return;
    }
  }

  // Read all the lines of the reply from server and print them to Serial
  while (client.available()) 
  {
    String line = client.readStringUntil('\r');
    Serial.print(line);
  }

  Serial.println();
  Serial.println("closing connection");
  client.stop();
}

void printLine() {
  Serial.println();
  for (int i=0; i<30; i++)
    Serial.print("-");
  Serial.println();
}

void setup() {
  // Initialize hardware:
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LIMITSW_PIN, INPUT_PULLUP);
  pinMode(MOTOR_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  digitalWrite(LED_PIN, LOW); // LED off
  digitalWrite(MOTOR_PIN, LOW); // motor off

  // Connect to the WiFi network (see function below loop)
  connectToWiFi(networkName, networkPswd);
  initTime(tzString);
  setBreakfastTimer();

  Serial.println("Press button 0 to feed the cat");
  blink_timer.start();
}

void loop() {
  blink_timer.update();
  breakfast_timer.update();
  nextbreakfast_timer.update();
  if (digitalRead(BUTTON_PIN) == LOW)
  { // Check if button has been pressed
    while (digitalRead(BUTTON_PIN) == LOW)
      ; // Wait for button to be released

    digitalWrite(LED_PIN, HIGH); // Turn on LED
    feedBuster();
    digitalWrite(LED_PIN, LOW); // Turn off LED
  }
}
