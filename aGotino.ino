/*
   aGotino a simple Goto with Arduino (Nano/Uno)

      * tracking 
      * cycle among forward and backward speeds on RA&Dec at buttons press
      * listen on serial port for basic LX200 commands (INDI LX200 Basic, SkySafari, Stellarium etc)
      * listen on serial port for aGotino commands
      * ST4 port support

    Command set and latest updates at https://github.com/mappite/aGotino
    
    by gspeed @ astronomia.com / qde / cloudynights.com forum
    This code is free software under GPL v3 License use at your risk and fun ;)

 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *

   How to calculate STEP_DELAY to drive motor at right sidereal speed for your mount

   Worm Ratio                  144   // 144 eq5/exos2, 135 heq5, 130 eq3-2
   Other (Pulley/Gear) Ratio     2.5 // depends on your pulley setup e.g. 40T/16T = 2.5
   Steps per revolution        400   // or usually 200 depends on your motor
   Microsteps                   32   // number of microsteps per step, depends on driver

   MICROSTEPS_PER_DEGREE_RA  12800   // = WormRatio*OtherRatio*StepsPerRevolution*Microsteps/360
                                     // = number of microsteps to rotate the scope by 1 degree

   STEP_DELAY                18699   // = (86164/360)/(MicroSteps per Degree)*1000000
                                     // = microseconds to advance a microstep at 1x
                                     // 86164 is the number of secs for earth 360deg rotation (23h56m04s)

 * Update the values below to match your mount/gear ratio and your preferences: 
 * * * * * * */

const unsigned long STEP_DELAY = 18699;                // see above calculation
const unsigned long MICROSTEPS_PER_DEGREE_RA  = 12800; // see above calculation
const unsigned long MICROSTEPS_PER_DEGREE_DEC = MICROSTEPS_PER_DEGREE_RA; // specify a different value if DEC gears/worm/microsteps differs from RA ones

// Set number of RA driver microsteps per step when raEnableMicroStepsPin is High or Low
// For DRV8825: 32 (High) and 1 (Low) 
const unsigned long MICROSTEPS_RA_HIGH  = 32; 
const unsigned long MICROSTEPS_RA_LOW = 1;    
// For TMC drivers refer to datasheet/your wiring to know correct values (see aGotino github page for examples)
//   example in TMC2208 if both MS1 & MS2 are driven by raEnableMicroStepsPin values are 16 (High) and 8 (Low)

// Same as above but for DEC
const unsigned long MICROSTEPS_DEC_HIGH  = 32; 
const unsigned long MICROSTEPS_DEC_LOW = 1;    

const long SERIAL_SPEED = 9600;          // serial interface baud. Make sure your computer/phone matches this
long MAX_RANGE = 1800;                   // default max slew range in deg minutes (1800'=30°). See +range command

// Motor clockwise direction: HIGH is for tracking as per original design (W)
//          change to LOW if you inverted wirings or motor position or... 
int RA_DIR   = HIGH;                    // ... change RA_DIR to LOW in Southern Hemisphere
int DEC_DIR  = HIGH;                    

unsigned int  SLOW_SPEED      = 8;      // RA&DEC slow motion speed (button press) - times the sidereal speed
const    int  SLOW_SPEED_INC  = 4;      // Slow motion speed increment at +speed command

unsigned long STEP_DELAY_SLEW = 1200;   // Slewing Pulse timing in micros (the higher the pulse, the slower the speed)
                                        // don't change this to too low values otherwise your scope may take off as an helicopter.

boolean SIDE_OF_PIER_WEST     = true;   // Default Telescope position is west of the mount. Press both buttons for 1 sec to reverse
boolean POWER_SAVING_ENABLED  = true;   // toggle with -sleep on serial, see decSleep(), note this is disabled if ST4 port is active
boolean DEBUG                 = false;  // toggle with +debug on serial

// Arduino Pin Layout
const int raDirPin    =  4;
const int raStepPin   =  3;
const int raButtonPin =  6;             // note this was 2 in ARto.ino
const int raEnableMicroStepsPin  = 2;
const int decDirPin   = 12;
const int decStepPin  = 11;
const int decButtonPin=  7;
const int decEnableMicroStepsPin = 9;
const int decSleepPin = 10;

/* Experimental: Uncomment the line below to enable ST4 Port on A0-A3 pins */
// #define ST4

#ifdef ST4
unsigned int ST4_PULSE_FACTOR = 20;     // st4 pulse factor, increase to increase period lenght (i.e. decrease speed). Range 10-30
const int st4NorthPin = A0;
const int st4SouthPin = A1;
const int st4EastPin  = A2;
const int st4WestPin  = A3;
#endif

/*
 * It is safe to keep the below untouched
 * 
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "catalogs.h" // load objects for aGoto protocol (Star List, Messier, NGC)

const unsigned long MICROSTEPS_RA_RATIO  = MICROSTEPS_RA_HIGH / MICROSTEPS_RA_LOW;  
const unsigned long MICROSTEPS_DEC_RATIO = MICROSTEPS_DEC_HIGH / MICROSTEPS_DEC_LOW;

// Number of Microsteps to move RA by 1hour
const unsigned long MICROSTEPS_PER_HOUR  = MICROSTEPS_PER_DEGREE_RA * 360 / 24;

// Compare Match Register for RA interrupt (PRESCALER=8)
//  to match with given STEP_DELAY
const int CMR = (STEP_DELAY*16/8/2)-1; 

unsigned long raPressTime  = 0;    // when RA  button has been pressed
unsigned long decPressTime = 0;    // when DEC button has been pressed
unsigned long bothPressTime= 0;    // when both buttons have been pressed, used for change of pier

unsigned long decLastTime  = 0;    // last time DEC pulse has changed status
boolean raStepPinStatus    = false;// true = HIGH, false = LOW
boolean decStepPinStatus   = false;// true = HIGH, false = LOW

boolean SLEWING = false; // true while it is slewing to block AR tracking

const long DAY_SECONDS =  86400; // secs in a day
const long NORTH_DEC   = 324000; // 90°

// Current coords in Secs (default to true north)
long currRA  = 0;     
long currDEC = NORTH_DEC;


const int DEC_HALT  = 0;   // dec is not moving
const int DEC_NORTH = 1;   // 1st time dec button is pressed // FIXME: IT MAY BE THE VICEVERSA NEED TO TEST!
const int DEC_SOUTH = 2;   // 2nd time dec button is pressed (at 3rd press, re-set to DEC_HALT)
const int DEC_ST4   = 3;   // dec is moving due to ST4 pulse
int decState = DEC_HALT;   // initial DEC state (don't move)

const int RA_TRACKING = 0; // tracking at 1x
const int RA_EAST = 1;     // 1st time button is pressed
const int RA_WEST = 2;     // 2nd time button is pressed (at 3rd press, re-set to tracking)
const int RA_ST4  = 3;     // moving due to ST4 pulse
int raState = RA_TRACKING; // initial RA speed (tracking)


// Serial Input
char input[20];     // stores serial input
int  in = 0;        // current char in serial input
// Serial Input (New) coords
long inRA    = 0;
long inDEC   = 0;

// Current position in Meade lx200 format, see updateLx200Coords()
String lx200RA  = "00:00:00#";
String lx200DEC = "+90*00:00#";

// Vars to implement accelleration
unsigned long MAX_DELAY      = 16383; // limit of delayMicroseconds() for Arduino Uno
unsigned long decStepDelay   = MAX_DELAY; // initial pulse lenght (slow, to start accelleration)
unsigned long decTargetDelay = STEP_DELAY/SLOW_SPEED; // pulse length to reach when Dec button is pressed
unsigned int  decPlayIdx     = 0; // pulse index, Dec will accellerate for first 100 pulses

String _aGotino = "aGotino";
const unsigned long _ver = 210708;

void setup() {
  Serial.begin(SERIAL_SPEED);
  Serial.print(_aGotino);
  // init Arduino->Driver Motors pins as Outputs
  pinMode(raStepPin, OUTPUT);
  pinMode(raDirPin,  OUTPUT);
  pinMode(raEnableMicroStepsPin, OUTPUT);
  pinMode(decStepPin, OUTPUT);
  pinMode(decDirPin,  OUTPUT);
  pinMode(decEnableMicroStepsPin, OUTPUT);
  pinMode(decSleepPin, OUTPUT);
  // set Dec asleep:
  decSleep(true);
  // set AR motor direction clockwise
  digitalWrite(raDirPin, RA_DIR);
  // make sure no pulse signal is sent
  digitalWrite(raStepPin,  LOW);
  digitalWrite(decStepPin, LOW);
  // enable microstepping on both motors
  digitalWrite(raEnableMicroStepsPin, HIGH);
  digitalWrite(decEnableMicroStepsPin, HIGH);
  // init Button pins as Input Pullup so no need resistor
  pinMode(raButtonPin,  INPUT_PULLUP);
  pinMode(decButtonPin, INPUT_PULLUP);

  #ifdef ST4
  // init ST4 port pins as well
  pinMode(st4NorthPin, INPUT_PULLUP);
  pinMode(st4SouthPin, INPUT_PULLUP);
  pinMode(st4EastPin,  INPUT_PULLUP);
  pinMode(st4WestPin,  INPUT_PULLUP);
  POWER_SAVING_ENABLED  = false;          // force disable  since we don't want glitches or delays on DEC sleep/wakeup while guiding
  #endif
  
  // init led and turn on for 0.5 sec
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(500);
  digitalWrite(LED_BUILTIN, LOW);
  /* set interrupt for RA motor and start tracking */
  setRaTimer(CMR);
  lx200DEC[3] = char(223); // set correct char in string as per earlier specs - FIXME: this should not be needed anymore
  Serial.println(" ready.");
}

/* 
 *  Timer to trigger RA driver pulse
 *   invoked in setup() and when RA speed changes on button press
 */
void setRaTimer(int cmr) {
  /*  STEP_DELAY/2 = 9349 => 1000000/9349 =  106.9575Hz 
   *  CMR = 16000000/(prescaler*(1000000/(STEP_DELAY/2)))-1;
   *  => CMR = 145.0959 with prescaler=1024, 18699  with prescaler=0
   *  let's go for the finest one
   */
  /* Using 8-bit Timer 2 (CMR less than 256)
  TCCR2A = 0;// set entire TCCR2A register to 0
  TCCR2B = 0;// same for TCCR2B
  TCNT2  = 0;//initialize counter value to 0
  OCR2A = CMR; // compare match register
  TCCR2A |= (1 << WGM21); // turn on CTC mode ( Clear Timer on Compare Match)
  TCCR2B |= (1<<CS20)|(1<<CS21)|(1<<CS22); // 1024 prescaler
  TIMSK2 |= (1 << OCIE2A);  // enable timer compare interrupt
  */
  cli(); // disable interrupts
  // Set Timer1 (16-bit)
  TCCR1A = 0; // reset register to 0
  TCCR1B = 0; // 
  TCNT1  = 0; // initialize counter
  OCR1A = cmr; // compare match register
  TCCR1B |= (1 << WGM12); // turn on CTC mode ( Clear Timer on Compare Match)
  //TCCR1B |= (1 << CS12) | (1 << CS10);  // 1024 prescaler
  //TCCR1B |= (1 << CS11) | (1 << CS10);  // 64 prescaler
  TCCR1B |= (1 << CS11);   // 8 prescaler
  TIMSK1 |= (1 << OCIE1A); // enable timer compare interrupt 
  sei(); // enable interrupts
}

/* Move RA motor (change pulse) - slow motion
 *   invoked by Interrupt when timer register reaches CMR 
 */
ISR(TIMER1_COMPA_vect){
  if (!SLEWING) { // change pulse only when not slewing
    raStepPinStatus = !raStepPinStatus;
    digitalWrite(raStepPin, (raStepPinStatus ? HIGH : LOW));
  }
}

/* Move DEC motor (change pulse) - slow motion
 *   invoked from main loop() if decState is not DEC_HALT 
 *   note: it is decStepDelay&decTargetDelay that drive the actual speed (period pulse)
 *   For the first 100 pulses (50 steps) motor accellerates to decTargetDelay
 *   For ST4 motor moves at decStepDelay
 */
void decPlay() {

  unsigned long dt  = micros() - decLastTime; // time elapsed since last pulse change
  long rttcp = (decStepDelay/2) - dt;         // remaining time to change pulse

  if ( rttcp < 200 && rttcp > 0 ) { // less than 200 micros to change pulse, do it
    delayMicroseconds(rttcp); // hold for the remaining millis (<200)
    decStepPinStatus = !decStepPinStatus;
    digitalWrite(decStepPin, (decStepPinStatus ? HIGH : LOW));
    decLastTime = micros(); // reset time
    if (decPlayIdx<=100 && decState != DEC_ST4) { // accellerate for first 50 steps (100 pulses) if this is not ST4 guiding
      // decrease the pulse from decStepDelay = MAX_DELAY to decStepDelay = decTargetDelay = STEP_DELAY/SLOW_SPEED
      unsigned int i = decPlayIdx/2; // 0, 0, 1,1,2,2,3,3,..., 50, 50
      decStepDelay = MAX_DELAY-( (MAX_DELAY-decTargetDelay)*i/50); 
      decPlayIdx++; 
    } // else use decStepDelay value for the pulse lenght 
  } else if ( rttcp < 0 ) { // something happened that causes the time elapsed to be too high, this should never happen
    printLog("Dec: tool late!");
    decLastTime = micros();
  }
}

/*
 *  Slew RA and Dec by seconds/arceconds (ra/dec)
 *   motors direction is set according to sign
 *   RA 1x direction is re-set at the end
 *   naming convention refer to DRV8825 where micostepping is disabled (full step) and then re-enabled (micro steps)
 */
int slewRaDecBySecs(long raSecs, long decSecs) {

  // If more than 12h, turn from the opposite side
  if (abs(raSecs) > DAY_SECONDS/2) { // reverse
    printLog("RA reversed, RA secs:");
    raSecs = raSecs+(raSecs>0?-1:1)*DAY_SECONDS;
    printLogUL(raSecs);
  }

  // check if within max range
  if ( (abs(decSecs) > (MAX_RANGE*60)) || ( abs(raSecs) > (MAX_RANGE*4)) ) {
    return 0; // failure
  }
  
  // set directions
  digitalWrite(raDirPin,  (raSecs  > 0 ? RA_DIR :(RA_DIR ==HIGH?LOW:HIGH)));
  digitalWrite(decDirPin, (decSecs > 0 ? DEC_DIR:(DEC_DIR==HIGH?LOW:HIGH)));

  // calculate how many steps are needed when max microstepping is enabled, i.e. raEnableMicroStepsPin and decEnableMicroStepsPin = high
  // inner division is to avoid long overflow for 46+ degrees
  unsigned long raSteps  = (abs(raSecs) * (MICROSTEPS_PER_HOUR/100)) / 36;
  unsigned long decSteps = (abs(decSecs) * (MICROSTEPS_PER_DEGREE_DEC/100)) / 36;

  // calculate how many full&micro steps are needed
  unsigned long raFullSteps   = raSteps / MICROSTEPS_RA_RATIO;               // this will truncate the result...
  unsigned long raMicroSteps  = raSteps - raFullSteps * MICROSTEPS_RA_RATIO; // ...remaining microsteps
  unsigned long decFullSteps  = decSteps / MICROSTEPS_DEC_RATIO;                // this will truncate the result...
  unsigned long decMicroSteps = decSteps - decFullSteps * MICROSTEPS_DEC_RATIO; // ...remaining microsteps

  // Disable microstepping (for DRV8825, for other drivers this may reduce, ref to  MICROSTEPS_RA_HIGH / MICROSTEPS_DEC_LOW  values)
  digitalWrite(raEnableMicroStepsPin, LOW);
  digitalWrite(decEnableMicroStepsPin, LOW);

  // Fast Move
  printLog(" RA FullSteps:  ");
  printLogUL(raFullSteps);
  printLog(" DEC FullSteps: ");
  printLogUL(decFullSteps);
  unsigned long slewTime = micros(); // record when slew code starts, RA 1x movement will be on hold hence we need to add the gap later on
  slewRaDecBySteps(raFullSteps, decFullSteps);
  printLog("FullSteps Slew Done");

  // re-enable micro stepping
  // printLog("Re-enabling Microstepping");
  digitalWrite(raEnableMicroStepsPin, HIGH);
  digitalWrite(decEnableMicroStepsPin, HIGH);

  // Final Adjustment
  // Note: we need 6.66 full steps per minute (with the defualt gears settings)
  //       i.e. one full step is  less than 1/6', i.e. <10". Who cares of fixing such small offset? Well, we do.
  printLog(" RA uSteps:");
  printLogUL(raMicroSteps);
  printLog(" DEC uSteps:");
  printLogUL(decMicroSteps);
  slewRaDecBySteps(raMicroSteps, decMicroSteps);
  printLog("uSteps Slew Done");

  // If slewing took more than 5" (secs), adjust RA
  slewTime = micros() - slewTime; // time elapsed for slewing
  if ( slewTime > (5 * 1000000) ) {
    printLog("* adjusting Ra by secs: ");
    printLogUL(slewTime / 1000000);
    slewRaDecBySecs(slewTime / 1000000, 0); // it's the real number of seconds!
    // printLog("*** adjusting Ra done");
  }

  // reset RA to right sidereal direction
  digitalWrite(raDirPin,  RA_DIR);

  // Success
  return 1;
}

/*
 *  Slew RA and Dec by steps
 *   . assume direction and microstepping is set
 *   . turn system led on 
 *   . set SLEWING to true to hold RA interrupt tracking
 *   . while slewing, listen on serial port and reply to lx200 GR&GD
 *      commands with current (initial) position to avoid
 *      INDI timeouts during long slewings actions
 */
void slewRaDecBySteps(unsigned long raSteps, unsigned long decSteps) {
  digitalWrite(LED_BUILTIN, HIGH);
  SLEWING = true;

  // wake up Dec motor if needed 
  // FIXME: shoud this be moved to slewRaDecBySecs to avoid glitches? This pauses 2millis every time
  if (decSteps != 0) {
    decSleep(false);
  }

  unsigned long delaySlew = 0; 
  unsigned long delayLX200Micros = 0; // mesure delay introduced by LX200 polling reply
  in = 0; // reset the  input buffer read index
  
  for (unsigned long i = 0; (i < raSteps || i < decSteps) ; i++) {
    if ((i<100)) { // Accellerate during inital 100 steps from MAX_DELAY to STEP_DELAY_SLEW
      delaySlew = MAX_DELAY-( (MAX_DELAY-STEP_DELAY_SLEW)/100*i);
    } else if ( (i>raSteps-100 && i<raSteps)|| (i>decSteps-100 && i<decSteps)) {
      delaySlew = STEP_DELAY_SLEW*2;// twice as slow in last 100 steps before a motor is about to stop 
    } else { 
      delaySlew = STEP_DELAY_SLEW; // full speed
    } 
    
    if (i < raSteps)  { digitalWrite(raStepPin,  HIGH); }
    if (i < decSteps) { digitalWrite(decStepPin, HIGH); }
    delayMicroseconds(delaySlew);
    
    if (i < raSteps)  { digitalWrite(raStepPin,  LOW);  }
    if (i < decSteps) { digitalWrite(decStepPin, LOW);  }
    
    // support LX200 polling while slewing
    delayLX200Micros = 0;
    if (Serial.available() > 0) {
      delayLX200Micros = micros();
      input[in] = Serial.read();
      if (input[in] == '#' && in > 1 ) {
        if (input[in-1] == 'R') { // :GR#
          Serial.print(lx200RA);
        } else if (input[in-1] == 'D') { // :GD#
          Serial.print(lx200DEC);
        } else if (input[in-1] == 'Q') { // :Q# stop FIXME: motors stops but current coordinates are set to new target...
          printLog("Slew Stop");
          break;
        }
        in = 0;
      } else {
        if (in++ >5) in = 0;
      }
      delayLX200Micros = micros()-delayLX200Micros;
      if (delayLX200Micros>delaySlew) {
        Serial.println("LX200 polling slows too much!"); // this should never happen. But it happens if we'd use SoftwareSerial
        /* reset position to north and exit
        inRA  = 0;     
        inDEC = NORTH_DEC;
        break; */
      }

    } 
    delayMicroseconds(delaySlew-delayLX200Micros);
  }
  // set Dec to sleep
  if (decSteps != 0) {
    decSleep(true);
  }
  digitalWrite(LED_BUILTIN, LOW);
  SLEWING = false;
}

/*
 * Put DEC motor Driver to sleep to save power or wake it up
 *
   FIXME: issue with glitces at wake up with cheap driver DRV8825
          motor resets to home position (worst case 4 full steps, i.e. less than a minute)
          https://forum.pololu.com/t/sleep-reset-problem-on-drv8825-stepper-controller/7345
          https://forum.arduino.cc/index.php?topic=669304.0
          use better drivers, like TMC ones, to avoid the issue (which is not such big actually)
 */
void decSleep(boolean b) {
  if (POWER_SAVING_ENABLED) {
    if (b == false) { // wake up!
      digitalWrite(decSleepPin, HIGH);
      // FIXME: blocking for 2millis, any better alternative?
      delayMicroseconds(2000); // as per DRV8825 specs drivers needs up to 1.7ms to wake up and stabilize
    } else {
      digitalWrite(decSleepPin, LOW); // HIGH = active (high power) LOW = sleep (low power consumption)
    }
  } else {
    digitalWrite(decSleepPin, HIGH);
  }
}

/* 
 *  Basic Meade LX200 protocol
 */
void lx200(String s) { // all :.*# commands are passed here 
  if (s.substring(1,3).equals("GR")) { // :GR# 
    printLog("GR");
    // send current RA to computer
    Serial.print(lx200RA);
  } else if (s.substring(1,3).equals("GD")) { // :GD# 
    printLog("GD");
    // send current DEC to computer
    Serial.print(lx200DEC);
  } else if (s.substring(1,3).equals("GV")) { // :GV*# Get Version *
    char c = s.charAt(3); 
    if ( c == 'P') {// GVP - Product name
       Serial.print(_aGotino);  
    } else if (c == 'N') { // GVN - firmware version
       Serial.print(_ver);  
    }
    Serial.print('#');
  } else if (s.substring(1,3).equals("Sr")) { // :SrHH:MM:SS# or :SrHH:MM.T# // no blanks after :Sr as per Meade specs
    printLog("Sr");
    // this is INITAL step for setting position (RA)
    long hh = s.substring(3,5).toInt();
    long mi = s.substring(6,8).toInt();
    long ss = 0;
    if (s.charAt(8) == '.') { // :SrHH:MM.T#
      ss = (s.substring(9,10).toInt())*60/10;
    } else {
      ss = s.substring(9,11).toInt();
    }
    inRA = hh*3600+mi*60+ss;
    Serial.print(1); // FIXME: input is not validated
  } else if (s.substring(1,3).equals("Sd")) { // :SdsDD*MM:SS# or :SdsDD*MM#
    printLog("Sd");
    // this is the FINAL step of setting a pos (DEC) 
    long dd = s.substring(4,6).toInt();
    long mi = s.substring(7,9).toInt();
    long ss = 0;
    if (s.charAt(9) == ':') { ss = s.substring(10,12).toInt(); }
    inDEC = (dd*3600+mi*60+ss)*(s.charAt(3)=='-'?-1:1);
    // FIXME: the below should not be needed anymore since :CM# command is honored
    if (currDEC == NORTH_DEC) { // if currDEC is still the initial default position (North)
      // assume this is to sync current position to new input
      currRA  = inRA;
      currDEC = inDEC;
      updateLx200Coords(currRA, currDEC); // recompute strings
    }
    Serial.print(1); // FIXME: input is not validated
  } else if (s.charAt(1) == 'M') { // MOVE:  :MS# (slew), :Mx# (slow move)
    if (s.charAt(2) == 'S' ) { // SLEW
      printLog("MS");
      // assumes Sr and Sd have been processed hence
      // inRA and inDEC have been set, now it's time to move
      long deltaRaSecs  = currRA-inRA;
      long deltaDecSecs = currDEC-inDEC;
      // FIXME: need to implement checks, but can't wait for slewRaDecBySecs
      //        reply since it may takes several seconds:
      Serial.print(0); // slew is possible 
      // slewRaDecBySecs replies to lx200 polling with current position until slew ends:
      if (slewRaDecBySecs(deltaRaSecs, deltaDecSecs) == 1) { // success         
        currRA  = inRA;
        currDEC = inDEC;
        updateLx200Coords(currRA, currDEC); // recompute strings
      } else { // failure
        Serial.print("1Range_too_big#");
      }
    } else {
      printLog("Mx");
      switch (s.charAt(2)) {
          case 'n':
            moveDecNorth();
            break;
          case 's':
            moveDecSouth();
            break;
          case 'w':
            moveRaWest();
            break;
          case 'e':
            moveRaEast();
            break;
        } // default is ignored;
    }
  } else if (s.charAt(1) == 'Q') { // :Q# or :Qx# stop Dec Motor and set RA to Tracking
    moveDecHalt();
    moveRaTracking();
  } else if (s.substring(1,3).equals("CM")) { // :CM# sync
    // assumes Sr and Sd have been processed
    // sync current position with input
    printLog("CM");
    currRA  = inRA;
    currDEC = inDEC;
    Serial.print("Synced#");
    updateLx200Coords(currRA, currDEC); // recompute strings
  }
}

/* Update lx200 RA&DEC string coords so polling 
 * (:GR# and :GD#) gets processed faster
 */
void updateLx200Coords(long raSecs, long decSecs) {
  unsigned long pp = raSecs/3600;
  unsigned long mi = (raSecs-pp*3600)/60;
  unsigned long ss = (raSecs-mi*60-pp*3600);
  lx200RA = "";
  if (pp<10) lx200RA.concat('0');
  lx200RA.concat(pp);lx200RA.concat(':');
  if (mi<10) lx200RA.concat('0');
  lx200RA.concat(mi);lx200RA.concat(':');
  if (ss<10) lx200RA.concat('0');
  lx200RA.concat(ss);lx200RA.concat('#');

  pp = abs(decSecs)/3600;
  mi = (abs(decSecs)-pp*3600)/60;
  ss = (abs(decSecs)-mi*60-pp*3600);
  lx200DEC = "";
  lx200DEC.concat(decSecs>0?'+':'-');
  if (pp<10) lx200DEC.concat('0');
  lx200DEC.concat(pp);lx200DEC.concat(char(223)); // FIXME: may be just * nowadays
  if (mi<10) lx200DEC.concat('0'); 
  lx200DEC.concat(mi);lx200DEC.concat(':');
  if (ss<10) lx200DEC.concat('0');
  lx200DEC.concat(ss);lx200DEC.concat('#');
 } 

/* 
 * Print current state in serial  
 */
void printInfo() {
  Serial.print("Current Position: ");
  printCoord(currRA, currDEC);
  Serial.print("Side of Pier: ");
  Serial.println(SIDE_OF_PIER_WEST?"W":"E");
  Serial.print("Slow Motion Speed: ");
  Serial.println(SLOW_SPEED);
  Serial.print("Max Range: ");
  Serial.println(MAX_RANGE/60);
  Serial.print("Dec Power Saving: ");
  Serial.println(POWER_SAVING_ENABLED?"enabled":"disabled");
  Serial.print("Version: ");
  Serial.println(_ver);
  #ifdef ST4
  Serial.print("ST4");
  #endif
}

/*
 * aGotino protocol
 */
void agoto(String s) {
  // keywords: debug, sleep, range, speed, side, info
  if (s.substring(1,6).equals("debug")) {
    DEBUG = (s.charAt(0) == '+')?true:false;
    if (DEBUG) Serial.println("Debug On"); 
          else Serial.println("Debug Off"); 
  } else if (s.substring(1,6).equals("sleep")) {
    POWER_SAVING_ENABLED = (s.charAt(0) == '+')?true:false;
    if (POWER_SAVING_ENABLED) {
      Serial.println("Power Saving Enabled");
      digitalWrite(decSleepPin, LOW); // sleep (goto or button dec movement will awake it for just the time of the movement)
    } else {
      Serial.println("Power Saving Disabled");
      digitalWrite(decSleepPin, HIGH); // wake up
    }
                         
  } else if (s.substring(1,6).equals("range")) {
    int d = (s.charAt(0) == '+')?15:-15;
    if (MAX_RANGE+d > 0 ) {
      MAX_RANGE = MAX_RANGE+d*60;
      Serial.println("New max range set to degrees:");
      Serial.println(MAX_RANGE/60);
    } else {
      Serial.println("Can't set range to zero");
    }
  } else if (s.substring(1,6).equals("speed")) {
    int d = (s.charAt(0) == '+')?(SLOW_SPEED_INC):(-SLOW_SPEED_INC);
    if (SLOW_SPEED+d > 0 ) {
      SLOW_SPEED  = SLOW_SPEED+d;
      decTargetDelay = STEP_DELAY/SLOW_SPEED; 
      Serial.println("Slow motion RA&Dec speed set to");
      Serial.println(SLOW_SPEED);
    } else {
      Serial.println("Can't set speed to zero");
    }
  } else if (s.substring(1,5).equals("side")) {
    changeSideOfPier();
    Serial.print("Side of Pier: ");
    Serial.println(SIDE_OF_PIER_WEST?"W":"E");
  } else if (s.substring(1,5).equals("info")) {
    printInfo();
  } else { // Move, Set or Goto commands

    long deltaRaSecs  = 0; // secs to move RA 
    long deltaDecSecs = 0; // secs to move Dec

    if ((s.charAt(0) == '+' || s.charAt(0) == '-') && (s.charAt(5) == '+' || s.charAt(5) == '-')) { // rRRRRdDDDD (r and d are signs) - Move by rRRRR and dDDDD deg mins
      // toInt() returns 0 if conversion fails, logic belows detects this
      if (!s.substring(1, 5).equals("0000")) {
        deltaRaSecs = s.substring(1, 5).toInt() * (s.charAt(0) == '+' ? +1 : -1) * 4;
        if (deltaRaSecs == 0) { Serial.println("RA conversion error"); return; }
      }
      if (!s.substring(6, 10).equals("0000")) {
        deltaDecSecs = s.substring(6, 10).toInt() * (s.charAt(5) == '+' ? +1 : -1) * 60;
        if (deltaDecSecs == 0) { Serial.println("Dec conversion error"); return; }
      }
      long tmp_inRA = currRA + deltaRaSecs;  // sign reversed to honor result (E > 0)
      long tmp_inDEC = currDEC + deltaDecSecs;
      if ( tmp_inRA<0 || tmp_inRA>86400 || abs(tmp_inDEC)>324000 ) {
        printLogL(tmp_inRA);
        printLogL(tmp_inDEC);
        Serial.println("Values out of range"); return; 
      } else {
        inRA = tmp_inRA ;
        inDEC = tmp_inDEC;      
      }
    } else { // decode coords to inRA and inDEC
      if (s.charAt(1) == 'M' || s.charAt(1) == 'm') { // MESSIER coords
        int m = s.substring(2,5).toInt(); // toInt() returns 0 if conversion fails
        if (m == 0 || m > 110) { Serial.println("Messier number error"); return; } 
        // this would fail from progmem: inRA =  Messier[m].ra;
        inRA = (long) pgm_read_dword(&(Messier[m].ra));
        inDEC= (long) pgm_read_dword(&(Messier[m].dec));
        Serial.print(s[0]=='s'?"Set ":"Goto ");
        Serial.print("M");Serial.println(m);
      } else if (s.charAt(1) == 'S' || s.charAt(1) == 's') { // STARS coords
        int n = s.substring(2,5).toInt(); // toInt() returns 0 if conversion fails
        if (n < 0 || n > 244) { Serial.println("Star number error"); return; } 
        inRA = (long) pgm_read_dword(&(Stars[n].ra));
        inDEC= (long) pgm_read_dword(&(Stars[n].dec));
        Serial.print(s[0]=='s'?"Set ":"Goto ");
        Serial.print("Star ");Serial.println(n);
      } else if (s.charAt(1) == 'N' || s.charAt(1) == 'n') { // NGC coords
        int n = s.substring(2,6).toInt(); // toInt() returns 0 if conversion fails
        if (n < 0 || n > 7840) { Serial.println("NGC number error"); return; } 
        int ngcElem = ngcLookup(n);
        if (ngcElem>0) {
          inRA  = (long) pgm_read_dword(&(NGCs[ngcElem].ra ));
          inDEC = (long) pgm_read_dword(&(NGCs[ngcElem].dec));
          Serial.print(s[0]=='s'?"Set ":"Goto ");
          Serial.print("NGC");Serial.println(n);
        } else {
          Serial.println("NGC item not in list");
          return;
        }
      } else { // HHMMSSdDDMMSS coords
        inRA  = s.substring(1, 3).toInt() * 60 * 60 + s.substring(3, 5).toInt() * 60 + s.substring(5, 7).toInt();
        inDEC = (s.charAt(7) == '+' ? 1 : -1) * (s.substring(8, 10).toInt() * 60 * 60 + s.substring(10, 12).toInt() * 60 + s.substring(12, 14).toInt());
        if (inRA == 0 || inDEC == 0) { Serial.println("Coordinates error"); return; }
      }
      
      // inRA&inDEC are set
      if (s.charAt(0) == 's') { // set
          currRA  = inRA;
          currDEC = inDEC;
          Serial.print("Current Position: ");
          printCoord(currRA, currDEC);
          printLog("RA/DEC in secs:");
          printLogL(currRA);
          printLogL(currDEC);
        } else { // goto
          deltaRaSecs  = currRA-inRA;
          deltaDecSecs = currDEC-inDEC;
        }
      }

      // deltaRaSecs and deltaDecSecs are not zero if slew is needed
      if (deltaRaSecs != 0 || deltaDecSecs != 0) { 
        printLog("delta RA/DEC in secs:");
        printLogL(deltaRaSecs);
        printLogL(deltaDecSecs);
        /* SLEW! */
        Serial.println(" *** moving...");
        if ( slewRaDecBySecs(deltaRaSecs, deltaDecSecs) == 1) { // success
          Serial.println(" *** done");
          // set new current position
          currRA  = inRA;
          currDEC = inDEC;
          Serial.print("Current Position: ");
          printCoord(currRA, currDEC);
        } else{ 
          Serial.println("Values exceed max range (deg) of:");
          Serial.println(MAX_RANGE/60);
        }
      }
  }
}

// Print nicely formatted coords
void printCoord(long raSecs, long decSecs) {
  long pp = raSecs/3600;
  Serial.print(pp);
  Serial.print("h");
  long mi = (raSecs-pp*3600)/60;
  if (mi<10) Serial.print('0');
  Serial.print(mi);
  Serial.print("'");
  long ss = (raSecs-mi*60-pp*3600);
  if (ss<10) Serial.print('0');
  Serial.print(ss);
  Serial.print("\" ");
  pp = abs(decSecs)/3600;
  Serial.print((decSecs>0?pp:-pp));
  Serial.print("°");
  mi = (abs(decSecs)-pp*3600)/60;
  if (mi<10) Serial.print('0');
  Serial.print(mi);
  Serial.print("'");
  ss = (abs(decSecs)-mi*60-pp*3600);
  if (ss<10) Serial.print('0');
  Serial.print(ss);
  Serial.println("\"");
}

/* Change Side of Pier - reverse Declination direction
 *  invoked when both buttons are pressed for 1 sec or with "+pier" command
 *  Default Side of Pier is WEST, i.e. scope is West of mount
 */
void changeSideOfPier() {
  SLEWING = true; // stop RA tracking hence give a "sound" feedback (motor stops)
  SIDE_OF_PIER_WEST = !SIDE_OF_PIER_WEST;

  // invert DEC direction
  DEC_DIR = (DEC_DIR==HIGH?LOW:HIGH);
  
  // visual feedback
  if (SIDE_OF_PIER_WEST) { // turn on led for 3 secs
    digitalWrite(LED_BUILTIN, HIGH); 
    delay(3000);digitalWrite(LED_BUILTIN, LOW);
  } else { // blink led twice for 1 sec
    digitalWrite(LED_BUILTIN, HIGH);
    delay(1000); digitalWrite(LED_BUILTIN, LOW);
    delay(1000); digitalWrite(LED_BUILTIN, HIGH);
    delay(1000); digitalWrite(LED_BUILTIN, LOW);
  }
  bothPressTime = 0; // this is to force another full sec to change again

  // reset motors
  moveRaTracking();
  moveDecHalt();
  
  SLEWING = false;
}

/*
 * Slow motion movement and tracking/halt 
 */
 
void moveRaWest() {
      raState = RA_WEST;
      setRaTimer(CMR/SLOW_SPEED); // set interrupt to slow motion speed
      digitalWrite(raDirPin, RA_DIR); // set RA_DIR (W)
}

void moveRaEast() {
      raState = RA_EAST;
      setRaTimer(CMR/SLOW_SPEED); // set interrupt to slow motion speed
      digitalWrite(raDirPin, (RA_DIR==HIGH?LOW:HIGH)); // set RA_DIR (E)
}

void moveRaTracking() {
      raState = RA_TRACKING;
      setRaTimer(CMR); // re-set interrupt to tracking speed
      digitalWrite(raDirPin, RA_DIR);
}

void moveDecNorth() {
      decState = DEC_NORTH;
      decSleep(false); // awake motor
      digitalWrite(decDirPin, DEC_DIR);
}

void moveDecSouth() {
      decState = DEC_SOUTH;
      decSleep(false); // awake motor
      digitalWrite(decDirPin, (DEC_DIR==HIGH?LOW:HIGH));
}

void moveDecHalt() {
      decState = DEC_HALT; // stop
      decSleep(true); // sleep
}


/*
 * main loop
 */
 
void loop() {
  
  // move Dec if needed
  if (decState != DEC_HALT) {
    decPlay();
  }

  // when both buttons are pressed for 1 sec, change side of pier
  if ( (digitalRead(raButtonPin) == LOW) && digitalRead(decButtonPin) == LOW) {
    if (bothPressTime == 0) { bothPressTime = micros(); }
    if ( (micros() - bothPressTime) > (1000000) ) { 
      changeSideOfPier();
    }
  } else {
    // if both buttons are not pressed, reset timer to 0
    bothPressTime = 0;  
  }
  
  // raButton pressed: skip if within 300ms from last press
  if ( (digitalRead(raButtonPin) == LOW)  && (micros() - raPressTime) > (300000) ) {
    raPressTime = micros();
    printLog("RA Slow Motion");
    // Cycle among: +1x -> -SLOW_SPEED (E) -> +SLOW_SPEED (W)
    if (raState == RA_TRACKING) {
      moveRaEast(); // reverse from default direction
    } else if  (raState == RA_EAST) {
      moveRaWest(); // back to default direction
    } else if  (raState == RA_WEST) {
      moveRaTracking(); // Note: direction stays default one (west) so no impact if backlash exists
    }
  }
  
  // decButton pressed: skip if within 300ms from last press
  if (digitalRead(decButtonPin) == LOW && (micros() - decPressTime) > (300000) ) {
    decPressTime = micros();
    printLog("Dec Slow Motion");
    // Cycle speed among: 0x -> +SLOW_SPEED -> -(SLOW_SPEED-)
    // note: actual speed is driven by decTargetDelay
    if (decState == DEC_HALT) {
      moveDecNorth();
    } else if (decState == DEC_NORTH) {
      moveDecSouth();
    } else if  (decState == DEC_SOUTH) {
      moveDecHalt();
    }
    decStepDelay = MAX_DELAY; // set initial pulse length (max possible to then accellerate)
    decPlayIdx = 0;           // set initial pulse index  (used for accelleration in the first 100 pulses)
    decLastTime = micros();
  }

  #ifdef ST4
  if (digitalRead(st4NorthPin) == LOW || digitalRead(st4SouthPin) == LOW) { // Pulse in DEC - N or S direction
    if (decState != DEC_ST4) { // if pulse is not already ongoing
      // set pulse is ongoing:
      decState = DEC_ST4;     // this allows loop() to call decPlay(), real speed is determined by decStepDelay:
      boolean isNorth = (digitalRead(st4NorthPin) == LOW )?true:false;
      digitalWrite(decDirPin, isNorth?DEC_DIR: (DEC_DIR==HIGH?LOW:HIGH));
      /*
       * Set speed for Dec guide correction: 
       */
      decStepDelay = STEP_DELAY*ST4_PULSE_FACTOR/10; // factor up, period increase, freq decrese (slower)
      
      printLog("ST4: DEC Start, new Pulse:");
      printLogUL(decStepDelay);
      if (isNorth) {printLog("ST4: North");} else {printLog("ST4: South");}
    }
  } else { // no ST4 DEC pulse
    if (decState == DEC_ST4) { // halt DEC if it was moving due to ST4 pulse
      moveDecHalt();
      printLog("ST4: Dec Halted");
    }
  }
  if (digitalRead(st4EastPin) == LOW || digitalRead(st4WestPin) == LOW) { // Pulse in RA - W or E direction
    if (raState != RA_ST4) { // if pulse is not ongoing
      raState = RA_ST4; // set oulse ongoing
      boolean isWest = (digitalRead(st4WestPin) == LOW)?true:false;
      unsigned int pulseCMR = 0;

      if (isWest) { 
        pulseCMR = (unsigned long) CMR*ST4_PULSE_FACTOR/(ST4_PULSE_FACTOR+10); // decreases pulseCRM, freq increases => increase tracking speed ("move" west)
      } else {
        pulseCMR = CMR+(unsigned long)2*CMR*10/ST4_PULSE_FACTOR; // increases pulseCRM, freq decreases => decrease tracking speed ("move" east)
      }
      /* 
       * Set speed of RA guide correction 
       */
      setRaTimer(pulseCMR); 
      
      printLog("ST4: RA Start, new CRM:");
      printLogUL(pulseCMR);
      if (isWest) {printLog("ST4: West");} else {printLog("ST4: East");}
    }
  } else { // no ST4 RA Pulse
    if (raState == RA_ST4) { // Re-set to tracking if it was moving due to ST4 pulse
      moveRaTracking();
      printLog("ST4: RA Tracking");
    }
  }
  #endif

  // Check if message on serial input
  if (Serial.available() > 0) {
    input[in] = Serial.read(); 

    // discard blanks. Meade LX200 specs states :Sd and :Sr are
    // not followed by a blank but some implementation does include it.
    // also this allows aGoto commands to be typed with blanks
    if (input[in] == ' ') return; 
    
    // acknowledge LX200 ACK signal (char(6)) for software that tries to autodetect protocol (i.e. Stellarium Plus)
    if (input[in] == char(6)) { Serial.print("P"); return; } // P = Polar

    if (input[in] == '#' || input[in] == '\n') { // after a # or a \n it is time to check what is in the buffer
      if ((input[0] == '+' || input[0] == '-' 
        || input[0] == 's' || input[0] == 'g')) { // it's agoto protocol
        agoto(input);
      } else if (input[0] == ':') { // it's lx200 protocol
        printLog(input);
        lx200(input);
      } else {
        // unknown command, print message only
        // if buffer contains more than one char
        // since stellarium seems to send extra #'s
        if (in > 0) {
          String s = input;
          Serial.print(s.substring(0,in));
          Serial.println(" unknown. Expected lx200 or aGotino commands");
        }
      }
      in = 0; // reset buffer // WARNING: the whole input buffer is passed anyway
    } else {
      if (in++>20) in = 0; // prepare for next char or reset buffer if max lenght reached
    } 
  }
}

// Helpers to write on serial when DEBUG is active
void printLog(  String s)         { if (DEBUG) { Serial.print(":");Serial.println(s); } }
void printLogL( long l)           { if (DEBUG) { Serial.print(":");Serial.println(l); } }
void printLogUL(unsigned long ul) { if (DEBUG) { Serial.print(":");Serial.println(ul);} }
