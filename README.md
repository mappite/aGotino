# aGotino
A telescope Goto solution based on Arduino (Nano or up) that supports:

- aGotino commands - any Android phone can act as a remote via an [USB OTG cable](https://www.amazon.com/s?k=usb+otg+cable) and a [Serial App](https://play.google.com/store/apps/details?id=de.kai_morich.serial_usb_terminal&hl=it)
- basic Meade LX200 protocol - drive with Stellarium or any software that supports INDI

aGotino allows precise tracking and *hybrid goto&starhopping*: you point the scope to something you can easily find and then reach a remote, low magnitude object nearby - default *nearby* is 30° so you will always find some bright stars around. Star alignment procedures are _not_ required, you can move and rotate your scope freely, until you need that extra help. Within the default 30° the hardware&software accuracy proved to point any objects with an error below 15' (half moon size), more than enough to spot it using a mid-focal eyepiece. At the benefit of not being locked to always use goto.

No additional boards needed, just wire Arduino and two stepper Drivers to do the job. While aGotino will grow in functionalities, you can  upgrade to other solutions, like [OnStep](https://onstep.groups.io/g/main), and re-use almost all of the hardware.  
Photos and hardware details on [CloudyNights (English)](https://www.cloudynights.com/topic/735800-agotino-a-simple-arduino-nano-goto/) or [Astronomia.com (Italian)](https://www.astronomia.com/forum/showthread.php?34605-aGotino-un-goto-con-Arduino).

![aGotino](https://www.cloudynights.com/uploads/gallery/album_14775/sml_gallery_329462_14775_4192.jpg)

### Features

- tracking - move Right Ascension at sidereal speed, 1x
- two buttons to cycle among RA&Dec forward and backward speeds (8x) 
  - press both buttons for 1sec to change side of pier (default West, see below).
- listen on serial port for basic LX200 commands
- listen on serial port for aGotino commands
  - 248 bright stars + 110 (all) Messier + 768 NGC objects are in memory
  - sync&slew to objects in memory or to any RA/DEC coordinates

### aGotino Command set
**x** can be **s (set)** or **g (goto)**:    
  - **`x HHMMSS±DDMMSS`** set/goto coordinates
  - **`x Mn`**            set/goto Messier object n
  - **`x Nn`**            set/goto NGC object n
  - **`x Sn`**            set/goto Star number n in aGotino Star List
  - **`±RRRR±DDDD`**     slew Ra&Dec by RRRR&DDDD primes (RRRRx4 corresponds to arcsecs)
  - **`±side`**        change side of pier (default west, see below)
  - **`±sleep`**       power saving on dec motor when unused (default enable)
  - **`±speed`**       increase or decrease speed by 4x
  - **`±range`**       increase or decrease max slew range (default 30°)
  - **`±debug`**       enable verbose output

blanks are ignored and can be omitted. 768 NGC objects up to mag 11 are in memory.

> WARNING: watch your scope while slewing!
> There are no controls to avoid collision with mount,
> default maximum range for slewing is 30°, pay extra attention if you increase it

#### [aGotino Star List](https://github.com/mappite/aGotino/blob/main/aGotino-StarList.pdf)

aGotino contains all α, β, γ constellation stars up to mag 4 and other stars up to mag 3. The goal is to provide a quick lookup number reference for easy-to-point stars vs having to type coords. Credits to Nasa's [BSC5P - Bright Star Catalog](https://heasarc.gsfc.nasa.gov/W3Browse/star-catalog/bsc5p.html) and KStars project for star names.

#### Example 

Point scope to Mizar in UMa, set (sync current position) Star 223 in aGotino Star List, and slew to M101 Pinwheel Galaxy;  point Altair in Aql, set using coordinates, and slew to M11 Wild Duck

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

Slew +1° Dec (North) and -1° in RA (West). Note 1°=60' (arcmins) and translates to 60*4 secs = 4 mins.

    19:22:28 Current Position: 02h03'54" 42°19'47
    > -0060+0060
    21:23:10  *** moving...
    21:23:12  *** done
    21:23:12 Current Position: 1h59'54" 43°19'47"

[Here a video: aGotino in action](https://www.youtube.com/watch?v=YF_J7_7lyB4)

### Meade LX200 Protocol Support

Sync&Slew actions are supported, commands **`:GR :GD :Sr :Sd :MS :CM`**  
Tested with Stellarium (direct) and INDI LX200 Basic driver (KStars, Cartes du Ciel, Stellarium, etc)

[Here a video: Stellarium with aGotino](https://youtu.be/PdkoGX5PcDA)

### Side of Pier

Default value for *Side of Pier* is West, meaning your scope is supposed to be West of the mount, usually pointing East. If your scope is on East side of the mount, you need to push both buttons for 1 sec or issue a **`+side`** command to let aGotino know that, since Declination movement has to invert direction. When setting East, motors pause for 3secs while onboard led turns on. When going back to West, onboard led blinks twice.

### Files

    aGotino.ino           C Source
    catalogs.h            Contains catalogues (Messier and aGotino Star List in J2000.0)
    aGotino-StarList.pdf  Star list in PDF
    aGotino-wiring.png    wirings 

### AstroMath (i.e. adapt for your mount)

Below is how to calculate stepper motor pulse lenth to drive your mount at sidereal speed
    
    Worm Ratio                144   // 144 eq5/exos2, 135 heq5, 130 eq3-2
    Other (Pulley/Gear) Ratio   2.5 // depends on your pulley setup e.g. 40T/16T = 2.5
    Steps per revolution      400   // or 200 depends on stepper motor. The higher the better.
    Microstep                  32   // depends on driver. The higher the better.
     
    MICROSTEPS_PER_DEGREE   12800   // = WormRatio*OtherRatio*StepsPerRevolution*Microsteps/360
                                    // = number of motor microsteps to rotate the scope by 1 degree
     
    STEP_DELAY              18699   // = (86164/360)/(MicroSteps per Degree)*1000000
                                    // = microseconds to advance a microstep
                                    // 86164 is the number of secs for earth 360deg rotation (23h56m04s)
                                  
The above example is for an EQ5/Exos2 with 40T-16T pulleys, this results in 53 microsteps/second or .281 arcsec/microstep (which appears to be the same figures of ES/Losmandy G-11 mount)

### Hardware

- 2 Stepper Motors:  Nema 17 400 step per revolution seems a great solution - for visual only you can select smaller ones
  - Supports to attach motors to the mount - depends on your mount, be creative and use a 3D printer if you have one ;)
- 2 Drivers: with at least 32 microsteps. Cheap DRV8825 works, but better TMC or LV can be used of course
- 4 Pulleys and 2 Belts (GT2). Size depends on your mount (see [Belt Calculator](https://www.bbman.com/belt-length-calculator/))
- RJ11 cable with two RJ11 sockets to connect the Dec Motor
- Dupont cables, a couple of momentary buttons and a 100µF Capacitor to protect the circuit.
- Arduino Nano

![Hardware](https://imgur.com/zhQLEPC.png)

### Todo

- Add more objects (NGCs)
- Lookup objects by constellation
- Support pulse guide LX200-style commands to allow guiding via phd2 / or add ST4 port
