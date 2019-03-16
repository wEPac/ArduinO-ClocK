# ArduinO'ClocK
A simple clock without RTC, based on:
  - 1 x Arduino Nano
  - 4 x MAX7219 8x8 Led Matrix
  - 1 x Resistor Light Dependent (optional)
  - 1 x passive Buzzer or Speaker (optional)

RLD and Buzzer are optional.
A base to learn or to build.

This is not so far original, we can find already so many sketch for a clock.

It is using the library <LEDMatrixDriver.hpp> by Bartosz Bielawski. This library is using SPI hardward and a buffer, that displays data fast as lightnings.
(https://github.com/bartoszbielawski/LEDMatrixDriver)

It is displaying digits with sliding effect and many options can be set, like design, font types...

A DS3231 RTC module can be added easily, to preserve time/date settings and to have accurate calendar.


TODO
  - chronometer
  - a RLD (Resistor Light Dependent)
  - settings management, for alarms too
  - music for the alarm (hope tpo manage 2 voices for pretty sound)
