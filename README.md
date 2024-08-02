# Laptop Touchpad Project

## Context

I harvested a touchpad from an old HP Envy Sleekbook 6 and wanted to use it as a standalone mouse. 

Target features:

* Regular mouse functionalities. I.e. cursor movements, left and right clicks.
* Two-finger scrolling.
* Two finger click as right click.

Most touchpads are made by Synaptics. They usually use a PS2 interface. By default (i.e. without any drivers), they can usually simulate a regular PS2 mouse. I.e. they can report finger movements and button clicks. But that's about it. They don't support more sophisticated gestures we normally find on PC or Mac laptops. To achieve those gestures, we need to implement proprietary but published (with some caveats) Synaptics protocols, documented in [Synaptics PS2 TouchPad Interfacing Guide](touchpad_RevB.pdf).

## Touchpad info

* Pulled from an HP Envy Sleekbook 6
* Synaptics chip # T1320A
* Physical pinout, reverse engineered after watching this [YouTube video](https://www.youtube.com/watch?v=XdznW0ZuzGo&t=381s)
![Pinout](IMG_0835.jpeg)

More info obtained from programatic querys, based on section 4.4. Information queries.

* Query 0x00 (Identify TouchPad): 01 47 18. 
  - Version 8.1

* Query 0x01 (Read TouchPad Modes): 20 87 40.
  - Model 0x0887

* Query 0x02 (Read Capabilities): D0 01 23
   - Extended capability: yes 
   - Extended queries:
   - Middle button: no
   - Model sub number: 01
   - Pass through: no
   - Low power: no
   - Multi finger report: yes
   - Can sleep: no
   - Four buttons: no
   - Ballitics: no
   - Multi finger detect: yes
   - Palm detect: yes

* Query 0x03 (Read Model ID): 01 E2 B1
 - Absolute mode: yes
 - Geometry: rectangule

* Query 0x06 (Read Serial Number Prefix): 00 00 00

* Query 0x07 (Read Serial Number Suffix): 00 00 00

* Query 0x08 (Read Resolutions): 2F 80 42
  - x units per mm: 47
  - y units per mm: 66
  - Note: these numbers don't seem accurate. But they are not really important.

* Query 0x09 (Extended Model ID): 84 03 00
  - Light control: no
  - Peak detect: no
  - Glass pass: no
  - Vertical wheel: no
  - Ext. W mode: yes
  - H Scroll: no
  - V Scroll: no
  - Extended buttons: 0
  - Product ID: 0x00

* Query 0x0C (Continued Capabilities): 12 6C 00
  - Covered Pad Gesture: no
  - MultiFinger Mode: 1.0
  - Advanced Gestures: no
  - ClearPad: no
  - Reports Max Coordinates: yes
  - TouchButton Adjustable Threshold: no
  - InterTouch: yes
  - Reports Min Coordinates: yes
  - Uniform ClickPad: no (Hinged mechanism)
  - Reports V: yes
  - No Absolute Position Filter: no
  - Deluxe LEDs: no
  - ClickPad: One-button ClickPad

* Query 0x0D (Maximum Coordinates): B1 6B 94
  - max x: 2843 
  - max y: 2374

* Query 0x0F (Minimum Coordinates): 27 94 22
  - min x: 628
  - min y: 553
  - Note: the min and max coordinates are definitely wrong but again, it doesn't matter.

## Implementing PS/2 on an MCU
So, I'm using an atmel mega32u4 to interface with the touchpad. Any Leonardo clone should work. The reason I picked this MCU is its native USB support. Another alternative is to use tinyusb library to bit bang USB protocol. It's probably pretty straight-forward too.

Basically, I implemented syncrhonous writing, synchronous reading, and asynchronous reading. Synchronous writing because async writing is difficult to use. However, given the async nature of PS/2 protocol, it makes sense to have async, interrupt based reading. I.e. bits are transferred via interrupts and stored in a buffer. I have also implemented synchronous reading as a means to read responses after each write.

 I'm using [external interrupts](https://developerhelp.microchip.com/xwiki/bin/view/products/mcu-mpu/8-bit-avr/structure/extint/) to interact with the clock pin. So the clock pin needs to be one of these: 0, 1, 2, 3, 7 (D2, D3, D1, D0, E6, respectively). It's also possible to use pin change interrupts with slight changes to the code:

```
*(digitalPinToPCICR(clock_pin)) |= 1 << digitalPinToPCICRbit(clock_pin);
*(digitalPinToPCMSK(clock_pin)) |= 1 << digitalPinToPCMSKbit(clock_pin);

... 

ISR(PCINT0_vect) {
   ...
}
```

With the PS2 implemented, it's really straight-forward to interact the touchpad as a standard PS2 mouse. The code is in the main branch of this repo.

The rest of this doc focuses on the proprietary Synaptics expansion of the PS2 protocol.

## State machine logic
I am simulating the behaviour of a MacBook since that's what I'm used to. Most PC laptops behave the same too, with "tap to click" feature turned off. In the following text, whenever I say "two fingers", I mean two or more fingers. This pad does recognize more fingers but it does not report the position of the third one and beyond. So no useful info can be derived, unless I supported three finger swipe, which I currently don't.

TODO:
* Make it more stable with thumb clicks.
* Horizontal scrolling. I think this is a standard USB HID feature and should be relatively easy to implement.
* Three finger swipes as back or forward button.
* Zooming with two fingers.

For now, we only do tracking, scrolling, left and right buttons, where right button click is simulated with a two finger click, just like MacBook. Windows optionally supports this too, as long as the touchpad is multi-touch.

The logic is quite complicated. I might need to give it a little more thought and simplify a little. For now, we have three main valid states, identified by two state variables, fingers and buttons. Fingers is the number of fingers on the pad. Buttons is a bitmap representing the states of left and right buttons.

### Idle
This is where no fingers are on the touchpad and no buttons are pressed. Conditions:
* fingers == 0 && buttons == 0

Transitions:
* Button pressed && no fingers pressed: report the button state and remain in the idle state. Note that if you press the pad with a non captive object such as a pen, you would be able to press the button with a finger count of zero.
* Button pressed && at least one finger pressed: report the button state and go to tracking.
* One finger pressed: report the button state and go to tracking.
* Two fingers pressed: report the button state go to scrolling.

### Tracking
This is the state where we report the coordinate delta to the OS who will move the cursor accordingly. There are a couple of substates.

Overall conditions:
* fingers == 1 || (fingers == 2 && buttons != 0)

When one finger is pressed, we're tracking regardless if the button is pressed. When two fingers are pressed and the button is *not* pressed, we always scroll. But if two fingers are pressed and the button is also pressed, we also track. This is MacBook's behaviour, which is different from Windows, at least Windows 11 on an HP Envy x360.

Conditions:
* fingers == 1

Transitions:
* Second finger pressed without the button pressed: go to scrolling.
* Second finger pressed and the button pressed: stay in tracking. This transition can happen if you use the second finger to press the button *quickly* such that the second finger and the button are reported in the same packet.
* Finger released: go back to idle.
* Button pressed: stay in tracking.
* Button released: stay in tracking.

Conditions:
* fingers == 2 && buttons != 0

Transitions:
* Button released: go to scrolling. Note that this behaviour is different on Windows.
* One finger released: stay in tracking.
* Button *and* one finger released: stay in tracking
* Both fingers released: go to idle.

### Scrolling
This is the state where two fingers are on the pad and their vertical movements are treated as scrolling. Conditions:
* fingers == 2 || buttons == 0

Transitions:
* Button pressed: go to tracking
* One finger released: go to tracking
* Both fingers released: go to idle

### Invalid state
Conditions:
* fingers == 0 && buttons != 0

## Final product:
![Breadboard](IMG_0914.jpeg)