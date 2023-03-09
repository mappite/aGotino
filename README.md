# aGotino
**A telescope Goto solution based on Arduino** (Nano, Uno or up).

aGotino can be controlled via bluetooth or USB from a mobile device or PC/Mac. Point the scope to something you can easily find (a bright star), align/sync with it, and then reach a remote, low magnitude object nearby. Star alignment procedures are _not_ required, you can move and rotate your scope freely, until you need that extra help. 

No additional boards needed, just wire Arduino and two stepper Drivers to do the job. While aGotino will grow in functionalities, you can  upgrade to other solutions, like [OnStep](https://onstep.groups.io/g/main), and re-use almost all of the hardware.

Photos and hardware details on [CloudyNights (English)](https://www.cloudynights.com/topic/735800-agotino-a-simple-arduino-nano-goto/) or [Astronomia.com (Italian)](https://www.astronomia.com/forum/showthread.php?34605-aGotino-un-goto-con-Arduino).

![aGotino](https://www.cloudynights.com/uploads/gallery/album_14775/sml_gallery_329462_14775_4192.jpg)

### Features

- tracking
- listen for basic Meade LX200 protocol - drive with Stellarium, SkySafari Plus/Pro(mobile), Kstars, Carte du Ciel or any software that supports INDI
- listen for aGotino commands
  - 248 bright stars, Messier (all) and 768 NGC objects (up to mag 11) are in memory
- ST4 port for guiding
- two buttons to cycle among RA&Dec forward and backward slow motion (8x) 
  - press both buttons for 1sec to change side of pier (default West, see below).

### LX200 Protocol
Sync&Slew actions are supported, commands **`:GR :GD :Sr :Sd :MS :Mx :CM :Q :GVO :GVN ACK`**  
Tested with Stellarium (direct), INDI LX200 Basic driver (KStars, Cartes du Ciel, Stellarium, etc), SkySafari Plus and Stellarium Plus (mobile)

[Video: SkySafari Mobile with aGotino](https://www.youtube.com/watch?v=mhODsDZTl5U)

[Video: Stellarium from a PC with aGotino](https://youtu.be/PdkoGX5PcDA)

### aGotino Protocol
From an Android device you can use [Serial USB Terminal](https://play.google.com/store/apps/details?id=de.kai_morich.serial_usb_terminal&hl=it) or [Serial Bluetooth Terminal](https://play.google.com/store/apps/details?id=de.kai_morich.serial_bluetooth_terminal&hl=it&gl=US) app or similar while from a PC/Mac any terminal emulator should work. Default serial speed is 9600 baud.

#### Command set
**x** can be **s (set)** or **g (goto)**:    
  - **`x HHMMSS±DDMMSS`** set/goto coordinates
  - **`x Mn`**            set/goto Messier object n
  - **`x Nn`**            set/goto NGC object n
  - **`x Sn`**            set/goto Star number n in aGotino Star List
  - **`±RRRR±DDDD`**     slew Ra&Dec by RRRR&DDDD arcmins (RRRR/15 corresponds to minutes)
  - **`±side`**        change side of pier (default west, see below)
  - **`±sleep`**       power saving on dec motor when unused (default enable)
  - **`±speed`**       increase or decrease slow motion speed by 4x
  - **`±range`**       increase or decrease max slew range (default 30°)
  - **`±info`**        display current settings
  - **`±debug`**       enable verbose output

blanks are ignored and can be omitted.

> WARNING: watch your scope while slewing!
> There are no controls to avoid collision with mount,
> default maximum range for slewing is 30°, pay extra attention if you increase it

#### [aGotino Star List](https://github.com/mappite/aGotino/blob/main/aGotino-StarList.pdf)

The list contains α, β, γ constellation stars up to mag 4 and all other stars up to mag 3. The goal is to provide a quick lookup number reference for all easy-to-point stars vs having to type coords. Credits to Nasa's [BSC5P - Bright Star Catalog](https://heasarc.gsfc.nasa.gov/W3Browse/star-catalog/bsc5p.html) and KStars project for star names.

#### Example 

Point scope to Mizar in UMa, set (sync/align current position) Star 223 in aGotino Star List, and slew to M101 Pinwheel Galaxy;  point Altair in Aql, set current position using coordinates

    > s S223
    19:06:58 Set Star 223      
    19:06:58 Current Position: 13h23'56" 54°55'31
    > g M101
    19:07:08 Goto M101
    19:07:08  *** moving...
    19:07:17  *** done
    19:07:17 Current Position: 14h03'12" 54°20'57"
    > s 195147+085206
    19:09:59 Current Position: 19h51'47" 08°52'06"

Slew -1° in RA (i.e. +1° West) and +1° Dec (North). Note 1°=60' (arcmins) which translates to 60/15 = 4 minutes.

    19:22:28 Current Position: 02h03'54" 42°19'47
    > -0060+0060
    21:23:10  *** moving...
    21:23:12  *** done
    21:23:12 Current Position: 1h59'54" 43°19'47"

[Video: aGotino in action](https://www.youtube.com/watch?v=YF_J7_7lyB4)

### Side of Pier

Default value for *Side of Pier* is West, meaning your scope is supposed to be West of the mount, usually pointing East. If your scope is on East side of the mount, you need to push both buttons for 1 sec or issue a **`+side`** command to let aGotino know that, since Declination movement has to invert direction. When setting East, motors pause for 3secs while onboard led turns on. When going back to West, onboard led blinks twice. 


### AstroMath (i.e. adapt for your mount)

Below is how to calculate stepper motor pulse length to drive your mount at sidereal speed
    
    Worm Ratio                 144   // 180 Eq6, 144 eq5&exos2, 135 heq5, 130 AR 65 DEC eq3-2
    Other (Pulley or Gear) Ratio 2.5 // depends on your pulleys setup e.g. 40T/16T = 2.5
    Steps per revolution       400   // or 200, depends on stepper motor. The higher the better.
    Microstep                   32   // depends on driver. The higher the better.
     
    MICROSTEPS_PER_DEGREE_AR 12800   // = WormRatio*OtherRatio*StepsPerRevolution*Microsteps/360
                                     // = number of motor microsteps to rotate the scope by 1 degree
     
    STEP_DELAY               18699   // = (86164/360)/(MICROSTEPS_PER_DEGREE_AR)*1000000
                                     // = microseconds to advance a microstep
                                     // 86164 is earth 360deg rotation time in secs (23h56m04s)
                                  
The above example is for an EQ5/Exos2 with 40T-16T pulleys: tracking precision is 53 microsteps/second or 0.281 arcsec/microstep - which appears to be the same figures of ES/Losmandy G-11 mount. With a good polar alignment, goto accuracy within the default 30° is under 5': if you get worse results check your pulleys are tightned, verify your mount polar alignemnt and level.  

### Implementation

#### Software

Files:

    aGotino.ino           Arduino C Source
    catalogs.h            Object Catalogues (J2000.0) for aGotino commands
    aGotino-StarList.pdf  Star list in PDF
    aGotino-wiring.png    wirings 

- Copy _aGotino.ino_ and _catalogs.h_ in Arduino/aGotino/ 
- Open _aGotino.ino_ in Arduino IDE and edit header to set `MICROSTEPS_PER_DEGREE_AR` & `STEP_DELAY` values for your mount and (optional), change any other default values to fit your preferences.
- Compile and upload to your Arduino device.

#### Hardware

- 2 Stepper Motors:  Nema 17 with 400 steps per revolution seems a great solution
  - Supports to attach motors to the mount - depends on your mount, be creative and use a 3D printer if you have one ;)
- 2 Drivers: cheap DRV8825 works well (32 microsteps), better TMC or LV can be used of course (see below).
- 4 Pulleys and 2 Belts (GT2). Size depends on your mount (see [Belt Calculator](https://www.bbman.com/belt-length-calculator/))
- RJ11 cable with two RJ11 sockets to connect the Dec Motor
- Dupont cables, a couple of momentary buttons and a 100µF Capacitor to protect the circuit.
- Arduino Nano (or any Arduino)
- (optional) Bluetooth Adapter, see below

![Hardware](https://imgur.com/zhQLEPC.png)

### Motor Driver

Default wiring and code is for the cheap DRV8825 driver, TMC (and other) drivers are valid alternative to reduce motor noise and increase smoothness (for example TMC2208 in legacy Mode 1 with "just" 16 microsteps has actually a fine 256 microsteps interpolation). 

TMC driver to Ardunio connections needs to be:

    TMC2208 <-> Arduino
        VIO <-> +5VDC
         EN <-> GND
    Do not connect NC, PDN, CLK (these would match with MS3, RES, SLP in DRV8825). 
    MS1&MS2 connection depends on driver while other pins continue to match DRV8825 schema.

Check your driver datasheet to know what is the number of microsteps when MS1&MS2 are High or Low, wire the driver according to your needs and update MICROSTEPS_RA_HIGH/LOW and MICROSTEPS_DEC_HIGH/LOW in aGotino.ino code (lines 40-47). 

For TMC2208, you can connect MS1 to VIO and leave just MS2 to be driven by Arduino D2 (RA motor) or D9 (DEC motor). This means when D2 (or D9) is LOW the driver will work with 2 microsteps, while when D2 (or D9) are HIGH the driver will work with 16 microsteps.

    MICROSTEPS_RA_HIGH  = 16
    MICROSTEPS_RA_LOW   = 2
    MICROSTEPS_DEC_HIGH = 16
    MICROSTEPS_DEC_LOW  = 2

For TMC2225, you can connect both MS1&MS2 to Arduino D2 (RA) or D9 (DEC) to obtain 32 microsteps when high and 4 when low.

    MICROSTEPS_HIGH_RA  = 32;
    MICROSTEPS_HIGH_DEC = 32;
    MICROSTEPS_LOW_RA  = 4;
    MICROSTEPS_LOW_DEC = 4;

If slewing is too slow, reduce (gently...) STEP_DELAY_SLEW. 

> Usage of TMC or other drivers are wellcome to be reported in [CloudyNight aGotino  post](https://www.cloudynights.com/topic/735800-agotino-a-simple-arduino-nano-goto/) so this section can be expanded.

### Bluetooth

Tested with HC-05 (Bluetooth Classic - recommended for SkySafari/Stellarium Plus) and HC-08/10 (BLE) BT modules - just wire BT module RX&TX pins to Arduino TX&RX, BT module RX pin is rated 3.3v so you should use a couple of resistors as voltage divider to lower Arduino TX voltage (I did not).

From a mobile device you can then connect using [Serial Bluetooth Terminal App](https://play.google.com/store/apps/details?id=de.kai_morich.serial_bluetooth_terminal&hl=it&gl=US) or SkySafary/Stellarium Plus - or I assume any other app that supports LX200.

On a PC you can configure the bluetooth connection as a serial device in your computer and connect via Stellarium/Indi (on Linux, setup /dev/rfcomm0 or for BLE devices see [BLE-Serial](https://github.com/Jakeler/ble-serial)). 

Note: when BT adapter is powered and wired to Arduino RX/TX, the Arduino USB port is not fully functional - disconnect BT Adapter power or RX/TX cables to upload a new firmware.

### ST4

Uncomment the `#ifdef ST4` line in aGotino.ino to enable ST4 port. 
Arduino pins A0,A1,A2,A3 (North, South, East, West) can be connected directly to your camera ST4 port via a RJ cable (6 wires - one is for GND). 
The variable `ST4_FACTOR` can be used to decrease/increase speed - default value (20) guides at 0.5x,  range goes from 10 (faster) to 30 (slower).

### Have fun!
