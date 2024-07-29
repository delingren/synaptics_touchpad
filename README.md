# Laptop Touchpad Project

This is the first step of this project.

## Goal

Turn this touchpad into a mouse that supports:
* Regular mouse functionalities. I.e. cursor movements, left and right clicks.
* Two-finger scrolling.
* Optionally: two finder click as right click.
* Optionally: tap as click.

The first goal is very straight-forward. And here's the implementation with Arduino framework.

## Touchpad info

* Pulled from an HP Envy Sleekbook 6
* Synaptics chip # T1320A
* Physical pinout
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

* Query 0x0C (Continued Capabilities): 12 6C 0
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

## Packet formats

Relative mode (mouse compatible)
```
   | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
(1)| Yo| Xo| Ys| Xs| 1 | M | R | L |
(2)|               X               |
(3)|               Y               |
```

## MCU & note on the code

I used atmel mega32u4. Any Leonardo clone should work. Other avr processors with native USB support should work too. I'm using [external interrupts](https://developerhelp.microchip.com/xwiki/bin/view/products/mcu-mpu/8-bit-avr/structure/extint/). So the clock pin needs to be one of these: 0, 1, 2, 3, 7 (D2, D3, D1, D0, E6, respectively). It's also possible to use pin chagne interrupts with slight changes to the code:

```
*(digitalPinToPCICR(clock_pin)) |= 1 << digitalPinToPCICRbit(clock_pin);
*(digitalPinToPCMSK(clock_pin)) |= 1 << digitalPinToPCMSKbit(clock_pin);

... 

ISR(PCINT0_vect) {
   ...
}
```

## Final product:
![Breadboard](IMG_0914.jpeg)