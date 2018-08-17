Fixed Jambox Sequencer Demo (Hackerbox #0028)
==========

* Author: Stephen Beechen 
* Copyright (C) 2018 Stephen Beechen.
* Released under the MIT license.

Simple step sequencer for running on a [Hackerboxes Jambox](https://hackerboxes.com/collections/past-hackerboxes/products/hackerbox-0028-jambox) with additional controls for volume, pitch, and square wav transformation.  Requires an [additional library](https://github.com/nhatuan84/esp32-led-matrix) for the 8x8 LED 

How to Use
----------
* Buy a [Jambox](https://hackerboxes.com/collections/past-hackerboxes/products/hackerbox-0028-jambox)
* Follow the inscructions on their [instructable](https://www.instructables.com/id/HACKERBOX-0028-JamBox/) up to step 7
* Downlaod JamboxSequencer.ino from this library, open it in Arduino IDE and flash it.
* Play with the knobs and buttons.

Why did you write this?
---------------------
My friends and I bought a Jambox kit and had a jolly ole' time soldering it together, but were supremely disappointed when all it produced was garbled audio with the included code.  After a great deal of time debugging the circuit, learning about I2S and exploring the capabilities of the ESP32 I determined that the problem was entirely in software!  This was written to fix the bugs plaguing that example, namely:
* The DAC expects its samples to be LSB
* The pre-buffered waveforms introduced weird noise at the end of each cycle because they didn't end exactly at the beginning/end of a complete cylce.
* Buffer timing issues caused the DAC to run out of bytes to stream before they could be recomputed.
* The original scale wasn't precisely tuned, which made chords sound disharmonius.

As an instructional tool the original code was problematic because it made heavy use of bit shifting, used crypic variable names, was almost without explanatory comments, and was full of bugs.

Additionally I added some functionality over the original:
* Knob 4 controls volume.
* Knob 3 increases the retro vibe.
* Knob 2 adjust pitch.
* Knob 1 adjusts cowbell.


License
-------

MIT License

Copyright (c) 2018 Stephen Beechen

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
