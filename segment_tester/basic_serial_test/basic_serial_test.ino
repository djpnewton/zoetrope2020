void setup() {
  // put your setup code here, to run once:
  Serial.begin(19200);
}
byte command_read[8];
const byte serial_preamble[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
void loop() {
  // put your main code here, to run repeatedly:
  if (Serial.available() > 0){
    Serial.readBytes(command_read, 8);//Serial.readStringUntil('\n');
    Serial.write(command_read, 8);
  }
  send_plaintext("Hello World!");
  delay(1000);
}

void send_plaintext(String message){
  Serial.write(serial_preamble, 8);
  String to_send = message + '\n';
  Serial.print(to_send);
}
