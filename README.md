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

More info obtained from programatic querys:

* Query 0x00: 01 47 18
```
Version 8.1
```

* Query 0x01: 20 87 40
```
Model 0x0887
```

* Query 0x02: D0 01 23
```
Model sub number: 01
Middle button: no
Pass through: no
Low power: no
Multi finger report: yes
Multi finger detect: yes
Can sleep: no
Palm detect: yes
```

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