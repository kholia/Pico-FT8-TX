# Appreciation

First of all, many thanks to Roman for this brilliant WSPR project! The world
has never seen a WSPR beacon on such an inexpensive and small hardware ;-).

Simply ingenious is the controller-controlled oscillator, which can be
modulated to milliherzts - the heart of the beacon and the project!

As Roman writes in his project description: "It doesn't require any hardware -
Pico board itself only."

With this fork, the first transmission of the beacon can be triggered by a
button. And then, after the first transmission ends, it is repeated every 4
minutes controlled by the internal RTC of the Pi Pico. The interval for the
following transmission can be configured.

Dhiru: This fork adds FT8 support.

# Hardware Setup

NEW (Updated): Add a button between Pin 21 (`GP16`) and Pin 23 (`GND`) on the
Pico board.

The antenna connects to Pin 9 (`GP6`) on the Pico board.

![Pico Board](Raspberry-Pi-Pico-Pinout.png)

# Bringing Pico 2 beacon on air

Step 1: Configure the FT8 message (`char *message = "CQ K1TE FN42";`) to send
in the `WSPRbeacon/WSPRbeacon.c` file.

Step 2: Compile the project and copy the `pico-wspr-tx.uf2` file to the
Pico board.

```
mkdir -p ~/repos

cd ~/repos

git clone --recursive https://github.com/raspberrypi/pico-sdk.git

export PICO_SDK_PATH=$HOME/repos/pico-sdk

cmake .; make -j4

./picoload.sh pico-wspr-tx.uf2  # optional
```

Step 3: Power the Pico board

Step 4: Wait for the start time of the next FT8 transfer window. Press the
button to start the next broadcast.

Step 5: Use the WSJT-X software to check if the transfer is within the valid
transfer window. If necessary, repeat the start procedure.

# TODO

https://www.raspberrypi.com/documentation/pico-sdk/high_level.html#group_pico_aon_timer

Use this instead of RTC (hardware_rtc) which is unavailable on RP2350.

# Amplifier?

https://github.com/kholia/HF-PA-v10/tree/master/IRFP-Hacks-v5-SMD is a pretty
rugged amplifier design for getting 1W to 30W RF output on all HF bands!

![Parametric amplfier](screenshots/Screenshot_2024-09-28_08-17-14.png)

In a pinch, you can also try the `2W Amplifier 1-930MHz module` that is
available on Amazon, and other places.

![2W amplifier module](screenshots/ready-made-amp-module.jpg)

# Demo

![Demo 1](screenshots/Screenshot_2024-07-31_09-44-43.png)

# Free power (Solar System)

- Connect a 6v solar panel to the CN3791 module.

- Connect a 18650 cell to the 1S BMS (using B+ and B- pads).

- Connect the 1S BMS to the CN3791 module (using P+ and P- pads).

- Connect the load (the beacon system) to the CN3791 module.

![CN3791 module](screenshots/CN3791-6V-MPPT-Solar-Charger-Module-Trimmed.webp)

![1S BMS](screenshots/BMS-2.webp)

# References

- https://www.wavecom.ch/content/ext/DecoderOnlineHelp/default.htm#!worddocuments/ft8.htm

- https://github.com/kgoba/ft8_lib

- https://github.com/Jochen-bit/pico-WSPR-tx (We have used this fork)

- [100 LED solar garden light teardown (with schematic)](https://www.youtube.com/watch?v=DH4zTmrdc1o)
