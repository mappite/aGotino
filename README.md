# aGotino
A telescope Goto solution based on Arduino (Nano or up) that supports:

- aGotino commands - an Android phone can act as a remote via an [USB OTG cable](https://www.amazon.com/s?k=usb+otg+cable) or (optional) via Bluetooth
- basic Meade LX200 protocol - drive with Stellarium, SkySafari Plus/Pro(mobile), Kstars, Carte du Ciel or any software that supports INDI

aGotino provides tracking and **hybrid goto&starhopping**: point the scope to something you can easily find and then reach a remote, low magnitude object nearby - default *nearby* is 30° so you will always find some bright stars around. Star alignment procedures are _not_ required, you can move and rotate your scope freely, until you need that extra help. 

No additional boards needed, just wire Arduino and two stepper Drivers to do the job. While aGotino will grow in functionalities, you can  upgrade to other solutions, like [OnStep](https://onstep.groups.io/g/main), and re-use almost all of the hardware.

Photos and hardware details on [CloudyNights (English)](https://www.cloudynights.com/topic/735800-agotino-a-simple-arduino-nano-goto/) or [Astronomia.com (Italian)](https://www.astronomia.com/forum/showthread.php?34605-aGotino-un-goto-con-Arduino).

![aGotino](https://www.cloudynights.com/uploads/gallery/album_14775/sml_gallery_329462_14775_4192.jpg)

### Features

- tracking - move Right Ascension at sidereal speed, 1x
- two buttons to cycle among RA&Dec forward and backward slow motion (8x) 
  - press both buttons for 1sec to change side of pier (default West, see below).
- listen on serial port for basic LX200 commands
- listen on serial port for aGotino commands
  - 248 bright stars, Messier (all) and 768 NGC objects (up to mag 11) are in memory

### aGotino Command set
From an Android device you can use [Serial USB Terminal](https://play.google.com/store/apps/details?id=de.kai_morich.serial_usb_terminal&hl=it) or [Serial Bluetooth Terminal](https://play.google.com/store/apps/details?id=de.kai_morich.serial_bluetooth_terminal&hl=it&gl=US) apps. From a PC any terminal emulator should work. 

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

Slew -1° in RA (i.e. +1° West) and +1° Dec (North). Note 1°=60' (arcmins) which translates to 60/15 = 4 minutes.

    19:22:28 Current Position: 02h03'54" 42°19'47
    > -0060+0060
    21:23:10  *** moving...
    21:23:12  *** done
    21:23:12 Current Position: 1h59'54" 43°19'47"

[Here a video: aGotino in action](https://www.youtube.com/watch?v=YF_J7_7lyB4)

### LX200 Protocol Support

Sync&Slew actions are supported, commands **`:GR :GD :Sr :Sd :MS :CM :Q :GVO :GVN ACK`**  
Tested with Stellarium (direct), INDI LX200 Basic driver (KStars, Cartes du Ciel, Stellarium, etc), SkySafari Plus and Stellarium Plus (mobile)

[Video: Stellarium from a PC with aGotino](https://youtu.be/PdkoGX5PcDA)

[Video: SkySafari Mobile with aGotino](https://www.youtube.com/watch?v=fBjxpKKCwJc)

### Side of Pier

Default value for *Side of Pier* is West, meaning your scope is supposed to be West of the mount, usually pointing East. If your scope is on East side of the mount, you need to push both buttons for 1 sec or issue a **`+side`** command to let aGotino know that, since Declination movement has to invert direction. When setting East, motors pause for 3secs while onboard led turns on. When going back to West, onboard led blinks twice. 

### Files

    aGotino.ino           Arduino C Source
    catalogs.h            Object Catalogues (J2000.0)
    aGotino-StarList.pdf  Star list in PDF
    aGotino-wiring.png    wirings 

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
                                  
The above example is for an EQ5/Exos2 with 40T-16T pulleys: it results in a tracking precision of 53 microsteps/second or 0.281 arcsec/microstep - which appears to be the same figures of ES/Losmandy G-11 mount. With a good polar alignment, goto accuracy within the default 30° is under 5'.

### Implementation

- Copy _catalogs.h_ in Arduino/libraries/aGotino/ and _aGotino.ino_ in Arduino/aGotino/ 
- Open _aGotino.ino_ in Arduino IDE and edit header to set MICROSTEPS_PER_DEGREE_AR & STEP_DELAY values for your mount and (optional) change any other default values to fit your preferences.
- Compile and upload to your Arduino device.

### Hardware

- 2 Stepper Motors:  Nema 17 400 step per revolution seems a great solution - for visual only you can select smaller ones
  - Supports to attach motors to the mount - depends on your mount, be creative and use a 3D printer if you have one ;)
- 2 Drivers: with at least 32 microsteps. Cheap DRV8825 works, better TMC or LV can be used of course
- 4 Pulleys and 2 Belts (GT2). Size depends on your mount (see [Belt Calculator](https://www.bbman.com/belt-length-calculator/))
- RJ11 cable with two RJ11 sockets to connect the Dec Motor
- Dupont cables, a couple of momentary buttons and a 100µF Capacitor to protect the circuit.
- Arduino Nano (or any Arduino)
- (optional) Bluetooth Adapter, see below

![Hardware](https://imgur.com/zhQLEPC.png)

### Bluetooth
Tested with HC-05 (Bluetooth Classic) and HC-08/10 (BLE) BT modules - just wire BT module RX&TX pins to Arduino TX&RX, BT module RX pin is rated 3.3v so you should use a couple of resistors as voltage divider to lower Arduino TX voltage.  You can then connect using [Serial Bluetooth Terminal App](https://play.google.com/store/apps/details?id=de.kai_morich.serial_bluetooth_terminal&hl=it&gl=US). Note: when BT adapter is wired to TX&RX the USB port is not functional.

SkySafari Plus/Pro or Stellarium Plus work directly with Bluetooth Classic (2.0), while with BLE you need a bridge. Note: You don't need bluetooth to use these mobile apps, you can wire via USB and use a [USB/WIFI/BT Bridge App](https://play.google.com/store/apps/details?id=masar.bluetoothbridge.pro&hl=en_US&gl=US) - see SkySafari video posted above.

You can of course configure the bluetooth connection as a serial device in your computer and connect via Stellarium/Indi (on Linux, setup /dev/rfcomm0 or for BLE devices see [BLE-Serial](https://github.com/Jakeler/ble-serial)). 

### Todo

- Lookup objects by constellation
- Support pulse guide LX200-style commands to allow guiding via phd2 / or add ST4 port
- Show Battery Level
