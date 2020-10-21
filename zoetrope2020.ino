/* Project: Zoetrope
 * Author: Damon Partington, Daniel Newton
 * Created: 2020-08-02
 * Updated: 2020-08-17
 * Overview: 
 * Output some basic led patterns
 * Getting used to the FastLED library and playing with strategies to
 * control ~720 leds with LPD8806 SPI
 */

#include <FastLED.h>
#include <Metro.h>

#include "rn4870.h"

//#define DEBUG_STRIP_LOCATION

/********************************** FastLED and Hardware Config *************************************/
#define GLOBAL_BRIGHTNESS    4          // LED brightness (0-255), defines the LED PWM duty cycle
#define NUM_LED_PER_STRIP   30          //
#define NUM_STRIPS_PER_SECTOR 8
#define NUM_SECTORS         3
#define NUM_STRIPS          (NUM_STRIPS_PER_SECTOR*NUM_SECTORS)
#define NUM_LEDS            (NUM_LED_PER_STRIP * NUM_STRIPS)
#define NUM_LOOPS            4
#define NUM_LEDS_PER_LOOP   (NUM_LEDS/NUM_LOOPS)
#define DATA_PIN1           11          // MOSI PIN
#define CLOCK_PIN1          13          // SCK PIN
#define LED_TYPE            LPD8806     // SPI Chipset LPD8806 (Same as 2019 Zoetrope)
#define COLOUR_ORDER        RGB         // Effects Colours (Changed from 2019 Zoetrope)

#define FPS                 24          // Generally 24 for tv and film etc
#define FRAME_INTERVAL      (1000/FPS)  // 41.66ms
#define ILLUMINATION_TIME   1           // 1ms

#define STEPPER_MICROSTEPS      2
#define STEPPER_INTERVAL_START  (1000000/10)   // 10hz          
#define STEPPER_INTERVAL_TARGET (1000000/(1036*STEPPER_MICROSTEPS)) // 1036hz
#define STEPPER_UPDATE_INTERVAL 1000
#define STEPPER_RAMP_FIXED      10
#define STEPPER_RAMP_DAMP       50
#define STEPPER_PULSE           10             // 10us

#define BUTTON_DEBOUNCE_TIME 50 // How long to debounce for

#define STEPPER_PIN_OUT     0
#define BUTTON_PIN_IN       1
#define TIMING_PIN_OUT      5

/**************************************** Definitions ***********************************************/

enum serial_cmd_t {
  CMD_DEBUG_SEGMENT = 0
};

#ifdef DEBUG_STRIP_LOCATION
#else
enum anim_mode_t {
    STOPPED = 0,
    ANIM_MOVING_DOT = 1,
    ANIM_STATIC_RGB = 2,
    ANIM_HUECYCLE = 3,
    ANIM_PALETTESHIFT = 4,
    ANIM_DEBUG_SEGMENT = 10, // only accessable from serial command, not the button
};
#endif

/**************************************** Global Variables ******************************************/

bool led_failsafe = false;  // failsafe to not run if the LEDs would be run too hard
CRGB leds[NUM_LEDS];        // LED Output Buffer
CRGB* loops[NUM_LOOPS];     // pointers to parts of the LED buffer
int stripIndex = 0;
#ifndef DEBUG_STRIP_LOCATION
enum anim_mode_t mode = STOPPED;
#endif
Metro eventAnim = Metro(1000);
Metro eventStepperUpdate = Metro(STEPPER_UPDATE_INTERVAL);
IntervalTimer intStepperOn;
IntervalTimer intStepperOff;
long stepperInterval = STEPPER_INTERVAL_START;
char bleBuffer[100];

volatile unsigned long last_interrupt = 0;  // Millis time of when the button was first registered as being pressed
volatile bool running_debounce = false; // Whether the debounce is being checked (To avoid resetting the start of debounce, if the interrupt repeatedly triggers)

/************************************* Setup + Main + Functions *************************************/

/*
 TODO
 - convert all anim to 1ms per frame, 24 FPS
 - ability to adjust FPS up or down by small increments
 - failsafe in setup function to make sure leds are driven too hard
 - GPIO to control stepper motor (PIN 0 or 1?), simple square wave to control speed
 - use ocilliscope to determine that we can actually clock out ~720 leds worth of data over a
   single SPI bus and check the timings etc (we want to be able to program all the leds probably in much less then 1ms)
 */
 
void setup() {
  // failsafe
  int leds_on_per_1000ms = ILLUMINATION_TIME * FPS;
  if (leds_on_per_1000ms > 100)
    led_failsafe = true;
  if (ILLUMINATION_TIME > 5)
    led_failsafe = true;
  
  // init loops array of indexes into leds buffer
  for (int i=0; i<NUM_LOOPS; i++) {
    loops[i] = leds + i * NUM_LEDS_PER_LOOP;
  }

  // GPIO stepper
  pinMode(STEPPER_PIN_OUT, OUTPUT);
  digitalWrite(STEPPER_PIN_OUT, LOW);
  
  // GPIO button
  pinMode(BUTTON_PIN_IN, INPUT_PULLUP);
  attachInterrupt(BUTTON_PIN_IN, button_pressed, FALLING); 

  // GPIO timing signal
  pinMode(TIMING_PIN_OUT, OUTPUT);
  digitalWrite(TIMING_PIN_OUT, LOW);
  
  // FastLED
  FastLED.addLeds<LED_TYPE, DATA_PIN1, CLOCK_PIN1, COLOUR_ORDER, DATA_RATE_MHZ(18)> (leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(GLOBAL_BRIGHTNESS);

  // events
  setupAnimation();
#ifndef DEBUG_STRIP_LOCATION
  setupStepper();
#endif

  // ble
  //setupBle();
  //Serial3.begin(115200);

  // serial
  Serial.begin(115200);
  Serial.println("Initialised serial");
}

void loop() {
  /*
  // read serial, write to serial3
  if (Serial.available() > 0) {
    uint8_t byte = Serial.read();
    Serial3.write(byte);
    //Serial.write(byte);
  }
  // read serial3, write to serial
  if (Serial3.available() > 0) {
    Serial.write(Serial3.read());
  }
  return;
  */

#ifndef DEBUG_STRIP_LOCATION
  // send data only when you receive data:
  if (Serial.available() > 0) {
    #define CMD_MAX 8
    static byte cmd[CMD_MAX] = {0};
    static int cmdIndex = 0;
    // read the incoming char
    byte in = Serial.read();
    cmd[cmdIndex] = in;
    cmdIndex++;
    if (cmdIndex >= CMD_MAX) {
      cmdIndex = 0;
      switch (cmd[0]) {
        case CMD_DEBUG_SEGMENT:
          stripIndex = cmd[1];
          if (stripIndex >= NUM_STRIPS)
            stripIndex = 0;
          mode = ANIM_DEBUG_SEGMENT;
      }
    }
  }
#endif

  // update stepper
  if (eventStepperUpdate.check() == 1) {
    updateStepper();
  }
  
  // run next animation frame
  if (eventAnim.check() == 1) {
    if (led_failsafe)
      animationCancel();
    else
      animationFrame();
  }

  // change animation mode
  /*
  if (ble_rn4870.hasAnswer()==dataAnswer) {
      Serial.write(ble_rn4870.getLastAnswer());
  }
  */
  if (running_debounce && ((millis() - last_interrupt) > BUTTON_DEBOUNCE_TIME) && !digitalRead(BUTTON_PIN_IN)) {
      running_debounce = false;
      last_interrupt = millis();
#ifdef DEBUG_STRIP_LOCATION
      stripIndex++;
#else
      mode = static_cast<enum anim_mode_t>(static_cast<int>(mode) + 1);
#endif
      setupAnimation();
  }
}

void setupAnimation(void) {
  eventAnim.interval(FRAME_INTERVAL);
  eventAnim.reset();

#ifdef DEBUG_STRIP_LOCATION
  if (stripIndex >= NUM_STRIPS)
    stripIndex = 0;
  Serial.print("STRIP INDEX: ");
  Serial.print(stripIndex);
  Serial.println();
#else
  switch (mode) {
    case STOPPED:
      Serial.println("STOPPED");
      break;
    case ANIM_MOVING_DOT:
      Serial.println("ANIM_MOVING_DOT");
      break;
    case ANIM_STATIC_RGB:
      Serial.println("ANIM_STATIC_RGB");
      break;
    case ANIM_HUECYCLE:
      Serial.println("ANIM_HUECYCLE");
      break;
    case ANIM_PALETTESHIFT:
      Serial.println("ANIM_PALETTESHIFT");
      break;
    default:
      mode = STOPPED;
      setupAnimation();
      break;
  }
#endif
}

void setupStepper(void) {
  intStepperOn.begin(stepperOn, stepperInterval);
}

int stepperIntervalDelta() {
  return STEPPER_RAMP_FIXED + (stepperInterval - STEPPER_INTERVAL_TARGET) / STEPPER_RAMP_DAMP;
}

void updateStepper(void) {
#ifndef DEBUG_STRIP_LOCATION
  long delta = stepperIntervalDelta();
  if (mode == STOPPED) {
    // update stepper interval until we have spooled down to slow
    if (stepperInterval != STEPPER_INTERVAL_START) {
      stepperInterval = stepperInterval + delta;
      if (stepperInterval >= STEPPER_INTERVAL_START - delta) {
        stepperInterval = STEPPER_INTERVAL_START;
        Serial.println("Stepper at slow speed");
      } else {
        Serial.print("Stepper interval at ");
        Serial.print(stepperInterval);
        Serial.println("us");
      }
      intStepperOn.update(stepperInterval);
    }    
  } else {
    // update stepper interval until we have spooled up to full speed
    if (stepperInterval != STEPPER_INTERVAL_TARGET) {
      stepperInterval = stepperInterval - delta;
      if (stepperInterval <= STEPPER_INTERVAL_TARGET + delta) {
        stepperInterval = STEPPER_INTERVAL_TARGET;
        Serial.println("Stepper at full speed");
      } else {
        Serial.print("Stepper interval at ");
        Serial.print(stepperInterval);
        Serial.println("us");
      }
      intStepperOn.update(stepperInterval);
    }
  }
#endif
}

void stepperOn(void) {
#ifndef DEBUG_STRIP_LOCATION
  // dont step if we are in stopped mode and spoolled down
  if (mode == STOPPED && stepperInterval == STEPPER_INTERVAL_START)
    return;
  // start stepper pulse
  intStepperOff.begin(stepperOff, STEPPER_PULSE);
  digitalWrite(STEPPER_PIN_OUT, HIGH);
#endif
}

void stepperOff(void) {
#ifndef DEBUG_STRIP_LOCATION
  // finish stepper pulse
  intStepperOff.end();
  digitalWrite(STEPPER_PIN_OUT, LOW);  
#endif
}

void setupBle(void) {
  delay(1000);
  ble_rn4870.begin(bleBuffer, sizeof(bleBuffer), &Serial3);  
  int count = 0;
  while (count++ < 5) {
    delay(100);
    if (!ble_rn4870.startBLE()) {
      Serial.println("Cannot Start the BLE");
    } else {
      Serial.print("Start the BLE with ");
      const char *mac = ble_rn4870.getAddress();
      for (int i=0; i<5; i++){
          Serial.print(mac[i], HEX);
          Serial.print('-');
      }        
      Serial.print(mac[5], HEX);
      Serial.println(" address");
      break;
    }
  }
}

void animationCancel(void) {
  Serial.println("FAILSAFE!");
  ledsClear();
}

void animationFrame(void) {
#ifdef DEBUG_STRIP_LOCATION
  movingDotDebug(stripIndex);
#else
  digitalWrite(TIMING_PIN_OUT, HIGH);
    switch (mode) {
    case STOPPED:
      break;
    case ANIM_MOVING_DOT:
      movingDot();
      break;
    case ANIM_STATIC_RGB:
      staticRGB();
      break;
    case ANIM_HUECYCLE:
      hueCycle();
      break;
    case ANIM_PALETTESHIFT:
      paletteShift();
      break;
    case ANIM_DEBUG_SEGMENT:
      movingDotDebug(stripIndex);
  }
#endif
  //delay(1);
  ledsClear();

  //digitalWrite(TIMING_PIN_OUT, LOW);
}

void movingDotDebug(int stripIndex) {
  static int stripDot = 0;
  int dot = stripIndex * NUM_LED_PER_STRIP + stripDot;
  // set all black
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  // set dot
  leds[dot] = CRGB(255, 0, 0);
  // increment dot for next time
  stripDot++;
  if (stripDot >= NUM_LED_PER_STRIP)
    stripDot = 0;
  // show leds
  FastLED.show();
}

void movingDot(void) {
  static uint8_t hues[NUM_LOOPS] = {0};
  static int dots[NUM_LOOPS] = {0};
  // init hues and dot indexs
  bool needInit = true;
  for (int i=0; i<NUM_LOOPS; i++)
    if (hues[i] != 0)
      needInit = false;
  if (needInit) {
    for (int i=0; i<NUM_LOOPS; i++) {
      hues[i] = i * 256/NUM_LOOPS;
      dots[i] = i * NUM_LEDS_PER_LOOP/NUM_LOOPS;
    } 
  }
  // set dots to current hue
  for (int i=0; i<NUM_LOOPS; i++) {
    int dot = dots[i];
    loops[i][dot] = CHSV(hues[i]++, 255, 255);
    // update dot index for next time
    dot++;
    if (dot >= NUM_LEDS_PER_LOOP)
      dot = 0;
    dots[i] = dot;
  }
  // show leds
  FastLED.show();
}

void staticRGB(void) {
  for (int i=0; i<NUM_LEDS; i++) {
    CRGB color;
    if (i % 3 == 0)
      color = CRGB(255, 0, 0);
    else if (i % 3 == 1)
      color = CRGB(0, 255, 0);
    else
      color = CRGB(0, 0, 255);
    leds[i] = color;
  }
  FastLED.show();
}

void hueCycle(void) {
  static uint8_t hue = 0;
  FastLED.showColor(CHSV(hue++, 255, 255));
}

void paletteShift(void) {
  static uint8_t offset = 0;
  static CRGBPalette16 red_blue_red = CRGBPalette16(
   CRGB(255, 0, 0),
   CRGB(0, 0, 255),
   CRGB(255, 0, 0)
  );
  CRGBPalette16 palettes[NUM_LOOPS] = {red_blue_red, PartyColors_p, OceanColors_p, LavaColors_p};
  for (int i=0; i<NUM_LOOPS; i++) {
    for (int j=0; j<NUM_LEDS_PER_LOOP; j++) {   
      loops[i][j] = ColorFromPalette(palettes[i], j + offset);
    }
  }
  offset++;
  FastLED.show();
}

void ledsClear(void) {
  // TODO: should just need clear!!
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  //FastLED.clear();
}

void button_pressed(void) {
  if((millis() - last_interrupt) > BUTTON_DEBOUNCE_TIME && !digitalRead(BUTTON_PIN_IN)){
      running_debounce = true;
      last_interrupt = millis(); 
  }
}
