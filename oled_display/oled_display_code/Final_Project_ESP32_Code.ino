/*
 * ESP32 Alarm System Controller
 * Receives commands from FRDM-MCXC444 via UART
 * Handles: Display, Timer countdown, Daily alarm, Time sync
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <U8g2lib.h>
#include <Arduino_JSON.h>
#include "esp_eap_client.h"
#include <time.h>

//#include "pitches.h"

// ==================== CONFIGURATION ====================
#define UART_RX_PIN 17  // Connect to MCX TX (PTE22)
#define UART_TX_PIN 18  // Connect to MCX RX (PTE23)
#define UART_BAUD   9600

// int  melody[] = {
//   NOTE_C4, NOTE_A3, NOTE_A3, NOTE_C4, NOTE_D4, NOTE_G3, NOTE_G3,  NOTE_A3, NOTE_AS3, NOTE_F4, NOTE_F4, NOTE_E4, NOTE_C4, NOTE_D4, NOTE_C4, NOTE_AS3,  NOTE_A3, NOTE_C4, NOTE_D4, NOTE_D4
// }; // declaring the notes of the melody (they  change depending on the song you wanna play)

// int durations[] = {
//   2,  2, 3, 3, 3, 2, 3, 3, 2, 1, 3, 3, 3, 5, 5, 5, 1, 2, 1, 3, 3, 3, 3, 1
// }; // declaring  the duration of each note (4 is a quarter note ecc)

// int songLength = sizeof(melody)/sizeof(melody[0]);  

const int BUZZER_PIN = 5;
const int TOUCH_PIN = 4;

// Global variables to keep track of where we are in the song
unsigned long previousNoteTime = 0;
int currentNote = 0;
bool notePlaying = false; // Tracks if the buzzer is currently making sound
int currentDuration = 0;  // Stores how long the current note should buzz
int currentPause = 0;     // Stores the total time before the next note
unsigned long noteStartTime = 0;

// WiFi for NTP time sync (optional)
const char* WIFI_SSID = "keane";
const char* WIFI_PASS = "keane123";
const char* NTP_SERVER = "pool.ntp.org";
const long  GMT_OFFSET_SEC = 8 * 3600;  // Singapore: UTC+8
const int   DAYLIGHT_OFFSET_SEC = 0;

// ==================== OLED SETUP ====================
// Using SSD1306 128x64 I2C OLED
// NEW (explicitly set I2C pins for ESP32-S2)
#define OLED_SDA  8
#define OLED_SCL  9
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, OLED_SCL, OLED_SDA, U8X8_PIN_NONE);
//U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// ==================== STATE MANAGEMENT ====================
typedef enum {
    STATE_IDLE,
    STATE_SET_TIMER,
    STATE_SET_ALARM,
    STATE_TIMER_RUNNING,
    STATE_ALARM_FIRING
} SystemState_t;

SystemState_t currentState = STATE_IDLE;

// Timer variables
int timerMinutes = 5;           // Default timer: 5 minutes
unsigned long timerEndTime = 0; // millis() when timer expires
bool timerActive = false;

// Daily alarm variables
int alarmHour = 7;
int alarmMinute = 0;
bool alarmEnabled = false;
bool alarmFiredToday = false;
int alarmSetDigit = 0;  // 0=hour tens, 1=hour ones, 2=min tens, 3=min ones

// Snooze
#define SNOOZE_MINUTES 5
unsigned long snoozeEndTime = 0;
bool snoozeActive = false;

// Display update timing
unsigned long lastDisplayUpdate = 0;
#define DISPLAY_UPDATE_INTERVAL 100  // ms

// ==================== FUNCTION DECLARATIONS ====================
void processCommand(String cmd);
void sendToMCX(const char* msg);
void updateDisplay();
void checkTimer();
void checkDailyAlarm();
String getCurrentTimeString();
void syncTimeNTP();

// ==================== SETUP ====================
void setup() {
     // Debug serial
    Serial.begin(115200);
    Serial.print("connecting to ssid");
    Serial.print(WIFI_SSID);

    WiFi.disconnect(true);
    //WiFi.mode(WIFI_STA);

    WiFi.begin(WIFI_SSID, WIFI_PASS);

    // Wifi connection and Initial time syhc block
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.print("\nConnected to Wifi network with IP address: ");
    Serial.print(WiFi.localIP());
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected");
        configTime(28800, 0, "pool.ntp.org");
    } else {
        Serial.println("\nWiFi failed - using default time");
    }

    // UART to MCX board
    Serial1.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
    Serial.println("UART2 initialized");

    // OLED
    Wire.begin(8,9);
    u8g2.begin();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.clearBuffer();
    u8g2.drawStr(10, 30, "Initializing...");
    u8g2.sendBuffer();

    
    Serial.println("Setup complete");
}

// ==================== MAIN LOOP ====================
void loop() {
    currentState = STATE_IDLE;

    updateDisplay();

    delay(500);

    currentState = STATE_SET_TIMER;

    updateDisplay();

    delay(500);

    currentState = STATE_SET_ALARM;

    updateDisplay();

    delay(500);

    currentState = STATE_TIMER_RUNNING;

    updateDisplay();

    currentState = STATE_ALARM_FIRING;

    updateDisplay();

    delay(500);


}
void updateDisplay() {
    u8g2.clearBuffer();
    
    // Current time at top
    struct tm timeinfo;
    char timeStr[20] = "00:00:00";
    if (getLocalTime(&timeinfo)) {
        strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
    }
    u8g2.setFont(u8g2_font_10x20_tf);
    u8g2.drawStr(24, 18, timeStr);
    
    // State-specific content
    u8g2.setFont(u8g2_font_6x10_tf);
    
    switch (currentState) {
        case STATE_IDLE:
            u8g2.drawStr(0, 35, "IDLE");
            if (alarmEnabled) {
                char alarmStr[20];
                snprintf(alarmStr, sizeof(alarmStr), "Alarm: %02d:%02d", alarmHour, alarmMinute);
                u8g2.drawStr(0, 50, alarmStr);
            }
            u8g2.drawStr(0, 62, "L:Timer  R:Alarm");
            break;
            
        case STATE_SET_TIMER:
            u8g2.drawStr(0, 35, "SET TIMER:");
            {
                char timerStr[20];
                snprintf(timerStr, sizeof(timerStr), "%d minutes", timerMinutes);
                u8g2.setFont(u8g2_font_10x20_tf);
                u8g2.drawStr(20, 55, timerStr);
            }
            u8g2.setFont(u8g2_font_6x10_tf);
            u8g2.drawStr(0, 62, "U/D:Adj PRESS:Start");
            break;
            
        case STATE_SET_ALARM:
            u8g2.drawStr(0, 35, "SET ALARM:");
            {
                char alarmStr[10];
                snprintf(alarmStr, sizeof(alarmStr), "%02d:%02d", alarmHour, alarmMinute);
                u8g2.setFont(u8g2_font_10x20_tf);
                u8g2.drawStr(34, 55, alarmStr);
                
                // Underline current digit
                int underlineX = 34 + (alarmSetDigit * 12);
                if (alarmSetDigit >= 2) underlineX += 12;  // Skip colon
                u8g2.drawLine(underlineX, 57, underlineX + 10, 57);
            }
            u8g2.setFont(u8g2_font_6x10_tf);
            u8g2.drawStr(0, 62, "U/D:Adj LONG:Save");
            break;
            
        case STATE_TIMER_RUNNING:
            u8g2.drawStr(0, 35, "TIMER RUNNING:");
            {
                unsigned long remaining = 0;
                if (millis() < timerEndTime) {
                    remaining = (timerEndTime - millis()) / 1000;
                }
                int mins = remaining / 60;
                int secs = remaining % 60;
                char countdownStr[10];
                snprintf(countdownStr, sizeof(countdownStr), "%02d:%02d", mins, secs);
                u8g2.setFont(u8g2_font_10x20_tf);
                u8g2.drawStr(34, 55, countdownStr);
            }
            u8g2.setFont(u8g2_font_6x10_tf);
            u8g2.drawStr(0, 62, "LONG:Cancel");
            break;
            
        case STATE_ALARM_FIRING:
            u8g2.setFont(u8g2_font_10x20_tf);
            // Flashing effect
            if ((millis() / 250) % 2 == 0) {
                u8g2.drawStr(20, 40, "ALARM!");
            }
            u8g2.setFont(u8g2_font_6x10_tf);
            u8g2.drawStr(0, 62, "SW2:Snooze/Stop");
            break;
    }
    
    u8g2.sendBuffer();
}

// // ==================== COMMAND PROCESSING ====================
// void processCommand(String cmd) {
//     // Joystick direction commands
//     if (cmd == "EVT_JOY_LEFT") {
//         if (currentState == STATE_SET_ALARM) {
//             Serial.println("prevstate = set alarm");
//             // Cancel alarm setting
//             currentState = STATE_IDLE;
//             Serial.println("currentstate = idle");
//         } else if (currentState == STATE_IDLE){
//             currentState = STATE_SET_TIMER;
//         }
//     }
//     else if (cmd == "EVT_JOY_RIGHT") {
//         if (currentState == STATE_IDLE) {
//             Serial.println("prevstate = idle");
//             // Move to next digit
//             currentState = STATE_SET_ALARM;
//             Serial.println("currentstate = setalarm");
//         } else if (currentState == STATE_SET_TIMER){
//             Serial.println("prevstate = set timer");
//             currentState == STATE_IDLE;
//             Serial.println("currentstate = idle");
//         }
//     }
//     else if (cmd == "EVT_JOY_UP") {
//         if (currentState == STATE_SET_TIMER) {
//             timerMinutes++;
//             Serial.println("currentstate = settimer");
//             Serial.println("timer count");
//             Serial.print(timerMinutes);
//             if (timerMinutes > 99) timerMinutes = 99;

//         }
//         else if (currentState == STATE_SET_ALARM) {
//             adjustAlarmDigit(1);
//             Serial.println("currentstate = setalarm");
//             Serial.println("alarm + 1");

//         }
//     }
//     else if (cmd == "EVT_JOY_DOWN") {
//         if (currentState == STATE_SET_TIMER) {
//             timerMinutes--;
//             if (timerMinutes < 1) timerMinutes = 1;
//         }
//         else if (currentState == STATE_SET_ALARM) {
//             adjustAlarmDigit(-1);
//         }
//     }
//     else if (cmd == "JOY:PRESS") {
//         if (currentState == STATE_SET_ALARM) {
//             // Confirm current digit, move to next
//             alarmSetDigit = (alarmSetDigit + 1) % 4;
//         }
//     }
//     else if (cmd == "JOY:LONG_PRESS") {
//         // Handled by MCX FSM
//     }
    
//     // State change commands from MCX FSM
//     else if (cmd == "TIMER_START") {
//         currentState = STATE_TIMER_RUNNING;
//         timerEndTime = millis() + (timerMinutes * 60 * 1000UL);
//         timerActive = true;
//         Serial.printf("Timer started: %d minutes\n", timerMinutes);
//     }
//     else if (cmd == "TIMER_CANCEL") {
//         currentState = STATE_IDLE;
//         timerActive = false;
//         Serial.println("Timer cancelled");
//     }
//     else if (cmd == "ALARM_SET") {
//         alarmEnabled = true;
//         alarmFiredToday = false;
//         currentState = STATE_IDLE;
//         Serial.printf("Alarm set for %02d:%02d\n", alarmHour, alarmMinute);
//     }
//     else if (cmd == "ALARM_CANCEL") {
//         currentState = STATE_IDLE;
//         alarmSetDigit = 0;
//         Serial.println("Alarm setting cancelled");
//     }
//     else if (cmd == "ALARM_STOP") {
//         currentState = STATE_IDLE;
//         snoozeActive = false;
//         Serial.println("Alarm stopped");
//     }
    
//     // SW2 press - snooze if alarm firing
//     else if (cmd.startsWith("SW2:")) {
//         if (currentState == STATE_ALARM_FIRING) {
//             // Snooze for 5 minutes
//             snoozeActive = true;
//             snoozeEndTime = millis() + (SNOOZE_MINUTES * 60 * 1000UL);
//             currentState = STATE_IDLE;
//             sendToMCX("SNOOZE\n");
//             Serial.printf("Snooze activated: %d minutes\n", SNOOZE_MINUTES);
//         }
//     }
    
//     // MCX requesting time sync
//     else if (cmd.startsWith("GET_TIME")) {
//         struct tm timeinfo;
//         if (getLocalTime(&timeinfo)) {
//             char timeStr[30];
//             strftime(timeStr, sizeof(timeStr), "TIME:%Y-%m-%d %H:%M:%S\n", &timeinfo);
//             sendToMCX(timeStr);
//             Serial.print("Sent time: ");
//             Serial.println(timeStr);
//         } else {
//             // Send default time if NTP failed
//             sendToMCX("TIME:2025-01-01 12:00:00\n");
//         }
//     }
    
//     // State indicators from MCX (for display sync)
//     else if (cmd.startsWith("LEFT, CHANGING TO STATE_SET_TIMER")) {
//         currentState = STATE_SET_TIMER;
//     }
//     else if (cmd.startsWith("RIGHT_CHANGING TO STATE SET ALARM")) {
//         currentState = STATE_SET_ALARM;
//         alarmSetDigit = 0;
//     }
// }

// // ==================== ALARM DIGIT ADJUSTMENT ====================
// void adjustAlarmDigit(int delta) {
//     switch (alarmSetDigit) {
//         case 0:  // Hour tens (0-2)
//             alarmHour += delta * 10;
//             if (alarmHour < 0) alarmHour = 20;
//             if (alarmHour > 23) alarmHour = alarmHour % 10;
//             break;
//         case 1:  // Hour ones (0-9, but max 23)
//             alarmHour += delta;
//             if (alarmHour < 0) alarmHour = 23;
//             if (alarmHour > 23) alarmHour = 0;
//             break;
//         case 2:  // Minute tens (0-5)
//             alarmMinute += delta * 10;
//             if (alarmMinute < 0) alarmMinute = 50;
//             if (alarmMinute > 59) alarmMinute = alarmMinute % 10;
//             break;
//         case 3:  // Minute ones (0-9)
//             alarmMinute += delta;
//             if (alarmMinute < 0) alarmMinute = 59;
//             if (alarmMinute > 59) alarmMinute = 0;
//             break;
//     }
// }

// // ==================== TIMER CHECK ====================
// void checkTimer() {
//     if (timerActive && millis() >= timerEndTime) {
//         timerActive = false;
//         currentState = STATE_ALARM_FIRING;
//         sendToMCX("TIMER_DONE\n");
//         Serial.println("Timer expired!");
//     }
// }

// // ==================== DAILY ALARM CHECK ====================
// void checkDailyAlarm() {
//     if (!alarmEnabled || alarmFiredToday) return;
    
//     // Check snooze
//     if (snoozeActive) {
//         if (millis() >= snoozeEndTime) {
//             snoozeActive = false;
//             currentState = STATE_ALARM_FIRING;
//             sendToMCX("ALARM_FIRING\n");
//             Serial.println("Snooze ended - alarm firing again");
//         }
//         return;
//     }
    
//     struct tm timeinfo;
//     if (!getLocalTime(&timeinfo)) return;
    
//     if (timeinfo.tm_hour == alarmHour && timeinfo.tm_min == alarmMinute) {
//         if (currentState != STATE_ALARM_FIRING) {
//             currentState = STATE_ALARM_FIRING;
//             alarmFiredToday = true;
//             sendToMCX("ALARM_FIRING\n");
//             Serial.println("Daily alarm firing!");
//         }
//     }
    
//     // Reset alarmFiredToday at midnight
//     if (timeinfo.tm_hour == 0 && timeinfo.tm_min == 0) {
//         alarmFiredToday = false;
//     }
// }

// // ==================== DISPLAY UPDATE ====================


// // ==================== UART SEND ====================
// void sendToMCX(const char* msg) {
//     Serial1.print(msg);
//     Serial.print("[ESP->MCX] ");
//     Serial.print(msg);
// }

// // ==================== NTP TIME SYNC ====================
// void syncTimeNTP() {
//     configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    
//     struct tm timeinfo;
//     if (getLocalTime(&timeinfo, 5000)) {
//         Serial.println("NTP time synced:");
//         Serial.println(&timeinfo, "  %Y-%m-%d %H:%M:%S");
//     } else {
//         Serial.println("Failed to get NTP time");
//     }
// }

// // ==================== GET CURRENT TIME STRING ====================
// String getCurrentTimeString() {
//     struct tm timeinfo;
//     if (!getLocalTime(&timeinfo)) {
//         return "2025-01-01 12:00:00";
//     }
//     char buf[30];
//     strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
//     return String(buf);
// }
// // database settings
// void sendToAzure(String actionType, unsigned long reactionTime) {
//   if (WiFi.status() == WL_CONNECTED) {
//     WiFiClient client;
//     //client.setInsecure();
//     HTTPClient http;
    
//     // Replace this with your actual Azure Function URL
//     const char* serverName = "https://alarmdata-dyfrdfc7fpaphbgs.malaysiawest-01.azurewebsites.net/api/log_alarm";
    
//     http.begin(client, serverName);
    
//     // Crucial: Tell Azure we are sending JSON data
//     http.addHeader("Content-Type", "application/json");
    
//     // Construct the JSON payload string exactly how your Python script expects it
//     String jsonPayload = "{\"ActionType\":\"" + actionType + "\",\"ReactionTime_ms\":" + String(reactionTime) + "}";
    
//     Serial.print("Sending to Azure: ");
//     Serial.println(jsonPayload);
    
//     // Fire the POST request and capture the server's response code
//     int httpResponseCode = http.POST(jsonPayload);
    
//     if (httpResponseCode > 0) {
//       Serial.print("Azure Response Code: ");
//       Serial.println(httpResponseCode);
//       String response = http.getString(); // Get the "Reaction logged successfully" message
//       Serial.println(response);
//     } else {
//       Serial.print("Error sending POST: ");
//       Serial.println(httpResponseCode);
//     }
    
//     // Free resources
//     http.end();
//   } else {
//     Serial.println("Error: WiFi Disconnected");
//   }
// }