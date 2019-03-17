/*
   fixedStrings.h
   
   There are 2 different fonts, they are not compatible. One (Digit) is to print
   the time with vertical slide show, one (Alpha) is to print the text. Both are
   optimized for the usage to perform better speed and to reduce memory usage.

   Written by Eric Paquot on 03/03/2019
*/


#include <avr/pgmspace.h>


const char * const sDOW[] PROGMEM = {
  "", "Dimanche", "Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi"
};

const char * const sMONTH[] PROGMEM = {
  "", "janvier", "février", "mars", "avril", "mai", "juin",
  "juillet", "août", "septembre", "octobre", "novembre", "décembre"
};

const char * const sMESSAGES[] PROGMEM = {
  "°C", "°F", "Temp: ", "Date: "
};

const char * const sSETTINGS[] PROGMEM = {
  "Set", "Chrono", "Date", "Show Date", "each ", "off", " min", "words", "d:m:y", "m:d:y", "Time", "Alarm1", "Alarm2", "Music1", "Music2", "Bright1", "Bright2"
};

const char * const sSPLASH[] PROGMEM = {
  "A SAMPLE... FOR FUN!", "ArduinO' ClocK"
};
