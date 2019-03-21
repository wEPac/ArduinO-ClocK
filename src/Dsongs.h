/*
   fontsDigit.h
   
   There are 2 different fonts, they are not compatible. One (Digit) is to print
   the time with vertical slide show, one (Alpha) is to print the text. Both are
   optimized for the usage to perform better speed and to reduce memory usage.

   Written by Eric Paquot on 03/03/2019
*/


#include <avr/pgmspace.h>

//*
const char song01[] PROGMEM = ": :e,8f,8g8a,8b,8c1,8d1,8e1"; //the C major scale. This format is known as RingTone Transfer Language or RTTL(It was used by Nokia's phone company).
const char song02[] PROGMEM = ":d=4 :c,d,e,f";                //d=4 means that every note without a number in front of the letter is assumed to be a quarter note.
const char song03[] PROGMEM = ":o=6,d=2 :16a,16b,8a,4b";
const char song04[] PROGMEM = ":o=5,d=2 :32g,32a,32b,32c";
//*/

// Fur Elise
//                                    12             13                        27                 28         29                        30
const char song11[] PROGMEM = ":d=16 :8a,8p,a,8p,c1, e1,d#1,e1,f1,e1,b,d_1,c1";//, e,g#,b,c1,c1,g,8b, 8a,8p,2p., e1,d#1,e1,f1,e1,b";//,d_1,c1";
const char song12[] PROGMEM = ":d=16 :8e,8p,c,8p,e,  1p,                       1p,              , 1p,      , 1p,                     ";
const char song13[] PROGMEM = ":o=6,d=8 :c, 1p";
const char song14[] PROGMEM = ":o=6,d=8 :c, 1p";


