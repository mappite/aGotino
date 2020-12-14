/*
   aGotino a simple Goto with Arduino (Nano/Uno)

      * tracking 
      * cycle among forward and backward speeds on RA&Dec at buttons press
      * listen on serial port for basic LX200 commands (INDI LX200 Basic)
      * listen on serial port for aGotino commands

    Command set and new code versions at https://github.com/mappite/aGotino
    
    by gspeed @ astronomia.com / qde / cloudynights.com forum
    This code is free software under GPL v3 License use at your risk and fun ;)

 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *

   How to calculate STEP_DELAY to drive motor at right sidereal speed for your mount

   Worm Ratio                  144   // 144 eq5/exos2, 135 heq5, 130 eq3-2
   Other (Pulley/Gear) Ratio     2.5 // depends on your pulley setup e.g. 40T/16T = 2.5
   Steps per revolution        400   // or usually 200 depends on your motor
   Microstep                    32   // depends on driver

   MICROSTEPS_PER_DEGREE_RA  12800   // = WormRatio*OtherRatio*StepsPerRevolution*Microsteps/360
                                     // = number of microsteps to rotate the scope by 1 degree

   STEP_DELAY                18699   // = (86164/360)/(MicroSteps per Degree)*1000000
                                     // = microseconds to advance a microstep at 1x
                                     // 86164 is the number of secs for earth 360deg rotation (23h56m04s)

 * Update the values below to match your mount/gear ratios and your preferences: 
 * * * * * * */

const unsigned long MICROSTEPS_PER_DEGREE_RA = 12800; // see above calculation
const unsigned long STEP_DELAY = 18699;               // see above calculation

const unsigned long MICROSTEPS_PER_DEGREE_DEC = MICROSTEPS_PER_DEGREE_RA; // calculate correct value if DEC gears/worm/microsteps differs
const unsigned long MICROSTEPS = 32;    // Driver Microsteps per step

const long SERIAL_SPEED = 9600;         // serial interface baud. Make sure your computer or phone match this
long          MAX_RANGE = 1800;         // max allowed movement range in deg minutes (1800'=30°).

// Motor clockwise direction: HIGH is as per original design
int RA_DIR   = HIGH;                    // set to LOW to reverse default direction
int DEC_DIR  = HIGH;                    // set to LOW to reverse default direction

unsigned int  RA_FAST_SPEED   = 8;      // RA  speed at button press, times the sidereal speed
unsigned int  DEC_FAST_SPEED  = 8;      // DEC speed at button press
unsigned long STEP_DELAY_SLEW = 1200;   // Step pulse timing in micros when slewing (the higher the slower)

boolean SIDE_OF_PIER_WEST     = true;   // Default Telescope position is west of the mount. Press both buttons for 1sec to reverse
boolean POWER_SAVING_ENABLED  = true;   // toggle with -sleep on serial, see decSleep()
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

/*
 * It is safe to keep the below untouched
 * 
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <catalogs.h> // load messier objects and others

// Number of Microsteps to move RA by 1hour
const unsigned long MICROSTEPS_PER_HOUR  = MICROSTEPS_PER_DEGREE_RA * 360 / 24;

// Compare Match Register for RA interrupt (PRESCALER=8)
const int CMR = (STEP_DELAY*16/8/2)-1; 

unsigned long raPressTime  = 0;    // time since when RA  button is pressed
unsigned long decPressTime = 0;    // time since when DEC button is pressed
unsigned long bothPressTime= 0;    // time since when both buttons are pressed, used for change of pier

unsigned long decLastTime  = 0;    // last time DEC pulse has changed status
boolean raStepPinStatus    = false;// true = HIGH, false = LOW
boolean decStepPinStatus   = false;// true = HIGH, false = LOW

boolean SLEWING = false; // true while it is slewing to block AR tracking

const long DAY_SECONDS =  86400; // secs in a day
const long NORTH_DEC   = 324000; // 90°

// Current and input coords in Secs
long currRA = 0;     
long currDEC = NORTH_DEC;
long inRA  = 0;
long inDEC = 0;

int raSpeed  = 1;    // default RA speed (start at 1x to follow stars)
int decSpeed = 0;    // default DEC speed (don't move)

// Serial Input
char input[20];     // stores serial input
int  in = 0;        // current char in serial input

// Current position in Meade lx200 format, see updateLx200Coords()
String lx200RA = "00:00:00#";
String lx200DEC= "+90*00:00#";

void setup() {
  Serial.begin(SERIAL_SPEED);
  Serial.print("aGotino:");
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
  // init led and turn on for 0.5 sec
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(500);
  digitalWrite(LED_BUILTIN, LOW);
  /* define interrupt for RA motor, start tracking: */
  initRaTimer(CMR);
  lx200DEC[3] = char(223); // set correct char in string...
  Serial.println(" ready.");
}

/* 
 *  Init Timer to trigger RA driver pulse
 *   invoked in setup() and when RA speed changes on button press
 */
void initRaTimer(int cmr) {
  /*  STEP_DELAY/2 = 9349 => 1000000/9349 =  106.9575Hz 
   *  CMR = 16000000/(prescaler*(1000000/(STEP_DELAY/2)))-1;
   *  => CMR = 145.0959 with prescaler=1024, 18699  with prescaler=0
   *  let's go for the finest one
   */
  /* Using 8-but Timer 2 (CMR less than 256)
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

/* 
 * Change RA pulse (move RA)
 *  Interrupt function invoked when timer register reaches cmr 
 */
ISR(TIMER1_COMPA_vect){
  if (!SLEWING) { // change pulse only if not slewing
    raStepPinStatus = !raStepPinStatus;
    digitalWrite(raStepPin, (raStepPinStatus ? HIGH : LOW));
  }
}


/* DEC move */
void decPlay(unsigned long stepDelay) {
  unsigned long halfStepDelay = stepDelay/2; // FIXME: implement accelleration to avoig glitch
  unsigned long dt  = micros() - decLastTime;

  if ( (halfStepDelay - dt) < (200) ) { // less than 200 micros, let's hold and change pulse
    delayMicroseconds(halfStepDelay - dt);
    decStepPinStatus = !decStepPinStatus;
    digitalWrite(decStepPin, (decStepPinStatus ? HIGH : LOW));
    decLastTime = micros(); // reset time
  } else if ( dt > halfStepDelay ) { // too late!
    // reset time but don't output any error message in serial
    decLastTime = micros();
  }
}

/*
 *  Slew RA and Dec by seconds (Hours and Degree secs)
 *   motors direction is set according to sign
 *   RA 1x direction is reset at the end
 *   microstepping is disabled for fast movements and (re-)enabled for finer ones
 */
int slewRaDecBySecs(long raSecs, long decSecs) {

  // If more than 12h turn from the opposite side
  if (abs(raSecs) > DAY_SECONDS/2) { // reverse
    printLog("RA reversed, new RA secs:");
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

  // FIXME: detect if direction has changed and add backlash steps

  // calculate how many micro-steps are needed
  unsigned long raSteps  = (abs(raSecs) * MICROSTEPS_PER_HOUR) / 3600;
  unsigned long decSteps = (abs(decSecs) * MICROSTEPS_PER_DEGREE_DEC) / 3600;

  // calculate how many full&micro steps are needed
  unsigned long raFullSteps   = raSteps / MICROSTEPS;             // this will truncate the result...
  unsigned long raMicroSteps  = raSteps - raFullSteps * MICROSTEPS; // ...remaining microsteps
  unsigned long decFullSteps  = decSteps / MICROSTEPS;            // this will truncate the result...
  unsigned long decMicroSteps = decSteps - decFullSteps * MICROSTEPS; // ...remaining microsteps

  // Disable microstepping (enable full steps)
  printLog("Disabling Microstepping");
  digitalWrite(raEnableMicroStepsPin, LOW);
  digitalWrite(decEnableMicroStepsPin, LOW);

  // Fast Move (full steps)
  printLog(" RA FullSteps:  ");
  printLogUL(raFullSteps);
  printLog(" DEC FullSteps: ");
  printLogUL(decFullSteps);
  unsigned long slewTime = micros(); // record when slew code starts, RA 1x movement will be on hold hence we need to add the gap later on
  slewRaDecBySteps(raFullSteps, decFullSteps);
  printLog("FullSteps Slew Done");

  // re-enable micro stepping
  printLog("Re-enabling Microstepping");
  digitalWrite(raEnableMicroStepsPin, HIGH);
  digitalWrite(decEnableMicroStepsPin, HIGH);

  // Final Adjustment
  // Note: this code is likley superflous since precision is 6.66 full steps per minute,
  //       i.e. one full step is already less than 1/6', i.e. 10". Who cares for such small offset?
  printLog(" RA MicroSteps:");
  printLogUL(raMicroSteps);
  printLog(" DEC MicroSteps:");
  printLogUL(decMicroSteps);
  slewRaDecBySteps(raMicroSteps, decMicroSteps);
  printLog("MicroSteps Slew Done");

  // If slewing took more than 5 secs, adjust RA
  slewTime = micros() - slewTime; // time elapsed for slewing
  if ( slewTime > (5 * 1000000) ) {
    printLog("*** adjusting Ra by secs: ");
    printLogUL(slewTime / 1000000);
    slewRaDecBySecs(slewTime / 1000000, 0); // it's the real number of seconds!
    printLog("*** adjusting Ra done");
  }

  // reset RA to right sidereal direction
  digitalWrite(raDirPin,  RA_DIR);

  // Success
  return 1;
}

/*
 *  Slew RA and Dec by steps
 *   . assume direction and microstepping is set
 *   . listen on serial port and reply to lx200 GR&GD
 *     commands with current (initial) position to avoid
 *     INDI timeouts during long slewings actions
 *   . set SLEWING to true to hold RA tracking
 *   . turn system led on 
 */
void slewRaDecBySteps(unsigned long raSteps, unsigned long decSteps) {
  digitalWrite(LED_BUILTIN, HIGH);
  SLEWING = true;

  // wake up Dec motor if needed
  if (decSteps != 0) {
    decSleep(false);
  }

  unsigned long delaySlew = 0; 
  unsigned long delayLX200Micros = 0; // mesure delay introduced by LX200 polling reply
  in = 0; // reset the  input buffer read index

  unsigned long MAX_DELAY = 16383; // limit of delayMicroseconds()
  
  for (unsigned long i = 0; (i < raSteps || i < decSteps) ; i++) {
    if ((i<100)) { // Accellerate during inital 100 steps from MAX_DELAY to STEP_DELAY_SLEW
      delaySlew = MAX_DELAY-( (MAX_DELAY-STEP_DELAY_SLEW)/100*i);
    } else if ( (i>raSteps-100 && i<raSteps)|| (i>decSteps-100 && i<decSteps)) {
      delaySlew = STEP_DELAY_SLEW*2;// twice as slow in last 100 steps before a motor is about to stop 
      // FIXME: implement nice accelleration as in initial 100 steps
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
      if (input[in] == '#' ) {
        if (in>1 && input[in-1] == 'R') { // :GR#
          Serial.print(lx200RA);
        } else if (in>2 && input[in-1] == 'D') { // :GD#
          Serial.print(lx200DEC);
        }
        in = 0;
      } else {
        if (in++ >5) in = 0;
      }
      delayLX200Micros = micros()-delayLX200Micros;
      if (delayLX200Micros>delaySlew) Serial.println("LX200 polling slows too much!");
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
   FIXME: issue with glitces at wake up with DRV8825
          motor resets to home position (worst case 4 full steps, i.e. less than a minute)
    https://forum.pololu.com/t/sleep-reset-problem-on-drv8825-stepper-controller/7345
    https://forum.arduino.cc/index.php?topic=669304.0
    it seems this gets solved by accellerating
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
 *  Basic Meade LX200 Protocol support:
 *    reply to #:GR# and #:GD# with current RA (currRA) and DEC (currDEC) position
 *    get #:Q#:Sr... to set input RA (inRA) and :Sd... to set input DEC (inDEC)
 *    if currDEC is NORTH, 
 *       assume this is the very first set operation and set current DEC&RA to input DEC&RA
 *    else 
 *       calculate deltas and slew to inRA and inDEC
 *     
 *  An lx200 session with Stellarium is (> = from Stellarium to Arduino)
> #:GR#
< 02:31:49#
> #:GD#
< +89ß15:51#
> #:Q#:Sr 11:22:33#
< 1
> :Sd +66*55:44#
< 1
> :MS#
> #:GR#
< 05:04:21#
> #:GD#
< +81ß11:39#
*/
void lx200(String s) { // all :.*# commands are passed 
  if (s.substring(1,3).equals("GR")) { // :GR# 
    printLog("GR");
    // send current RA to computer
    Serial.print(lx200RA);
  } else if (s.substring(1,3).equals("GD")) { // :GD# 
    printLog("GD");
    // send current DEC to computer
    Serial.print(lx200DEC);
  } else if (s.substring(1,3).equals("Sr")) { // :SrHH:MM:SS# // no blanks after :Sr as per Meade specs
    printLog("Sr");
    // this is INITAL step for setting position (RA)
    long hh = s.substring(3,5).toInt();
    long mi = s.substring(6,8).toInt();
    long ss = s.substring(9,11).toInt();
    inRA = hh*3600+mi*60+ss;
    Serial.print(1);
  } else if (s.substring(1,3).equals("Sd")) { // :SdsDD*MM:SS# // blanks after :Sd
    printLog("Sd");
    // this is the FINAL step of setting a pos (DEC) 
    long dd = s.substring(4,6).toInt()*(s.charAt(3)=='-'?-1:1);
    long mi = s.substring(7,9).toInt();
    long ss = s.substring(10,12).toInt();
    inDEC = dd*3600+mi*60+ss;
    if (currDEC == NORTH_DEC) { // if currDEC is still the initial default position (North)
      // assume this is to sync current position to new input
      currRA  = inRA;
      currDEC = inDEC;
      updateLx200Coords(currRA, currDEC); // recompute strings
    } 
    Serial.print(1);
  } else if (s.substring(1,3).equals("MS")) { // :MS# move
    printLog("MS");
    // assumes Sr and Sd have been processed
    // inRA and inDEC have been set, now it's time to move
    long deltaRaSecs  = currRA-inRA;
    long deltaDecSecs = currDEC-inDEC;
    // FIXME: need to implement checks, can't wait for slewRaDecBySecs
    //        reply since it may takes several seconds:
    Serial.print(0); // slew is possible 
    // slewRaDecBySecs reply to lx200 polling with current position until slew ends:
    if (slewRaDecBySecs(deltaRaSecs, deltaDecSecs) == 1) { // success         
      currRA  = inRA;
      currDEC = inDEC;
      updateLx200Coords(currRA, currDEC); // recompute strings
    } else { // failure
      Serial.print("1Range_too_big#");
    }
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
  Serial.print("Micro Speed: ");
  Serial.println(RA_FAST_SPEED);
  Serial.print("Max Range: ");
  Serial.println(MAX_RANGE/60);
  Serial.print("Sleep: ");
  Serial.println(POWER_SAVING_ENABLED?"enabled":"disabled");
}

/*
 * aGoto simple protocol
 */
void agoto(String s) {
  // keywords: debug, sleep, range, speed, side, info
  if (s.substring(1,6).equals("debug")) {
    DEBUG = (s.charAt(0) == '+')?true:false;
    if (DEBUG) Serial.println("Debug On"); 
          else Serial.println("Debug Off"); 
  } else if (s.substring(1,6).equals("sleep")) {
    POWER_SAVING_ENABLED = (s.charAt(0) == '+')?true:false;
    if (POWER_SAVING_ENABLED) Serial.println("Power Saving Enabled");
                         else Serial.println("Power Saving Disabled");
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
    int d = (s.charAt(0) == '+')?4:-4;
    if (RA_FAST_SPEED+d > 0 ) {
      RA_FAST_SPEED  = RA_FAST_SPEED+d;
      DEC_FAST_SPEED = DEC_FAST_SPEED+d;
      Serial.println("New fast RA&Dec speed set to");
      Serial.println(RA_FAST_SPEED);
    } else {
      Serial.println("Can't set speed to zero");
    }
  } else if (s.substring(1,5).equals("side")) {
    changeSideOfPier();
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
        if (m == 0 || m > 110) { Serial.println("Messier number conversion error"); return; } 
        // this would fail from progmem: inRA =  Messier[m].ra;
        inRA = (long) pgm_read_dword(&(Messier[m].ra));
        inDEC= (long) pgm_read_dword(&(Messier[m].dec));
        Serial.print(s[0]=='s'?"Set ":"Goto ");
        Serial.print("M");Serial.println(m);
      } else if (s.charAt(1) == 'S' || s.charAt(1) == 's') { // STARS coords
        int n = s.substring(2,5).toInt(); // toInt() returns 0 if conversion fails
        if (n < 0 || n > 244) { Serial.println("Star number conversion error"); return; } 
        inRA = (long) pgm_read_dword(&(Stars[n].ra));
        inDEC= (long) pgm_read_dword(&(Stars[n].dec));
        Serial.print(s[0]=='s'?"Set ":"Goto ");
        Serial.print("Star ");Serial.println(n);
      } else if (s.charAt(1) == 'N' || s.charAt(1) == 'n') { // NGC coords
        int n = s.substring(2,6).toInt(); // toInt() returns 0 if conversion fails
        if (n < 0 || n > 7840) { Serial.println("NGC number conversion error"); return; } 
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
        if (inRA == 0 || inDEC == 0) { Serial.println("Coordinates conversion error"); return; }
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
          Serial.println("Values exceed max allowed range of degrees:");
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
 *  invoked when both buttons are pressed for 1sec or with "+pier" command
 *  Default Side of Pier is WEST, i.e. scope is West of mount pointing a target on East side
 */
void changeSideOfPier() {
  SLEWING = true; // stop RA tracking hence give a "sound" feedback (motor stops)
  SIDE_OF_PIER_WEST = !SIDE_OF_PIER_WEST;
  Serial.print("Side of Pier: "); // FIXME: hope this does not create errors with LX200
  Serial.println(SIDE_OF_PIER_WEST?"W":"E");

  // invert DEC direction
  DEC_DIR = (DEC_DIR==HIGH?LOW:HIGH);
  
  // visual feedback
  if (SIDE_OF_PIER_WEST) { // turn on led 1sec
    digitalWrite(LED_BUILTIN, HIGH); 
    delay(3000);digitalWrite(LED_BUILTIN, LOW);
  } else { // blink led twice
    digitalWrite(LED_BUILTIN, HIGH);
    delay(1000); digitalWrite(LED_BUILTIN, LOW);
    delay(1000); digitalWrite(LED_BUILTIN, HIGH);
    delay(1000); digitalWrite(LED_BUILTIN, LOW);
  }
  bothPressTime = 0; // force another full sec to change again
  SLEWING = false;
}

void loop() {
  
  // Move Dec if needed
  if (decSpeed != 0) {
    decPlay(STEP_DELAY / decSpeed); // fixme, use a timer as per RA
  }

  // when both buttons are pressed for 1sec, change side of pier
  if ( (digitalRead(raButtonPin) == LOW) && digitalRead(decButtonPin) == LOW) {
    if (bothPressTime == 0) bothPressTime = micros();
    if ( (micros() - bothPressTime) > (1000000) ) { changeSideOfPier(); }
  } else {
    // if both buttons are pressed for less than 1 secs, reset timer to 0
    bothPressTime = 0;  
  }
  
  // raButton pressed: skip if within 300ms from last press
  if ( (digitalRead(raButtonPin) == LOW)  && (micros() - raPressTime) > (300000) ) {
    raPressTime = micros();
    printLog("RA Speed: ");
    // 1x -> +RA_FAST_SPEED -> -(RA_FAST_SPEED-2)
    if (raSpeed == 1) {
      raSpeed = RA_FAST_SPEED;
      digitalWrite(raDirPin, RA_DIR);
    } else if  (raSpeed == RA_FAST_SPEED) {
      raSpeed = (RA_FAST_SPEED - 2);
      digitalWrite(raDirPin, (RA_DIR==HIGH?LOW:HIGH)); // change direction
    } else if  (raSpeed == (RA_FAST_SPEED - 2)) {
      raSpeed = 1;
      digitalWrite(raDirPin, RA_DIR);
    }
    initRaTimer(CMR/raSpeed);
    printLogUL(raSpeed);
  }
  
  // decButton pressed: skip if within 500ms from last press
  if (digitalRead(decButtonPin) == LOW && (micros() - decPressTime) > (300000) ) {
    decPressTime = micros();  // time when button has been pressed
    printLog("Dec Speed: ");
    // 0x -> +DEC_FAST_SPEED -> -(DEC_FAST_SPEED-1)
    if (decSpeed == 0) {
      decSpeed = DEC_FAST_SPEED;
      decSleep(false); // awake it
      digitalWrite(decDirPin, DEC_DIR);
    } else if (decSpeed == DEC_FAST_SPEED) {
      decSpeed = (DEC_FAST_SPEED - 1);
      digitalWrite(decDirPin, (DEC_DIR==HIGH?LOW:HIGH));
    } else if  (decSpeed == (DEC_FAST_SPEED - 1)) {
      decSpeed = 0; // stop
      decSleep(true); // go to low power consumption
    }
    printLogUL(decSpeed);
    decLastTime = micros(); // reset Dec time
  }

  // Check if message on serial input
  if (Serial.available() > 0) {
    input[in] = Serial.read(); 

    // discard blanks. Meade LX200 specs states :Sd and :Sr are
    // not followed by a blank but some implementation do...
    // also this allows aGoto commands to be typed with blanks in
    if (input[in] == ' ') return; 
    
    if (input[in] == '#' || input[in] == '\n') { // time to check what is in the buffer
      if ((input[0] == '+' || input[0] == '-' 
        || input[0] == 's' || input[0] == 'g')) { // agoto
        agoto(input);
      } else if (input[0] == ':') { // lx200
        printLog(input);
        lx200(input);
      } else {
        // unknown command, print message only
        // if buffer contains more than one char
        // since stellarium seems to send extra #'s
        if (in > 0) Serial.println("String unknown. Expected lx200 or aGoto commands");
      }
      in = 0; // reset buffer // FIXME!!! the whole input buffer is passed anyway
    } else {
      if (in++>20) in = 0; // prepare for next char or reset buffer if max lenght reached
    }
    
  }

}

// Helpers to write on serial when DEBUG is active
void printLog(  String s)         { if (DEBUG) { Serial.print(":::");Serial.println(s); } }
void printLogL( long l)           { if (DEBUG) { Serial.print(":::");Serial.println(l); } }
void printLogUL(unsigned long ul) { if (DEBUG) { Serial.print(":::");Serial.println(ul);} }
