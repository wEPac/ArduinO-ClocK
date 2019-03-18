
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


   There are 2 different fonts, they are not compatible. One (Digit) is to print
   the time with vertical slide show, one (Alpha) is to print the text. Both are
   optimized for the usage to perform better speed and to reduce memory usage.
*/


//-----------------------------------------------------------------------

//#define  DEBUG



#include <avr/pgmspace.h>
#include "LEDMatrixDriver.hpp"  // https://github.com/bartoszbielawski/LEDMatrixDriver

//#include "VirtualRTC.h"           // a virtual Unix Time RTC to simulate a DS32331 or else

#include "DfontsAlpha.h"        // Data alike font, string message....
#include "DfontsDigit.h"
#include "DfixedStrings.h"



//--- SPI pins definition, adjust here at your wills--
#define PIN_CS_MAX     9
#define PIN_MOSI       11
#define PIN_CLK        13
// For different Matrix, display could be upside down, adjust REVERSED here
#define     INVERT_SEGMENT_X    0x01
#define     INVERT_DISPLAY_X    0x02
#define     INVERT_Y            0x04
#define REVERSED       INVERT_Y | INVERT_SEGMENT_X
#define NB_MATRIX      4
LEDMatrixDriver MAX(NB_MATRIX, PIN_CS_MAX, REVERSED);

//--- Just to know, it can be useful :P
#define MATRIX_WIDTH   NB_MATRIX * 8

//--- Output pins
#define PIN_BUZZER     3

//--- Input pins for setting Time and display
#define SET_DISPLAY    4
#define SET_FONT       5
#define SET_BRIGHT     6
#define SET_MIN        7
#define SET_HOUR       8


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
struct  DateTime  Tnow     = {2019, 03, 17,     23, 58, 00};  // {Year, Month, Day,     h, min, s}
struct  DateTime  Told     = Tnow;  // record old digits to make the slide show
struct  DateTime  Talarm1  = {2019, 03, 17,     23, 59, 00};  // {Year, Month, Day,     h, min, s}
struct  DateTime  Talarm2  = {2019, 03, 17,     00, 01, 00};  // {Year, Month, Day,     h, min, s}

#define ALARM_MAX      30000    // how long we ring in millis (shouldnt be < 5000s)
unsigned long AlarmMillis;      // tracks the time to stop the alarm
byte    Alarm1Flag     = true;  // set alarm 1, on:true / off:false
byte    Alarm2Flag     = true;  // set alarm 2, on:true / off:false
byte    Alarm1Repeat   = false; // 0x00:no alarm, 0x01:rpt min, 0x02:rpt h, 0x04:rpt day, 0x08:rpt DoW,
                                //      rpt, rpt week, rpt month
byte    Alarm2Repeat   = true;  // true: alarm flag stay on

//--- About Display
#define WHITE          0xFF
#define BLACK          0x00
#define INTENSITY_MAX  8        // 0~15 but upon 7 there is no real change
byte    Intensity      = 0;     // 0~7

#define IS_CELSIUS     true

#define DISPLAY_MAX    5 + IS_CELSIUS // how many design we have (max 6, 5 for °F)
byte    DisplayN       = 5;     // index to show design from array DESIGN[][] settings

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
{ //fMax,fidx,h10xy,h1xy, font, m10xy,  m1xy, font, s10xy,  s1xy, dotW, H,  x,y1,y2,   al1xy, al2xy,  opt, x,y,
  //         2      4        6      8     10        12     14       16     18    20       22     24         26  
  {4,    0,  0, 0,  8, 0,    0, 18, 0, 26, 0,   -1,  0, 0,  0, 0,    2, 2, 15, 1, 4,   15, 7, 16, 7,    0,  0, 0}, // 0 h:m
  {4,   -1,  0, 0,  0, 0,    0,  0, 0,  8, 0,    0, 18, 0, 26, 0,    2, 2, 15, 1, 4,   15, 7, 16, 7,    0,  0, 0}, // 1 m:s
  {2,   14,  1, 1,  8, 1,   14, 18, 1, 25, 1,   -1,  0, 0,  0, 0,    2, 1, 15, 2, 4,   15, 0, 16, 0,    1,  0, 7}, // 4 h:m + tL
  {2,   -1,  0, 0,  0, 0,   14,  1, 1,  8, 1,   14, 18, 1, 25, 1,    2, 1, 15, 2, 4,   15, 0, 16, 0,    1,  0, 7}, // 5 m:s + tL
  {2,    6,  0, 0,  5, 0,    6, 12, 0, 17, 0,   10, 23, 2, 28, 2,    1, 2, 10, 1, 4,   30, 0, 31, 0,    0,  0, 0}, // 2 h:m:s
  {2,    6,  0, 0,  5, 0,    6, 12, 0, 17, 0,   -1,  0, 0,  0, 0,    1, 2, 10, 1, 4,   30, 0, 31, 0,    2, 25, 7}, // 3 h:m:T
};


//--- About Time digit
#define SLIDE_SPEED   40        // 20~100 how much to slower digits sliding (shouldnt be slower than 20 = 1/3 s)
byte    ScrollTime;             // true: Time will come on screen, scrolled from right to center

//--- About Alpha Text
#define SCROLL_SPEED  50        // 20~80, how to slower scrolling text
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
  //MAX.setDecode(0x00);  // Disable decoding for all digits.

  //testText();
  SplashScreen();

  MAX.setIntensity(Intensity);
  
  setTime();                    // set time for Soft RTC, Unix Time ref, 1970-01-01 1h:00m00s, Thursday
  getTime();                    // get time from Soft RTC, stored in Tbuff
  Tnow = Tbuff;                 // set Arduino RTC
  
  PreviousMillis = millis();    // the Arduino RTC reference now
  
  ScrollTime = true;            // move the time on screen from right to center
  ShowTime(0xFF, true);
}

void loop()
{
  //testText();
  Tbuff = Tnow;
  ComputeTime(true);           // computes new digits and set TimeFlag for them
  Tnow  = Tbuff;


  if (Alarm1Flag == 1)         // check if we must ring for the alarm
  {
    if ((Talarm1.hh == Tnow.hh) && (Talarm1.mm == Tnow.mm) && (Talarm1.hh == Tnow.ss))
    {
      if (Alarm2Flag > 1) Alarm2Flag = (Alarm2Repeat) ? true : false;  // check if Alarm2 is running
      else                digitalWrite(PIN_BUZZER, HIGH); // ===> START the Alarm
      Alarm1Flag++;
      AlarmMillis = PreviousMillis;
    }
  }
  if (Alarm2Flag == 1)         // check if we must ring for the alarm
  {
    if ((Talarm2.hh == Tnow.hh) && (Talarm2.mm == Tnow.mm) && (Talarm2.hh == Tnow.ss))
    {
      if (Alarm1Flag > 1) Alarm1Flag = (Alarm1Repeat) ? true : false;  // check if Alarm1 is running
      else                digitalWrite(PIN_BUZZER, HIGH); // ===> START the Alarm
      Alarm2Flag++;
      AlarmMillis = PreviousMillis;
    }
  }

  // speed up to reach the time after a busy period (ie displaying alarm or date)
  if ((unsigned long)(millis() - PreviousMillis) > TIME_SPEED)
  {
    TimeFlag     = 0xFF;     // force to show all digits (not only those they changed)
    goto endLoop;
  }

  // set flag, used to prevent duplicate actions, see below...
  byte change_flag = 0xFF;

  //---> Run the alarm
  if      (Alarm1Flag > 1 || Alarm2Flag > 1)
  {
    RunAlarm();
    change_flag  = 0x00;     // dont display DP, lock settings
  }
  //---> 
  else if (!(Tnow.mm & 0x01) && Tnow.ss == 31)
  {
    ShowDate();
    change_flag  = 0x00;     // when no parity displays date, dont display DP, lock settings
    ShowTime(0xFF, true);
  }
  //---> 
  else if ( (Tnow.mm & 0x01) && Tnow.ss == 31)
  {
    ShowTemp();
    change_flag  = 0x00;     // when parity displays temperature, dont display DP, lock settings
    ShowTime(0xFF, true);
  }
  //---> 
  else ShowTime(0x00, true); // ===> show casual time (digits accorded to TimeFlag, display time)



  //---> wait for 1 second betwwen each loop
  // this is here our RTC will work, not so bad, but lost with power down
  //unsigned long currentMillis = millis();
  //while ((unsigned long)(currentMillis - PreviousMillis) < TIME_SPEED)
  while ((unsigned long)(millis() - PreviousMillis) < TIME_SPEED)
  {
    //--- Set DP off
    // after a while we set DP off, pretty with 2/3 rate, so 650
    //if ((change_flag & 0x01) && ((unsigned long)(currentMillis - PreviousMillis) > DOT_SPEED))
    if ((change_flag & 0x01) && ((unsigned long)(millis() - PreviousMillis) > DOT_SPEED))
    {
      change_flag ^= 0x01;  // we do it once only, so we set the flag
      ShowDP(false);        // set DP off
      MAX.display();
    }

    //--- Adj Intensity
    if ((change_flag & 0x02) && digitalRead(SET_BRIGHT) == LOW)
    {
      change_flag ^= 0x02;          // we do it once only, so we set the flag
      Intensity++;
      Intensity   %= INTENSITY_MAX;
      MAX.setIntensity(Intensity);  // set new brightness
    }

    //--- Adj Display type
    if ((change_flag & 0x04) && digitalRead(SET_DISPLAY) == LOW)
    {
      change_flag ^= 0x04;        // we do it once only, so we set the flag
      DisplayN++;
      DisplayN    %= DISPLAY_MAX;
      ShowTime(0xFF, true);       // show current setting
    }

    //--- Adj Font type
    if ((change_flag & 0x08) && digitalRead(SET_FONT) == LOW)
    {
      change_flag ^= 0x08;        // we do it once only, so we set the flag
      //DESIGN[DisplayN][28]++;
      //DESIGN[DisplayN][28] %= DESIGN[DisplayN][ 0];
      DesignFont[DisplayN]++;
      DesignFont[DisplayN] %= pgm_read_byte_near(DESIGN[DisplayN]);
      ShowTime(0xFF, true);       // show current setting
    }

    //--- Adj Time Value
    if ((change_flag & 0x10) && digitalRead(SET_MIN) == LOW)
    {
      change_flag ^= 0x10;        // we do it once only, so we set the flag
      Told.mm = Tnow.mm;
      Tnow.mm++;
      Tbuff   = Tnow;
      TimeFlag |= 0x04;
      ComputeTime(false);
      Tnow    = Tbuff;
      setTime();                   // set time for Soft RTC, Unix Time ref, 1970-01-01 1h:00m00s, Thursday
      ShowTime(false, true);       // show current setting
    }

    if ((change_flag & 0x20) && digitalRead(SET_HOUR) == LOW)
    {
      change_flag ^= 0x20;         // we do it once only, so we set the flag
      Told.hh = Tnow.hh;
      Tnow.hh++;
      Tbuff   = Tnow;
      TimeFlag |= 0x10;
      ComputeTime(false);
      Tnow    = Tbuff;
      setTime();                   // set time for Soft RTC, Unix Time ref, 1970-01-01 1h:00m00s, Thursday
      ShowTime(false, true);       // show current setting
    }

    //currentMillis = millis();
  }

endLoop:
  //---> set new time ref => + 1s
  PreviousMillis += 1000;          // we wont use millis() here to keep our RTC accurate, add 1s for each loop
  Told = Tnow;                     // save current time to slide digits
  Tnow.ss++;                       // add 1s to current time
  TimeFlag       |= 0x01;          // seconds will always increase inside the loop => flag it to force digit on screen
  UnixTime++;                      // virtual RTC, UnixTime, add 1s for each loop
}



//-----------------------------------------------------------------------
// Display functions
//-----------------------------------------------------------------------



byte* segmentBuffer(byte coordX, byte coordY)
{
  if (REVERSED) coordY = 7 ^ coordY;
  return pBuffer + (coordY * NB_MATRIX) + (coordX >> 3);
}

void ClearScreen()
{
  MAX.clear();
  MAX.display();
}

void FillScreen(byte mask1, byte mask2)
{
  byte n = NB_MATRIX * 4;
  while (n--)
  {
    MAX.setColumn(2 * n    , mask1);
    MAX.setColumn(2 * n + 1, mask2);
  }
}

void ReverseScreen()
{
  int idx = 8 * NB_MATRIX;
  while (idx--) pBuffer[idx] ^= 0xFF;
}

void WipeScreen()
{
  // scroll screen content to left, end with wiped SCREEN
  unsigned long timerStamp = millis();
  byte   counter  = NB_MATRIX * 8;
  while (counter--)
  {
    while ((unsigned long)(millis() - timerStamp) < SCROLL_SPEED / 2);
    timerStamp = millis();
    MAX.scroll(LEDMatrixDriver::scrollDirection::scrollLeft);
    MAX.display();
  }
}

void DrawPixel(byte coordX, byte coordY, byte isShown)
{
  MAX.setPixel(coordX, coordY, isShown);

  //byte* pSegment = segmentBuffer(coordX, coordY);
  //coordX = 7 & coordX; if (!REVERSED) coordX = 7 ^ coordX;
  //if (isShown) *pSegment |=  (1 << coordX);
  //else         *pSegment &= ~(1 << coordX);
}

byte setRow(byte coordY, byte mask)
{
  //uint8_t* getFrameBuffer() const {return frameBuffer;}
  ;
}

void DrawLineH(byte coordX, byte coordY, byte len, byte isShown)
{
  if (len < 1) return;
  while (len--) MAX.setPixel(coordX + len, coordY, isShown);
}

void DrawLineV(byte coordX, byte coordY, byte len, byte isShown)
{
  if (len < 1) return;
  while (len--) MAX.setPixel(coordX, coordY + len, isShown);
}

void DrawSquare(byte coordX, byte coordY, byte lenX, byte lenY, byte isShown)
{
  DrawLineH(coordX,            coordY,            lenX, isShown);
  DrawLineV(coordX + lenX - 1, coordY,            lenY, isShown);
  DrawLineH(coordX,            coordY + lenY - 1, lenX, isShown);
  DrawLineV(coordX,            coordY,            lenY, isShown);
}

void DrawSquareFilled(byte coordX, byte coordY, byte lenX, byte lenY, byte isShown)
{
  if (lenY < 1) return;
  while (lenY--) DrawLineH(coordX, coordY + lenY, lenX, isShown);
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
  int text_length = S_Text.length();
  int text_width  = getTextWidth();
  int xPos        = MATRIX_WIDTH;                                   // start out of screen at right
  int idx         = text_width + ((offScreen) ? MATRIX_WIDTH : 0);    // end out of screen at left
  unsigned long timerStamp = millis();

  if (!append) MAX.clear();
  while (idx--)
  {
    while ((unsigned long)(millis() - timerStamp) < SCROLL_SPEED);
    timerStamp = millis();
    if (append) MAX.scroll(LEDMatrixDriver::scrollDirection::scrollLeft);
    setText(text_length, xPos--, coordY);
    MAX.display();
  }
  S_Text = "";
}

// write text of the given length for the given position, into the driver buffer.
void setText(int text_length, int coordX, int coordY)
{
  for (int i = 0; i < text_length; i++)
  {
    if (coordX > MATRIX_WIDTH) return;  // stop if char is outside visible area

    byte  ascII      = S_Text[i] - 32;  // font starts with " " space char
    byte* pChar      = pFont + FontWidth * ascII;
    int   char_width = getCharWidth(ascII, pChar);

    // only draw if char is visible
    if (coordX > - FontWidth)
    {
      // writes char to the driver buffer by passing position (x,y), char width and char start column
      for (int iX = 0; iX < char_width + CHAR_SPACING; iX++)
      {
        //byte segment = pChar[char_start + iX];
        byte segment = pgm_read_byte_near(pChar + iX);
        byte bitMask = 1;
        for (int iY = 0; iY < FontHeight; iY++)
        {
          MAX.setPixel(coordX + iX, coordY + iY, (char_width - 1 < iX) ? false : (bool)(segment & bitMask));
          bitMask <<= 1;
        }
      }
    }

    coordX += char_width + CHAR_SPACING;
  }
}

// calculates character width - ignoring whitespace
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

  return char_width;
}

// calculates the public String "S_Text" width using variable character width and whitespace
int getTextWidth()
{
  int text_width = 0;
  int text_idx   = S_Text.length();
  while (text_idx--)
  {
    byte  ascII       = S_Text[text_idx] - 32;
    byte* pChar       = pFont + FontWidth * ascII;
    int   char_width  = getCharWidth(ascII, pChar);
    text_width       += char_width + CHAR_SPACING;
  }
  return text_width;
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



float getTemp(byte isCelsius)
{
  unsigned int wADC;
  int          t;
  // The internal temperature has to be used with the internal reference of 1.1V.
  // Channel 8 can not be selected with the analogRead function yet.

  // Set the internal reference and mux.
  ADMUX = (_BV(REFS1) | _BV(REFS0) | _BV(MUX3));
  ADCSRA |= _BV(ADEN);  // enable the ADC

  delay(20);                        // wait for voltages to become stable.
  ADCSRA |= _BV(ADSC);              // Start the ADC
  while (bit_is_set(ADCSRA,ADSC));  // Detect end-of-conversion
  wADC = ADCW;                      // Reading register "ADCW"

  // The offset of 324.31 could be wrong. It is just an indication.

  //return float((wADC - 324.31) / 1.22);  // in °C
  float temperature = (wADC - 324.31) / 1.22;   // in °C
  if (isCelsius) return temperature;
  return (temperature * 9) / 5.0 + 32;          // in °F
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
      }
    }
  }
  
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

  TimeFlag |= flag;
  
  if (ScrollTime)
  {
    TimeFlag   = 0xFF;
    ScrollTime = 32;
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
      ShowOptions(TimeFlag);  // dispaly the line of seconds (for tiny fonts only)
    }
    
    byte slide = 1; if (TimeFlag != 0xFF) slide = 8;
    unsigned long timerStamp = millis();
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
      else if (ScrollTime) while ((unsigned long)(millis() - timerStamp) < SCROLL_SPEED / 2);
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



void RunAlarm()
{
#define SHOW_NOTE true
  //---> Display time or alarm
  if      ((unsigned long)(millis() - AlarmMillis) > ALARM_MAX) // ===> STOP the Alarm
  {
    if (Alarm1Flag > 1) Alarm1Flag = (Alarm1Repeat) ? true : false;
    if (Alarm2Flag > 1) Alarm2Flag = (Alarm2Repeat) ? true : false;
    digitalWrite(PIN_BUZZER, LOW);
    MAX.setIntensity(Intensity);
    if (SHOW_NOTE) WipeScreen();  // stopped with notes displayed, scroll them off
    
    ScrollTime = true;
  }
  else if (Alarm1Flag > 1 || Alarm2Flag > 1)     // ===> RUN the Alarm
  {
    if (!SHOW_NOTE && Alarm1Flag & 0x01)         // when parity show time (all digits)
    {
      MAX.setIntensity(Intensity);
      ShowTime(0xFF, true);   // show time (all digits, display)
    }
    else
    {
      if (Alarm1Flag == 2 || Alarm2Flag == 2)                   // ===> START the Alarm
      {
        if (digitalRead(PIN_BUZZER) == LOW)                       // ===> START the Alarm
        {
          digitalWrite(PIN_BUZZER, HIGH);
        }

        if (SHOW_NOTE) WipeScreen();
        else           WipeScreen();
      }

      if (SHOW_NOTE) ShowNote();
      else
      {
        //FillScreen(WHITE, WHITE);

        ReverseScreen();
        MAX.display();
        MAX.setIntensity(INTENSITY_MAX);  // display all dots with high brightness to flash
      }
    }
    if (Alarm1Flag > 1) Alarm1Flag++;
    if (Alarm2Flag > 1) Alarm2Flag++;
  }
}

void ShowDP(byte isOn)
{
  // show / hide the DP - dotW-H,x,y1,y2
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

  // show / hide the alarm dots - al1x,y,al2x,y,timeline
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
  DrawPixel(al1_x, al1_y, (Alarm1Flag) ? color : BLACK);
  DrawPixel(al2_x, al2_y, (Alarm2Flag) ? color : BLACK);
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
  if      (opt_flag & 0x01)   // ======> TimeLine
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
  else if (opt_flag & 0x02)   // ======> Temperature
  {
    //float temperature = getTemp(IS_CELSIUS);
    int   temperature = int(getTemp(IS_CELSIUS));
    byte  minus       = (temperature < 0) ? 1 : 0;
    int   temp0       = abs(temperature);
    byte  temp10      = temp0 / 10;
    byte  temp1       = temp0 - 10 * temp10;
    /*
#ifdef DEBUG
    Serial.println("***************");
    Serial.println(temperature);
    Serial.println(temp0);
    Serial.print(((minus) ? "-" : "+")); Serial.print(temp10); Serial.println(temp1);
#endif
    //*/
    
    // get pointers for digits
    int   font_idx   = 13;  // tiny thin 2 : 3x5 (digital)

    byte  width      = FONT_DIGIT_SIZE[font_idx][0];
    byte  height     = FONT_DIGIT_SIZE[font_idx][1];
    font_idx        *= FONT_DIGIT_STRIDE;
    byte* pNew_10    = FONT_DIGIT[font_idx + temp10];
    byte* pNew_1     = FONT_DIGIT[font_idx + temp1 ];

    byte  sign_x     = opt_x - 3;
    byte  sign_y     = opt_y - height / 2;

    opt_y += 1 - height;              // Y coord origin on top of char
    if (temp10 == 0) pNew_10 = 0xFF;  // display empty char

    byte n = 2 * width;
    while (n--) MAX.setColumn(sign_x + n, BLACK);
    if (temp10 == 0)
    {
      sign_x += width + 1;  // show minus sign just before 1st digit
      pNew_10 = 0xFF;       // display empty char
    }

    ShowDigit(0, pNew_10, pNew_10, opt_x,             opt_y, width, height);
    ShowDigit(0, pNew_1,  pNew_1,  opt_x + width + 1, opt_y, width, height);

    DrawLineH(sign_x, sign_y, 2, WHITE * minus);

    sign_y -= 4;
    DrawLineH(sign_x,     sign_y, 3, WHITE);
    DrawLineV(sign_x + 1, sign_y, 3, WHITE);
  }
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
    MAX.setColumn(i - 9, 0x00);
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
  setFont(*FONT_REGULAR);
  WipeScreen();

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
  
  ScrollTime = true;
}

void ShowTemp()
{
  setFont(*FONT_REGULAR);
  WipeScreen();

  // use convertText() to manage accent and special chars alike "°"
  float  temperature = -23.674574;                    // in °C
  //#ifdef 
  temperature        = getTemp(IS_CELSIUS);

  byte   minus       = false; if (temperature < 0.0) minus = true;
  temperature        = abs(temperature);
  int    temp10      = int(temperature);
  temperature       -= temp10;
  temperature       *= 10;
  int    temp1       = int(temperature);
  
  S_Text             = String((char*)pgm_read_ptr(& sMESSAGES[2])); // "Temp: "
  if (minus) S_Text += "-";
  S_Text            += String(temp10) + "." + String(temp1) + " "
    + convertText(String((char*)pgm_read_ptr(& sMESSAGES[0])));  // " °C" / " °F" (0 / 1) 
  ScrollText(0, false, true);
  
  ScrollTime = true;
}

void ShowNote()
{
  setFont(*MUSIC_NOTE);
  //MAX.setIntensity(INTENSITY_MAX);  // display all dots with high brightness to flash
  randomSeed(analogRead(0));
  bool   rest = true;
  int    n    = 32;
  S_Text      = "";
  while (n-- > 0)   // compose some partition with random notes
  {
    // add random notes
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

