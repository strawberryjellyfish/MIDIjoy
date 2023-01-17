/* 
**  MIDIjoy - joystick based USB MIDI controller for manipulation of 3 CC controls
**
**  Hardware: Arduino Pro Micro / LeonardoIntended
**            3 axis auto centering joystick with control button
**
**  strawberryjellyfish.com         
**  Version: 0.1
*/

// Required libraries and includes ---------------------------------------------
#include <Arduino.h>
#include <debounce.h>
#include <Wire.h>
#include <ACROBOTIC_SSD1306.h>
#include "MIDIUSB.h"


// Configuration variables ----------------------------------------------------

byte midiCh = 1;                //  MIDI channel
bool isCenterOffset = false;    // Centre mode - if true allows setting of midi values for center position of offset pots
const int commandSwitch = 7;    // Pin for command switch (used to toggle mode and set centre)

// Potentiometer definition - all arrays here should have NUMBER_OF_POTS items
const int NUMBER_OF_POTS = 3;   // Number of pots in controller
const int POT_PINS[NUMBER_OF_POTS] = { A0, A1, A2 };    // Analog PIN for each pot
const bool POT_IS_OFFSET[NUMBER_OF_POTS] = { true, true, true };    // set true if pot has initial offset, eg resting position is non 0
int potCCNumber[NUMBER_OF_POTS] = { 1, 2, 3 };    // MIDI CC number for each pot

// Pot Smoothing
const int POT_TIMEOUT = 300;        // Amount of time the potentiometer will be read after it exceeds the POT_VARI_THRESHOLD
const int POT_VARI_THRESHOLD = 10;  // Amount pot value is required to change before it should considered a change (higher for more smoothing)


// Should be no realneed to modify anything below ---------------------------------
// Note: remove 0 from array definitions if for some reason  NUMBER_OF_POTS = 0

int potCurrValue[NUMBER_OF_POTS] = { 0 };       // current/most recent values of pots
int potPrevValue[NUMBER_OF_POTS] = { 0 };       // previous values of pots
int potHome[NUMBER_OF_POTS] = { 0 };            // initial "home" value of any pot with a true flag in POT_IS_OFFSET[]
int potHomeMidi[NUMBER_OF_POTS] = { 0 };        // initial MIDI value of any pot with a true flag in POT_IS_OFFSET[]
int potSoftHomeMidi[NUMBER_OF_POTS] = { 0 };    // home MIDI value for a pot with an offset
int midiCurrentValue[NUMBER_OF_POTS] = { 0 };   // current MIDI value for pots  
int midiPrevValue[NUMBER_OF_POTS] = { 0 };      // previous MIDI value for pots 

int potVariance = 0;                            // difference between the current and previous value
boolean isPotMoving = true;                     // If the potentiometer is moving
unsigned long potPrevTime[NUMBER_OF_POTS] = { 0 };  // previous time for pot smoothing
unsigned long potTimer[NUMBER_OF_POTS] = { 0 };     // time that has elapsed since the pot smoothing timer was reset

const int MIDI_MIN_VALUE = 0;       
const int MIDI_MAX_VALUE = 127;
const int ANALOG_MIN_VALUE = 0;
const int ANALOG_MAX_VALUE = 1023;


// Functions ------------------------------------------------------------------

// Send a MIDI Note On message
void noteOn(byte channel, byte pitch, byte velocity) {
  midiEventPacket_t noteOn = { 0x09, 0x90 | channel, pitch, velocity };
  MidiUSB.sendMIDI(noteOn);
}

// Send a MIDI Note Off message
void noteOff(byte channel, byte pitch, byte velocity) {
  midiEventPacket_t noteOff = { 0x08, 0x80 | channel, pitch, velocity };
  MidiUSB.sendMIDI(noteOff);
}

// Send a MIDI CC message
void controlChange(byte channel, byte control, byte value) {
  midiEventPacket_t event = { 0x0B, 0xB0 | channel, control, value };
  MidiUSB.sendMIDI(event);
}

// Command Button callback for single click
// Switches to offset centre mode and sets soft home positions to current positions
static void commandButtonClickHandler(uint8_t btnId, uint8_t btnState) {
  if (btnState == BTN_PRESSED) {
    isCenterOffset = true;
    // update the pot home positions to current position.
    setSoftHomeValues(!isCenterOffset);
    updateDisplay();
    return;
  }
}

// Command Button callback for long press and release
// Switches to full range mode and sets soft home positions to initial/default positions
static void commandButtonHoldHandler(uint8_t btnId, uint8_t btnState) {
  if (btnState == BTN_PRESSED) {
    return;  // Do nothing on press-down.
  }

  isCenterOffset = false;
  setSoftHomeValues(!isCenterOffset);
  updateDisplay();
}

// Attach button callbacks
static Button commandButtonClick(0, commandButtonClickHandler);
static Button commandButtonHold(0, commandButtonHoldHandler);

// Set the soft home values for all pots to either their current position
// or if reset is true, their initial home position.
static void setSoftHomeValues(bool reset) {
  for (int i = 0; i < NUMBER_OF_POTS; i++) {
    if(POT_IS_OFFSET[i]){
      if(reset) {
        potSoftHomeMidi[i] = map(potHome[i], ANALOG_MIN_VALUE, ANALOG_MAX_VALUE, MIDI_MIN_VALUE, MIDI_MAX_VALUE);
      }
      else{
        potSoftHomeMidi[i] = map(analogRead(POT_PINS[i]), ANALOG_MIN_VALUE, ANALOG_MAX_VALUE, MIDI_MIN_VALUE, MIDI_MAX_VALUE);
      }
    }
  }
}

// Set the home position of any pots marked in POT_IS_OFFSET[] to their current position.
static void setHomeValues() {
  for (int i = 0; i < NUMBER_OF_POTS; i++) {
    if(POT_IS_OFFSET[i]){
      potHome[i] = analogRead(POT_PINS[i]);
      potHomeMidi[i] = map(potHome[i], ANALOG_MIN_VALUE, ANALOG_MAX_VALUE, MIDI_MIN_VALUE, MIDI_MAX_VALUE);
    }
  }
}

// check for button changes
static void pollButtons() {
  commandButtonHold.update(digitalRead(commandSwitch));
  commandButtonClick.update(digitalRead(commandSwitch));
}

// Check for potentiometer changes
// Loop through potentiometers and update the mapped variables if they have 
// changed, any difference from the previous value that is less than 
// POT_VARI_THRESHOLD is considered noise and ignored.
void potentiometers() {

  for (int i = 0; i < NUMBER_OF_POTS; i++) {

    // get raw analog value, apply some smoothing to reduce analogue jitter  determine if the pot is actually moving after smoothing jitter
    potCurrValue[i] = analogRead(POT_PINS[i]);

    // Start smoothing
    potVariance = abs(potCurrValue[i] - potPrevValue[i]);

    if (potVariance > POT_VARI_THRESHOLD) {
      potPrevTime[i] = millis();
    }

    // Reset timout timer
    potTimer[i] = millis() - potPrevTime[i];

    // Pot is considered moving unless we have exceeded the max time to watch it
    if (potTimer[i] < POT_TIMEOUT) {
      isPotMoving = true;
    } else {
      isPotMoving = false;
    }

    if (isPotMoving == true) {

      if(POT_IS_OFFSET[i]){

        // if a pot is POT_IS_OFFSET[x] it is assumed we use sprung pots that return to centre 
        // initial positions are stored in potHome[x], potSoftHomeMidi[x] holds the mapped 
        // midi equivalent of potHome[x]
        // Both home values are passed into weightedMap() to calculate the new midi value 
        // which will use different linear scales below and above the home position if
        // potSoftHomeMidi[x] has been offset from default position using setSoftHomeValues()

        midiCurrentValue[i] = weightedMap(potCurrValue[i], ANALOG_MIN_VALUE, ANALOG_MAX_VALUE, MIDI_MIN_VALUE, MIDI_MAX_VALUE, potHome[i], potSoftHomeMidi[i]);
      }
      else
      {
        // standard pot gets a straight mapping between analog value range and midi value range      
        midiCurrentValue[i] = map(potCurrValue[i], ANALOG_MIN_VALUE, ANALOG_MAX_VALUE, MIDI_MIN_VALUE, MIDI_MAX_VALUE); 
      }

      if (midiPrevValue[i] != midiCurrentValue[i]) {

        // MIDI value has changed so we send a CC message accordingly
        controlChange(midiCh, potCCNumber[i], midiCurrentValue[i]);
        MidiUSB.flush();

        // update variables for comaparisons next time
        potPrevValue[i] = potCurrValue[i];
        midiPrevValue[i] = midiCurrentValue[i];

        updateDisplay();
      }
    }
  }
}

void updateDisplay(){
  oled.setTextXY(1, 0);
  if(isCenterOffset){
    oled.putString("Mode: Normal");
  }
  else
  {
    oled.putString("Mode: Offset");
  }
  oled.setTextXY(2, 0);
  oled.putString("X: ");
  oled.putNumber(midiCurrentValue[0]);
  oled.putString(" ");
  oled.setTextXY(3, 0);
  oled.putString("Y: ");
  oled.putNumber(midiCurrentValue[1]);
  oled.putString(" ");
  oled.setTextXY(4, 0);
  oled.putString("Z: ");
  oled.putNumber(midiCurrentValue[2]);
  oled.putString(" ");
}

// Allows weighting of a map above/below a reference value
// It behaves like a standard map() if refIn is mid point between inMin and inMax 
// and refOut is mid pooint between outMin and outMax.
// Adjusting refOut in relation to refIn allows shifting the relative centre of the map, 
// with ranges above and below having different scaling.
int weightedMap(int value, int inMin, int inMax, int outMin, int outMax, int refIn, int refOut) {
  int outVal = 0;
  if (value < refIn) {
    outVal = map(value, inMin, refIn, outMin, refOut);
  } else if (value > refIn) {
    outVal = map(value, refIn, inMax, refOut, outMax);
  } else {
    outVal = refOut;
  }
  return outVal;
}

// Arduino Setup function -----------------------------------------------------
void setup() {

  oled.init();           // Initialze SSD1306 OLED display
  oled.clearDisplay();
  oled.setTextXY(0, 0);
  oled.putString("MIDI Joy");

  // Set pin modes for potentiometers and switches
  pinMode(commandSwitch, INPUT_PULLUP);
  
  for (int i = 0; i < NUMBER_OF_POTS; i++) {
    pinMode(POT_PINS[i], INPUT);
  }

  // Set button debounce times
  commandButtonHold.setPushDebounceInterval(1000);
  commandButtonClick.setPushDebounceInterval(25);

  // Get home position of potentiometers
  setHomeValues();
  setSoftHomeValues(!isCenterOffset);
}

// Arduino Loop function ------------------------------------------------------
void loop() {
  pollButtons();
  potentiometers();
}
