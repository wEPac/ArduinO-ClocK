
/* Simple_MAX7219_Matrix_Clock

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
   optimized for the usage to perform better speed.
   The Alpha font was created using the LEDMatrixPainter application
   by James Gohl (https://github.com/jamesgohl/LEDMatrixPainter)
*/


//-----------------------------------------------------------------------

#define  DEBUG


#include <avr/pgmspace.h>
#include <LEDMatrixDriver.hpp>

#include "fonts.h"

//--- SPI pins definition, adjust here at your wills--
#define CS_PIN         9
#define MOSI_PIN       11
#define CLK_PIN        13
#define NB_MATRIX      4
#define REVERSED       true     // For different Matrix, display could be upside down, adjust here
LEDMatrixDriver MAX(NB_MATRIX, CS_PIN, REVERSED);

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


//--- Date Var
byte DateDoW        =  3;     // 0:monday
byte DateDay        =  14;
byte DateMonth      =  2;     // 0:january
int  DateYear       =  2019;


//--- Time Var
byte TimeHours10    =  2;
byte TimeHours1     =  3;
byte TimeMinutes10  =  5;
byte TimeMinutes1   =  7;
byte TimeSeconds10  =  0;
byte TimeSeconds1   =  0;
unsigned long PreviousMillis; // tracks the time to prevent roll up after 50 days
byte TimeFlag;                // to know what digit changed and so, we must display it


//--- Alarm Var
#define ALARM_MAX      30      // how long we ring in s (shouldnt be < 5s)
byte Alarm1Flag     = true;   // set alarm 1, on:true / off:false
byte Alarm2Flag     = false;  // set alarm 2, on:true / off:false
byte AlarmHours10   =  2;
byte AlarmHours1    =  3;
byte AlarmMinutes10 =  5;
byte AlarmMinutes1  =  8;
byte AlarmSeconds10;
byte AlarmSeconds1;


//--- Recording old digits to make the slide show
byte LastHours10    =  0;
byte LastHours1     =  0;
byte LastMinutes10  =  0;
byte LastMinutes1   =  0;
byte LastSeconds10  =  0;
byte LastSeconds1   =  0;


//--- Display Var
#define WHITE         0xFF
#define BLACK         0x00
#define INTENSITY_MAX 8       // 0~15 but upon 7 there is no real change
byte Intensity      = 0;      // 0~7

//byte FontType       = 2;      // 0~3, 0=analog big, 1=digital big, 2=analog tiny, 3=digital tiny

#define DISPLAY_MAX   6;      // how many design we have
byte    DisplayN    = 4;      // index to show design from array Design[][] settings
byte    FontN;                // index for different font in a design kind
// Design[][] contains settings for each kind of display, each row is a kind:
//       0      fnt#,               how many fonts we have in this design (rollup, fonts must be in right order)
//       1      font,               hour:           1st font of a kind (-1 to hide this field)
//       2-3    h10xy,              hour:           x, y coordinates for 1st digit 10
//       4-5    h1xy,               hour:           x, y coordinates for 2nd digit 01
//       6-10   font, m10xy, m1xy,  same for minute
//      11-15   font, s10xy, s1xy,  same for second
//      16-17   dotW,H,             dots separator: Width, Height
//      18-20   x,y1,y2             dots separator: x coordinate, y1 coord for dot 1, y2 coord for dot 2
//      21-22   al1xy,              alarm1 dot:     x, y coordinates
//      23-24   al2xy,              alarm2 dot:     x, y coordinates
//      25-27   opt,x,y,              0:null
//                                    1:timeline
//                                    2:Temperature
//                                    x,y, coord for the option
//      28      curF,               current font number used for this design
byte  Design[][29] = // DisplayN will give the font and coords
{ //fnt#,font,h10xy,h1xy, font, m10xy,  m1xy, font, s10xy,  s1xy, dotW, H,  x,y1,y2,   al1xy, al2xy, opt,x, y
  //         2      4        6      8     10        12     14       16     18    20       22     24         26    28
  {4,    0,  0, 0,  8, 0,    0, 18, 0, 26, 0,   -1,  0, 0,  0, 0,    2, 2, 15, 1, 4,   15, 7, 16, 7,    0,  0, 0, 0}, // h:m
  {4,   -1,  0, 0,  0, 0,    0,  0, 0,  8, 0,    0, 18, 0, 26, 0,    2, 2, 15, 1, 4,   15, 7, 16, 7,    0,  0, 0, 0}, // m:s
  {2,    6,  0, 0,  5, 0,    6, 12, 0, 17, 0,   10, 23, 2, 28, 2,    1, 2, 10, 1, 4,   30, 0, 31, 0,    0,  0, 0, 1}, // h:m:s
  {2,    6,  0, 0,  5, 0,    6, 12, 0, 17, 0,   -1,  0, 0,  0, 0,    1, 2, 10, 1, 4,   30, 0, 31, 0,    2, 25, 7, 1}, // h:m:T
  {2,   14,  1, 1,  8, 1,   14, 18, 1, 25, 1,   -1,  0, 0,  0, 0,    2, 1, 15, 2, 4,   15, 0, 16, 0,    1,  0, 7, 0}, // h:m + tL
  {2,   -1,  0, 0,  0, 0,   14,  1, 1,  8, 1,   14, 18, 1, 25, 1,    2, 1, 15, 2, 4,   15, 0, 16, 0,    1,  0, 7, 0}, // m:s + tL
};

//--- About Time digit
#define SLIDE_SPEED   40      // 20~100 how much to slower digits sliding (shouldnt be slower than 20 = 1/3 s)
byte SlideShow      = true;   // 0~1 to change digit with or without a slide show
byte LastSlideShow;

//--- About Alpha Text
#define SPACE_WIDTH   3       // define width of " " since code is handling whitespace
#define CHAR_SPACING  1       // pixels between characters
#define SCROLL_DELAY  50      // 20~80, how to slower scrolling text
byte Scroll_Test    = true;   // flag to scroll text or not
byte* pFont;                  // pointer for current font
byte FontWidth;               // current font dimension
byte FontHeight;

//--- About buffer
byte* pBuffer;                // frameBuffer address


//-----------------------------------------------------------------------



void setup()
{
  //--- initialize I/O pins
  pinMode(SET_DISPLAY,  INPUT_PULLUP);
  pinMode(SET_FONT,     INPUT_PULLUP);
  pinMode(SET_BRIGHT,   INPUT_PULLUP);
  pinMode(SET_MIN,      INPUT_PULLUP);
  pinMode(SET_HOUR,     INPUT_PULLUP);
  pinMode(PIN_BUZZER,   OUTPUT);

  //--- set Led Matrix on
  pBuffer = MAX.getFrameBuffer();
  ClearScreen();
  MAX.setIntensity(Intensity);
  MAX.setEnabled(true);
  //MAX.setDecode(0x00);  // Disable decoding for all digits.

  //testText();
  ShowWelcome();

  PreviousMillis = millis();  // the RTC reference now
  TimeFlag       = 0xFF;      // 1st sequence, we will print each digits
  FontN          = Design[DisplayN][28];
}

void loop()
{
  //testText();

  ComputeTime(true);              // computes new digits and set TimeFlag for them

  
  if (Alarm1Flag == 1)         // check if we must ring for the alarm
  {
    if ((AlarmHours10 == TimeHours10) && (AlarmHours1 == TimeHours1)
        && (AlarmMinutes10 == TimeMinutes10) && (AlarmMinutes1 == TimeMinutes1)
        && (TimeSeconds1 == 0))
    {
      digitalWrite(PIN_BUZZER, HIGH); // ===> START the Alarm
      Alarm1Flag++;
    }
  }

  // speed up to reach the time after a busy period (ie displaying alarm or date)
  if ((unsigned long)(millis() - PreviousMillis) > 1000)
  {
    TimeFlag       = 0xFF;  // force to show all digits (not only those they changed)
    goto endLoop;
  }

  // set flag, used to prevent duplicate actions, see below...
  byte change_flag = 0xFF;

  //---> Run the alarm
  if (Alarm1Flag > 1)
  {
    RunAlarm();
    change_flag  = 0x00;    // when parity fill screen, dont display DP, lock settings
  }
  else if (TimeMinutes1 & 0x01 && TimeSeconds10 == 3 && TimeSeconds1 == 0)
  {
    ShowDate();
    change_flag  = 0x00;    // when parity fill screen, dont display DP, lock settings
    ShowTime(0xFF);
  }
  else if (!(TimeMinutes1 & 0x01) && TimeSeconds10 == 3 && TimeSeconds1 == 0)
  {
    ShowTemp();
    change_flag  = 0x00;    // when parity fill screen, dont display DP, lock settings
    ShowTime(0xFF);
  }
  else ShowTime(0x00);  // ===> show casual time (only new digits)



  //---> wait for 1 second betwwen each loop
  // this is here our RTC will work, not so bad, but lost with power down
  unsigned long currentMillis = millis();
  while ((unsigned long)(currentMillis - PreviousMillis) < 1000)
  {
    //--- Set DP off
    // after a while we set DP off, pretty with 2/3 rate, so 650
    if ((change_flag & 0x01) && ((unsigned long)(currentMillis - PreviousMillis) > 650))
    {
      change_flag ^= 0x01;  // we do it once only, so we set the flag
      ShowDP(false);        // set DP off
      MAX.display();
    }

    //--- Adj Intensity
    if ((change_flag & 0x02) && digitalRead(SET_BRIGHT) == LOW)
    {
      change_flag ^= 0x02;  // we do it once only, so we set the flag
      Intensity++;
      Intensity %= INTENSITY_MAX;
      MAX.setIntensity(Intensity);  // set new brightness
    }

    //--- Adj Display type
    if ((change_flag & 0x04) && digitalRead(SET_DISPLAY) == LOW)
    {
      change_flag ^= 0x04;  // we do it once only, so we set the flag
      DisplayN++;
      DisplayN    %= DISPLAY_MAX;
      //if (FontN >= Design[DisplayN][ 0]) FontN = 0; // if current font is out of range, reset font
      FontN        = Design[DisplayN][28]; // restore font
      ShowTime(0xFF);       // show current setting
    }

    //--- Adj Font type
    if ((change_flag & 0x08) && digitalRead(SET_FONT) == LOW)
    {
      change_flag ^= 0x08;  // we do it once only, so we set the flag
      FontN++;
      FontN       %= Design[DisplayN][ 0];
      Design[DisplayN][28] = FontN;
      ShowTime(0xFF);       // show current setting
    }

    //--- Adj Time Value
    if ((change_flag & 0x10) && digitalRead(SET_MIN) == LOW)
    {
      change_flag ^= 0x10;  // we do it once only, so we set the flag
      LastMinutes1 = TimeMinutes1;
      TimeMinutes1++;
      ComputeTime(false);
      ShowTime(0x04);       // show current setting
    }

    if ((change_flag & 0x20) && digitalRead(SET_HOUR) == LOW)
    {
      change_flag ^= 0x20;  // we do it once only, so we set the flag
      LastHours1   = TimeHours1;
      TimeHours1++;
      ComputeTime(false);
      ShowTime(0x10);       // show current setting
    }

    currentMillis = millis();
  }

  endLoop:
  //---> set new time ref => + 1s
  PreviousMillis += 1000; // we wont use millis() here to preserve our RTC
  LastSeconds1    = TimeSeconds1;
  TimeFlag       |= 0x01; // TimeSeconds1 will always increase inside the loop => flag it
  TimeSeconds1++;         // yes yes, we re older now from 1 second!!!
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



/*
void testText()
{
  byte   val     = 27;
  String text    = "Gn" + String(char(128)) + String(char(128));

  text = String(val) + "!\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_'abcdefghijklmnopqrstuvwxyz{|}~" + String(char(128)) + String(char(127));
  text = convertText(String("°àâèéêëîïùû"));
  //text = "Gnnn" + convertText("ééééé... aïe!");
  
  randomSeed(analogRead(0));
  byte disp = random(40) / 10;
  //disp = 3;
  switch (disp)
  {
    case 0:
      setFont(*FONT_TINY);
      text.toUpperCase();
      break;
    case 1:
      setFont(*FONT_DIGITAL);
      break;
    case 2:
      setFont(*FONT_NARROW);
      break;
    case 3:
      setFont(*FONT_ALPHA);
      break;
  }

  byte counter = 3;
  while (counter--) ScrollText(text, (8 - FontHeight) / 2); // scroll text at height center
  delay(10000);
}
//*/

void setFont(byte* font_name)
{
  //FontWidth  = font_name[0];
  //FontHeight = font_name[1];
  FontWidth  = pgm_read_byte_near(font_name);
  FontHeight = pgm_read_byte_near(font_name + 1);
  pFont      = font_name;
}

//=========> display a string with scrolling, at pos Y
void ScrollText(String text, byte coordY)
{
  int text_length = text.length();
  int text_width  = getTextWidth(text);
  int xPos        = MATRIX_WIDTH;
  int idx         = text_width + MATRIX_WIDTH;
  unsigned long scrollTimerStamp = (unsigned long)(SCROLL_DELAY + millis());

  MAX.clear();
  while (idx--)
  {
    while (millis() < scrollTimerStamp) ;
    scrollTimerStamp = SCROLL_DELAY + millis();
    setText(text, text_length, xPos--, coordY);
    MAX.display();
  }
}

// writes text of the given length for the given position, to the driver buffer.
void setText(String text, int text_length, int coordX, int coordY)
{
  for (int i = 0; i < text_length; i++)
  {
    if (coordX > MATRIX_WIDTH) return;  // stop if char is outside visible area

    byte  ascII      = text[i] - 31;     // font starts with " " space char
    byte* pChar      = pFont + FontWidth * ascII;
    int   char_width = getCharWidth(ascII, pChar);

    // only draw if char is visible
    if (coordX > - 8)
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
  if (ascII == 0) return SPACE_WIDTH;   // " " space char

  byte char_width = FontWidth;
  while (char_width--)                  // check 1st not empty byte
  {
    //if (font[ascII][char_width])
    if (pgm_read_byte_near(pChar + char_width))
    {
      char_width++;
      break;
    }
  }
  if      (ascII ==  1) char_width++;   // give extra spacing for "!"
  else if (ascII == 14) char_width++;   // give extra spacing for "."

  return char_width;
}

// calculates the text width using variable character width and whitespace
int getTextWidth(String text)
{
  int text_width = 0;
  int text_idx   = text.length();
  while (text_idx--)
  {
    byte  ascII      = text[text_idx] - 32;
    byte* pChar      = pFont + FontWidth * ascII;
    int   char_width = getCharWidth(ascII, pChar);
    text_width      += char_width + CHAR_SPACING;
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
      if (val == 194 || val == 195) // extended ascII
      {
        text.remove(idx, 1);
        len--;
        val = byte(text.charAt(idx));
        if      (val == 176) text.setCharAt(idx, char(127)); // 127 = °
        else if (val == 160) text.setCharAt(idx, char(128)); // 128 = à
        else if (val == 162) text.setCharAt(idx, char(129)); // 129 = â
        else if (val == 168) text.setCharAt(idx, char(130)); // 130 = è
        else if (val == 169) text.setCharAt(idx, char(131)); // 131 = é
        else if (val == 170) text.setCharAt(idx, char(132)); // 132 = ê
        else if (val == 171) text.setCharAt(idx, char(133)); // 133 = ë
        else if (val == 174) text.setCharAt(idx, char(134)); // 134 = î
        else if (val == 175) text.setCharAt(idx, char(135)); // 135 = ï
        else if (val == 185) text.setCharAt(idx, char(136)); // 136 = ù
        else if (val == 187) text.setCharAt(idx, char(137)); // 137 = û
      }
    }
  }
  return text;
}
/*
// A little tool to find the correspondance
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
    if (val == 194 || val == 195) // extended ascII
    {
      indice = val;
      val = byte(text.charAt(idx +1));
      text.remove(idx, 1);
      len--;
    }
    Serial.print(idx); Serial.print(" => "); Serial.print(indice); Serial.print(", "); Serial.println(val);
  }
  Serial.end();
}
//*/



//-----------------------------------------------------------------------
// Times functions - compute & display
//-----------------------------------------------------------------------



void ComputeTime(byte full)
{
  // full:true, switch upon 59 seconds will increase minutes
  // false is used when we are setting time manualy
#ifdef DEBUG
  full = true;
#endif

  if (TimeSeconds1  >= 10)
  {
    TimeSeconds1  = 0;
    TimeFlag     |= 0x02;
    LastSeconds10 = TimeSeconds10;
    TimeSeconds10++;
  }
  if (TimeSeconds10 >=  6)
  {
    TimeSeconds10 = 0;
    if (full)
    {
      TimeFlag     |= 0x04;
      LastMinutes1  = TimeMinutes1;
      TimeMinutes1++;
    }
  }
  if (TimeMinutes1  >= 10)
  {
    TimeMinutes1  = 0;
    TimeFlag     |= 0x08;
    LastMinutes10 = TimeMinutes10;
    TimeMinutes10++;
  }
  if (TimeMinutes10 >=  6)
  {
    TimeMinutes10 = 0;
    if (full)
    {
      TimeFlag     |= 0x10;
      LastHours1    = TimeHours1;
      TimeHours1++;
    }
  }
  if (TimeHours1    >= 10)
  {
    TimeHours1    = 0;
    TimeFlag     |= 0x20;
    LastHours10   = TimeHours10;
    TimeHours10++;
  }
  if (TimeHours10   >=  2 && TimeHours1    >=  4) // special case when we reach MidNight
  {
    TimeHours1    = 0;
    TimeHours10   = 0;
    TimeFlag     |= 0x30;
    LastHours1    = TimeHours1;
    LastHours10   = TimeHours10;
    if (full)
    {
      DateDoW       = (++DateDoW) % 7;
      DateDay++;
    }
  }

  // ======> here, using a DS36231 RTC, we should check date at midnight to adjust <======
  if ( (DateDay >= 28 && DateMonth == 1 && (DateMonth % 4) != 0)
    || (DateDay >= 29 && DateMonth == 1)
    || (DateDay >= 30 && (DateMonth == 3 || DateMonth == 5 || DateMonth == 8 || DateMonth == 10))
    || (DateDay >= 31))
  {
    DateDay = 1;
    if (full)
    {
      DateMonth++;
    }
  }
  if (DateMonth >= 12)
  {
    DateMonth = 0;
    if (full)
    {
      DateYear++;
    }
  }
  // ======> here, using a DS36231 RTC, we should check date at midnight to adjust <======
}

void ShowTime(byte flag)
{
  // For digit concerned by TimeFlag, we display the digit
  TimeFlag |= flag;   // can be used to force full display from each digits flag == 0xFF
  if (TimeFlag == 0xFF) MAX.clear();

  ShowDP(true);           // display the DP
  ShowOptions(TimeFlag);  // dispaly the line of seconds (for tiny fonts only)


  // set values and pointers for h, m, s for the next loop, so it will run faster
  int   font_idx;
  font_idx       = Design[DisplayN][ 1] + FontN;
  byte  width_h  = FONT_DIGIT_SIZE[font_idx][0];
  byte  height_h = FONT_DIGIT_SIZE[font_idx][1];
  font_idx      *= FONT_DIGIT_STRIDE;
  byte* pOld_h10 = FONT_DIGIT[font_idx + LastHours10  ];
  byte* pNew_h10 = FONT_DIGIT[font_idx + TimeHours10  ];
  byte* pOld_h1  = FONT_DIGIT[font_idx + LastHours1   ];
  byte* pNew_h1  = FONT_DIGIT[font_idx + TimeHours1   ];

  font_idx       = Design[DisplayN][ 6] + FontN;
  byte  width_m  = FONT_DIGIT_SIZE[font_idx][0];
  byte  height_m = FONT_DIGIT_SIZE[font_idx][1];
  font_idx      *= FONT_DIGIT_STRIDE;
  byte* pOld_m10 = FONT_DIGIT[font_idx + LastMinutes10];
  byte* pNew_m10 = FONT_DIGIT[font_idx + TimeMinutes10];
  byte* pOld_m1  = FONT_DIGIT[font_idx + LastMinutes1 ];
  byte* pNew_m1  = FONT_DIGIT[font_idx + TimeMinutes1 ];

  font_idx       = Design[DisplayN][11] + FontN;
  byte  width_s  = FONT_DIGIT_SIZE[font_idx][0];
  byte  height_s = FONT_DIGIT_SIZE[font_idx][1];
  font_idx      *= FONT_DIGIT_STRIDE;
  byte* pOld_s10 = FONT_DIGIT[font_idx + LastSeconds10];
  byte* pNew_s10 = FONT_DIGIT[font_idx + TimeSeconds10];
  byte* pOld_s1  = FONT_DIGIT[font_idx + LastSeconds1 ];
  byte* pNew_s1  = FONT_DIGIT[font_idx + TimeSeconds1 ];

  if      (TimeHours10 == 0) pNew_h10 = 0xFF; // display empty char
  else if (LastHours10 == 0) pOld_h10 = 0xFF; // display empty char

  byte slide = 1; if (SlideShow) slide = 8;
  unsigned long tempo;
  while (slide--)
  {
    if (Design[DisplayN][11] != 0xFF)
    {
      if (TimeFlag & 0x01)
        ShowDigit(slide, pNew_s1,  pOld_s1,  Design[DisplayN][14], Design[DisplayN][15], width_s, height_s);
      if (TimeFlag & 0x02)
        ShowDigit(slide, pNew_s10, pOld_s10, Design[DisplayN][12], Design[DisplayN][13], width_s, height_s);
    }
    if (Design[DisplayN][ 6] != 0xFF)
    {
      if (TimeFlag & 0x04)
        ShowDigit(slide, pNew_m1,  pOld_m1,  Design[DisplayN][ 9], Design[DisplayN][10], width_m, height_m);
      if (TimeFlag & 0x08)
        ShowDigit(slide, pNew_m10, pOld_m10, Design[DisplayN][ 7], Design[DisplayN][ 8], width_m, height_m);
    }
    if (Design[DisplayN][ 1] != 0xFF)
    {
      if (TimeFlag & 0x10)
        ShowDigit(slide, pNew_h1,  pOld_h1,  Design[DisplayN][ 4], Design[DisplayN][ 5], width_h, height_h);
      if (TimeFlag & 0x20)
        ShowDigit(slide, pNew_h10, pOld_h10, Design[DisplayN][ 2], Design[DisplayN][ 3], width_h, height_h);
    }

    MAX.display();

    tempo = millis();
    if (slide) while ((unsigned long)(millis() - tempo) < SLIDE_SPEED) bit(8);
  }

  TimeFlag = 0;      // reset the flag, it will get new value at next "computeTime"
}

//=========> display time digit with a slide show
void ShowDigit(byte slide, byte* pNewDigit, byte* pOldDigit, byte coordX, byte coordY, byte width, byte height)
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
      if (pNewDigit != 0xFF) code = pgm_read_byte_near(pNewDigit + n + slide);
      //code = pgm_read_byte_near(pNewDigit + n + slide);
    }

    // send this segment
    byte i = width;
    while (i--) DrawPixel(coordX - i, coordY + n, bitRead(code, i));
  }
}



//-----------------------------------------------------------------------



void RunAlarm()
{
  //---> Display time or alarm
  if      (Alarm1Flag >= ALARM_MAX + 2)                    // ===> STOP the Alarm
  {
    Alarm1Flag = false;
    digitalWrite(PIN_BUZZER, LOW);
    MAX.setIntensity(Intensity);
    ShowTime(0xFF);     // show time (all digits)
    SlideShow  = LastSlideShow;
  }
  else if (Alarm1Flag > 1)                                 // ===> RUN the Alarm
  {
    if (Alarm1Flag & 0x01)   // when parity show time (all digits)
    {
      MAX.setIntensity(Intensity);
      ShowTime(0xFF);
    }
    else
    {
      if (Alarm1Flag == 2)
      {
        LastSlideShow = SlideShow;
        SlideShow     = false;
        digitalWrite(PIN_BUZZER, HIGH); // ===> START the Alarm
      }

      //FillScreen(WHITE, WHITE);
      ReverseScreen();
      MAX.display();
      MAX.setIntensity(INTENSITY_MAX);  // display all dots with high brightness to flash
    }
    Alarm1Flag++;
  }
}

void ShowDP(byte isOn)
{
  // show / hide the DP - dotW-H,x,y1,y2
  byte   dot_width  = Design[DisplayN][16];
  byte   dot_height = Design[DisplayN][17];
  byte   dot_x      = Design[DisplayN][18];
  byte   dot_y1     = Design[DisplayN][19];
  byte   dot_y2     = Design[DisplayN][20];
  if (dot_width)
  {
    DrawSquareFilled(dot_x, dot_y1, dot_width, dot_height, WHITE * isOn);
    DrawSquareFilled(dot_x, dot_y2, dot_width, dot_height, WHITE * isOn);
  }

  // show / hide the alarm dots - al1x,y,al2x,y,timeline
  byte   al1_x     = Design[DisplayN][21];
  byte   al1_y     = Design[DisplayN][22];
  byte   al2_x     = Design[DisplayN][23];
  byte   al2_y     = Design[DisplayN][24];
  DrawPixel(al1_x, al1_y, WHITE * (Alarm1Flag > 0));
  DrawPixel(al2_x, al2_y, WHITE * (Alarm2Flag > 0));
}

void ShowOptions(byte flag)
{
  if      (!Design[DisplayN][25]) return;

  // show / hide the options - al1x,y,al2x,y,opt_xy
  byte   opt_flag   = Design[DisplayN][25];
  byte   opt_x      = Design[DisplayN][26];
  byte   opt_y      = Design[DisplayN][27];

  // draw a dot/second if time > 0s and time < 31s, then erase a dot/second until time == 59s
  // when flag = 0xFF draw all the line
  if      (opt_flag & 0x01)   // ======> TimeLine
  {
    int seconds = TimeSeconds10 * 10 + TimeSeconds1;

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
    //byte temperature = (byte)((ADCW - 324.31) / 1.22);
    byte temperature = (byte)(-56.79);
    byte minus       = (temperature & 0x80) > 1;
    temperature      = abs(temperature);
    byte temp10      = temperature / 10;
    byte temp1       = temperature - temp10;
    //*
    minus       = 1;
    temp10      = 2;
    temp1       = 8;
    //*/

    // get pointers for digits
    //int   font_idx = 19;  // micro thin 2 : 3x5 (digital)
    int   font_idx = 13;  // tiny thin 2 : 3x5 (digital)

    byte  width    = FONT_DIGIT_SIZE[font_idx][0];
    byte  height   = FONT_DIGIT_SIZE[font_idx][1];
    font_idx      *= FONT_DIGIT_STRIDE;
    byte* pNew_10  = FONT_DIGIT[font_idx + temp10];
    byte* pNew_1   = FONT_DIGIT[font_idx + temp1 ];

    byte sign_x = opt_x - 3;
    byte sign_y = opt_y - height / 2;

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



void ShowWelcome()
{
  byte* pLogo = EPACLOGO;
  byte  i     = 24;
  MAX.clear();
  while (i--) MAX.setColumn(4 + i, pgm_read_byte_near(pLogo + i));
  //ReverseScreen();
  MAX.display();
  delay(3000);
  
  byte  x       = 40;
  byte* pSprite = *PACMAN;
  byte  spriteN = 0;
  byte  dir     = 1;
  while (x--)
  {
    byte  idx = 9;
    while (idx--) MAX.setColumn(x - 8 + idx, pgm_read_byte_near(pSprite + spriteN * 9 + idx));
    if      (spriteN == 3) dir = - 1;
    else if (spriteN == 0) dir =   1;
    spriteN = spriteN + dir;
    MAX.display();
    delay(150);
  }
  delay(1000);

  //setFont(*FONT_TINY);
  //setFont(*FONT_ALPHA);
  //setFont(*FONT_NARROW);
  //setFont(*FONT_DIGITAL);

  setFont(*FONT_ALPHA);
  ScrollText("ArduinO' ClocK", 1);
  setFont(*FONT_TINY);
  ScrollText("A SAMPLE... FOR FUN !", (8 - FontHeight) / 2);
  setFont(*FONT_ALPHA);
}

void ShowDate()
{
  // use convertText() to manage accent and special chars alike "°"
  String space    = String("   ");
  String sDoW[]   = {"Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi", "Dimanche"};
  String sMonth[] = {"janvier", convertText("février"), "mars", "avril", "mai", "juin",
                     "juillet", convertText("août"), "septembre", "octobre", "novembre", convertText("décembre")
                    };
  String text     = sDoW[DateDoW] + space + String(DateDay) + space + sMonth[DateMonth] + space + String(DateYear);

  ScrollText(text, 0);
}

void ShowTemp()
{
  // use convertText() to manage accent and special chars alike "°"
  float  temperature = -23.6;
  byte   minus     = false; if (temperature < 0.0) minus = true;
  temperature      = abs(temperature);
  int    temp10    = int(temperature);
  temperature     -= temp10;
  temperature     *= 10;
  int    temp1     = int(temperature);
  String text      = "Temp   ";
  if (minus) text += "-";
  text            += String(temp10) + "." + String(temp1) + convertText("°C");

  ScrollText(text, 0);
}


