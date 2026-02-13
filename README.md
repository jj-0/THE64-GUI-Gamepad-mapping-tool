A GUI tool to run on THE64 Mini, Maxi, VIC-20 that allows you to map a connected USB controller to THEJOYSTICK buttons/axes. Put everything from a Release on the root of an MBR, FAT32 formatted USB stick that you know works on THE64, insert it in a THE64 USB port and run the fake upgrade from it.
In the menu (after the mapping is done) you can control the menu with the new controller, THEJOYSTICK and also a USB keyboard.
If you save the mapping a <Controller GUID>.txt file is created on the USB drive with the mapping string that needs to be added to the /usr/share/the64/ui/data/gamecontrollerdb.txt file.

## keyboard2thejoystick

Translates USB keyboard input into virtual THEJOYSTICK events via Linux uinput. This creates a virtual joystick device that THEC64 firmware recognizes natively, allowing you to play games with a keyboard as if a THEJOYSTICK were connected.

### Usage

```
keyboard2thejoystick [OPTIONS]
```

By default, directions use a QWEASDZXC layout:

```
Q=Up-Left    W=Up      E=Up-Right
A=Left       (S=n/a)   D=Right
Z=Down-Left  X=Down    C=Down-Right
```

Default button keys: Space=Left Fire, Left Alt=Right Fire, [=Left Triangle, ]=Right Triangle, 7-0=Menu 1-4.

All keys are customizable via command-line options:

```
Direction keys:
  --up KEY         (default: w)       --upleft KEY    (default: q)
  --down KEY       (default: x)       --upright KEY   (default: e)
  --left KEY       (default: a)       --downleft KEY  (default: z)
  --right KEY      (default: d)       --downright KEY (default: c)

Button keys:
  --leftfire KEY   (default: space)   --rightfire KEY (default: lalt)
  --lefttri KEY    (default: bracketleft)
  --righttri KEY   (default: bracketright)
  --menu1 KEY      (default: 7)       --menu2 KEY     (default: 8)
  --menu3 KEY      (default: 9)       --menu4 KEY     (default: 0)

Other:
  --help           Show usage with current configuration
  --guimap         Interactive framebuffer mapping mode
```

Key names can be single characters (`a`, `7`) or names (`space`, `lalt`, `lctrl`, `lshift`, `rshift`, `tab`, `enter`, `esc`, `bracketleft`, `bracketright`, `f1`-`f12`, `up`, `down`, `left`, `right`, etc.).

### Interactive mapping (--guimap)

The `--guimap` option launches a framebuffer GUI (same style as the gamepad mapper) that walks through each of the 16 inputs, lets you press the desired key, and saves the result as an executable shell script you can run directly.

### Cross-compile

```
arm-linux-gnueabihf-gcc -std=gnu99 -static -O2 -o keyboard2thejoystick keyboard2thejoystick.c
```

### Running on THE64

Copy the `keyboard2thejoystick` binary to a USB stick, get a shell on THE64, and run it. The program grabs the keyboard exclusively (so keypresses don't leak to the console) and creates a virtual THEJOYSTICK. Press Ctrl+C to stop.

Created using Claude Code
