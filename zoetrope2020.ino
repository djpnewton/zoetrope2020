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

/********************************** FastLED and Hardware Config *************************************/
#define GLOBAL_BRIGHTNESS    4          // LED brightness (0-255), defines the LED PWM duty cycle
#define NUM_LED_PER_STRIP   20          // Max LED Circuits under test (New boards have 20 per metre)
#define NUM_STRIPS          04
#define NUM_LEDS            (NUM_LED_PER_STRIP * NUM_STRIPS)
#define NUM_LOOPS            4
#define NUM_LEDS_PER_LOOP   (NUM_LEDS/NUM_LOOPS)
#define DATA_PIN1           11          // MOSI PIN
#define CLOCK_PIN1          13          // SCK PIN
#define DATA_PIN2           26          // MOSI PIN
#define CLOCK_PIN2          27          // SCK PIN
//#define DATA_PIN3           50          // MOSI PIN
//#define CLOCK_PIN3          49          // SCK PIN
#define LED_TYPE            LPD8806     // SPI Chipset LPD8806 (Same as 2019 Zoetrope)
#define COLOUR_ORDER        RGB         // Effects Colours (Changed from 2019 Zoetrope)

#define FPS                 24          // Generally 24 for tv and film etc
#define FRAME_INTERVAL      (1000/FPS)  // 41.66ms
#define ILLUMINATION_TIME   1           // 1ms

#define BUTTON_PIN_IN       2
#define BUTTON_PIN_OUT      3
#define TIMING_PIN_OUT      5
/**************************************** Definitions ***********************************************/
enum anim_mode_t {
    ANIM_MOVING_DOT = 0,
    ANIM_STATIC_RGB = 1,
    ANIM_HUECYCLE = 2,
    ANIM_PALETTESHIFT = 3
};

/**************************************** Global Variables ******************************************/
bool led_failsafe = false;  // failsafe to not run if the LEDs would be run too hard
CRGB leds[NUM_LEDS];        // LED Output Buffer
CRGB* loops[NUM_LOOPS];     // pointers to parts of the LED buffer
int debounce = 0;
enum anim_mode_t mode = ANIM_MOVING_DOT;
Metro eventMetro = Metro(1000);

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
  
  // GPIO button
  pinMode(BUTTON_PIN_IN, INPUT_PULLUP);
  pinMode(BUTTON_PIN_OUT, OUTPUT);
  digitalWrite(BUTTON_PIN_OUT, LOW);

  // GPIO timing signal
  pinMode(TIMING_PIN_OUT, OUTPUT);
  digitalWrite(TIMING_PIN_OUT, LOW);
  
  // FastLED
  FastLED.addLeds<LED_TYPE, DATA_PIN1, CLOCK_PIN1, COLOUR_ORDER> (leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  //FastLED.addLeds<LED_TYPE, DATA_PIN1, CLOCK_PIN1, COLOUR_ORDER> (leds, NUM_LED_PER_STRIP).setCorrection(TypicalLEDStrip);
  //FastLED.addLeds<LED_TYPE, DATA_PIN2, CLOCK_PIN2, COLOUR_ORDER> (leds + NUM_LED_PER_STRIP, NUM_LED_PER_STRIP * 2).setCorrection(TypicalLEDStrip);
  //FastLED.addLeds<LED_TYPE, DATA_PIN3, CLOCK_PIN3, COLOUR_ORDER> (ledstrip3, NUM_LED_PER_STRIP).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(GLOBAL_BRIGHTNESS);

  // events
  setupAnimation();

  // serial
  Serial.begin(115200);
  Serial.println("Started");
}

void loop() {
  // run next animation frame
  if (eventMetro.check() == 1) {
    if (led_failsafe)
      animationCancel();
    else
      animationFrame();
  }

  // change animation mode
  if (digitalRead(BUTTON_PIN_IN) == LOW) {
    debounce++;
    if (debounce > 100) {
      delay(100);
      mode = static_cast<enum anim_mode_t>(static_cast<int>(mode) + 1);
      setupAnimation();
      debounce = 0;
    }
  } else
    debounce = 0;
}

void setupAnimation(void) {
  eventMetro.interval(FRAME_INTERVAL);
  eventMetro.reset();
  
  switch (mode) {
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
      mode = ANIM_MOVING_DOT;
      setupAnimation();
      break;
  }
}

void animationCancel(void) {
  Serial.println("FAILSAFE!");
  ledsClear();
}

void animationFrame(void) {
  digitalWrite(TIMING_PIN_OUT, HIGH);
  
  switch (mode) {
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
  }

  digitalWrite(TIMING_PIN_OUT, LOW);
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
  delay(1);
  ledsClear();
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
  delay(1);
  ledsClear();
}

void hueCycle(void) {
  static uint8_t hue = 0;
  FastLED.showColor(CHSV(hue++, 255, 255));
  delay(1);
  ledsClear();
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
  delay(1);
  ledsClear();
}

void ledsClear(void) {
  // TODO: should just need clear!!
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  //FastLED.clear();
}
