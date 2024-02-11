# Appreciation
 First of all, many thanks to Roman for this brilliant WSPR project!
 The world has never seen a wspr beacon on such an inexpensive and small hardware ;-).
 Simply ingenious is the controller-controlled oscillator, which can be modulated to millihearts! The heart of the beacon and the Projekt.

# The intention of this fork 
 As Roman writes in his project description: "It doesn't require any hardware - Pico board itself only." 
 Unfortunately, the current version can only be used for hours or longer with the help of a gps module.
 But I would like to have a beacon that works, after a first manual synchronization (button) autonomous (previously configurable all 2, 4, 8,... minutes). And maybe even change the frequencies.

 In this initial fork version, the transmission of the beacon can be triggered via a button between pin 27 and 35 on the Pico board. And then, after the transmission ends, it can be triggered again and again.


