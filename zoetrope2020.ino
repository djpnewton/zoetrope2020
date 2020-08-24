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
#define GLOBAL_BRIGHTNESS   64          // Global Brightness (Must be 255 for waveform tests)
#define NUM_LED_PER_STRIP   36          // Max LED Circuits under test (New boards have 20 per metre)
#define NUM_STRIPS           3
#define NUM_LEDS            (NUM_LED_PER_STRIP * NUM_STRIPS)
#define DATA_PIN1           11          // MOSI PIN
#define CLOCK_PIN1          13          // SCK PIN
#define DATA_PIN2           26          // MOSI PIN
#define CLOCK_PIN2          27          // SCK PIN
//#define DATA_PIN3           50          // MOSI PIN
//#define CLOCK_PIN3          49          // SCK PIN
#define LED_TYPE            LPD8806     // SPI Chipset LPD8806 (Same as 2019 Zoetrope)
#define COLOUR_ORDER        BRG         // Effects Colours (Changed from 2019 Zoetrope)

/**************************************** Definitions ***********************************************/
enum anim_mode_t {
    ANIM_MOVING_DOT = 0,
    ANIM_HUECYCLE = 1,
    ANIM_PALETTESHIFT = 2
};

/**************************************** Global Variables ******************************************/
CRGB leds[NUM_LEDS]; // LED Output Buffer
int debounce = 0;
enum anim_mode_t mode = ANIM_MOVING_DOT;
Metro eventMetro = Metro(1000);

/************************************* Setup + Main + Functions *************************************/
void setup() {
  // GPIO
  pinMode(3, INPUT_PULLUP);
  pinMode(2, OUTPUT);
  digitalWrite(2, LOW);

  // FastLED
  FastLED.addLeds<LED_TYPE, DATA_PIN1, CLOCK_PIN1, COLOUR_ORDER> (leds, NUM_LED_PER_STRIP).setCorrection(TypicalLEDStrip);
  FastLED.addLeds<LED_TYPE, DATA_PIN2, CLOCK_PIN2, COLOUR_ORDER> (leds + NUM_LED_PER_STRIP, NUM_LED_PER_STRIP * 2).setCorrection(TypicalLEDStrip);
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
  if (eventMetro.check() == 1)
    animationFrame();

  // change animation mode
  if (digitalRead(3) == LOW) {
    debounce++;
    if (debounce > 100) {
      ledsRed();
      delay(100);
      ledsClear();
      mode = static_cast<enum anim_mode_t>(static_cast<int>(mode) + 1);
      setupAnimation();
      debounce = 0;
    }
  } else
    debounce = 0;
}

void setupAnimation(void) {
  switch (mode) {
    case ANIM_MOVING_DOT:
      Serial.println("ANIM_MOVING_DOT");
      eventMetro.interval(30);
      eventMetro.reset();
      break;
    case ANIM_HUECYCLE:
      Serial.println("ANIM_HUECYCLE");
      eventMetro.interval(10);
      eventMetro.reset();
      break;
    case ANIM_PALETTESHIFT:
      Serial.println("ANIM_PALETTESHIFT");
      eventMetro.interval(10);
      eventMetro.reset();
      break;
    default:
      mode = ANIM_MOVING_DOT;
      setupAnimation();
      break;
  }
}

void animationFrame(void) {
  switch (mode) {
    case ANIM_MOVING_DOT:
      movingDot();
      break;
    case ANIM_HUECYCLE:
      hueCycle();
      break;
    case ANIM_PALETTESHIFT:
      paletteShift();
      break;
  }
}

void hueCycle(void) {
  static uint8_t hue = 0;
  FastLED.showColor(CHSV(hue++, 255, 255)); 
}

void movingDot(void) {
  static uint8_t hue = 0;
  static int dot = 0;
  leds[dot] = CHSV(hue++, 255, 255);
  FastLED.show();
  // clear this led for the next time around the loop
  leds[dot] = CRGB::Black;
  dot++;
  if (dot >= NUM_LED_PER_STRIP * NUM_STRIPS)
    dot = 0;
}

void paletteShift(void) {
  static uint8_t offset = 0;
  static CRGBPalette16 red_blue_red = CRGBPalette16(
   CRGB(255, 0, 0),
   CRGB(0, 0, 255),
   CRGB(255, 0, 0)
);
  for (int i=0; i<NUM_LEDS; i++) {   
      leds[i] = ColorFromPalette(red_blue_red, i + offset);
  }
  FastLED.show();
  offset++;
}

void ledsRed(void) {
  fill_solid(leds, NUM_LEDS, CRGB::Red);
  FastLED.show();
}

void ledsClear(void) {
  FastLED.clear();
}
