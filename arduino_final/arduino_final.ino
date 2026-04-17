#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "esp_eap_client.h"
#include  "pitches.h"
#include <Wire.h>
#include <U8g2lib.h>
#include "EncryptedUART.h"

const char* rootCA = R"EOF(
-----BEGIN CERTIFICATE-----
MIIJTzCCBzegAwIBAgITMwN9YtVOtlpRoLvOxgAAA31i1TANBgkqhkiG9w0BAQwF
ADBdMQswCQYDVQQGEwJVUzEeMBwGA1UEChMVTWljcm9zb2Z0IENvcnBvcmF0aW9u
MS4wLAYDVQQDEyVNaWNyb3NvZnQgQXp1cmUgUlNBIFRMUyBJc3N1aW5nIENBIDAz
MB4XDTI2MDMxNTA5MzAwMVoXDTI2MDgyNTIzNTk1OVowajELMAkGA1UEBhMCVVMx
CzAJBgNVBAgTAldBMRAwDgYDVQQHEwdSZWRtb25kMR4wHAYDVQQKExVNaWNyb3Nv
ZnQgQ29ycG9yYXRpb24xHDAaBgNVBAMMEyouYXp1cmV3ZWJzaXRlcy5uZXQwggEi
MA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQCl1/tp68A/IOEGEWDJO30Ry16t
hRIJdQJDEMg/RW0tYiSrChHbF/++bKqqgHHyVNnS1YzPWpzUxXgDGPlL+IiOSYQ0
rplS7CC5Az8yCQboGqa8r9j3wqk8O9o9CHcM/mYqg8Lli1WHeX3Xf8tyJ1Pje4W5
f61lM9QchBlWu5d6a7MudNRIZEUMs1b/eFpfskYB0SCgWOt+mvGXZizDLE5cIUCE
i7poDa32j3KHckJxROXzXetWzkuK01Eh707wlTkwyGw/E9NxbHf4ACvnGN+bGHqa
sXtQbFBlfykq9L5HPTKaQzljuuHyXNqU/R2/1X3vai2WVrXP3xaXwq7KpIKZAgMB
AAGjggT5MIIE9TCCAQQGCisGAQQB1nkCBAIEgfUEgfIA8AB2ANgJVTuUT3r/yBYZ
b5RPhauw+Pxeh1UmDxXRLnK7RUsUAAABnPDeAbkAAAQDAEcwRQIgBOKOQ00gCOdW
Lka4sJzxv80J+HJ6Ioh1ggRgje22uOoCIQCUw4CEvDIGypXZNBI/VWFukL1wa5qj
jRB86YXvYjYe0gB2AMIxfldFGaNF7n843rKQQevHwiFaIr9/1bWtdprZDlLNAAAB
nPDeAV8AAAQDAEcwRQIhAObJFxTlw4teun1DPQ42wgstAd+lyydO/vsIxrXpiESz
AiB0zGrbmX4r+oVjU5pZyyEJ8D5H7KadCHG/pD2FU9zE7zAnBgkrBgEEAYI3FQoE
GjAYMAoGCCsGAQUFBwMBMAoGCCsGAQUFBwMCMDwGCSsGAQQBgjcVBwQvMC0GJSsG
AQQBgjcVCIe91xuB5+tGgoGdLo7QDIfw2h1dgoTlaYLzpz8CAQACAQEwgbQGCCsG
AQUFBwEBBIGnMIGkMHMGCCsGAQUFBzAChmdodHRwOi8vd3d3Lm1pY3Jvc29mdC5j
b20vcGtpb3BzL2NlcnRzL01pY3Jvc29mdCUyMEF6dXJlJTIwUlNBJTIwVExTJTIw
SXNzdWluZyUyMENBJTIwMDMlMjAtJTIweHNpZ24uY3J0MC0GCCsGAQUFBzABhiFo
dHRwOi8vb25lb2NzcC5taWNyb3NvZnQuY29tL29jc3AwHQYDVR0OBBYEFB/g2GBb
z5MJWR5qo4PPHwVUkhx/MA4GA1UdDwEB/wQEAwIFoDCCAWsGA1UdEQSCAWIwggFe
ghMqLmF6dXJld2Vic2l0ZXMubmV0ghcqLnNjbS5henVyZXdlYnNpdGVzLm5ldIIX
Ki5zc28uYXp1cmV3ZWJzaXRlcy5uZXSCIyoubWFsYXlzaWF3ZXN0LTAxLmF6dXJl
d2Vic2l0ZXMubmV0gicqLnNjbS5tYWxheXNpYXdlc3QtMDEuYXp1cmV3ZWJzaXRl
cy5uZXSCJyouc3NvLm1hbGF5c2lhd2VzdC0wMS5henVyZXdlYnNpdGVzLm5ldIIi
Ki5tYWxheXNpYXdlc3QuYy5henVyZXdlYnNpdGVzLm5ldIImKi5zY20ubWFsYXlz
aWF3ZXN0LmMuYXp1cmV3ZWJzaXRlcy5uZXSCJiouc3NvLm1hbGF5c2lhd2VzdC5j
LmF6dXJld2Vic2l0ZXMubmV0ghIqLmF6dXJlLW1vYmlsZS5uZXSCFiouc2NtLmF6
dXJlLW1vYmlsZS5uZXQwDAYDVR0TAQH/BAIwADB5BgNVHR8EcjBwMG6gbKBqhmho
dHRwOi8vd3d3Lm1pY3Jvc29mdC5jb20vcGtpb3BzL2NybC9NaWNyb3NvZnQlMjBB
enVyZSUyMFJTQSUyMFRMUyUyMElzc3VpbmclMjBDQSUyMDAzX1BhcnRpdGlvbjAw
MTAwLmNybDBmBgNVHSAEXzBdMAgGBmeBDAECAjBRBgwrBgEEAYI3TIN9AQEwQTA/
BggrBgEFBQcCARYzaHR0cDovL3d3dy5taWNyb3NvZnQuY29tL3BraW9wcy9Eb2Nz
L1JlcG9zaXRvcnkuaHRtMB8GA1UdIwQYMBaAFP4JcUBVBRBE2KSBdbieGulKBojI
MB0GA1UdJQQWMBQGCCsGAQUFBwMBBggrBgEFBQcDAjANBgkqhkiG9w0BAQwFAAOC
AgEAPqooqJpf/WND8l8Any4Zycss8JO7MLEc9V0AYNZBmsbKnXbtkRuJkUqBnxri
YoJyL41zE7t7KmJi3Syt0jkgXs9St1ERBZuyXdk89BUOCAccNQ8cLwGsl8TEq7ud
3Vgph0xW2UWvnPqWbGRUyvcrhG2t7pyINfOP94/NOgxiz1HAgA3FN5xlSvV+u4eP
lyTUo1/ybNNaZAw5scVha9H14g5ZbfdHVe+JWZGBpcbleJ6b3YmumvNHCDgURULA
tCTiAp+hntcxSgJxO1MsI2DLiQlDtiWEBDvL6dOmEhh6UDZAJ2VpdjLUlQURrzOd
SYKFDvEa0B5kj17uCOcjAyqLFb1hHJJWy9kjfHFweU7cE/nZ3B4mCsJifSatMd+2
d6m473zO0qhJrEt0CHQcKVJKiiBR2rG1oI930yVOTc1DHixksW2RTC2+nvAHpIxi
CwtLreNFb8hSGPN+QKnrof0pc7k0wUjXzNB5JRrqxxvO4i5eNSYvn4GSGRLc4eFT
OgD7tJJJx8JAbCryYdqcP64MOVijfc69Ro7zWURtlKExSqwmIShtnEstGo8aNjwC
nh65NVV1zh1nQZQwktB2GUcaRx6XjWI9otSY0pPptcEzQumGDHpgzeYSaQ/aM7Y3
Ufga/GpHIFj83+S4IGy6/n4l7D4xm5jDtnAgxCKAnpDb3CU=
-----END CERTIFICATE-----
)EOF";


// Initialize OLED
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// --- STATE MACHINE ---
enum State {
  STATE_CLOCK,
  STATE_SET_ALARM,
  STATE_SET_TIMER,
  STATE_ALARM_SAVED,
  STATE_ALARM_RINGING,
  STATE_TIMER_RINGING
};

State currentState = STATE_CLOCK;
unsigned long savedMessageTimer = 0;

const char* ssid = "iPhone (6)";
const char* password = "12345678";

String jsonBuffer;

//melody for Alarm
int  melody[] = {
  NOTE_C4, NOTE_A3, NOTE_A3, NOTE_C4, NOTE_D4, NOTE_G3, NOTE_G3,  NOTE_A3, NOTE_AS3, NOTE_F4, NOTE_F4, NOTE_E4, NOTE_C4, NOTE_D4, NOTE_C4, NOTE_AS3,  NOTE_A3, NOTE_C4, NOTE_D4, NOTE_D4
}; 

//melody for Timer
int timerMelody[] = {
    NOTE_C4, NOTE_C4, NOTE_G3, NOTE_G3,
    NOTE_A3, NOTE_A3, NOTE_G3,
    NOTE_F4, NOTE_F4, NOTE_E4, NOTE_E4,
    NOTE_D4, NOTE_D4, NOTE_C4
};

int timerDuration[] = {
    4, 4, 4, 4,
    4, 4, 2,
    4, 4, 4, 4,
    4, 4, 2
};

int durations[] = {
  2,  2, 3, 3, 3, 2, 3, 3, 2, 1, 3, 3, 3, 5, 5, 5, 1, 2, 1, 3, 3, 3, 3, 1
}; // declaring  the duration of each note

int timerSongLength = sizeof(timerMelody)/sizeof(melody[0]);
int songLength = sizeof(melody)/sizeof(melody[0]); 

const int BUZZER_PIN = 5;
const int TOUCH_PIN = 4;
const int NEW_TX_PIN = 17;
const int NEW_RX_PIN = 18;  

// Global variables to keep track of where we are in the song
unsigned long previousNoteTime = 0;
int currentNote = 0;
bool notePlaying = false; // Tracks if the buzzer is currently making sound
int currentDuration = 0;  // Stores how long the current note should buzz
int currentPause = 0;     // Stores the total time before the next note
unsigned long noteStartTime = 0;

unsigned long alarmStartTime = 0;
bool stopFlagTimer = false;
bool stopFlagAlarm = false;
bool snoozeFlag = false;
int alarmSetDigit = 0;
int timerSetDigit = 0;

//Gloabal variables for alarm
int alarmHour = 0;
int alarmMinute = 0;
int tempAlarmHour = 12;
int tempAlarmMinute = 0;

//Master Switch for alarm
bool isAlarmEnabled = false;

int timerHour = 0;
int timerMinute = 0;
int timerSeconds = 0;

//Master Switch for Timer 
bool isTimerEnabled = 0;

void setup() {
  Serial.begin(115200);
  Serial1.begin(9600, SERIAL_8N1, NEW_RX_PIN, NEW_TX_PIN);
  Serial.print("Connecting to SSID:");
  Serial.println(ssid);

  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA); 

  WiFi.begin(ssid, password);
  Serial.println("Connecting");
  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.print("\nConnected to WiFi network with IP Address: ");
  Serial.println(WiFi.localIP());

  configTime(28800, 0, "pool.ntp.org");

  pinMode(TOUCH_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  initEncryptedUART();

  // Initialize the OLED screen
  Wire.begin(8, 9);
  u8g2.begin();

  Serial.println("====================================");
  Serial.println("OLED UI Test Booted!");
  Serial.println("====================================");

}

void loop() {

  String incomingMessage = "";
  int TouchValue = digitalRead(TOUCH_PIN);

  if (TouchValue && currentState == STATE_ALARM_RINGING) {
    snoozeFlag = true;
    sendEncryptedMessage("SNOOZE");
  }

  // 1. Check for new encrypted packets from the MCX
  if (pollEncryptedUART(incomingMessage)) {
      // Clean off any invisible carriage returns
      incomingMessage.trim(); 
      
      if (incomingMessage.startsWith("SW2:")) {
        if (currentState == STATE_TIMER_RINGING) {
          stopFlagTimer = true;
        } else if (currentState == STATE_ALARM_RINGING) {
          stopFlagAlarm = true;
        }
        Serial.println("SW2 pressed");
      } else if (currentState != STATE_ALARM_SAVED){
      handleJoystickPackets(incomingMessage);
      }
  }

  //revert back to clock state after the msg is displayed for 2 seconds
  if (currentState == STATE_ALARM_SAVED) {
      if (millis() - savedMessageTimer >= 2000) { 
          currentState = STATE_CLOCK;
      }
  }

  // --- 2. TIME & TRIGGER LOGIC ---
  struct tm timeinfo;
  
  if (getLocalTime(&timeinfo)) { 
    
    updateDisplay(&timeinfo);

    // Check if the alarm should trigger
    if (isAlarmEnabled) {
        if (timeinfo.tm_hour == alarmHour && timeinfo.tm_min == alarmMinute) {
            if (currentState != STATE_ALARM_RINGING) {
                Serial.println("ALARM TRIGGERED! WAKE UP!");
                sendEncryptedMessage("ALARM_FIRING");
                currentState = STATE_ALARM_RINGING;
                alarmStartTime = millis();    
            }
        }
    }
  }

  static unsigned long lastTimerTick = 0;
  //Timer logic
  if (isTimerEnabled) {
    if (millis() - lastTimerTick >= 1000) {
        
      lastTimerTick = millis(); 

      if (timerHour > 0 || timerMinute > 0 || timerSeconds > 0) {
            timerSeconds--;

        if (timerSeconds < 0){
          timerSeconds = 59;
          timerMinute--;
        }

        if (timerMinute < 0){
          timerMinute = 59;
          timerHour--;
        }
            
        if (timerHour == 0 && timerMinute == 0 && timerSeconds == 0){
          if (currentState != STATE_TIMER_RINGING){
            Serial.println("TIMER TRIGGERED!");
            sendEncryptedMessage("ALARM_FIRING");
            currentState = STATE_TIMER_RINGING;
          }
        }
      }
    } 
  }
  

  // --- BUZZER STATE MACHINE ---
  
  if (currentState == STATE_ALARM_RINGING) {
      // Alarm takes priority. Play the standard melody.
      playBuzzerSequence(melody, durations, songLength);
      
  } else if (currentState == STATE_TIMER_RINGING) {
      // If only the timer is ringing, play the timer melody.
      playBuzzerSequence(timerMelody, timerDuration, timerSongLength);
  }

  // --- 4. REACTION LOGIC ---
  if (currentState == STATE_ALARM_RINGING) {
    if (snoozeFlag) {
      unsigned long reactionTime = millis() - alarmStartTime;
      currentState = STATE_CLOCK;
      noTone(BUZZER_PIN);
      currentNote = 0; // Reset song for next time
      unsigned long dynamicSnooze_ms = logAndGetSnoozeTime("SNOOZE", reactionTime);
      int snoozeMinutes = dynamicSnooze_ms / 60000;
      Serial.printf("Setting new alarm for %d minutes from now...\n", snoozeMinutes);

      alarmMinute = timeinfo.tm_min + snoozeMinutes;
      alarmHour = timeinfo.tm_hour;
      if (alarmMinute >= 60) {
          alarmMinute -= 60;
          alarmHour += 1;
          if (alarmHour >= 24) alarmHour = 0;
      }
      snoozeFlag = false;
      
    } else if (stopFlagAlarm) {
      unsigned long reactionTime = millis() - alarmStartTime;
      currentState = STATE_CLOCK;
      noTone(BUZZER_PIN);
      currentNote = 0; // Reset song for next time

      isAlarmEnabled = false;
      stopFlagAlarm = false;

      logAndGetSnoozeTime("STOP", reactionTime);
    }
  } else if (currentState == STATE_TIMER_RINGING) {
    if (stopFlagTimer) {
      currentState = STATE_CLOCK;
      noTone(BUZZER_PIN);
      currentNote = 0; // Reset song for next time

      isTimerEnabled = false;
      stopFlagTimer = false;
    }
  }
}

unsigned long logAndGetSnoozeTime(String actionType, unsigned long reactionTime) {
  
  // The Hardware Fallback: Default to 5 minutes (300,000 ms) if anything goes wrong
  unsigned long grantedSnoozeTime_ms = 300000; 

  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    
    // Bypasses SSL certificate verification (Standard for ESP32 testing HTTPS)
    client.setCACert(rootCA);
    HTTPClient http;
    
    const char* serverName = "https://alarmdata-dyfrdfc7fpaphbgs.malaysiawest-01.azurewebsites.net/api/log_alarm";
    http.begin(client, serverName);
    http.addHeader("Content-Type", "application/json");
    
    // 1. Build the outgoing JSON payload safely
    JsonDocument sendDoc;
    sendDoc["ActionType"] = actionType;
    sendDoc["ReactionTime_ms"] = reactionTime;
    
    String jsonPayload;
    serializeJson(sendDoc, jsonPayload);
    
    Serial.print(">>> Sending to Azure: ");
    Serial.println(jsonPayload);
    
    // 2. Fire the POST request
    int httpResponseCode = http.POST(jsonPayload);
    
    if (httpResponseCode > 0) {
      String response = http.getString(); 
      Serial.println("<<< Azure Reply: " + response);
      
      // 3. If it's a SNOOZE event, parse the returned math
      if (actionType == "SNOOZE") {
          JsonDocument receiveDoc;
          DeserializationError error = deserializeJson(receiveDoc, response);

          if (!error) {
              // Extract the cloud's calculations
              unsigned long avg7Day_ms = receiveDoc["Average7Day_ms"].as<unsigned long>();
              
              // Overwrite the fallback 5-minutes with the cloud's decision
              grantedSnoozeTime_ms = receiveDoc["NewSnoozeTime_ms"].as<unsigned long>();
              
              Serial.printf("Parsed Math -> 7-Day Avg: %lu ms | Granted Snooze: %lu ms\n", 
                            avg7Day_ms, grantedSnoozeTime_ms);
          } else {
              Serial.print("Failed to parse Azure's JSON reply: ");
              Serial.println(error.c_str());
          }
      }
    } else {
      Serial.print("Error sending POST. Code: ");
      Serial.println(httpResponseCode);
    }
    
    http.end();
  } else {
    Serial.println("Error: WiFi Disconnected. Using 5-minute fallback.");
  }
  
  return grantedSnoozeTime_ms;
}

void handleJoystickPackets(String cmd) {
    Serial.print("MCX Sent: ");
    Serial.println(cmd);

    if (currentState == STATE_CLOCK) {
      if (cmd == "JOY:RIGHT") {
        currentState = STATE_SET_ALARM;

        alarmSetDigit = 0; // Start by editing the hour
        Serial.println("Entered Alarm Menu (Editing Hours)");
      } else if (cmd == "JOY:LEFT"){
        currentState = STATE_SET_TIMER;
        timerSetDigit = 0;
        Serial.println("Entered Timer Menu (Editing Minutes)");
      }
    } else if (currentState == STATE_SET_ALARM) {
      if (cmd == "JOY:UP") { // UP
        if (alarmSetDigit == 0) {
          tempAlarmHour = (tempAlarmHour + 1) % 24;
        } else {
          tempAlarmMinute = (tempAlarmMinute + 1) % 60;
        }
      } else if (cmd == "JOY:DOWN") { // DOWN
        if (alarmSetDigit == 0) {
          tempAlarmHour = (tempAlarmHour - 1 < 0) ? 23 : tempAlarmHour - 1;
        } else {
          tempAlarmMinute = (tempAlarmMinute - 1 < 0) ? 59 : tempAlarmMinute - 1;
        }

      } else if (cmd == "JOY:RIGHT") { // PRESS
        alarmSetDigit = 1; 
        Serial.println("Editing Minutes");
      } else if (cmd == "JOY:LEFT") { // PRESS
        alarmSetDigit = 0; 
        Serial.println("Editing Minutes");
      } else if (cmd == "JOY:LONG_PRESS"){
          alarmHour = tempAlarmHour;
          alarmMinute = tempAlarmMinute;
          isAlarmEnabled = true;
          currentState = STATE_CLOCK; // Save and return to main screen
          Serial.println("Alarm Saved! Returned to Clock Mode.");
      } else if (cmd == "JOY:PRESS"){
          Serial.println("Cancelled! Going Back to Clock Mode.");
          currentState = STATE_CLOCK;
      }
    } else if (currentState == STATE_SET_TIMER){
      if (cmd == "JOY:UP"){
        if (timerSetDigit == 0){
          timerHour = (timerHour + 1) % 24;
        } else if (timerSetDigit == 1) {
          timerMinute = (timerMinute + 1) % 60;
        } else {
          timerSeconds = (timerSeconds + 1) % 60;
        } 
      } else if (cmd == "JOY:DOWN"){
         if (timerSetDigit == 0){
          timerHour = (timerHour - 1 < 0) ? 24 : timerHour - 1;
        } else if (timerSetDigit == 1) {
          timerMinute = (timerMinute - 1 < 0) ? 59 : timerMinute - 1;
        } else {
          timerSeconds = (timerSeconds - 1 < 0) ? 59 : timerSeconds - 1;
        }
      }
      else if (cmd == "JOY:LEFT"){
        timerSetDigit = (timerSetDigit - 1 < 0) ? 3 : timerSetDigit - 1;
      } else if (cmd == "JOY:RIGHT"){
        timerSetDigit = (timerSetDigit + 1) % 3;
      } else if (cmd == "JOY:LONG_PRESS"){
        isTimerEnabled = true;
        currentState = STATE_CLOCK;
        Serial.println("Timer saved, returning to clock mode");
      } else if (cmd == "JOY:PRESS"){
        Serial.println("Cancelled! Going back to clock mode");
        currentState = STATE_CLOCK;
      }
    }
}

void playBuzzerSequence(const int* activeMelody, const int* activeDurations, int activeSongLength) {
    unsigned long currentMillis = millis();

    // 1. BOOTSTRAP: If this is the start of the song, trigger the first note
    if (currentDuration == 0) {
        currentDuration = 1000 / activeDurations[currentNote]; 
        currentPause = currentDuration * 1.5;            
        
        tone(BUZZER_PIN, activeMelody[currentNote]); 
        noteStartTime = currentMillis;
        notePlaying = true;
    }

    // 2. THE BUZZING PHASE: Is it time to shut up?
    if (notePlaying) {
        if (currentMillis - noteStartTime >= currentDuration) {
            noTone(BUZZER_PIN);  
            notePlaying = false; 
        }
    }
    
    // 3. THE SILENCE PHASE: Is it time for the next note?
    else {
        if (currentMillis - noteStartTime >= currentPause) {
            // Move to the next note
            currentNote++; 
            if (currentNote >= activeSongLength) {
                currentNote = 0; 
            }

            // Calculate times for the new note
            currentDuration = 1000 / activeDurations[currentNote];
            currentPause = currentDuration * 1.5;

            // Fire the new note
            tone(BUZZER_PIN, activeMelody[currentNote]); 
            noteStartTime = currentMillis;
            notePlaying = true;
        }
    }
}

void updateDisplay(struct tm *timeinfo) {
  u8g2.clearBuffer();

  // 1. ALWAYS DRAW THE CURRENT TIME AT THE TOP
  char timeStr[10]; // Needs to be at least 9 characters to hold HH:MM:SS + null terminator
  
  // Format the real time from the struct passed into the function
  strftime(timeStr, sizeof(timeStr), "%H:%M:%S", timeinfo);
  
  u8g2.setFont(u8g2_font_10x20_tf);
  u8g2.drawStr(24, 20, timeStr);

  // 2. DRAW THE BOTTOM HALF BASED ON THE STATE
  if (currentState == STATE_CLOCK) {
    u8g2.setFont(u8g2_font_6x10_tf);
    if(isTimerEnabled) {
      u8g2.drawStr(0, 45, "Mode: Timer");
      char timerStr[20];
      snprintf(timerStr, sizeof(timerStr), "Timer %02d:%02d:%02d", timerHour, timerMinute, timerSeconds);
      u8g2.drawStr(0, 60, timerStr);
    } else if (isAlarmEnabled) {
      u8g2.drawStr(0, 45, "Mode: Alarm");
      char alarmStr[20];
      snprintf(alarmStr, sizeof(alarmStr), "Alarm %02d:%02d", alarmHour, alarmMinute);
      u8g2.drawStr(0, 60, alarmStr);
    } else {
      u8g2.drawStr(0, 45, "Mode: Clock");
      u8g2.setFont(u8g2_font_5x8_tf);
      u8g2.drawStr(0, 60, "Left: timer Right: Alarm");
    }
  } else if (currentState == STATE_SET_ALARM) {
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(0, 40, "SET ALARM:");

    char alarmStr[10];
    snprintf(alarmStr, sizeof(alarmStr), "%02d:%02d", tempAlarmHour, tempAlarmMinute);
    u8g2.setFont(u8g2_font_10x20_tf);
    u8g2.drawStr(34, 58, alarmStr);

    // Draw the cursor underline!
    // X=34 puts it under the Hour. X=64 skips the colon and puts it under the Minute.
    int underlineX = (alarmSetDigit == 0) ? 34 : 64; 
    u8g2.drawLine(underlineX, 61, underlineX + 18, 61);
  } else if (currentState == STATE_ALARM_SAVED) {
    u8g2.setFont(u8g2_font_6x10_tf);
    
    // Draw a nice confirmation message
    u8g2.drawStr(24, 45, "ALARM SAVED!");
    
    // Show them exactly what time they just saved
    char savedStr[20];
    snprintf(savedStr, sizeof(savedStr), "Time: %02d:%02d", alarmHour, alarmMinute);
    u8g2.drawStr(24, 60, savedStr);
  } else if (currentState == STATE_ALARM_RINGING || currentState == STATE_TIMER_RINGING) {
    // Use the large font for the main warning
      u8g2.setFont(u8g2_font_10x20_tf);
      
      // Hardware-free flashing effect (blinks twice a second)
      if ((millis() / 250) % 2 == 0) { 
          u8g2.drawStr(9, 38, "!! ALARM !!");
      }
      
      // Switch back to the small font for the instructions
      u8g2.setFont(u8g2_font_6x10_tf);
      
      // Remind the user what the physical buttons do!
      u8g2.drawStr(0, 62, "Touch:Snooze SW2:Stop");
  } else if (currentState == STATE_SET_TIMER) {
      u8g2.setFont(u8g2_font_6x10_tf);
      u8g2.drawStr(0, 40, "SET TIMER:");

      char timerStr[12];
      snprintf(timerStr, sizeof(timerStr), "%02d:%02d:%02d", timerHour, timerMinute, timerSeconds);
      u8g2.setFont(u8g2_font_10x20_tf);
      u8g2.drawStr(14, 58, timerStr);  // Shifted left to fit HH:MM:SS

      // Draw the cursor underline
      // Position depends on which digit is being edited (0=hour, 1=minute, 2=second)
      int underlineX;
      if (timerSetDigit == 0) {
          underlineX = 14;   // Under hours
      } else if (timerSetDigit == 1) {
          underlineX = 44;   // Under minutes
      } else {
          underlineX = 74;   // Under seconds
      }
      u8g2.drawLine(underlineX, 61, underlineX + 18, 61);
    }

  u8g2.sendBuffer();
}