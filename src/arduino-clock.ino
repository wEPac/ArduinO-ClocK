
/* Arduino'Clock

   A simple clock for Arduinos, using 4 x MAX7219 Led Matrix.

   Written by Eric Paquot on 03/03/2019

   This is not accurate, this wont record settings after power off,
   this is just a sample and should be used to test the Matrix.

   Features:
      - multi display types
      - Brightness setting
      - Hour & Minute settings
      - 1 Alarm (Cant be changed, it is set in this sketch)

   Including <LEDMatrixDriver.hpp> by Bartosz Bielawski. This library is
   using SPI hardward and a buffer, that displays digit fast as lightnings.
   (https://github.com/bartoszbielawski/LEDMatrixDriver)
      Coordinates follow these rules:
        0--------------------------------------------> X
        |
        |
        V Y
      //create the object
        LEDMatrixDriver::LEDMatrixDriver(NB_MATRIX, CS_PIN, REVERSED);
            NB_MATRIX,  1~255, how many matrix to use
            CS_PIN,     SPI Case Select pin
            REVERSED,   true~false, flip the display for different matrix wiring
      //all these commands work on ALL segments
        void     setEnabled(bool enabled);
        void     setIntensity(uint8_t level);
        void     setPixel(int16_t x, int16_t y, bool enabled);
        bool     getPixel(int16_t x, int16_t y)       const;
        void     setColumn(int16_t x, uint8_t value);
        uint8_t  getSegments()                        const {return N;}
        uint8_t* getFrameBuffer()                     const {return frameBuffer;}
      //flush the data to the display
        void     display();
      //flush a single row to the display
        void     displayRow(uint8_t row) {_displayRow(row);}
      //clear the framebuffer
        void     clear() {memset(frameBuffer, 0, 8*N);}
      //scroll the framebuffer 1 pixel in the given direction
        void     scroll( scrollDirection direction );
            scrollUp = 0, scrollDown, scrollLeft, scrollRight
            ex: nnn.scroll(LEDMatrixDriver::scrollDirection::scrollDown)
      //frameBuffer organisation:

        Buffer byte #     rowY #    columnX #       module #
        -----------------------------------------------------
            0             0         0~7             0
            1             0         8~15            1
            ...
            n-1           0         8*n-1~8*n+6     n-1
            -------------------------------------------------
            n             1         0~7             0
            n+1           1         8~15            1
            ...
            2n-1          1         8*n-1~8*n+6     n-1
            -------------------------------------------------
            ...
        -----------------------------------------------------


   Including <MusicWithoutDelay.h> by nathanRamaNoodles. This library
   allows to plat polyphonic songs. A tricky library using Timer1 and
   Timer2, so choice for pin buzzer is fixed and specific for the board
   we will use. (https://github.com/nathanRamaNoodles/Noodle-Synth)
        Board                         Stable  Max     Pin     Pin     Stereo
                                      Poly.   Poly.   CHA    [CHB]    Support
        ---------------------------------------------------------------------
        Arduino Uno, Nano, Pro Mini   4       8       11     [3]      yes
            (boards with atmega328p)
        Arduino Micro, Pro Micro      4       8       6       N/A     no
            (boards with atmega32u4)
        Arduino MEGA-2560             10      16~18   10      9       yes
        Teensy 2.0                    4       8       A9      N/A     no
        Teensy LC, 3.0+               16      30*     3       4       YES (output > 2 pins)
            (the Teensy 3rd generation)
        ESP8266 (under construction)  2       ~8      RX(i2s) N/A     No!!!

        in thhis sketch we use CHB, so PIN_BUZZER == 3
        

   There are 2 different fonts, they are not compatible. One (Digit) is to print
   the time with vertical slide show, one (Alpha) is to print the text. Both are
   optimized for the usage to perform better speed and to reduce memory usage.

   Must save:
     intensity1               intensity min
     intensity2               intensity max
     IsCelsius                temp in Celcius
     AM_PM                    24/12 h display
     DateN                    date type
     DisplayN                 design type
     show date / show temp
     DesignFont[DISPLAY_MAX]  font type in each design
     AlarmStates[][3]
*/


//-----------------------------------------------------------------------

//#define  DEBUG
#define WIPE_2 1



#include <avr/pgmspace.h>
#include "LEDMatrixDriver.hpp"  // https://github.com/bartoszbielawski/LEDMatrixDriver
#include "MusicWithoutDelay.h"  // https://github.com/nathanRamaNoodles/Noodle-Synth

//#include "VirtualRTC.h"           // a virtual Unix Time RTC to simulate a DS32331 or else

#include "DfontsAlpha.h"        // Data alike font, string message....
#include "DfontsDigit.h"
#include "DfixedStrings.h"
#include "Dsongs.h"


//--- Output pins
#define PIN_BUZZER     3

//--- Input pins for setting Time and display
#define SET_DISPLAY    A0
#define SET_FONT       A1
#define SET_BRIGHT     A2
#define SET_MIN        A3
#define SET_HOUR       A4
#define PIN_RND        A6     // random seed generator
#define PIN_LDR        A7     // light captor

//--- SPI pins definition, adjust here at your wills--
#define PIN_CS_MAX     7
#define PIN_MOSI       11
#define PIN_CLK        13

// For different Matrix, display could be upside down, adjust REVERSED here
#define NB_MATRIX      4
#define MATRIX_WIDTH   NB_MATRIX * 8 //--- Just to know, it can be useful :P
#define     INVERT_SEGMENT_X    0x01
#define     INVERT_DISPLAY_X    0x02
#define     INVERT_Y            0x04
#define REVERSED       0 | INVERT_Y | INVERT_SEGMENT_X
LEDMatrixDriver MAX(NB_MATRIX, PIN_CS_MAX, REVERSED);

// Let play some music
MusicWithoutDelay instrument1(song11);
MusicWithoutDelay instrument2(song02);
MusicWithoutDelay instrument3(song03);
MusicWithoutDelay instrument4(song04);




//-----------------------------------------------------------------------


//--- RTC management
#define TIME_SPEED     1000      // 1000 == 1s, this interval shouldnt be changed
#define DOT_SPEED      2 * (TIME_SPEED / 3)  // after a while we set DP off, pretty with 2/3 rate
unsigned long PreviousMillis;   // tracks the time to prevent roll up after 50 days
byte    TimeFlag;               // to know what digit changed and so, we must display it

unsigned long UnixTime;         // time since 01/01/1970 in seconds
byte    DoW;                    // Day of the Week

#define DATE_MAX       =  3;
byte    DateN          =  0;    // 0:alpha, 1:DD-MM-YYYY, 2:MM-DD-YYYY

//--- About Date/Time
struct  DateTime {      //  format    example
  int  y;   // Year: 0~n    ####      2019
  byte m;   // Month:1~12   #~##      3
  byte d;   // Day:  1~31   #~##      27
  byte hh;  // hour: 0~23   #~##      8
  byte mm;  // min:  0~60   #~##      0
  byte ss;  // s:    0~60   #~##      12
};
//struct  Tbuf   DateTime;
struct  DateTime  Tbuff;            // used to compute in some sub routines
struct  DateTime  Tnow     = {2019, 03, 21,     03, 01, 00};  // {Year, Month, Day,     h, min, s}
struct  DateTime  Told     = Tnow;  // record old digits to make the slide show
struct  DateTime  Talarm1  = {2019, 03, 17,     02, 22, 00};  // {Year, Month, Day,     h, min, s}
struct  DateTime  Talarm2  = {2019, 03, 17,     02, 23, 00};  // {Year, Month, Day,     h, min, s}

int     AlarmTime      = 28;    // how long we ring in sec (shouldnt be < 5s)
unsigned long AlarmMillis;      // tracks the time to stop the alarm
#define Alarm0Flag     true     // set alarm 1, true:on / false:off (when ringing, used like index)
#define Alarm1Flag     true     // set alarm 2, true:on / false:off (when ringing, used like index)
// Alarm(x)Repeat
//              every_hours,             minutes match:  bit0   0x03   Bxxxxx011
//              every_days,    hours and minutes match:  bit1   0x02   Bxxxxx010
//              date_of_month, hours and minutes match:  bit2   0x00   Bxxxxx000 (see bit 3 to repeat)
//              day_of_week,   hours and minutes match:  bit2   0x04   Bxxxxx100 (see bit 3 to repeat, bits 4~6 current day)
//              repeat date_month, day_week              bit3   0x08   Bxxxx1xxx
//              current DoW from the alarm               bit4~6        Bx111xxxx
//      so alarm will repeated when (Alarm1Repeat & B00001011) is true, 0x0B
#define Alarm0Repeat   B00000011
#define Alarm1Repeat   B00000010
#define Alarm0Days     B00000001   // for repeat day_of_week, bit selects days (0x01:true == Sunday)
#define Alarm1Days     B00111110   //     B00000001:Sunday, B00111110:all days but Sunday and Saturday
byte AlarmStates[][3] = {
  {Alarm0Flag, Alarm0Repeat, Alarm0Days},
  {Alarm1Flag, Alarm1Repeat, Alarm1Days}
};
// Alarm Kind / Repeat
//              every_hours,             minutes match:  0x01   Bxxxx0011
//              every_days,    hours and minutes match:  0x02   Bxxxx0100
//              date_of_month, hours and minutes match:  0x04   Bxxxx0000 (see bit 4 to repeat)
//              day_of_week,   hours and minutes match:  0x08   Bxxxx1000 (see bits 5~7 to repeat)
//              repeat date_of_month, true/false         0x10   Bxxx1xxxx
//              repeat day_of_week, select days          0x02~0x08, bit true for a day (0x02:true == Sunday)
// so alarm will repeated when (Alarm1Repeat & B11110111) is true, 0xF7
//byte    Alarm1Repeat   = B0000;
//byte    Alarm2Repeat   = true;

//--- About Display
#define WHITE          0xFF
#define BLACK          0x00
#define INTENSITY_MAX  8        // 0~15 but upon 7 there is no real change
byte    Intensity      = 0;     // 0~7

byte    IsCelsius      = true;
#define AM_PM          true

#define DISPLAY_MAX    6        // how many design we have
byte    DisplayN       = 5;     // 0~DISPLAY_MAX-1, index to show design from array DESIGN[][] settings  

// DesignFont[] contain the selected font index used for a design in the array DESIGN[]
byte    DesignFont[DISPLAY_MAX] = {2, 0, 1, 1, 0, 0};   // font = FONT_DIGIT[fidx + DesignFont[DisplayN]] 
// DESIGN[][] contains settings for each kind of display, each row is a kind:
//       0      fMax,               how many fonts we have in this design (FONT_DIGIT[] must be in right order)
//       1      fidx,               hour: 1st font of a kind (-1 to hide this field), font is from [fidx, fidx + fMax[
//       2-3    h10xy,              hour: x, y coordinates for 1st digit 10
//       4-5    h1xy,               hour: x, y coordinates for 2nd digit 01
//       6-10   font, m10xy, m1xy,  same for minute
//      11-15   font, s10xy, s1xy,  same for second
//      16-17   dotW,H,             dots separator: Width, Height
//      18-20   x,y1,y2             dots separator: x coordinate, y1 coord for dot 1, y2 coord for dot 2
//      21-22   al1xy,              alarm1 dot:     x, y coordinates
//      23-24   al2xy,              alarm2 dot:     x, y coordinates
//      25-27   opt,x,y,            option:
//                                    0:null
//                                    1:timeline
//                                    2:Temperature
//                                  x, y coordinates for the option
#define DESIGN_MAX  28
const byte DESIGN[][DESIGN_MAX] PROGMEM =  // DisplayN will give the font and coords
{ //fMax,fidx,h10xy,h1xy,   font,m10xy,m1xy,    font,s10xy,s1xy,    dotW,H,x, y1, y2, al1xy, al2xy,     opt,x, y,
  //         2      4        6      8     10        12     14       16     18    20       22     24         26  
  {4,    0,  0, 0,  8, 0,    0, 18, 0, 26, 0,   -1,  0, 0,  0, 0,    2, 2, 15, 1, 4,   15, 7, 16, 7,    0,  0, 0}, // 0 h:m
  {4,   -1,  0, 0,  0, 0,    0,  0, 0,  8, 0,    0, 18, 0, 26, 0,    2, 2, 15, 1, 4,   15, 7, 16, 7,    0,  0, 0}, // 1 m:s
  {2,   14,  1, 1,  8, 1,   14, 18, 1, 25, 1,   -1,  0, 0,  0, 0,    2, 1, 15, 2, 4,   15, 0, 16, 0,    1,  0, 7}, // 4 h:m + tL
  {2,   -1,  0, 0,  0, 0,   14,  1, 1,  8, 1,   14, 18, 1, 25, 1,    2, 1, 15, 2, 4,   15, 0, 16, 0,    1,  0, 7}, // 5 m:s + tL
  {2,    6,  0, 0,  5, 0,    6, 12, 0, 17, 0,   10, 23, 2, 28, 2,    1, 2, 10, 1, 4,   30, 0, 31, 0,    0,  0, 0}, // 2 h:m:s
  {2,    6,  0, 0,  5, 0,    6, 12, 0, 17, 0,   -1,  0, 0,  0, 0,    1, 2, 10, 1, 4,   30, 0, 31, 0,    2,  0, 0}, // 3 h:m:T
};


//--- About Time digit
#define SLIDE_SPEED   40        // 20~100 how much to slower digits sliding (shouldnt be slower than 20 = 1/3 s)
byte    ScrollTime;             // true: Time will come on screen, scrolled from right to center

//--- About Alpha Text
#define SCROLL_SPEED  50        // 20~80, how slow to scroll text
#define WIPE_SPEED    SCROLL_SPEED / 3    // How slow to show/hide time display from screen
#define SPACE_WIDTH   3         // define width of " " since code is handling whitespace
#define CHAR_SPACING  1         // pixels between characters
String  S_Text;                 // text to be scrolled
byte*   pFont;                  // pointer for current font
byte    FontWidth;              // current font dimension
byte    FontHeight;

//--- About Led Matrix frameBuffer
byte*   pBuffer;                // frameBuffer address



//-----------------------------------------------------------------------



void setup()
{
#ifdef DEBUG
  Serial.begin(115200);
#endif
  //--- initialize I/O pins
  pinMode(PIN_LDR,      INPUT_PULLUP); // for better ADC
  pinMode(SET_DISPLAY,  INPUT_PULLUP);
  pinMode(SET_FONT,     INPUT_PULLUP);
  pinMode(SET_BRIGHT,   INPUT_PULLUP);
  pinMode(SET_MIN,      INPUT_PULLUP);
  pinMode(SET_HOUR,     INPUT_PULLUP);
  pinMode(PIN_BUZZER,   OUTPUT);

  //--- set Led Matrix on
  pBuffer = MAX.getFrameBuffer();
  //ClearScreen();
  //MAX.setIntensity(Intensity);
  MAX.setEnabled(true);
  ClearScreen();

  //testText();
  SplashScreen();

  MAX.setIntensity(Intensity);
  
  setTime();                    // set time for Soft RTC, Unix Time ref, 1970-01-01 1h:00m00s, Thursday
  DoW  = getTime();             // get time from Soft RTC, stored in Tbuff
  Tnow = Tbuff;                 // set Arduino RTC
  
  PreviousMillis = millis();    // the Arduino RTC reference now
  
  ScrollTime = true;            // move the time on screen from right to center
  ShowTime(0xFF, true);
}

void loop()
{
  //testText();
  Tbuff = Tnow;
  ComputeTime(true);  // computes current TimeStamp and set TimeFlag for each digit it changed
  Tnow  = Tbuff;

  if (!Tnow.ss) AlarmCheckStart();     // each min, check if we must ring for the alarm

#ifdef PIN_LDR
  SetIntensity();
#endif

  // speed up to reach the current time after a busy period (ie displaying alarm or message)
  if ((unsigned long)(millis() - PreviousMillis) > TIME_SPEED)
  {
    TimeFlag     = 0xFF;  // next loop, force to show all digits (not only those they changed)
    goto endLoop;
  }

  // set flag, used to prevent duplicated ACTIONS, see below...
  byte change_flag = 0xFF;

  ////---> run the alarm when they started
  //if      (AlarmStates[0][0] > 1 || AlarmStates[1][0] > 1)
  //{
  //  RunAlarm();
  //  change_flag  = 0x00;     // dont display DP, lock settings
  //}
  //---> when no parity displays date, dont display DP & lock settings
  //else if (!(Tnow.mm & 0x01) && Tnow.ss == 31)
  if      (!(Tnow.mm & 0x01) && Tnow.ss == 31)
  {
    ShowDate();
    change_flag  = 0x00; 
    //ShowTime(0xFF, true);
  }
  //---> when parity displays temperature, dont display DP & lock settings
  else if ( (Tnow.mm & 0x01) && Tnow.ss == 31)
  {
    ShowTemp();
    change_flag  = 0x00;
    //ShowTime(0xFF, true);
  }
  //===> No Special display
  // So, show current time (print only digits they changed, accorded to TimeFlag)
  else
  {
    ShowTime(0x00, true);
  }


  //--------------------------------------------------------------
  //---> wait for 1 second betwwen each loop
  //--------------------------------------------------------------
  // This is our soft RTC, not so bad, but lost with power down
  // We wont request Virtual RTC every seconds, but to synch this soft RTC
  // While waiting, we can check for settings buttons
  while ((unsigned long)(millis() - PreviousMillis) < TIME_SPEED)
  {
    //--- Set DP off ---
    // after a while we set DP off, pretty with 2/3 rate, so 650
    if ((change_flag & 0x01) && ((unsigned long)(millis() - PreviousMillis) > DOT_SPEED))
    {
      change_flag ^= 0x01;  // we do it once only, so we set the flag
      ShowDP(false);        // set DP off (this will set on Alarm dots)
      MAX.display();
    }

    //--- Adj Intensity ---
    // set brightness
    if ((change_flag & 0x02) && digitalRead(SET_BRIGHT) == LOW)
    {
      change_flag ^= 0x02;          // we do it once only, so we set the flag
      Intensity++;
      Intensity   %= INTENSITY_MAX;
      MAX.setIntensity(Intensity);  // set new brightness
    }

    //--- Adj Display type ---
    // choose between the different design to display time
    if ((change_flag & 0x04) && digitalRead(SET_DISPLAY) == LOW)
    {
      change_flag ^= 0x04;        // we do it once only, so we set the flag
      DisplayN++;
      DisplayN    %= DISPLAY_MAX;
      ShowTime(0xFF, true);       // show current setting
    }

    //--- Adj Font type ---
    // choose between different fonts to display a design
    if ((change_flag & 0x08) && digitalRead(SET_FONT) == LOW)
    {
      change_flag ^= 0x08;        // we do it once only, so we set the flag
      DesignFont[DisplayN]++;
      //DESIGN[DisplayN][28] %= DESIGN[DisplayN][ 0];
      DesignFont[DisplayN] %= pgm_read_byte_near(DESIGN[DisplayN]);
      ShowTime(0xFF, true);       // show current setting
    }

    //--- Adj Time Value ---
    // adjust date, hour, min....
    if ((change_flag & 0x10) && digitalRead(SET_MIN) == LOW)
    {
      change_flag ^= 0x10;        // we do it once only, so we set the flag
      Told.mm = Tnow.mm;          // record current value for min
      Tnow.mm++;
      Tbuff   = Tnow;
      TimeFlag |= 0x04;           // indicate that unit minute changed
      ComputeTime(false);         // compute time (but sec is set to 0 && hour will never increase)
      Tnow    = Tbuff;
      setTime();                  // set time for Soft RTC, Unix Time ref, 1970-01-01 1h:00m00s, Thursday
      ShowTime(false, true);      // show current setting
    }

    if ((change_flag & 0x20) && digitalRead(SET_HOUR) == LOW)
    {
      change_flag ^= 0x20;        // we do it once only, so we set the flag
      Told.hh = Tnow.hh;          // record current value for hour
      Tnow.hh++;
      Tbuff   = Tnow;
      TimeFlag |= 0x10;           // indicate that unit hour changed
      ComputeTime(false);         // compute time (but sec is set to 0 && hour will never increase)
      Tnow    = Tbuff;
      setTime();                  // set time for Soft RTC, Unix Time ref, 1970-01-01 1h:00m00s, Thursday
      ShowTime(false, true);      // show current setting
    }
  }
  //--------------------------------------------------------------


  
  endLoop:
  //---> set new time ref => + 1s
  PreviousMillis += 1000;         // we wont use millis() here to keep our RTC accurate, add 1s for each loop
  Told = Tnow;                    // save current time to slide digits
  Tnow.ss++;                      // add 1s to current time
  TimeFlag       |= 0x01;         // seconds will always increase inside the loop => flag it to force digit on screen
  UnixTime++;                     // virtual RTC, UnixTime, add 1s for each loop

  // ===> synch time with the RTC at midnight
  if (!Tnow.hh && !Tnow.mm && !Tnow.ss)
  {
    while (!Tbuff.hh) DoW = getTime();  // get time from Soft RTC, stored in Tbuff
    Tnow = Tbuff;
  }
}

void SetIntensity()
{
  #define INT_MIN 600
  #define INT_MAX 1024
  byte  n;
  int   val     = 0;
  for (n = 0; n < 10; n++)
  {
    val += analogRead(PIN_LDR);
    delay(1);
  }
  val        /= n;
  val         = max(0, val - INT_MIN);
  val         = (INTENSITY_MAX * val) / (INT_MAX - INT_MIN);
  val         = min(INTENSITY_MAX - 1, val);
  Intensity   = val;
  
  MAX.setIntensity(Intensity);
}



//-----------------------------------------------------------------------
// Display functions
//-----------------------------------------------------------------------



//byte* segmentBuffer(byte coordX, byte coordY)
//{
//  if (REVERSED) coordY = 7 ^ coordY;
//  return pBuffer + (coordY * NB_MATRIX) + (coordX >> 3);
//}

void ClearScreen()
{
  MAX.clear();
  MAX.display();
}

void FillScreen(byte mask1, byte mask2)
{
  // fill screen with alternative masks for rows
  int y_idx = 8;
  while(y_idx--) setRow(y_idx, ((y_idx & 0x01) ? mask2 : mask1));
}

void ReverseScreen()
{
  int idx = 8 * NB_MATRIX;
  while (idx--) pBuffer[idx] ^= 0xFF;
}

void WipeScreen(byte dir, byte rest)
{
  // scroll screen content to a direction, end with clean wiped SCREEN
  //      dir:0,  to left
  //          1,  to right
  //          2,  to down
  //          3,  to up
  unsigned long timerStamp = millis();
  byte   idx  = (dir < 2) ? NB_MATRIX * 8 : 8;
  while (idx--)
  {
    while ((unsigned long)(millis() - timerStamp) < rest);
    timerStamp = millis();
    if      (!dir)     MAX.scroll(LEDMatrixDriver::scrollDirection::scrollLeft);
    else if (dir == 1) MAX.scroll(LEDMatrixDriver::scrollDirection::scrollRight);
    else if (dir == 2) MAX.scroll(LEDMatrixDriver::scrollDirection::scrollDown);
    else if (dir == 3) MAX.scroll(LEDMatrixDriver::scrollDirection::scrollUp);
    MAX.display();
  }
}

void CrunchScreen(byte rest)
{
  // crunch screen content from sides to center, end with clean wiped SCREEN
  unsigned long timerStamp = millis();
  byte   center = NB_MATRIX * 8 / 2;
  byte   x_idx  = center;
  while (x_idx--)
  {
    MAX.setColumn(center - x_idx - 1, WHITE);
    MAX.setColumn(center - x_idx - 2, BLACK);

    MAX.setColumn(center + x_idx,     WHITE);
    MAX.setColumn(center + x_idx + 1, BLACK);
    
    MAX.display();
    while ((unsigned long)(millis() - timerStamp) < rest * 2);
    timerStamp = millis();
  }
  MAX.setColumn(center - 1, BLACK);
  MAX.setColumn(center    , BLACK);
  MAX.display();
}

void DropScreen(byte rest)
{
  unsigned long timerStamp = millis();
  byte y_idx  = 8 * NB_MATRIX;
  while (y_idx--)
  byte y_idx  = 0;
  for (byte y_idx = 0; y_idx < 8; y_idx++)
  {
    if (y_idx < 7)
    {
      byte x_idx = NB_MATRIX;
      while (x_idx--) pBuffer[x_idx + (y_idx + 1) * NB_MATRIX] |= pBuffer[x_idx + y_idx * NB_MATRIX];
    }
    setRow(y_idx, BLACK);
    
    MAX.display();
    while ((unsigned long)(millis() - timerStamp) < (rest * 30) / (y_idx + 1));
    timerStamp = millis();
  }
}

void DrawPixel(byte coordX, byte coordY, byte isShown)
{
  MAX.setPixel(coordX, coordY, isShown);
}

void setRow(byte coordY, byte mask)
{
  // draw the mask all along a row
  byte x_idx  = NB_MATRIX;
  int  offset = coordY * NB_MATRIX;
  while (x_idx--) pBuffer[x_idx + offset] = mask;
}

void setColumn(byte coordX, byte mask)
{
  MAX.setColumn(coordX, mask);
}

void DrawLineH(byte coordX, byte coordY, byte len, byte mask)
{
  //if (len < 1) return;
  //while (len--) MAX.setPixel(coordX + len, coordY, mask);
  
  if (len < 1 || coordY > 7) return;
  //if (len < 1 || coordY > 7 || coordX >= NB_MATRIX * 8) return;

  byte deviceNb = coordX / 8;  coordX %= 8;
  while (len && (deviceNb < NB_MATRIX))
  {
    if (len > 7 && !coordX)   // X == 0 && length > 7, a full segment
    {
      pBuffer[coordY * NB_MATRIX + deviceNb] = mask;
      len -= 8;
    }
    else                      // X != 0 || length < 8, start / stop in middle of a device
    {
      while (len && coordX < 8)
      {
        MAX.setPixel(deviceNb * 8 + coordX, coordY, bitRead(mask, coordX));
        len--; coordX++;
      }
      coordX = 0;
    }
    deviceNb++;
  }
}

void DrawLineV(byte coordX, byte coordY, byte len, byte mask)
{
  if (len < 1) return;
  mask = bitRead(mask, coordX % 8);
  while (len--) MAX.setPixel(coordX, coordY + len, mask);
}

void DrawSquare(byte coordX, byte coordY, byte lenX, byte lenY, byte mask)
{
  DrawLineH(coordX,            coordY,            lenX, mask);
  DrawLineV(coordX + lenX - 1, coordY,            lenY, mask);
  DrawLineH(coordX,            coordY + lenY - 1, lenX, mask);
  DrawLineV(coordX,            coordY,            lenY, mask);
}

void DrawSquareFilled(byte coordX, byte coordY, byte lenX, byte lenY, byte mask)
{
  if (lenY < 1) return;
  while (lenY--) DrawLineH(coordX, coordY + lenY, lenX, mask);
}



//-----------------------------------------------------------------------
// Text functions
//-----------------------------------------------------------------------



void setFont(byte* font_name)
{
  FontWidth  = pgm_read_byte_near(font_name);
  FontHeight = pgm_read_byte_near(font_name + 1);
  pFont      = font_name + FontWidth;
}

//=========> display the public String "S_Text" with scrolling, at pos Y
void ScrollText(int coordY, bool append, bool offScreen)
{
  // append:true    original screen is scrolled while text appears
  // offScreen:true text is scrolled until to disappear from screen
  unsigned long timerStamp = millis();
  int text_length = S_Text.length();
  int text_width  = getTextWidth();
  int xPos        = MATRIX_WIDTH - 1;                               // start out of screen at right
  int str_offset  = text_width + ((offScreen) ? MATRIX_WIDTH : 0);  // end out of screen at left
  text_width      = ((offScreen) ? MATRIX_WIDTH : 0);
  
  // scroll to left then draw each char from the string, one by one column
  int text_offset = 0;  // start at 1st char
  int char_offset = 0;  // start at first byte from the char
  if (!append) MAX.clear();
  while (str_offset-- > 0)
  {
    while ((unsigned long)(millis() - timerStamp) < SCROLL_SPEED);
    timerStamp = millis();

    if (str_offset >= text_width)
    {
      MAX.scroll(LEDMatrixDriver::scrollDirection::scrollLeft);
      int  offset = setText(text_length, text_offset, char_offset, xPos, coordY);
      char_offset = int( offset       & 0x00FF);
      text_offset = int((offset >> 8) & 0x00FF);
    }
    else MAX.scroll(LEDMatrixDriver::scrollDirection::scrollLeft);
    
    MAX.display();
    
    //AlarmSong();
  }
  S_Text = "";
}

// write text of the given length for the given position, into the driver buffer.
// return last offset from char to be shown inside the visible area
unsigned long setText(int text_length, int text_offset, int char_offset, int coordX, int coordY)
{
  for (int i = text_offset; i < text_length; i++)
  {
    // ------ stop if char is outside the right visible area
    if (coordX >= MATRIX_WIDTH) break;

    byte  ascII      = S_Text[i] - 32;  // font starts with " " space char
    byte* pChar      = pFont + FontWidth * ascII;
    int   char_width = getCharWidth(ascII, pChar);

    // ------ draw if last char is not outside the left visible area
    if (coordX > - FontWidth + char_offset)
    {
      // draw char to the driver buffer by passing position (x,y), char width and char start column
      for (int iX = char_offset; iX < char_width; iX++)
      {
        char_offset      = iX;
        if (coordX >= MATRIX_WIDTH) return (text_offset << 8) | (char_offset); // char segment is outside visible area
        
        //byte segment     = pChar[char_start + iX];
        byte segment     = pgm_read_byte_near(pChar + iX);
        byte bitMask     = (char_offset == char_width - CHAR_SPACING) ? false : true; // CHAR_SPACING bytes => mask == 0
        for (int iY = 0; iY < FontHeight; iY++)
        {
          MAX.setPixel(coordX, coordY + iY, (bool)(segment & bitMask));
          bitMask <<= 1;
        }
        coordX++;
      }
      // last char was fully inside the visible area
      text_offset++;
      char_offset = 0;
    }
  }
  return (text_offset << 8) | (char_offset);
}

// calculates character width
int getCharWidth(byte ascII, byte* pChar)
{
  byte char_width = FontWidth;

  if (ascII == 0) return char_width / 2;  // " " space char

  while (char_width--)                    // check for the 1st byte is not empty
  {
    //if (font[ascII][char_width]) break;
    if (pgm_read_byte_near(pChar + char_width)) break;
  }
  char_width++;
  if      (ascII ==  1) char_width++;     // give extra spacing for "!"
  else if (ascII == 14) char_width++;     // give extra spacing for "."

  return char_width + CHAR_SPACING;       // give the right width with CHAR_SPACING at the end
}

// calculates the public String "S_Text" width using variable character width
int getTextWidth()
{
  int text_width = 0;
  int text_idx   = S_Text.length();
  while (text_idx--)
  {
    byte  ascII       = S_Text[text_idx] - 32;
    byte* pChar       = pFont + FontWidth * ascII;
    int   char_width  = getCharWidth(ascII, pChar);
    text_width       += char_width;
  }
  return text_width - CHAR_SPACING; // remove last CHAR_SPACING from the string
}

// Replace extended ascII char to match with our font
String convertText(String text)
{
  int len = text.length();
  if (len)
  {
    for (byte idx = 0; idx < len; idx++)
    {
      byte val = byte(text.charAt(idx));
      if      (val == 153)   text.setCharAt(idx, char(127)); // 127 = °
      else if (val == 194 || val == 195) // extended ascII
      {
        text.remove(idx, 1);
        len--;
        val           = byte(text.charAt(idx));
        byte newAscII = 0;
        switch (val)
        {
          case 176: newAscII = 127; break; // 127 = °
          case 160: newAscII = 128; break; // 128 = à
          case 162: newAscII = 129; break; // 129 = â
          case 168: newAscII = 130; break; // 130 = è
          case 169: newAscII = 131; break; // 131 = é
          case 170: newAscII = 132; break; // 132 = ê
          case 171: newAscII = 133; break; // 133 = ë
          case 174: newAscII = 134; break; // 134 = î
          case 175: newAscII = 135; break; // 135 = ï
          case 185: newAscII = 136; break; // 136 = ù
          case 187: newAscII = 137; break; // 137 = û
        }
        text.setCharAt(idx, char(newAscII));
      }
    }
  }
  return text;
}
/*
  // A little tool to find the correspondence for special char
  void giveCharset()
  {
  Serial.begin(57600);
  delay(100);
  String text = "°àâèéêëîïùû";
  byte   len  = text.length();
  Serial.print("Length = "); Serial.println(len);

  for (byte idx = 0; idx < len; idx++)
  {
    byte val    = byte(text.charAt(idx));
    byte indice = 0;
    if (val == 194 || val == 195) // extended ascII (composed from 2 bytes)
    {
      indice = val;
      val    = byte(text.charAt(idx +1));
      text.remove(idx, 1);
      len--;
    }
    Serial.print(idx); Serial.print(" => "); Serial.print(indice); Serial.print(", "); Serial.println(val);
  }
  Serial.end();
  }
  //*/



//-----------------------------------------------------------------------
// Times functions - virtual RTC, UnixTime
//-----------------------------------------------------------------------



float getTemp()
{
  // The internal temperature has to be used with the internal reference of 1.1V.
  // Channel 8 can not be selected with the analogRead function yet.

  ADMUX   = (_BV(REFS1) | _BV(REFS0) | _BV(MUX3));  // set the internal reference and mux
  ADCSRA |= _BV(ADEN);                              // enable the ADC
  delay(20);                                        // wait for voltages to become stable

  Serial.begin(9600);
  unsigned int wADC;
  byte n = 11;
  while (n--)
  {
    ADCSRA |= _BV(ADSC);              // start the ADC
    while (bit_is_set(ADCSRA,ADSC));  // detect end-of-conversion
    wADC   += (n < 10) ? ADCW : 0;    // read register "ADCW", drop 1st read
    delay(1);
  }
  wADC /= 10;

  // The offset of 324.31 could be wrong. It is just an indication
  float temp = (wADC - 324.31) / 1.22;   // in °C
  if (IsCelsius) return temp;
  return (temp * 9) / 5.0 + 32;          // in °F
}

void setTime()
{ 
  // Use the content from Tnow to compute UnixTime
  unsigned long SECS_PER_DAY  = 86400;
  unsigned long SECS_PER_HOUR = 3600;
  unsigned long SECS_PER_MIN  = 60;
  int i;
  UnixTime = 0;
  
  i = Tnow.y;                                 // Unix Time [1970]/01/01, 1h00:00 Thursday 
  while (i-- > 1970) UnixTime += (LeapYear(i) ? 366 : 365) * SECS_PER_DAY;

  i = Tnow.m;                                 // 1~12, Unix Time 1970/[01]/01, 1h00:00 Thursday
  while (--i)
  {
    if      (i == 2)
      UnixTime += (LeapYear(Tnow.y) ? 29 : 28) * SECS_PER_DAY;
    else if (i == 4 || i == 6 || i == 9 || i == 11)
      UnixTime += 30 * SECS_PER_DAY;
    else
      UnixTime += 31 * SECS_PER_DAY;
  }
  UnixTime += (Tnow.d  - 1) * SECS_PER_DAY;   // 1~31, Unix Time 1970/01/[01], 1h00:00 Thursday 
  UnixTime += (Tnow.hh - 1) * SECS_PER_HOUR;  // Unix Time 1970/01/01, [1h]00:00 Thursday 
  UnixTime += Tnow.mm * SECS_PER_MIN;
  UnixTime += Tnow.ss;

#ifdef DEBUG
  Serial.println("==================");
  //Serial.print("UNIX ref  = "); Serial.println(1552863480); //Serial.println("xxxx");
  Serial.print("UNIX time = "); Serial.println(UnixTime);
#endif
}

byte getTime()
{
  // Paste Date & Time into Tbuff, computed from UnixTime, return Day of the Week
  unsigned long time = UnixTime;
  unsigned long day_of_week;
  unsigned int  days;
  
  Tbuff.ss = time % 60;
  time /= 60;
  Tbuff.mm = time % 60;
  time /= 60;
  time += 1;                    // Unix Time 1970/01/01, [1h]00:00 Thursday
  Tbuff.hh = time % 24;
  time /= 24;
  
  DoW   = ((time + 4) % 7) + 1; // 1~7, Unix Time 1970/01/[01], 1h00:00 [Thursday](+4), 1st day is Sunday => +1

  Tbuff.y = 0;
  days    = 0;
  while (time >= (days += (unsigned long)((LeapYear(Tbuff.y) ? 366 : 365)))) Tbuff.y++;
  Tbuff.y += 1970;              // Unix Time [1970]/01/01, 1h00:00 Thursday
  
  days -= (unsigned long)(LeapYear(Tbuff.y) ? 366 : 365);
  time -= days; // days in the current year
  
  Tbuff.m = 0;
  days    = 0;
  while (time >= days)
  {
    time -= days;
    Tbuff.m++;                  // Unix Time 1970/[01]/01, 1h00:00 Thursday => we start with inc
    
    days    = 31;
    if      (Tbuff.m == 2) days = (LeapYear(Tbuff.y) ? 29 : 28);
    else if (Tbuff.m == 4 || Tbuff.m == 6 || Tbuff.m == 9 || Tbuff.m == 11) days = 30;
  }
  
  Tbuff.d = time + 2;           // Unix Time 1970/01/[01], 1h00:00 Thursday => +1, 1st day == 1 => +1
#ifdef DEBUG
  Serial.println("------------------");
  //Serial.print("ref date = "); Serial.println("17 / 3 / 2019");
  Serial.print("now date = ");
  Serial.print(Tbuff.d); Serial.print(" / "); Serial.print(Tbuff.m); Serial.print(" / "); Serial.println(Tbuff.y);
  Serial.print("now time = ");
  Serial.print(Tbuff.hh); Serial.print(":"); Serial.print(Tbuff.mm); Serial.print(":"); Serial.println(Tbuff.ss);
  Serial.print("DoW = "); Serial.println(DoW);
#endif
  return DoW;
}

byte getDoW()
{
  return getTime();
}



//-----------------------------------------------------------------------
// Times functions - compute & display
//-----------------------------------------------------------------------



bool LeapYear(int year)
{
  // if year (1970~xxxx) is a leap year, return true
  return !((year % 4) && (!(year % 100) || (year % 400)));
}

void ComputeTime(bool full)
{
  // full:true, switch upon 59 seconds will increase minutes
  // false is used when we are setting time manualy
#ifdef DEBUG
  full = true;
#endif

  // Tbuff.y; Tbuff.m; Tbuff.d; Tbuff.hh; Tbuff.mm; Tbuff.ss;
  //byte flag = 0;

  // prevent time to change when we re setting it, allow to set s to zero as well
  if (!full)
  {
    Tbuff.ss  = 0;
    TimeFlag &= ~0x01;
  }
  if (!(Tbuff.ss % 10))
  {
    TimeFlag |= (TimeFlag & 0x01) ? 0x02 : 0;
    if (Tbuff.ss > 59)
    {
      Tbuff.ss = 0;
      if (full)
      {
        TimeFlag |= 0x04;
        Tbuff.mm++;
      }
    }
  }
  if (!(Tbuff.mm % 10))
  {
    TimeFlag |= (TimeFlag & 0x04) ? 0x08 : 0;
    if (Tbuff.mm > 59)
    {
      Tbuff.mm = 0;
      if (full)
      {
          TimeFlag |= 0x10;
          Tbuff.hh++;
      }
    }
  }
  if (!(Tbuff.hh % 10) || Tbuff.hh > 23)
  {
    TimeFlag |= (TimeFlag & 0x10) ? 0x20 : 0;
    if (Tbuff.hh > 23)
    {
      Tbuff.hh = 0;
      TimeFlag |= 0x30;
      if (full)
      {
        Tbuff.d++;
        DoW = getTime();
      }
    }
  }
  /*
  if (    (Tbuff.d > 27 && Tbuff.m == 2 && !LeapYear(Tbuff.y))
       || (Tbuff.d > 28 && Tbuff.m == 2)
       || (Tbuff.d > 29 && (Tbuff.m == 4 || Tbuff.m == 6 || Tbuff.m == 9 || Tbuff.m == 11))
       || (Tbuff.d > 30))
  {
    Tbuff.d = 1;
    if (full) Tbuff.m++;
  }
  if (Tbuff.m > 11)
  {
    Tbuff.m = 1;
    if (full) Tbuff.y++;
  }
  //*/
}

void ShowTime(byte flag, bool showIt)
{
  // Show current time (Tnow) on screen, This function has 4 parameters:
  //    2 global vars:
  //        ScrollTime,   true:Time appear from right and scroll to center of screen,   false:no scroll
  //        TimeFlag,     0x00~0xFF:flag to know what digit changed and must be printed (0x01:Unit seconds)
  //    2 private vaars:
  //        flag,         0x00~0xFF:force to print some/all digits
  //        showIt        true:display the result,  false:only fill the frameBuffer
  //    0x01:second unit
  //    0x02:second dozen
  //    0x04:minute unit
  //    0x08:minute dozen
  //    0x10:hour unit
  //    0x20:hour dozen

  TimeFlag |= flag;
  
  unsigned long timerStamp;
  
  if (ScrollTime)
  {
    TimeFlag   = 0xFF;
    ScrollTime = 8 * NB_MATRIX;
  }
  else ScrollTime = 1;

  // set values and pointers for h, m, s for the next loop, so it will run faster
  byte  fontN    = DesignFont[DisplayN];

  int   font_idx;
  //font_idx       = DESIGN[DisplayN][ 1] + fontN;
  font_idx       = pgm_read_byte_near(DESIGN[DisplayN] +  1) + fontN;
  byte  width_h  = FONT_DIGIT_SIZE[font_idx][0];
  byte  height_h = FONT_DIGIT_SIZE[font_idx][1];
  font_idx      *= FONT_DIGIT_STRIDE;
  byte* pNow_h10 = FONT_DIGIT[font_idx + Tnow.hh / 10];
  byte* pNow_h1  = FONT_DIGIT[font_idx + Tnow.hh % 10];
  byte* pOld_h10 = FONT_DIGIT[font_idx + Told.hh / 10];
  byte* pOld_h1  = FONT_DIGIT[font_idx + Told.hh % 10];

  //font_idx       = DESIGN[DisplayN][ 6] + fontN;
  font_idx       = pgm_read_byte_near(DESIGN[DisplayN] +  6) + fontN;
  byte  width_m  = FONT_DIGIT_SIZE[font_idx][0];
  byte  height_m = FONT_DIGIT_SIZE[font_idx][1];
  font_idx      *= FONT_DIGIT_STRIDE;
  byte* pNow_m10 = FONT_DIGIT[font_idx + Tnow.mm / 10];
  byte* pNow_m1  = FONT_DIGIT[font_idx + Tnow.mm % 10];
  byte* pOld_m10 = FONT_DIGIT[font_idx + Told.mm / 10];
  byte* pOld_m1  = FONT_DIGIT[font_idx + Told.mm % 10];

  //font_idx       = DESIGN[DisplayN][11] + fontN;
  font_idx       = pgm_read_byte_near(DESIGN[DisplayN] + 11) + fontN;
  byte  width_s  = FONT_DIGIT_SIZE[font_idx][0];
  byte  height_s = FONT_DIGIT_SIZE[font_idx][1];
  font_idx      *= FONT_DIGIT_STRIDE;
  byte* pNow_s10 = FONT_DIGIT[font_idx + Tnow.ss / 10];
  byte* pNow_s1  = FONT_DIGIT[font_idx + Tnow.ss % 10];
  byte* pOld_s10 = FONT_DIGIT[font_idx + Told.ss / 10];
  byte* pOld_s1  = FONT_DIGIT[font_idx + Told.ss % 10];

  if (!(Tnow.hh / 10)) pNow_h10 = 0xFF; // display empty char
  if (!(Told.hh / 10)) pOld_h10 = 0xFF; // display empty char


  while (ScrollTime--)
  {
    if (ScrollTime) MAX.scroll(LEDMatrixDriver::scrollDirection::scrollLeft);
    else
    {
      if (TimeFlag == 0xFF) MAX.clear();

      ShowDP(true);           // display the DP
      ShowOptions(TimeFlag);  // dispaly design option: line of seconds, temperature...
    }
    
    byte slide = (TimeFlag != 0xFF) ? slide = 8 : 1;  // digits change with animation or not
    timerStamp = millis();
    while (slide--)
    {
      //if (DESIGN[DisplayN][11] != 0xFF)
      if (pgm_read_byte_near(DESIGN[DisplayN] + 11) != 0xFF)
      {
        if (TimeFlag & 0x01)
          //ShowDigit(slide, pNow_s1,  pOld_s1,  DESIGN[DisplayN][14], DESIGN[DisplayN][15], width_s, height_s);
          ShowDigit(slide, pNow_s1,  pOld_s1,
                    pgm_read_byte_near(DESIGN[DisplayN] + 14),
                    pgm_read_byte_near(DESIGN[DisplayN] + 15), width_s, height_s);
        if (TimeFlag & 0x02)
          //ShowDigit(slide, pNow_s10, pOld_s10, DESIGN[DisplayN][12], DESIGN[DisplayN][13], width_s, height_s);
          ShowDigit(slide, pNow_s10, pOld_s10,
                    pgm_read_byte_near(DESIGN[DisplayN] + 12),
                    pgm_read_byte_near(DESIGN[DisplayN] + 13), width_s, height_s);
      }
      //if (DESIGN[DisplayN][ 6] != 0xFF)
      if (pgm_read_byte_near(DESIGN[DisplayN] +  6) != 0xFF)
      {
        if (TimeFlag & 0x04)
          //ShowDigit(slide, pNow_m1,  pOld_m1,  DESIGN[DisplayN][ 9], DESIGN[DisplayN][10], width_m, height_m);
          ShowDigit(slide, pNow_m1,  pOld_m1,
                    pgm_read_byte_near(DESIGN[DisplayN] +  9),
                    pgm_read_byte_near(DESIGN[DisplayN] + 10), width_m, height_m);
        if (TimeFlag & 0x08)
          //ShowDigit(slide, pNow_m10, pOld_m10, DESIGN[DisplayN][ 7], DESIGN[DisplayN][ 8], width_m, height_m);
          ShowDigit(slide, pNow_m10, pOld_m10,
                    pgm_read_byte_near(DESIGN[DisplayN] +  7),
                    pgm_read_byte_near(DESIGN[DisplayN] +  8), width_m, height_m);
      }
      //if (DESIGN[DisplayN][ 1] != 0xFF)
      if (pgm_read_byte_near(DESIGN[DisplayN] +  1) != 0xFF)
      {
        if (TimeFlag & 0x10)
          //ShowDigit(slide, pNow_h1,  pOld_h1,  DESIGN[DisplayN][ 4], DESIGN[DisplayN][ 5], width_h, height_h);
          ShowDigit(slide, pNow_h1,  pOld_h1,
                    pgm_read_byte_near(DESIGN[DisplayN] +  4),
                    pgm_read_byte_near(DESIGN[DisplayN] +  5), width_h, height_h);
        if (TimeFlag & 0x20)
          //ShowDigit(slide, pNow_h10, pOld_h10, DESIGN[DisplayN][ 2], DESIGN[DisplayN][ 3], width_h, height_h);
          ShowDigit(slide, pNow_h10, pOld_h10,
                    pgm_read_byte_near(DESIGN[DisplayN] +  2),
                    pgm_read_byte_near(DESIGN[DisplayN] +  3), width_h, height_h);
      }

      if (showIt) MAX.display();

      if      (slide)      while ((unsigned long)(millis() - timerStamp) < SLIDE_SPEED);
      else if (ScrollTime) while ((unsigned long)(millis() - timerStamp) < WIPE_SPEED);
      timerStamp = millis();
    }
  }
  
  ScrollTime = false;
  TimeFlag   = 0;    // reset the flag, it will get new value at next "computeTime"
}

//=========> display time digit with a slide show
void ShowDigit(byte slide, byte* pNowDigit, byte* pOldDigit, byte coordX, byte coordY, byte width, byte height)
{
  // slide != 1: display the right area from two merged digits (old / new)
  // slide == 1: display a full new digit

  if (slide > height) return;

  coordX += width - 1;  // used in the loop below to print the digit

  for (byte n = 0; n < height; n++)
  {
    byte code = B00000000; // null char
    if (height - n <= slide)
    {
      if (pOldDigit != 0xFF) code = pgm_read_byte_near(pOldDigit + n + slide - height);
      //code = pgm_read_byte_near(pOldDigit + n + slide - height);
    }
    else
    {
      if (pNowDigit != 0xFF) code = pgm_read_byte_near(pNowDigit + n + slide);
      //code = pgm_read_byte_near(pNewDigit + n + slide);
    }

    // send this segment
    byte i = width;
    while (i--) DrawPixel(coordX - i + ScrollTime, coordY + n, bitRead(code, i));
  }
}



//-----------------------------------------------------------------------



void ShowDP(byte isOn)
{
  // ============> show / hide the DP - dotW-H,x,y1,y2
  /*
    byte   dot_width  = DESIGN[DisplayN][16];
    byte   dot_height = DESIGN[DisplayN][17];
    byte   dot_x      = DESIGN[DisplayN][18];
    byte   dot_y1     = DESIGN[DisplayN][19];
    byte   dot_y2     = DESIGN[DisplayN][20];
    //*/
  byte   dot_width  = pgm_read_byte_near(DESIGN[DisplayN] + 16);
  byte   dot_height = pgm_read_byte_near(DESIGN[DisplayN] + 17);
  byte   dot_x      = pgm_read_byte_near(DESIGN[DisplayN] + 18);
  byte   dot_y1     = pgm_read_byte_near(DESIGN[DisplayN] + 19);
  byte   dot_y2     = pgm_read_byte_near(DESIGN[DisplayN] + 20);
  if (dot_width)
  {
    DrawSquareFilled(dot_x, dot_y1, dot_width, dot_height, (isOn) ? WHITE : BLACK);
    DrawSquareFilled(dot_x, dot_y2, dot_width, dot_height, (isOn) ? WHITE : BLACK);
  }

  // ============> show / hide the alarm dots - al1x,y,al2x,y,timeline
  /*
    byte   al1_x     = DESIGN[DisplayN][21];
    byte   al1_y     = DESIGN[DisplayN][22];
    byte   al2_x     = DESIGN[DisplayN][23];
    byte   al2_y     = DESIGN[DisplayN][24];
    //*/
  byte   al1_x     = pgm_read_byte_near(DESIGN[DisplayN] + 21);
  byte   al1_y     = pgm_read_byte_near(DESIGN[DisplayN] + 22);
  byte   al2_x     = pgm_read_byte_near(DESIGN[DisplayN] + 23);
  byte   al2_y     = pgm_read_byte_near(DESIGN[DisplayN] + 24);

  byte   color     = (isOn) ? BLACK : WHITE;
  DrawPixel(al1_x, al1_y, (AlarmStates[0][0]) ? color : BLACK);
  DrawPixel(al2_x, al2_y, (AlarmStates[1][0]) ? color : BLACK);
}

void ShowOptions(byte flag)
{
  //if      (!DESIGN[DisplayN][25]) return;
  if      (!pgm_read_byte_near(DESIGN[DisplayN] + 25)) return;

  // show / hide the options - al1x,y,al2x,y,opt_xy
  /*
    byte   opt_flag   = DESIGN[DisplayN][25];
    byte   opt_x      = DESIGN[DisplayN][26];
    byte   opt_y      = DESIGN[DisplayN][27];
    //*/
  byte   opt_flag   = pgm_read_byte_near(DESIGN[DisplayN] + 25);
  byte   opt_x      = pgm_read_byte_near(DESIGN[DisplayN] + 26);
  byte   opt_y      = pgm_read_byte_near(DESIGN[DisplayN] + 27);

  // draw a dot/second if time > 0s and time < 31s, then erase a dot/second until time == 59s
  // when flag = 0xFF draw all the line
  if      (opt_flag & 0x01)   // ============> TimeLine <============
  {
    int seconds = Tnow.ss;

    if (flag == 0xFF)   // to restore time line when a new screen
    {
      if (seconds > 30)
      {
        DrawLineH(           1, opt_y, seconds - 30, BLACK);
        DrawLineH(seconds - 29, opt_y, 60 - seconds, WHITE);
      }
      else
      {
        DrawLineH(           1, opt_y, seconds     , WHITE);
        DrawLineH(seconds +  1, opt_y, 30 - seconds, BLACK);
      }
    }
    else                // just add / remove a dot
    {
      seconds -= 1; if (seconds < 0) seconds = 59; // dec to draw the 1st plot at 1s
      if (seconds > 29) DrawPixel(seconds - 29, opt_y, BLACK);
      else              DrawPixel(seconds +  1, opt_y, WHITE);
    }
  }
  else if (opt_flag & 0x02)   // ============> Temperature <============
  {
    int   temp       = int(getTemp());
    bool  minus      = (temp < 0) ? true : false;
    int   temp0      = min(abs(temp), 199);
    byte  temp10     = temp0 / 10;
    byte  temp1      = temp0 - 10 * temp10;
    bool  temp100    = false;
    if (temp10 > 9)
    {
      temp10  -= 10;
      temp100  = true; // there is hundred
    }
    
    // get pointers for digits
    int   font_idx   = 19;  // micro thin 2 : 3x5 (digital)
    byte  width      = FONT_DIGIT_SIZE[font_idx][0];
    byte  height     = FONT_DIGIT_SIZE[font_idx][1];
    font_idx        *= FONT_DIGIT_STRIDE;
    byte* pNew_10    = FONT_DIGIT[font_idx + temp10];
    byte* pNew_1     = FONT_DIGIT[font_idx + temp1 ];
    if (temp10 == 0 && !temp100) pNew_10 = 0xFF;  // -10<C<10 || F<100, display empty char
    
    byte n = 11;
    while (--n) MAX.setColumn(MATRIX_WIDTH - n, BLACK);             // erase
    //DrawSquareFilled(byte coordX, byte coordY, byte lenX, byte lenY, byte mask)
    /*
    DrawLineH(MATRIX_WIDTH - 10, 0, 3,           WHITE);            // draw symbol
    DrawLineV(MATRIX_WIDTH -  9, 0, 3 - temp100, WHITE);
    //*/
    //*
    DrawLineH(MATRIX_WIDTH - 10, 0, 2, WHITE);                      // draw symbol
    DrawLineV(MATRIX_WIDTH - 10, 0, 4, WHITE);
    DrawPixel(MATRIX_WIDTH -  9, ((IsCelsius) ? 3 : 2), WHITE);
    //*/
    opt_x            = MATRIX_WIDTH - width;                        // cooords for unit digit
    opt_y            = 8 - height;
    ShowDigit(0, pNew_1,  pNew_1,  opt_x, opt_y, width, height);    // draw unit
    if (pNew_10 != 0xFF)
    {
      opt_x         -= width + 1;
      ShowDigit(0, pNew_10, pNew_10, opt_x, opt_y, width, height);  // dozen
    }
    if      (temp100)
    {
      opt_x         -= 2;
      DrawLineV(opt_x, opt_y + 1, height - 1, WHITE);               // hundred
    }
    else if (minus) // else cause we dont have space to draw more than 199 or -99
    {
      opt_x         -= 3;
      opt_y         += height / 2;
      DrawLineH(opt_x, opt_y, 2, WHITE);                            // minus sign
    }
  }
}



//-----------------------------------------------------------------------
// Times functions - Alarms
//-----------------------------------------------------------------------



byte AlarmCheckStart()
{
  byte alarm_flag = false;
  byte n = 2;
  while (n--)
  {
    if (AlarmStates[n][0] == 1)    // Alarm in stanby
    {
      Tbuff = (n) ? Talarm2 : Talarm1;
      byte state_flag = AlarmStates[n][1];
      byte ring       = false;
      if (Tbuff.mm == Tnow.mm)                            // ===> minute match
      {
        if      (state_flag & B00000011)   ring = true;
        else if (Tbuff.hh == Tnow.hh)                           // ===> hour match
        {
          if      (state_flag & B00000010) ring = true;
          else if      (Tbuff.d == Tnow.d)
          {
            if (!(state_flag & B00000100)) ring = true;               // ===> day of the month match
          }
          else if (state_flag & B00000100)
          {
            if (((state_flag & B01110000) >> 4) == DoW) ring = true;  // ===> day of the week match
          }
        }
      }
      if (ring) AlarmStates[n][0]++;
      alarm_flag |= ring;
    }
  }
  
  if (AlarmStates[0][0] > 1 || AlarmStates[1][0] > 1)
  {
    AlarmMillis = millis();
    AlarmRun();
    if (AlarmStates[0][0] > 1) AlarmStates[0][0] = 0;
    if (AlarmStates[1][0] > 1) AlarmStates[1][0] = 0;
    AlarmCheckRepeat();
  }
  return alarm_flag;
}

void AlarmCheckRepeat()
{
  byte n = 2;
  while (n--)
  {
    if (AlarmStates[n][0] == 0)    // Alarm is Off
    {
      Tbuff = (n) ? Talarm2 : Talarm1;
      byte state_flag = AlarmStates[n][1];
      byte state_days = AlarmStates[n][1];
      if      (  state_flag & B00000011)    AlarmStates[n][0]++;    // every hour
      else if (  state_flag & B00000010)    AlarmStates[n][0]++;    // every day
      else if (!(state_flag & B00000100))
      {
        if (state_flag & B00001000)         AlarmStates[n][0]++;    // day from every month
      }
      else if (state_flag & B00000100)
      {
        if (state_flag & B00001000)                                 // day(s) from every week
        {
          byte last_day = (state_flag & B01110000) >> 4;
          byte new_day  = 0;

          //!!!!!!!!!!!!!!!!!!!!!!!!!!! must be completed
          if (n) Talarm2.d = new_day;
          else   Talarm1.d = new_day;
        }
      }
    }
  }
}

void AlarmRun()
{
#define SHOW_NOTE true

  if (SHOW_NOTE)
  {
    // clean screen from time display
#if   (WIPE_2 == 0)
    WipeScreen(true, WIPE_SPEED);
#elif (WIPE_2 == 1)
    CrunchScreen(WIPE_SPEED);
#elif (WIPE_2 == 2)
    CrumbleScreen(WIPE_SPEED);
#endif

    //ShowNoteX();
    MAX.setIntensity(INTENSITY_MAX);
    
    instrument1.begin(CHB, SQUARE, ENVELOPE0, 0);
    instrument2.begin(CHB, SQUARE, ENVELOPE0, 0);
    instrument3.begin(CHB, SQUARE, ENVELOPE0, 0);
    instrument4.begin(CHB, SQUARE, ENVELOPE0, 0);
  }
  
  int  counter = 0;
  while ((unsigned long)(millis() - AlarmMillis) < AlarmTime * 1000L) // ===> RUN the Alarm;
  {
    if (SHOW_NOTE)
    {
      //ShowNote1();
      //AlarmRing();

      instrument1.update();
      instrument2.update();
      instrument3.update();
      instrument4.update();

      
      if (!(counter % 1000))  // when parity
      {
        ReverseScreen();
        MAX.display();
        //ShowNote1();
      }
  
      //while (!(instrument1.isEnd() && instrument2.isEnd() && instrument3.isEnd() && instrument4.isEnd())) AlarmSong();
    }
    else
    {
      if (counter & 0x01) // when parity show time (all digits)
      {
        MAX.setIntensity(Intensity);
        ShowTime(0xFF, true);   // show time (all digits, display)
      }
      else
      {
        //FillScreen(WHITE, WHITE);
        ReverseScreen();
        MAX.display();
        MAX.setIntensity(INTENSITY_MAX);  // display all dots with high brightness to flash
      }
    }
    
    counter++;
  }

  // Alarm is done
  if (SHOW_NOTE)
  {
    FillScreen(WHITE, WHITE);
#if   (WIPE_2 == 0)
    WipeScreen(true, WIPE_SPEED);
#elif (WIPE_2 == 1)
    CrunchScreen(WIPE_SPEED);
#elif (WIPE_2 == 2)
    CrumbleScreen(WIPE_SPEED);
#endif
    //S_Text = " ";
    //ScrollText(0, true, true);         // not append, scroll untill offScreen
  }
  
  digitalWrite(PIN_BUZZER, LOW);
  MAX.setIntensity(Intensity);
  ScrollTime = true;                   // next display will wipe the screen
}

void AlarmRing()
{
  byte n = 20;
  while (n--)
  {
    analogWrite(PIN_BUZZER, 200);
    delay(20);
    analogWrite(PIN_BUZZER, LOW);
    delay(20);
  }
  delay(200);
}

void AlarmSong()
{
  instrument1.begin(CHB, SQUARE, ENVELOPE0, 10);
  instrument2.begin(CHB, SQUARE, ENVELOPE0, 0);
  instrument3.begin(CHB, SQUARE, ENVELOPE0, 0);
  instrument4.begin(CHB, SQUARE, ENVELOPE0, 0);
}



//-----------------------------------------------------------------------
// Message fonctions - date, temp...
//-----------------------------------------------------------------------



void SplashScreen()
{
  byte* pLogo   = EPACLOGO;
  byte  i       = 25;
  MAX.clear();
  MAX.setIntensity(INTENSITY_MAX / 3);
  while (i--) MAX.setColumn(3 + i, pgm_read_byte_near(pLogo + i));
  //ReverseScreen();
  MAX.display();
  delay(3000);

  i             = 40;
  byte  spriteN = 0;
  byte  dir     = 1;
  while (i--)
  {
    //MAX.setColumn(i - 9, 0x00);
    DrawLineV(i - 9, 0, 2, BLACK);
    DrawLineV(i - 9, 6, 2, BLACK);
    byte  idx = 9;
    while (idx--) MAX.setColumn(i - 8 + idx, pgm_read_byte_near(*PACMAN + spriteN * 9 + idx));
    if      (spriteN == 3) dir = - 1;
    else if (spriteN == 0) dir =   1;
    spriteN += dir;
    MAX.display();
    delay(100);
  }
  delay(1000);

  //setFont(*FONT_TINY);
  //setFont(*FONT_NARROW);
  //setFont(*FONT_DIGITAL);
  //setFont(*FONT_REGULAR);

  setFont(*FONT_REGULAR);
  i = 2;
  while (i--)
  {
    S_Text = String((char*)pgm_read_ptr(& sSPLASH[i]));
    ScrollText((8 - FontHeight) / 2, false, true);     // not append, scroll untill offScreen
    setFont(*FONT_TINY);
  }
}

void ShowDate()
{
  #if   (WIPE_2 == 0)
  WipeScreen(true, WIPE_SPEED);
  #elif (WIPE_2 == 1)
  CrunchScreen(WIPE_SPEED);
  #elif (WIPE_2 == 2)
  CrumbleScreen(WIPE_SPEED);
  #endif
  
  setFont(*FONT_REGULAR);

  byte day_of_week = getDoW();

  // use convertText() to manage accent and special chars alike "°"
  S_Text     = String((char*)pgm_read_ptr(& sMESSAGES[3])); // "Date: ";
  String sep = " - ";

  switch (DateN)
  {
    case 0:
      //S_Text  = sDoW[day_of_week] + space + String(Tnow.d) + space + sMonth[DateMonth];
      S_Text  = String((char*)pgm_read_ptr(& sDOW[day_of_week])) + " " + String(Tnow.d) + " "
        + convertText(String((char*)pgm_read_ptr(& sMONTH[Tnow.m]))) + " ";
      break;

    case 1:
      S_Text += String(Tnow.d) + sep + String(Tnow.m) + sep;
      break;

    case 2:
      S_Text += String(Tnow.m) + sep + String(Tnow.d) + sep;
      break;
  }
  S_Text += String(Tnow.y);
  ScrollText(0, false, true);
  
  ScrollTime = true;    // then show time, scrolling from right to center
}

void ShowTemp()
{
  #if   (WIPE_2 == 0)
  WipeScreen(true, WIPE_SPEED);
  #elif (WIPE_2 == 1)
  CrunchScreen(WIPE_SPEED);
  #elif (WIPE_2 == 2)
  CrumbleScreen(WIPE_SPEED);
  #endif
  
  setFont(*FONT_REGULAR);

  // use convertText() to manage accent and special chars alike "°"
  float  temp        = getTemp();
  bool   minus       = ((temp < 0.0) ? true : false);
  temp               = abs(temp);
  int    temp10      = int(temp);
  int    temp1       = int(temp * 10) % 10;
  
  S_Text             = String((char*)pgm_read_ptr(& sMESSAGES[2]));      // "Temp: "
  if (minus) S_Text += "-";
  S_Text            += String(temp10) + "." + String(temp1) + " "
    + convertText(String((char*)pgm_read_ptr(& sMESSAGES[IsCelsius])));  // " °C" / " °F" (0 / 1) 
  ScrollText(0, false, true);
  
  ScrollTime = true;    // then show time, scrolling from right to center
}


void ShowNote1()
{
  setFont(*MUSIC_NOTE);
  //MAX.setIntensity(INTENSITY_MAX);  // display all dots with high brightness to flash
  randomSeed(analogRead(PIN_RND));
  bool   rest = true;
  S_Text      = " ";

  byte ascII;
  if (rest)
  {
    ascII = 33 + (random( 70) / 10);    // choose anything but a rest
    rest  = false;
  }
  else
  {
    ascII = 33 + (random(110) / 10);
    if (ascII >= 41) rest = true;
  }
  S_Text += String(char(ascII));

  // add some more spaces when needed
  if      (ascII == 33 || ascII == 34 || ascII == 41) S_Text += "  ";
  else if (ascII == 35 || ascII == 36 || ascII == 42) S_Text += " ";

  ScrollText(0, true, false);     // not append, scroll untill offScreen
}


void ShowNoteX()
{
  setFont(*MUSIC_NOTE);
  //MAX.setIntensity(INTENSITY_MAX);  // display all dots with high brightness to flash
  randomSeed(analogRead(0));
  bool   rest;// = true;
  int    n    = 8;
  S_Text      = "";
  while (n-- > 0)   // compose some partition with random notes
  {
    // add random notes
    byte ascII;
    if (rest)
    {
      ascII = 33 + (random( 70) / 10);    // choose anything but a rest
      //rest  = false;
    }
    else
    {
      ascII = 33 + (random(110) / 10);
      if (ascII >= 41) rest = true;
    }
    S_Text += String(char(ascII));

    // add some more spaces when needed
    if      (ascII == 33 || ascII == 34 || ascII == 41)
    {
      S_Text += "  ";
      n      -= 3;
    }
    else if (ascII == 35 || ascII == 36 || ascII == 42)
    {
      S_Text += " ";
      n      -= 1;
    }
  }

  ScrollText(0, true, false);     // not append, scroll untill offScreen
}

