/*
dmxChaser - a simple example for espDMX library
Copyright (c) 2016, Matthew Tong
https://github.com/mtongnz/espDMX

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any
later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program.
If not, see http://www.gnu.org/licenses/
*/

#include <espDMX.h>

// 10 steps with 10 channels in each
byte dmxChase[][10] = { { 255, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
                        { 0, 255, 0, 0, 0, 0, 0, 0, 0, 0 },
                        { 0, 0, 255, 0, 0, 0, 0, 0, 0, 0 },
                        { 0, 0, 0, 255, 0, 0, 0, 0, 0, 0 },
                        { 0, 0, 0, 0, 255, 0, 0, 0, 0, 0 },
                        { 0, 0, 0, 0, 0, 255, 0, 0, 0, 0 },
                        { 0, 0, 0, 0, 0, 0, 255, 0, 0, 0 },
                        { 0, 0, 0, 0, 0, 0, 0, 255, 0, 0 },
                        { 0, 0, 0, 0, 0, 0, 0, 0, 255, 0 },
                        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 255},
                        { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
                        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};


void setup() {
  
  // Start dmxA, status LED on pin 12 with full intensity
  dmxA.begin(12);

  // Start dmxB, status LED on pin 13 with 75/255 intensity
  dmxB.begin(13, 75);

}


void loop() {
  for (int i = 0; i < 10; i++) {
    
    // Output channels 1 - 10 on dmxA
    dmxA.setChans(dmxChase[i], 10, 1);

    // Map values 1-5 to dmx channels 101 - 105 on dmxB
    dmxB.setChans(dmxChase[i], 5, 101);

    // 1 second between each step
    delay(1000);
  }
}
