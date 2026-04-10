#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Arduino_JSON.h>
#include "esp_eap_client.h"

#include  "pitches.h"


const char* ssid = "iPhone (6)";
const char* password = "12345678";

String jsonBuffer;

int  melody[] = {
  NOTE_C4, NOTE_A3, NOTE_A3, NOTE_C4, NOTE_D4, NOTE_G3, NOTE_G3,  NOTE_A3, NOTE_AS3, NOTE_F4, NOTE_F4, NOTE_E4, NOTE_C4, NOTE_D4, NOTE_C4, NOTE_AS3,  NOTE_A3, NOTE_C4, NOTE_D4, NOTE_D4
}; // declaring the notes of the melody (they  change depending on the song you wanna play)

int durations[] = {
  2,  2, 3, 3, 3, 2, 3, 3, 2, 1, 3, 3, 3, 5, 5, 5, 1, 2, 1, 3, 3, 3, 3, 1
}; // declaring  the duration of each note (4 is a quarter note ecc)


int songLength = sizeof(melody)/sizeof(melody[0]);  // defining the song length, in this case it is equal to the length of the melody

const int BUZZER_PIN = 5;
const int TOUCH_PIN = 4;

// Global variables to keep track of where we are in the song
unsigned long previousNoteTime = 0;
int currentNote = 0;
bool notePlaying = false; // Tracks if the buzzer is currently making sound
int currentDuration = 0;  // Stores how long the current note should buzz
int currentPause = 0;     // Stores the total time before the next note
unsigned long noteStartTime = 0;

bool alarmRinging = false;
unsigned long alarmStartTime = 0;
bool stopFlag = false;
bool snoozeFlag = false;

//Gloabal variables for alarm
int alarmHour = 17;
int alarmMinute = 52;

//Master Switch for alarm
bool isAlarmEnabled = true;


void setup() {
  Serial.begin(115200);
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

}

void loop() {
  
  // --- 1. SENSOR READING ---
  int TouchValue = digitalRead(TOUCH_PIN);
  if (TouchValue && alarmRinging) {
      snoozeFlag = true;
  }

  if (Serial.available() > 0 && alarmRinging) {
    char key = Serial.read();
    if (key == 's' || key == 'S') {
        stopFlag = true;
        Serial.println("Stop command received via Serial.");
    }
  }

  // (Read MCXC444 UART messages here)

  // --- 2. TIME & TRIGGER LOGIC ---
  struct tm timeinfo;
  
  // We can call this as fast as possible because it just reads internal memory!
  if (getLocalTime(&timeinfo)) { 
    
    // Optional: Print the time every 1 second (so it doesn't spam your monitor)
    static unsigned long lastPrintTime = 0;
    if (millis() - lastPrintTime > 10000) {
        char timeString[9];
        strftime(timeString, sizeof(timeString), "%H:%M:%S", &timeinfo);
        Serial.println(timeString);
        lastPrintTime = millis();
    }

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

  // --- 4. REACTION LOGIC ---
  if (alarmRinging) {
    if (snoozeFlag) {
      unsigned long reactionTime = millis() - alarmStartTime;
      alarmRinging = false;
      noTone(BUZZER_PIN);
      currentNote = 0; // Reset song for next time

      alarmMinute = timeinfo.tm_min + 5;
      alarmHour = timeinfo.tm_hour;
      if (alarmMinute >= 60) {
          alarmMinute -= 60;
          alarmHour += 1;
          if (alarmHour >= 24) alarmHour = 0;
      }
      snoozeFlag = false;
      sendToAzure("Snooze", reactionTime);
    } else if (stopFlag) {
      unsigned long reactionTime = millis() - alarmStartTime;
      alarmRinging = false;
      noTone(BUZZER_PIN);
      currentNote = 0; // Reset song for next time

      isAlarmEnabled = false;
      stopFlag = false;
      sendToAzure("Stop", reactionTime);
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
      Serial.print("snoozeFlag:          "); Serial.println(snoozeFlag ? "TRUE" : "FALSE");
      Serial.print("stopFlag:            "); Serial.println(stopFlag ? "TRUE" : "FALSE");
      
      Serial.println("==========================");
      
      // Reset the debug timer
      lastDebugTime = millis();
  }
}

void sendToAzure(String actionType, unsigned long reactionTime) {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    
    // Replace this with your actual Azure Function URL
    const char* serverName = "https://alarmdata-dyfrdfc7fpaphbgs.malaysiawest-01.azurewebsites.net/api/log_alarm";
    
    http.begin(client, serverName);
    
    // Crucial: Tell Azure we are sending JSON data
    http.addHeader("Content-Type", "application/json");
    
    // Construct the JSON payload string exactly how your Python script expects it
    String jsonPayload = "{\"ActionType\":\"" + actionType + "\",\"ReactionTime_ms\":" + String(reactionTime) + "}";
    
    Serial.print("Sending to Azure: ");
    Serial.println(jsonPayload);
    
    // Fire the POST request and capture the server's response code
    int httpResponseCode = http.POST(jsonPayload);
    
    if (httpResponseCode > 0) {
      Serial.print("Azure Response Code: ");
      Serial.println(httpResponseCode);
      String response = http.getString(); // Get the "Reaction logged successfully" message
      Serial.println(response);
    } else {
      Serial.print("Error sending POST: ");
      Serial.println(httpResponseCode);
    }
    
    // Free resources
    http.end();
  } else {
    Serial.println("Error: WiFi Disconnected");
  }
}
