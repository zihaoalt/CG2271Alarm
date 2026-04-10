const int NEW_TX_PIN = 1;
const int NEW_RX_PIN = 2;
int TRIG_PIN = 13;
int ECHO_PIN = 12;

float SPEED_OF_SOUND = 0.0345;

void setup() {
  // put your setup code here, to run once:
  pinMode(TRIG_PIN, OUTPUT);
  digitalWrite(TRIG_PIN, LOW);
  pinMode(ECHO_PIN, INPUT);
  Serial.begin(9600);
  Serial1.begin(9600, SERIAL_8N1, NEW_RX_PIN, NEW_TX_PIN);
  delay(1000);
  Serial.print("UART DEMO\n\n");

}

void loop() {
  Serial1.println("Hello this is ESP32");

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  int microsecs = pulseIn(ECHO_PIN, HIGH);
  float cms = microsecs*SPEED_OF_SOUND/2;
  
  if (cms <= 0 || cms > 10) {
    Serial1.println("0");
  } else {
    Serial1.println("1");
  }

  Serial.print("Sent distance: ");
  Serial.println(cms);
  delay(1000);
}

