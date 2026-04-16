#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "esp_eap_client.h"
#include  "pitches.h"
#include <Wire.h>
#include <U8g2lib.h>
#include "EncryptedUART.h"

// Initialize OLED (Using your exact pins: SDA=8, SCL=9)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// --- STATE MACHINE ---
enum State {
  STATE_CLOCK,
  STATE_SET_ALARM,
  STATE_SET_TIMER
};

State currentState = STATE_CLOCK;

const char* ssid = "keane";
const char* password = "keane123";

String jsonBuffer;

int  melody[] = {
  NOTE_C4, NOTE_A3, NOTE_A3, NOTE_C4, NOTE_D4, NOTE_G3, NOTE_G3,  NOTE_A3, NOTE_AS3, NOTE_F4, NOTE_F4, NOTE_E4, NOTE_C4, NOTE_D4, NOTE_C4, NOTE_AS3,  NOTE_A3, NOTE_C4, NOTE_D4, NOTE_D4
}; // declaring the notes of the melody (they  change depending on the song you wanna play)

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

// 4 = quarter note, 2 = half note, 8 = eighth note}
int durations[] = {
  2,  2, 3, 3, 3, 2, 3, 3, 2, 1, 3, 3, 3, 5, 5, 5, 1, 2, 1, 3, 3, 3, 3, 1
}; // declaring  the duration of each note (4 is a quarter note ecc)

int timerSongLength = sizeof(timerMelody)/sizeof(melody[0]);
int songLength = sizeof(melody)/sizeof(melody[0]);  // defining the song length, in this case it is equal to the length of the melody

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

bool alarmRinging = false;
bool timerRinging = false;
unsigned long alarmStartTime = 0;
bool stopFlagAlarm = false;
bool snoozeFlagAlarm = false;
bool stopFlagTimer = false;
int alarmSetDigit = 0;
int timerSetDigit = 0;

//Gloabal variables for alarm
int alarmHour = 0;
int alarmMinute = 0;

//Master Switch for alarm
bool isAlarmEnabled = false;

//Global Variables for Timer
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

  if (alarmRinging & TouchValue) {
    snoozeFlagAlarm = true;
    sendEncryptedMessage("SNOOZE");
  }

  // 1. Check for new encrypted packets from the MCX
  if (pollEncryptedUART(incomingMessage)) {
      // Clean off any invisible carriage returns
      incomingMessage.trim(); 
      
      if (alarmRinging || timerRinging) {
        if (incomingMessage.startsWith("SW2:")) {
          stopFlagAlarm = true;
          stopFlagTimer = true;
          Serial.println("Both stop flag is true");
        }
      } else {
        handleJoystickPackets(incomingMessage);
      }
  }

  // --- 2. TIME & TRIGGER LOGIC ---
  struct tm timeinfo;
  
  // We can call this as fast as possible because it just reads internal memory!
  if (getLocalTime(&timeinfo)) { 
    
    updateDisplay(&timeinfo);

    // Check if the alarm should trigger
    if (isAlarmEnabled) {
        if (timeinfo.tm_hour == alarmHour && timeinfo.tm_min == alarmMinute) {
            if (!alarmRinging) {
                Serial.println("ALARM TRIGGERED! WAKE UP!");
                alarmRinging = true;
                alarmStartTime = millis();    
            }
        }
    }
  }

  if (isTimerEnabled) { 
    if (timerHour > 0 || timerMinute > 0 || timerSeconds > 0){
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
        if (!timerRinging){
          Serial.println("TIMER TRIGGERED!");
          timerRinging = true;
        }
      }
    }
  }

  if (timerRinging && alarmRinging) {

        unsigned long currentMillis = millis();

        // 1. BOOTSTRAP: If this is the start of the song, trigger the first note
        if (currentDuration == 0) {
            currentDuration = 1000 / durations[currentNote]; // Back to your fast speed!
            currentPause = currentDuration * 1.5;            // Back to your crisp gap!
            
            tone(BUZZER_PIN, melody[currentNote]); // WARNING: Notice we do NOT pass 'currentDuration' here!
            noteStartTime = currentMillis;
            notePlaying = true;
        }

        // 2. THE BUZZING PHASE: Is it time to shut up?
        if (notePlaying) {
            if (currentMillis - noteStartTime >= currentDuration) {
                noTone(BUZZER_PIN);  // We explicitly kill the sound ourselves
                notePlaying = false; // Transition into the silence phase
            }
        }
        
        // 3. THE SILENCE PHASE: Is it time for the next note?
        else {
            if (currentMillis - noteStartTime >= currentPause) {
                // Move to the next note
                currentNote++; 
                if (currentNote >= songLength) {
                    currentNote = 0; 
                }

                // Calculate times for the new note
                currentDuration = 1000 / durations[currentNote];
                currentPause = currentDuration * 1.5;

                // Fire the new note
                tone(BUZZER_PIN, melody[currentNote]); 
                noteStartTime = currentMillis;
                notePlaying = true;
            }
        }
  }

  // --- 3. BUZZER STATE MACHINE ---
  if (alarmRinging) {
        unsigned long currentMillis = millis();

        // 1. BOOTSTRAP: If this is the start of the song, trigger the first note
        if (currentDuration == 0) {
            currentDuration = 1000 / durations[currentNote]; // Back to your fast speed!
            currentPause = currentDuration * 1.5;            // Back to your crisp gap!
            
            tone(BUZZER_PIN, melody[currentNote]); // WARNING: Notice we do NOT pass 'currentDuration' here!
            noteStartTime = currentMillis;
            notePlaying = true;
        }

        // 2. THE BUZZING PHASE: Is it time to shut up?
        if (notePlaying) {
            if (currentMillis - noteStartTime >= currentDuration) {
                noTone(BUZZER_PIN);  // We explicitly kill the sound ourselves
                notePlaying = false; // Transition into the silence phase
            }
        }
        
        // 3. THE SILENCE PHASE: Is it time for the next note?
        else {
            if (currentMillis - noteStartTime >= currentPause) {
                // Move to the next note
                currentNote++; 
                if (currentNote >= songLength) {
                    currentNote = 0; 
                }

                // Calculate times for the new note
                currentDuration = 1000 / durations[currentNote];
                currentPause = currentDuration * 1.5;

                // Fire the new note
                tone(BUZZER_PIN, melody[currentNote]); 
                noteStartTime = currentMillis;
                notePlaying = true;
            }
        }
    }

  if (timerRinging){
     unsigned long currentMillis = millis();

      // 1. BOOTSTRAP: If this is the start of the song, trigger the first note
      if (currentDuration == 0) {
          currentDuration = 1000 / timerDuration[currentNote]; // Back to your fast speed!
          currentPause = currentDuration * 1.5;            // Back to your crisp gap!
          
          tone(BUZZER_PIN, timerMelody[currentNote]); // WARNING: Notice we do NOT pass 'currentDuration' here!
          noteStartTime = currentMillis;
          notePlaying = true;
      }

      // 2. THE BUZZING PHASE: Is it time to shut up?
      if (notePlaying) {
          if (currentMillis - noteStartTime >= currentDuration) {
              noTone(BUZZER_PIN);  // We explicitly kill the sound ourselves
              notePlaying = false; // Transition into the silence phase
          }
      }
      
      // 3. THE SILENCE PHASE: Is it time for the next note?
      else {
          if (currentMillis - noteStartTime >= currentPause) {
              // Move to the next note
              currentNote++; 
              if (currentNote >= songLength) {
                  currentNote = 0; 
              }

              // Calculate times for the new note
              currentDuration = 1000 / durations[currentNote];
              currentPause = currentDuration * 1.5;

              // Fire the new note
              tone(BUZZER_PIN, timerMelody[currentNote]); 
              noteStartTime = currentMillis;
              notePlaying = true;
          }
      }
  }
  // --- 4. REACTION LOGIC ---
  if (alarmRinging) {
    if (snoozeFlagAlarm) {
      unsigned long reactionTime = millis() - alarmStartTime;
      alarmRinging = false;
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
      snoozeFlagAlarm = false;
      
    } else if (stopFlagAlarm) {
      unsigned long reactionTime = millis() - alarmStartTime;
      alarmRinging = false;
      noTone(BUZZER_PIN);
      currentNote = 0; // Reset song for next time

      isAlarmEnabled = false;
      stopFlagAlarm = false;
      logAndGetSnoozeTime("STOP", reactionTime);
    }
  }

  if (timerRinging) {
    if (stopFlagTimer) {
      timerRinging = false;
      noTone(BUZZER_PIN);
      currentNote = 0; // Reset song for next time

      isTimerEnabled = false;
      stopFlagTimer = false;
    }
  }

  // Only print once every 2000 milliseconds (2 seconds)
  static unsigned long lastDebugTime = 0;
  if (millis() - lastDebugTime > 2000) {
      Serial.println("\n=== ALARM SYSTEM DEBUG ===");
      
      // 1. Print the Target Time (Formatted with leading zeros)
      Serial.print("Target Alarm Time:   ");
      if (alarmHour < 10) Serial.print("0");
      Serial.print(alarmHour);
      Serial.print(":");
      if (alarmMinute < 10) Serial.print("0");
      Serial.println(alarmMinute);
      
      // 2. Print all the state flags
      // We use the ternary operator (? "TRUE" : "FALSE") so it prints readable words instead of just 1 and 0
      Serial.print("isAlarmEnabled:      "); Serial.println(isAlarmEnabled ? "TRUE" : "FALSE");
      Serial.print("alarmRinging:        "); Serial.println(alarmRinging ? "TRUE" : "FALSE");
      Serial.print("snoozeFlagAlarm:          "); Serial.println(snoozeFlagAlarm ? "TRUE" : "FALSE");
      Serial.print("stopFlagAlarm:            "); Serial.println(stopFlagAlarm ? "TRUE" : "FALSE");
      Serial.print("isTimerEnabled:         "); Serial.println(isTimerEnabled ? "TRUE" : "FALSE");
      Serial.print("timerRinging:            "); Serial.println(timerRinging ? "TRUE" : "FALSE");
      Serial.print("stopFlagTimer:           "); Serial.println(stopFlagTimer ? "TRUE" : "FALSE");
      
      Serial.println("==========================");
      
      // Reset the debug timer
      lastDebugTime = millis();
  }
}

unsigned long logAndGetSnoozeTime(String actionType, unsigned long reactionTime) {
  
  // The Hardware Fallback: Default to 5 minutes (300,000 ms) if anything goes wrong
  unsigned long grantedSnoozeTime_ms = 300000; 

  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    
    // Bypasses SSL certificate verification (Standard for ESP32 testing HTTPS)
    client.setInsecure(); 
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

void updateDisplay(struct tm *timeinfo) {
  u8g2.clearBuffer();

  // 1. ALWAYS DRAW THE CURRENT TIME AT THE TOP
  char timeStr[10]; // Needs to be at least 9 characters to hold HH:MM:SS + null terminator
  
  // Format the real time from the struct passed into the function
  strftime(timeStr, sizeof(timeStr), "%H:%M:%S", timeinfo);
  
  u8g2.setFont(u8g2_font_10x20_tf);
  
  // Shifted X from 34 to 24 to keep the wider string centered on a 128px screen
  u8g2.drawStr(24, 20, timeStr);

  // 2. DRAW THE BOTTOM HALF BASED ON THE STATE
  if (currentState == STATE_CLOCK) {
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(0, 45, "Mode: CLOCK");
    u8g2.drawStr(0, 60, "Press joystick to set alarm...");
  } 
  else if (currentState == STATE_SET_ALARM) {
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(0, 40, "SET ALARM:");

    char alarmStr[10];
    snprintf(alarmStr, sizeof(alarmStr), "%02d:%02d", alarmHour, alarmMinute);
    u8g2.setFont(u8g2_font_10x20_tf);
    u8g2.drawStr(34, 58, alarmStr);

    // Draw the cursor underline!
    // X=34 puts it under the Hour. X=64 skips the colon and puts it under the Minute.
    int underlineX = (alarmSetDigit == 0) ? 34 : 64; 
    u8g2.drawLine(underlineX, 61, underlineX + 18, 61);
  }
  else if (currentState == STATE_SET_TIMER) {
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
          alarmHour = (alarmHour + 1) % 24;
        } else {
          alarmMinute = (alarmMinute + 1) % 60;
        }
      } else if (cmd == "JOY:DOWN") { // DOWN
        if (alarmSetDigit == 0) {
          alarmHour = (alarmHour - 1 < 0) ? 23 : alarmHour - 1;
        } else {
          alarmMinute = (alarmMinute - 1 < 0) ? 59 : alarmMinute - 1;
        }

      } else if (cmd == "JOY:RIGHT") { // PRESS
        alarmSetDigit = 1; 
        Serial.println("Editing Minutes");
      } else if (cmd == "JOY:LEFT") { // PRESS
        alarmSetDigit = 0; 
        Serial.println("Editing Minutes");
      } else if (cmd == "JOY:LONG_PRESS"){
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
      }
      else if (cmd == "JOY:RIGHT"){
        timerSetDigit = (timerSetDigit + 1) % 3;
      }
      else if (cmd == "JOY:LONG_PRESS"){
        isTimerEnabled = true;
        currentState = STATE_CLOCK;
        Serial.println("Timer saved, returning to clock mode");
      }
      else if (cmd == "JOY:PRESS"){
        Serial.println("Cancelled! Going back to clock mode");
        currentState = STATE_CLOCK;
      }
    }
}
