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

// TODO: Test without the stepper
#define STEPPER
#define EXTENSION_STRIP
//#define DEBUG_STRIP_LOCATION

/********************************** FastLED and Hardware Config *************************************/
#define GLOBAL_BRIGHTNESS   255         // LED brightness (0-255), defines the LED PWM duty cycle
#define NUM_LED_PER_STRIP   30          //
#define NUM_STRIPS_PER_SECTOR 8
#define NUM_SECTORS         3
#define NUM_STRIPS          (NUM_STRIPS_PER_SECTOR*NUM_SECTORS)
#define NUM_LEDS            (NUM_LED_PER_STRIP * NUM_STRIPS)
#define NUM_LOOPS            4
#define NUM_LEDS_PER_LOOP   (NUM_LEDS/NUM_LOOPS)
#define NUM_LEDS_X          (NUM_LEDS_PER_LOOP/2)
#define NUM_LEDS_Y          (NUM_LOOPS*2)
#define DATA_PIN1           11          // MOSI PIN
#define CLOCK_PIN1          13          // SCK PIN
#define LED_TYPE            LPD8806     // SPI Chipset LPD8806 (Same as 2019 Zoetrope)
#define LED_MHZ             10
#define COLOUR_ORDER        RGB         // Effects Colours (Changed from 2019 Zoetrope)

#define FPS                 (14.5)  // Generally 24 for tv and film etc
#define ILLUMINATION_TIME   1           // 1ms

#define STEPPER_STEPS_PER_REV   ((1036*2)/3)
#define STEPPER_MICROSTEPS      2
#define STEPPER_INTERVAL_START  (1000000/10)   // 10hz
#define STEPPER_INTERVAL_TARGET (1000000/(STEPPER_STEPS_PER_REV*STEPPER_MICROSTEPS)) // 1036hz, 1 RPS
#define STEPPER_UPDATE_INTERVAL 480
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
    ANIM_STATIC_LOOPS = 1,
    ANIM_MOVING_DOT = 2,
    ANIM_FILL_LOOPS = 3,
    ANIM_STATIC_RGB = 4,
    ANIM_HUECYCLE = 5,
    ANIM_PALETTESHIFT = 6,
    ANIM_BUBBLES = 7,
    ANIM_ROLLING_LOOP = 8,
    ANIM_DEBUG_SEGMENT = 10, // only accessable from serial command, not the button
};
#endif

/**************************************** Global Variables ******************************************/

bool led_failsafe = false;                 // failsafe to not run if the LEDs would be run too hard
CRGB leds[NUM_LEDS + NUM_LED_PER_STRIP];   // LED Output Buffer
int stripIndex = 0;
#ifndef DEBUG_STRIP_LOCATION
enum anim_mode_t mode = ANIM_STATIC_LOOPS;
#endif
float fps = FPS;
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
#ifdef EXTENSION_STRIP
  FastLED.addLeds<LED_TYPE, DATA_PIN1, CLOCK_PIN1, COLOUR_ORDER, DATA_RATE_MHZ(LED_MHZ)> (leds, NUM_LEDS + NUM_LED_PER_STRIP).setCorrection(TypicalLEDStrip);
#else
  FastLED.addLeds<LED_TYPE, DATA_PIN1, CLOCK_PIN1, COLOUR_ORDER, DATA_RATE_MHZ(LED_MHZ)> (leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
#endif
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

// |---------------------------------------|
// | Code to address LEDS in a matrix form |
// |---------------------------------------|

// Params for width and height
const uint8_t kMatrixWidth = (NUM_LEDS/NUM_LOOPS);
const uint8_t kMatrixHeight = NUM_LOOPS;
#ifdef EXTENSION_STRIP
const uint16_t XYTable[] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 569, 568, 567, 566, 565, 564, 563, 562, 561, 560, 559, 558, 557, 556, 555, 554, 553, 552, 551, 550, 549, 548, 547, 546, 545, 544, 543, 542, 541, 540, 330, 331, 332, 333, 334, 335, 336, 337, 338, 339, 340, 341, 342, 343, 344, 345, 346, 347, 348, 349, 350, 351, 352, 353, 354, 355, 356, 357, 358, 359, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 689, 688, 687, 686, 685, 684, 683, 682, 681, 680, 679, 678, 677, 676, 675, 674, 673, 672, 671, 670, 669, 668, 667, 666, 665, 664, 663, 662, 661, 660, 450, 451, 452, 453, 454, 455, 456, 457, 458, 459, 460, 461, 462, 463, 464, 465, 466, 467, 468, 469, 470, 471, 472, 473, 474, 475, 476, 477, 478, 479, 
  59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 510, 511, 512, 513, 514, 515, 516, 517, 518, 519, 520, 521, 522, 523, 524, 525, 526, 527, 528, 529, 530, 531, 532, 533, 534, 535, 536, 537, 538, 539, 329, 328, 327, 326, 325, 324, 323, 322, 321, 320, 319, 318, 317, 316, 315, 314, 313, 312, 311, 310, 309, 308, 307, 306, 305, 304, 303, 302, 301, 300, 179, 178, 177, 176, 175, 174, 173, 172, 171, 170, 169, 168, 167, 166, 165, 164, 163, 162, 161, 160, 159, 158, 157, 156, 155, 154, 153, 152, 151, 150, 630, 631, 632, 633, 634, 635, 636, 637, 638, 639, 640, 641, 642, 643, 644, 645, 646, 647, 648, 649, 650, 651, 652, 653, 654, 655, 656, 657, 658, 659, 449, 448, 447, 446, 445, 444, 443, 442, 441, 440, 439, 438, 437, 436, 435, 434, 433, 432, 431, 430, 429, 428, 427, 426, 425, 424, 423, 422, 421, 420, 
  60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 749, 748, 747, 746, 745, 744, 743, 742, 741, 740, 739, 738, 737, 736, 735, 734, 733, 732, 731, 730, 729, 728, 727, 726, 725, 724, 723, 722, 721, 720, 270, 271, 272, 273, 274, 275, 276, 277, 278, 279, 280, 281, 282, 283, 284, 285, 286, 287, 288, 289, 290, 291, 292, 293, 294, 295, 296, 297, 298, 299, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 629, 628, 627, 626, 625, 624, 623, 622, 621, 620, 619, 618, 617, 616, 615, 614, 613, 612, 611, 610, 609, 608, 607, 606, 605, 604, 603, 602, 601, 600, 390, 391, 392, 393, 394, 395, 396, 397, 398, 399, 400, 401, 402, 403, 404, 405, 406, 407, 408, 409, 410, 411, 412, 413, 414, 415, 416, 417, 418, 419, 
  119, 118, 117, 116, 115, 114, 113, 112, 111, 110, 109, 108, 107, 106, 105, 104, 103, 102, 101, 100, 99, 98, 97, 96, 95, 94, 93, 92, 91, 90, 690, 691, 692, 693, 694, 695, 696, 697, 698, 699, 700, 701, 702, 703, 704, 705, 706, 707, 708, 709, 710, 711, 712, 713, 714, 715, 716, 717, 718, 719, 269, 268, 267, 266, 265, 264, 263, 262, 261, 260, 259, 258, 257, 256, 255, 254, 253, 252, 251, 250, 249, 248, 247, 246, 245, 244, 243, 242, 241, 240, 239, 238, 237, 236, 235, 234, 233, 232, 231, 230, 229, 228, 227, 226, 225, 224, 223, 222, 221, 220, 219, 218, 217, 216, 215, 214, 213, 212, 211, 210, 570, 571, 572, 573, 574, 575, 576, 577, 578, 579, 580, 581, 582, 583, 584, 585, 586, 587, 588, 589, 590, 591, 592, 593, 594, 595, 596, 597, 598, 599, 389, 388, 387, 386, 385, 384, 383, 382, 381, 380, 379, 378, 377, 376, 375, 374, 373, 372, 371, 370, 369, 368, 367, 366, 365, 364, 363, 362, 361, 360,
};
#else
const uint16_t XYTable[] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 539, 538, 537, 536, 535, 534, 533, 532, 531, 530, 529, 528, 527, 526, 525, 524, 523, 522, 521, 520, 519, 518, 517, 516, 515, 514, 513, 512, 511, 510, 330, 331, 332, 333, 334, 335, 336, 337, 338, 339, 340, 341, 342, 343, 344, 345, 346, 347, 348, 349, 350, 351, 352, 353, 354, 355, 356, 357, 358, 359, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 659, 658, 657, 656, 655, 654, 653, 652, 651, 650, 649, 648, 647, 646, 645, 644, 643, 642, 641, 640, 639, 638, 637, 636, 635, 634, 633, 632, 631, 630, 450, 451, 452, 453, 454, 455, 456, 457, 458, 459, 460, 461, 462, 463, 464, 465, 466, 467, 468, 469, 470, 471, 472, 473, 474, 475, 476, 477, 478, 479, 
  59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 480, 481, 482, 483, 484, 485, 486, 487, 488, 489, 490, 491, 492, 493, 494, 495, 496, 497, 498, 499, 500, 501, 502, 503, 504, 505, 506, 507, 508, 509, 329, 328, 327, 326, 325, 324, 323, 322, 321, 320, 319, 318, 317, 316, 315, 314, 313, 312, 311, 310, 309, 308, 307, 306, 305, 304, 303, 302, 301, 300, 179, 178, 177, 176, 175, 174, 173, 172, 171, 170, 169, 168, 167, 166, 165, 164, 163, 162, 161, 160, 159, 158, 157, 156, 155, 154, 153, 152, 151, 150, 600, 601, 602, 603, 604, 605, 606, 607, 608, 609, 610, 611, 612, 613, 614, 615, 616, 617, 618, 619, 620, 621, 622, 623, 624, 625, 626, 627, 628, 629, 449, 448, 447, 446, 445, 444, 443, 442, 441, 440, 439, 438, 437, 436, 435, 434, 433, 432, 431, 430, 429, 428, 427, 426, 425, 424, 423, 422, 421, 420, 
  60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 719, 718, 717, 716, 715, 714, 713, 712, 711, 710, 709, 708, 707, 706, 705, 704, 703, 702, 701, 700, 699, 698, 697, 696, 695, 694, 693, 692, 691, 690, 270, 271, 272, 273, 274, 275, 276, 277, 278, 279, 280, 281, 282, 283, 284, 285, 286, 287, 288, 289, 290, 291, 292, 293, 294, 295, 296, 297, 298, 299, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 599, 598, 597, 596, 595, 594, 593, 592, 591, 590, 589, 588, 587, 586, 585, 584, 583, 582, 581, 580, 579, 578, 577, 576, 575, 574, 573, 572, 571, 570, 390, 391, 392, 393, 394, 395, 396, 397, 398, 399, 400, 401, 402, 403, 404, 405, 406, 407, 408, 409, 410, 411, 412, 413, 414, 415, 416, 417, 418, 419, 
  119, 118, 117, 116, 115, 114, 113, 112, 111, 110, 109, 108, 107, 106, 105, 104, 103, 102, 101, 100, 99, 98, 97, 96, 95, 94, 93, 92, 91, 90, 660, 661, 662, 663, 664, 665, 666, 667, 668, 669, 670, 671, 672, 673, 674, 675, 676, 677, 678, 679, 680, 681, 682, 683, 684, 685, 686, 687, 688, 689, 269, 268, 267, 266, 265, 264, 263, 262, 261, 260, 259, 258, 257, 256, 255, 254, 253, 252, 251, 250, 249, 248, 247, 246, 245, 244, 243, 242, 241, 240, 239, 238, 237, 236, 235, 234, 233, 232, 231, 230, 229, 228, 227, 226, 225, 224, 223, 222, 221, 220, 219, 218, 217, 216, 215, 214, 213, 212, 211, 210, 540, 541, 542, 543, 544, 545, 546, 547, 548, 549, 550, 551, 552, 553, 554, 555, 556, 557, 558, 559, 560, 561, 562, 563, 564, 565, 566, 567, 568, 569, 389, 388, 387, 386, 385, 384, 383, 382, 381, 380, 379, 378, 377, 376, 375, 374, 373, 372, 371, 370, 369, 368, 367, 366, 365, 364, 363, 362, 361, 360,
};
#endif

uint16_t XY(uint16_t x, uint16_t y) {
  uint16_t i = (y * kMatrixWidth) + x;
  return XYTable[i];
}

uint16_t XYsafe(uint8_t x, uint8_t y) {
  int len = sizeof(XYTable)/sizeof(XYTable[0]);
  if (len != NUM_LEDS) {
    Serial.print("XYsafe: XYTable length not the same as NUM_LEDS");
    return 0;
  }
  if (x >= kMatrixWidth) {
    Serial.print("XYsafe: x param out of bounds - ");
    Serial.print(x);
    Serial.println();
    return 0;
  }
  if (y >= kMatrixHeight) {
    Serial.print("XYsafe: y param out of bounds - ");
    Serial.print(y);
    Serial.println();
    return 0;
  }
  return XY(x, y);
}

//
// Utility functions
//

void setupAnimation(void) {
  int frame_interval = round(1000 / fps);
  eventAnim.interval(frame_interval);
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
    case ANIM_FILL_LOOPS:
      Serial.println("ANIM_FILL_LOOPS");
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
    case ANIM_STATIC_LOOPS:
      Serial.println("ANIM_STATIC_LOOPS");
      break;
    case ANIM_BUBBLES:
      Serial.println("ANIM_BUBBLES");
      break;
    case ANIM_ROLLING_LOOP:
      Serial.println("ANIM_ROLLING_LOOP");
      break;
    default:
      mode = STOPPED;
      setupAnimation();
      break;
  }
#endif
}

void setupStepper(void) {
#ifdef STEPPER
  intStepperOn.begin(stepperOn, stepperInterval);
#endif
}

int stepperIntervalDelta() {
  return STEPPER_RAMP_FIXED + (stepperInterval - STEPPER_INTERVAL_TARGET) / STEPPER_RAMP_DAMP;
}

void updateStepper(void) {
#ifndef DEBUG_STRIP_LOCATION
#ifdef STEPPER
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
#endif
}

void stepperOn(void) {
#ifndef DEBUG_STRIP_LOCATION
#ifdef STEPPER
  // dont step if we are in stopped mode and spoolled down
  if (mode == STOPPED && stepperInterval == STEPPER_INTERVAL_START)
    return;
  // start stepper pulse
  intStepperOff.begin(stepperOff, STEPPER_PULSE);
  digitalWrite(STEPPER_PIN_OUT, HIGH);
#endif
#endif
}

void stepperOff(void) {
#ifndef DEBUG_STRIP_LOCATION
#ifdef STEPPER
  // finish stepper pulse
  intStepperOff.end();
  digitalWrite(STEPPER_PIN_OUT, LOW);
#endif 
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

void button_pressed(void) {
  if ((millis() - last_interrupt) > BUTTON_DEBOUNCE_TIME && !digitalRead(BUTTON_PIN_IN)) {
      running_debounce = true;
      last_interrupt = millis(); 
  }
}

//
// Animation functions
//

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
    case ANIM_FILL_LOOPS:
      fillLoops();
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
    case ANIM_STATIC_LOOPS:
      //staticColorLoops();
      stroboColorLoops();
      break;
    case ANIM_BUBBLES:
      bubbles();
      break;
    case ANIM_ROLLING_LOOP:
      rollingColorLoops();
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

void rollingColorLoops(void) {
  static uint8_t hues[NUM_LOOPS] = {0};
  static uint8_t offset = 0;
  // init hues
  bool needInit = true;
  for (int i=0; i<NUM_LOOPS; i++)
    if (hues[i] != 0)
      needInit = false;
  if (needInit) {
    for (int i=0; i<NUM_LOOPS; i++) {
      hues[i] = i * 256/NUM_LOOPS;
    } 
  }

  if(offset > NUM_LOOPS - 1){
    offset = 0;
  }

  // set loops to hue
  for (int i=0; i<NUM_LOOPS; i++) {
    for (int j=0; j < NUM_LEDS_PER_LOOP; j++) {
      int logicalLedIndex = XYsafe(j, i + offset);
      leds[logicalLedIndex] = CHSV(hues[i + offset], 255, 255);
    }
  }

  offset++;

  // show leds
  FastLED.show();
  
}

void staticColorLoops(void) {
  static uint8_t hues[NUM_LOOPS] = {0};
  // init hues
  bool needInit = true;
  for (int i=0; i<NUM_LOOPS; i++)
    if (hues[i] != 0)
      needInit = false;
  if (needInit) {
    for (int i=0; i<NUM_LOOPS; i++) {
      hues[i] = i * 256/NUM_LOOPS;
    } 
  }
  // set loops to hue
  for (int i=0; i<NUM_LOOPS; i++) {
    for (int j=0; j < NUM_LEDS_PER_LOOP; j++) {
      int logicalLedIndex = XYsafe(j, i);
      leds[logicalLedIndex] = CHSV(hues[i], 255, 255);
    }
  }
  // show leds
  FastLED.show();
}

void stroboColorLoops(void) {
#define COLOR_DWELL_BASE 140
static uint8_t color_dwell_var = COLOR_DWELL_BASE;
static uint8_t color_dwell_random = 0;

#define COLOR_DWELL_MIN 80
#define COLOR_DWELL_MAX 200

#define COLOR_CHANGE_BASE 35
#define COLOR_CHANGE_MIN 30
#define COLOR_CHANGE_MAX 55

static uint8_t color_change_var = COLOR_CHANGE_BASE;
static uint8_t color_change_random = 0;
static uint8_t random_color_stuff_count = 0;
static uint8_t first_palette_index = 0;
static uint8_t second_palette_index = 0;

// TODO Increase random reset period
#define RESET_RANDOM 20

#define FPS_BASE_MOD (0.05)

#define fps_multiplyer_min 1
#define fps_multiplyer_max 6
static uint8_t random_fps_accel_multiplier = random(fps_multiplyer_min, fps_multiplyer_max);
static uint8_t random_fps_deccel_multiplier = random(1, 2);
static uint8_t random_fps_count = 0;
#define FPS_SUSTAIN_PERIOD 140
#define FPS_ACCEL_PERIOD 30

  enum fps_state_t {
    STANDARD,
    SLOWING,
    SLOW,
    ACCELERATING,
  };

  enum color_state_t {
    FIRST_COLOR_PAIR,
    TO_SECOND_COLOR_PAIR,
    SECOND_COLOR_PAIR,
    TO_FIRST_COLOR_PAIR,
  };
  // Add more palettes
  static CHSVPalette16 peach_with_white = CHSVPalette16(
   CHSV(0, 255, 255),
   CHSV(0, 25, 235),
   CHSV(0, 255, 255)
  );
  static CHSVPalette16 purple_with_blue = CHSVPalette16(
   CHSV(192, 255, 255),
   CHSV(128, 255, 255),
   CHSV(192, 255, 255)
  );
  static CHSVPalette16 randomness_one = CHSVPalette16(
    CHSV(90, 255, 255),
    CHSV(120, 125, 255),
    CHSV(90, 255, 255)
  );

  static CHSVPalette16 randomness_two = CHSVPalette16(
    CHSV(64, 255, 255),
    CHSV(30, 255, 255),
    CHSV(64, 255, 255)
  );

  static CHSVPalette16 randomness_three = CHSVPalette16(
    CHSV(140, 255, 255),
    CHSV(160, 255, 255),
    CHSV(140, 255, 255)
  );
  static CHSVPalette16 randomness_four = CHSVPalette16(
    CHSV(224, 255, 255),
    CHSV(210, 255, 255),
    CHSV(224, 255, 255)
  );

//  static CHSVPalette16 randomness_two = CHSVPalette16(
  //  CHSV(240, 255, 255),
  //  CHSV(100, 255, 255),
  //  CHSV(240, 255, 245)
 // );

  #define NUM_PALETTES 6
  static CHSVPalette16 reference_palettes[NUM_PALETTES] = {peach_with_white, purple_with_blue, randomness_one, randomness_two, randomness_three, randomness_four};


  static enum fps_state_t fps_state = STANDARD;
  static uint32_t fps_count = 0;
  static enum color_state_t color_state = FIRST_COLOR_PAIR;
  static uint32_t color_count = 0;
  static uint8_t offsetLoop = 0;
  static uint8_t offsetPalette = 0;
  //static CHSV hues[NUM_LOOPS] = {CHSV(0, 255, 255), CHSV(0, 25, 235), CHSV(192, 255, 255), CHSV(128, 255, 255)};
  static CHSV hues[NUM_LOOPS] = {CHSV(0, 255, 255), CHSV(0, 255, 255), CHSV(128, 255, 255), CHSV(128, 255, 255)};
  static CHSVPalette16 palettes[NUM_LOOPS] = {randomness_one, randomness_one, randomness_four, randomness_four};
  // set loops to hue
  /*
  for (int i=0; i<NUM_LOOPS; i++) {
    for (int j=0; j < NUM_LEDS_PER_LOOP; j++) {
      int logicalLedIndex = XYsafe(j, i);
      leds[logicalLedIndex] = hues[(i + offset) % NUM_LOOPS];
    }
  }
  */
  for (int i=0; i<NUM_LOOPS; i++) {
    for (int j=0; j<NUM_LEDS_PER_LOOP; j++) {
      int logicalLedIndex = XYsafe(j, i);
      if (i == 0 && j == 0) {
        // TODO: Implement variablity on the framerate

        switch (fps_state) {
          case STANDARD:
            if (fps_count >= FPS_SUSTAIN_PERIOD) {
              fps_state = SLOWING;
              fps_count = 0;
            }
            break;
          case SLOWING: {
            fps = fps - (FPS_BASE_MOD * random_fps_deccel_multiplier);
            int frame_interval = round(1000 / fps);
            eventAnim.interval(frame_interval);
            if (fps_count >= FPS_ACCEL_PERIOD) {
              fps_state = SLOW;
              fps_count = 0;         
            }
            break;
          }
          case SLOW:
            if (fps_count >= FPS_SUSTAIN_PERIOD) {
              fps_state = ACCELERATING;
              fps_count = 0;
            }
            break;
          case ACCELERATING: {
            fps = fps + (FPS_BASE_MOD * random_fps_accel_multiplier);
            int frame_interval = round(1000 / fps);
            eventAnim.interval(frame_interval);
            if (fps_count >= FPS_ACCEL_PERIOD) {

              
              if(random_fps_count >= 5){
                random_fps_count = 0;
                random_fps_accel_multiplier = random(fps_multiplyer_min,fps_multiplyer_max);
                random_fps_deccel_multiplier = random(1,2);
              }
              // reset just in case floating point shenanegans
                fps = FPS;
              random_fps_count ++;
              fps_state = STANDARD;
              fps_count = 0;
              
              frame_interval = round(1000 / fps);
              eventAnim.interval(frame_interval);
            }

            break;
          } 
        }
      }
      switch (color_state) {
        case FIRST_COLOR_PAIR:
          leds[logicalLedIndex] = ColorFromPalette(palettes[(i + offsetLoop) % NUM_LOOPS], 0/*j + offsetPalette*/);
          if (color_count >= color_dwell_var) {
            color_state = TO_SECOND_COLOR_PAIR;
            color_count = 0;
          }
          break;
        case TO_SECOND_COLOR_PAIR:
          leds[logicalLedIndex] = ColorFromPalette(palettes[(i + offsetLoop) % NUM_LOOPS], (color_count * 128) / 30);
          if (color_count >= color_change_var) {
            color_state = SECOND_COLOR_PAIR;
            color_count = 0;         
          }
          break;
        case SECOND_COLOR_PAIR:
          leds[logicalLedIndex] = ColorFromPalette(palettes[(i + offsetLoop) % NUM_LOOPS], 128);
          if (color_count >= color_dwell_var) {
            color_state = TO_FIRST_COLOR_PAIR;
            color_count = 0;
          }
          break;
        case TO_FIRST_COLOR_PAIR:
        
          leds[logicalLedIndex] = ColorFromPalette(palettes[(i + offsetLoop) % NUM_LOOPS], 128 + (color_count * 128) / 30);
          // TODO: Change over to be a while loop with two random nums, one for each slot.
          // While loop, to check to make sure that they are not the same value for each slot
          if (color_count >= color_change_var) {
              
              while(true){
                first_palette_index = random(0, NUM_PALETTES);
                second_palette_index = random(0, NUM_PALETTES);
                if(first_palette_index != second_palette_index){
                  palettes[0] = reference_palettes[first_palette_index];
                  Serial.println("Changing");
                  String msg = "Palette 1: " + String(first_palette_index);
                  Serial.println(msg);
                  palettes[1] = reference_palettes[first_palette_index];
                  palettes[2] = reference_palettes[second_palette_index];
                  msg = "Palette 2: " + String(second_palette_index);
                  Serial.println(msg);
                  palettes[3] = reference_palettes[second_palette_index];
                  break;
                }
              }

              color_state = FIRST_COLOR_PAIR;
              color_count = 0;     
    
              color_dwell_random = random(-10, 10);
              color_dwell_var += color_dwell_random;
              color_dwell_var = constrain(color_dwell_var, COLOR_DWELL_MIN, COLOR_DWELL_MAX);
    
              color_change_random = random(-4, 4);
              color_change_var += color_change_random;
              color_change_var = constrain(color_change_var, COLOR_CHANGE_MIN, COLOR_CHANGE_MAX);
              random_color_stuff_count++;
              if(random_color_stuff_count >= 10){
                color_dwell_var = COLOR_DWELL_BASE;
                color_change_var = COLOR_CHANGE_BASE;
                random_color_stuff_count = 0;
              }
            }
          break;   
      }
    }
  }
  
  offsetLoop++;
  fps_count++;
  color_count++;
  offsetPalette++;
  // show leds
  FastLED.show();
}

void movingDot(void) {
  static uint8_t hues[NUM_LOOPS] = {0};
  static int dots[NUM_LOOPS] = {0};
  // init hues and dot indexs
  bool needInit = false;
  for (int i=0; i<NUM_LOOPS; i++) {
    if (hues[i] == 0) {
      needInit = true;
    }
 }
  if (needInit) {
    for (int i=0; i<NUM_LOOPS; i++) {
      hues[i] = i * 256/NUM_LOOPS;
      dots[i] = 0;
    } 
  }
  // set dots to current hue
  for (int i=0; i<NUM_LOOPS; i++) {
    int dot = dots[i];
    int logicalLedIndex = XYsafe(dot, i);
    leds[logicalLedIndex] = CHSV(hues[i]/*++*/, 255, 255);
    // update dot index for next time
    dot++;
    if (dot >= NUM_LEDS_PER_LOOP) {
      dot = 0;
    }
    dots[i] = dot;
  }
  // show leds
  FastLED.show();
}

void fillLoops(void) {
  static uint8_t hues[NUM_LOOPS] = {0};
  static int dots[NUM_LOOPS] = {0};
  static bool filling[NUM_LOOPS] = {0};
  //static int loopIndex = 3;
  // init hues and dot indexs
  bool needInit = true;
  for (int i=0; i<NUM_LOOPS; i++)
    if (hues[i] != 0)
      needInit = false;
  if (needInit) {
    for (int i=0; i<NUM_LOOPS; i++) {
      hues[i] = i * 256/NUM_LOOPS;
      dots[i] = 0;
      filling[i] = true;
    } 
  }
  // set dots to current hue
  for (int i=0; i<NUM_LOOPS; i++) {
    //if (loopIndex != i)
    //  continue;

    int dot = dots[i];
    for (int j=0; j<NUM_LEDS_PER_LOOP; j++) {
      int logicalLedIndex = XYsafe(j, i);
      if (filling[i]) {
        if (j <= dot)
          leds[logicalLedIndex] = CHSV(hues[i]/*++*/, 255, 255);
        else
          leds[logicalLedIndex] = CRGB(0, 0, 0);
      } else {
        if (j > dot)
          leds[logicalLedIndex] = CHSV(hues[i]/*++*/, 255, 255);
        else
          leds[logicalLedIndex] = CRGB(0, 0, 0);        
      }
    }
    // update dot index for next time
    dot++;
    if (dot >= NUM_LEDS_PER_LOOP) {
      dot = 0;
      filling[i] = !filling[i];
    }
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
  static CHSVPalette16 peach_with_white = CHSVPalette16(
   CHSV(0, 255, 255),
   CHSV(0, 25, 235),
   CHSV(0, 255, 255)
  );
  static CHSVPalette16 purple_with_blue = CHSVPalette16(
   CHSV(192, 255, 255),
   CHSV(128, 255, 255),
   CHSV(192, 255, 255)
  );
  CHSVPalette16 palettes[NUM_LOOPS] = {peach_with_white, peach_with_white, purple_with_blue, purple_with_blue};
  for (int i=0; i<NUM_LOOPS; i++) {
    for (int j=0; j<NUM_LEDS_PER_LOOP; j++) {
      int logicalLedIndex = XYsafe(j, i);
      leds[logicalLedIndex] = ColorFromPalette(palettes[i], j + offset);
    }
  }
  offset = offset + 4;
  FastLED.show();
}

//
// Bubble animation
//
// TODO:
// shift pattern backwards at same rate of rotation (should cause the pattern to curve in on itself)
// 30 leds per strip, 3 sectors, so 3 * 30 = 90, 90 leds full rotation, 360/90 = 4 degrees per led
// bubbles pulsing in size (4-8 led diameter), also color shifting? on a color shifting background?

struct bubble_t {
  uint8_t x, y, radius;  
};

float _x_transalation_required_per_step(void) {
  int leds_per_circle = NUM_SECTORS * NUM_LED_PER_STRIP; // 90
  int degrees_per_led = 360 / leds_per_circle;           // 4
  int degrees_per_frame = 360 / FPS;                     // 360 / 24 = 15
  return degrees_per_frame / degrees_per_led;            // 15 / 4 = 3.75 - 3, 4, 4, 4 ???
}

void _xy_double_loop_to_bubblegrid(int* x, int* y) {
  *y = *y * 2;
  if (*x >= NUM_LEDS_X) {
    *x = *x - NUM_LEDS_X;
    *y = *y + NUM_LOOPS;
  }
}

void _xy_bubblegrid_to_double_loop(int* x, int* y) {
  if (*x < NUM_LEDS_X) {
    *x = *x + NUM_LEDS_X;
    *y = *y - NUM_LOOPS;
  }
  *y = *y / 2;
}

struct coord_t {
  int x, y;
};

struct coord_t _x_translate(int x, int y, bool next) {
  // to bubble grid
  _xy_double_loop_to_bubblegrid(&x, &y);
  // get translation amount
  static int c = 0;
  if (next)
    c++;
  int trans_x = 4;
  if (c % 4 == 0)
    trans_x = 3;
  // translate x
  x = x - trans_x;
  if (x < 0)
    x = NUM_LEDS_X-1;
  // to double loop
  _xy_bubblegrid_to_double_loop(&x, &y);
  struct coord_t res = {x, y};
  return res;
}

int _modulate(void) {
  return random(0, 24);
}

void _move_bubbles(struct bubble_t* bubbles, int max_bubbles) {
#define RAD_MIN 1
#define RAD_MAX 4
  for (int i=0; i < max_bubbles; i++) {
    struct bubble_t* bubble = &bubbles[i];
    
    if (bubble->radius == 0) {
      // init bubble
      bubble->radius = RAD_MAX;
      bubble->x = i * NUM_LEDS_X / max_bubbles;
      bubble->y = 0;
    }

    //
    //continue;
    
    // change size
    int m = _modulate();
    if (m == 0) {
      if (bubble->radius < RAD_MAX)
        bubble->radius++;
    } else if (m == 1) {
      if (bubble->radius > RAD_MIN)
        bubble->radius--;
    }
    // change x
    m = _modulate();
    if (m == 0) {
      bubble->x++;
      if (bubble->x >= NUM_LEDS_X)
        bubble->x = 0;
    } else if (m == 1) {
      bubble->x--;
      if (bubble->x >= NUM_LEDS_X)
        bubble->x = NUM_LEDS_X - 1;
    }
    // change y
    m = _modulate();
    if (m == 0) {
      bubble->y++;
      if (bubble->y >= NUM_LEDS_Y)
        bubble->y = 0;
    } else if (m == 1) {
      bubble->y--;
      if (bubble->y >= NUM_LEDS_Y)
        bubble->y = NUM_LEDS_Y - 1;
    }
/*
    Serial.print("x :");
    Serial.print(bubble->x);
    Serial.print(", ");
    Serial.print("y :");
    Serial.print(bubble->y);
    Serial.print(", ");
    Serial.print("r :");
    Serial.print(bubble->radius);
    Serial.println();
*/
  }
}
  
bool _in_bubble(struct bubble_t* bubbles, int max_bubbles, int x, int y) {
  _xy_double_loop_to_bubblegrid(&x, &y);
  
  for (int i=0; i < max_bubbles; i++) {
    struct bubble_t* bubble = &bubbles[i];
    int dx = abs(bubble->x - x);
    dx = min(dx, NUM_LEDS_X - dx);
    int dy = abs(bubble->y - y);
    dy = min(dy, NUM_LEDS_Y - dy);
    if (dx + dy < bubble->radius)
      return true;
  }
  return false;
}

void bubbles(void) {
#define MAX_BUBBLES 4
  static bubble_t bubbles[MAX_BUBBLES] = {0};
  static uint8_t offset = 0;
  static CHSVPalette16 peach_with_white = CHSVPalette16(
   CHSV(0, 255, 255),
   CHSV(0, 25, 235),
   CHSV(0, 255, 255)
  );
  static CHSVPalette16 purple_with_blue = CHSVPalette16(
   CHSV(192, 255, 255),
   CHSV(128, 255, 255),
   CHSV(192, 255, 255)
  );
  _move_bubbles(bubbles, MAX_BUBBLES);
  for (int i=0; i<NUM_LOOPS; i++) {
    for (int j=0; j<NUM_LEDS_PER_LOOP; j++) {
      //struct coord_t translation = _x_translate(j, i, (i==0 && j==0));
      struct coord_t translation = {0, 0};
      int logicalLedIndex = XYsafe(j + translation.x, i + translation.y);
      if (_in_bubble(bubbles, MAX_BUBBLES, j, i))
        leds[logicalLedIndex] = ColorFromPalette(peach_with_white, 0/*j + offset*/);
      else
        leds[logicalLedIndex] = ColorFromPalette(purple_with_blue, j + offset);
    }
  }
  offset = offset + 1;
  FastLED.show();
}



void ledsClear(void) {
  // TODO: should just need clear!!
#ifdef EXTENSION_STRIP
  fill_solid(leds, NUM_LEDS + NUM_LED_PER_STRIP, CRGB::Black);
#else
  fill_solid(leds, NUM_LEDS, CRGB::Black);
#endif
  FastLED.show();
  //FastLED.clear();
}
