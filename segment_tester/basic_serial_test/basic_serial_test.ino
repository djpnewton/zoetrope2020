void setup() {
  // put your setup code here, to run once:
  Serial.begin(19200);
}
byte command_read[8];
void loop() {
  // put your main code here, to run repeatedly:
  if (Serial.available() > 0){
    Serial.readBytes(command_read, 8);//Serial.readStringUntil('\n');
    Serial.write(command_read, 8);
  }
}
