# Appreciation
 First of all, many thanks to Roman for this brilliant WSPR project!
 The world has never seen a wspr beacon on such an inexpensive and small hardware ;-).
 Simply ingenious is the controller-controlled oscillator, which can be modulated to millihearts! The heart of the beacon and the Projekt.

# The intention of this fork 
 As Roman writes in his project description: "It doesn't require any hardware - Pico board itself only." 
 Unfortunately, the current version can not be used for hours or longer without the help of a gps module.
 But I would like to have a beacon that works, after a first manual synchronization (button) autonomously (previously configurable all 2, 4, 8,... minutes).
 And maybe even change the frequencies ...

 With this fork, the frist transmission of the beacon can be triggered via a button between pin 27 and 35 on the Pico board. And then, after the first transmission ends, it is repeated every 4 minutes controlled by the internal rtc of the pi pico. The interval for the following transfers can be configured.

# Changes on hardware
Just add a button between pin 27 and 35 on the Pico board.

# HOW TO bring pico beacon on air
Step 1: Configure a) your call b) your locator c) the tx frequency d) the transmission interval within the define section of main.c 
Step 2: Compile the project and copy the uf2 file to the pico board. 
Step 3: Power pico board 
Step 4: Wait for the start time of the next wspr transfer window. Press the button to start the next broadcast. 
Step 5: Use the wstj software to check if the transfer is within the valid transfer window. If necessary, repeat the start procedure. 
