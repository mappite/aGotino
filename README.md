# aGotino
A simple telescope Goto solution based on Arduino Nano (or Uno) that supports

- aGotino commands - an Android phone can do the job via an [USB OTG cable](https://www.amazon.com/s?k=usb+otg+cable) and a [Serial App](https://play.google.com/store/apps/details?id=de.kai_morich.serial_usb_terminal&hl=it)
- basic Meade LX200 protocol (INDI)

Goal is to provide a simple&cheap, but high precision, solution for *augmented starhopping*: you point the scope to something you can find easily and get help moving to a remote, low magnitude object.

Photos and hardware details on [CloudyNights (English)](https://www.cloudynights.com/topic/735800-agotino-a-simple-arduino-nano-goto/) or [Astronomia.com (Italian)](https://www.astronomia.com/forum/showthread.php?34605-aGotino-un-goto-con-Arduino).

### Features

- move Right Ascension at sidereal speed (1x) 
- at button 1 & 2 press, cycle among forward and backward speeds (8x) on RA&Dec
- listen on serial port for basic LX200 commands (tested with INDI LX200 Basic driver and Stellarium, Kstar, Cartes du Ciel)
- listen on serial port for aGotino commands

### aGotino Command set
**x** can be **s (sync)** or **g (goto)**:    
  - **`x HHMMSS±DDMMSS`** sync/goto position
  - **`x Mn`**            sync/goto Messier object n
  - **`x Sn`**            sync/goto Star number n in aGotino Star List
  - **`±RRRR±DDDD`**     slew by Ra&Dec by RRRR&DDDD degree mins (RRRRx4 corresponds to seconds hours) r & d are signs  (r = + is clockwise)
  - **`±debug`**       verbose output
  - **`±sleep`**       power saving on dec motor when unused
  - **`±speed`**       increase or decrease speed by 4x
  - **`±range`**       increase or decrease max slew range (default 30°)

blanks are ignored and can be omitted.

> WARNING: watch your scope while slewing!
> There are no controls to avoid collisions with mount,
> for this reason defaul maximum range for slew is 30°

#### Example 

Point scope to Mizar in UMa (star 223 in aGotino Star List) and slew to M101 Pinwheel Galaxy, then point Altair in Aql (using coordinates) and slew to M11 Wild Duck

    19:06:34 aGotino: ready.
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
    > g M11
    19:10:19 Goto M11
    19:10:19  *** moving...
    19:10:32  *** done
    19:10:32 Current Position: 18h51'06" ‑06°16'00"

Slew by 1° W and 1° S.  1° = 60' (which in HH:MI corresponds to 60x4 secs = 4 mins).

    19:22:28 Current Position: 02h03'54" 42°19'47
    > +0060+0060
    21:23:10  *** moving...
    21:23:12  *** done
    21:23:12 Current Position: 1h59'54" 41°19'47"

### Code
    aGotino.ino           C Source
    catalogs.h            Contains catalogues (Messiers and aGotino Star List)
    aGotino-StarList.pdf  Star list in PDF
    aGotino-wiring.png    wirings 

### AstroMath (i.e. adapt for your mount)

Suppose to drive a stepper motor that takes 32 microsteps to complete one of the 400 steps (0.9°) needed for a complete motor rotation that will rotate 1/2.5 times (via pulleys) the worm screw that needs 144 rotations to drive a complete a 360° RA axis rotation (24h): it may sound tricky but it is (32x400x2.5x144) / 360° = 12800 microsteps (or 400 steps) to move the scope by one degree.

How long to properly follow stars? Well, earth 360° rotation takes 23h 56m 4 sec, i.e. 86164 secs thus 86164/360=239.34 are secs to rotate by one degree.

This leads to 239.34/12800 = 0.018699 secs for each microsteps.

Arduino Nano runs at 16MHz, i.e. 16 million cycles per secs so driving something at 18699µsecs is more than doable, leaving a lot of time to do something else (listed on serial, do some validations etc.).

    How to calculate STEP_DELAY to drive motor at right sidereal speed for your mount
    
    Worm Ratio                144   // 144 eq5/exos2, 135 heq5, 130 eq3-2
    Other (Pulley/Gear) Ratio   2.5 // depends on your pulley setup e.g. 40T/16T = 2.5
    Steps per revolution      400   // or usually 200 depends on your motor
    Microstep                  32   // depends on driver
     
    MICROSTEPS_PER_DEGREE   12800   // = WormRatio*OtherRatio*StepsPerRevolution*Microsteps/360
                                    // = number of motor microsteps to rotate the scope by 1 degree
     
    STEP_DELAY              18699   // = (86164/360)/(MicroSteps per Degree)*1000000
                                    // = microseconds to advance a microstep
                                    // 86164 is the number of secs for earth 360deg rotation (23h56m04s)
