#include  "pitches.h" // including the library with the frequencies of the note 

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
int alarmHour = 7;
int alarmMinute = 30;

//Master Switch for alarm
bool isAlarmEnabled = false;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);

  pinMode(TOUCH_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  alarmRinging = true;
  
}

void loop() {
  // put your main code here, to run repeatedly:
  //buzzer section
    // 1. Read the digitalPin (1 for touched)
  int TouchValue = digitalRead(TOUCH_PIN);
  if (TouchValue == 1) {
      snoozeFlag = true;
  }

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

  if (getLocalTime(&timeinfo)) {
    if (isAlarmEnabled) {
        
        // Check if the current clock match your alarm time
        if (timeinfo.tm_hour == alarmHour && timeinfo.tm_min == alarmMinute) {
            
            if (!alarmRinging) {
                Serial.println("ALARM TRIGGERED! WAKE UP!");
                
                alarmRinging = true;
                alarmSetOff = true;
                alarmStartTime = millis();    
            }
        } 
    }
  }

  if (alarmRinging) {
    if (snoozeFlag) {
      unsigned long reactionTime = millis() - alarmStartTime;
      sendToAzure("Snooze", reactionTime);

      alarmRinging = false;
      noTone(BUZZER_PIN);
      currentNote = 0; // Reset song for next time

      alarmMinute += 5;
      if (alarmMinute >= 60) {
          alarmMinute -= 60;
          alarmHour += 1;
          if (alarmHour >= 24) alarmHour = 0; 
      }
      snoozeFlag = false;

    } else if (stopFlag) {
      unsigned long reactionTime = millis() - alarmStartTime;
      sendToAzure("Stop", reactionTime);

      alarmRinging = false;
      noTone(BUZZER_PIN);
      currentNote = 0; // Reset song for next time

      isAlarmEnabled = false;
      stopFlag = false;
    }
  }



  // 2. Print both values to the Serial Monitor
  Serial.print("   |   Digital Switch State: ");
  Serial.println(TouchValue);

}
