/*
 * STM32F103C8T6 Oscilloscope using a 320x240 TFT Version 1.12
 * The max DMA sampling rates is 5.14Msps with single channel, 2.57Msps with 2 channels.
 * The max software loop sampling rates is 125ksps with 2 channels.
 * + Pulse Generator
 * + PWM DDS Function Generator (23 waveforms)
 * Copyright (c) 2023, Siliconvalley4066
 */
/*
 * Arduino Oscilloscope using a graphic LCD
 * The max sampling rates are 4.3ksps with 2 channels and 8.6ksps with a channel.
 * Copyright (c) 2009, Noriaki Mitsunaga
 */

#include <SPI.h>
#include "Adafruit_GFX_AS.h"
#include <Adafruit_ILI9341_STM.h>
#include <XPT2046_Touchscreen.h>
#include "PeriodCount_STM.h"

#define TFT_CS         PB1
#define TFT_DC         PB10
#define TFT_RST        PB11
Adafruit_ILI9341_STM display = Adafruit_ILI9341_STM(TFT_CS, TFT_DC, TFT_RST); // Mosi - PA7, SCK - PA5
SPISettings TFT_SPISet(SAFE_FREQ, MSBFIRST, SPI_MODE0, DATA_SIZE_16BIT);

// This is calibration data for the raw touch data to the screen coordinates
#define TS_MINX 3900
#define TS_MINY 3750
#define TS_MAXX 320
#define TS_MAXY 340
#define CS_PIN  PA4
XPT2046_Touchscreen ts(CS_PIN);

#define GPIN1 (22)
#define BUTTON5DIR
#define EEPROM_START 0
#ifdef EEPROM_START
#include <EEPROM.h>
#endif
#include "arduinoFFT.h"
#define FFT_N 256
double vReal[FFT_N]; // Real part array, actually float type
double vImag[FFT_N]; // Imaginary part array
ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, FFT_N, 1.0);  // Create FFT object

float waveFreq[2];             // frequency (Hz)
float waveDuty[2];             // duty ratio (%)
int dataMin[2];                // buffer minimum value (smallest=0)
int dataMax[2];                //        maximum value (largest=4095)
int dataAve[2];                // 10 x average value (use 10x value to keep accuracy. so, max=40950)
int saveTimer;                 // remaining time for saving EEPROM
int timeExec;                  // approx. execution time of current range setting (ms)
extern byte duty;
extern byte p_range;
extern unsigned short count;
extern long ifreq;
extern byte wave_id;

const int LCD_WIDTH = 320;
const int LCD_HEIGHT = 240;
const int LCD_YMAX = 200;
const int SAMPLES = 300;
const int NSAMP = 2000;
const int DISPLNG = 300;
const int DOTS_DIV = 25;
const int XOFF = 10;
const int YOFF = 20;
const byte ad_ch0 = PA0;                // Analog pin for channel 0
const byte ad_ch1 = PA1;                // Analog pin for channel 1
const long VREF[] = {83, 165, 413, 825, 1650}; // reference voltage 3.3V ->  82.5 :   1V/div range (40mV/dot)
                                        //                        -> 165 : 0.5V/div
                                        //                        -> 413 : 0.2V/div
                                        //                        -> 825 : 100mV/div
                                        //                        -> 1650 : 50mV/div
                                        // 3.3V / attn * DOTS_DIV / vdiv
//const int MILLIVOL_per_dot[] = {100, 50, 20, 10, 5}; // mV/dot
const int ac_offset[] PROGMEM = {2917, 434, -1055, -1552, -1800}; // 4 div offset
//                            = 4 * vdiv / 3.3 * 4096 - 2048
const int MODE_ON = 0;
const int MODE_INV = 1;
const int MODE_OFF = 2;
const char Modes[3][4] PROGMEM = {"ON", "INV", "OFF"};
const int TRIG_AUTO = 0;
const int TRIG_NORM = 1;
const int TRIG_SCAN = 2;
const int TRIG_ONE  = 3;
const char TRIG_Modes[4][5] PROGMEM = {"Auto", "Norm", "Scan", "One "};
const int TRIG_E_UP = 0;
const int TRIG_E_DN = 1;
#define RATE_MIN 0
#define RATE_MAX 19
#define RATE_ILV 0
#define RATE_DMA 4
#define RATE_DUAL 1
#define RATE_SLOW 10
#define RATE_ROLL 13
#define ITEM_MAX 29
const char Rates[20][5] PROGMEM = {"4.9u", "9.7u", "18u ", "57u ", "94u ", "200u", "500u", " 1ms", " 2ms", " 5ms", "10ms", "20ms", "50ms", "100m", "200m", "0.5s", " 1s ", " 2s ", " 5s ", " 10s"};
const unsigned long HREF[] PROGMEM = {2, 4, 7, 23, 38, 80, 200, 400, 800, 2000, 4000, 8000, 20000, 40000, 80000, 200000, 400000, 800000, 2000000, 4000000, 8000000};
const float dmahref[5] = {1.944, 3.889, 7.222, 22.78, 37.78};
#define RANGE_MIN 0
#define RANGE_MAX 4
#define VRF 3.3
const char Ranges[5][5] PROGMEM = {" 1V ", "0.5V", "0.2V", "0.1V", "50mV"};
byte range0 = RANGE_MIN;
byte range1 = RANGE_MIN;
byte ch0_mode = MODE_ON, ch1_mode = MODE_ON, rate = 0, orate, wrate = 0;
byte trig_mode = TRIG_AUTO, trig_lv = 10, trig_edge = TRIG_E_UP, trig_ch = ad_ch0;
bool Start = true;  // Start sampling
byte item = 0;      // Default item
short ch0_off = 0, ch1_off = 400;
byte data[4][SAMPLES];                  // keep twice of the number of channels to make it a double buffer
uint32_t cap_buf32[NSAMP / 2], cap_buf33[NSAMP / 4];
uint16_t *cap_buf = (uint16_t *)&cap_buf32, *cap_buf1 = (uint16_t *)&cap_buf33;
byte odat00, odat01, odat10, odat11;    // old data buffer for erase
byte sample=0;                          // index for double buffer
bool fft_mode = false, pulse_mode = false, dds_mode = false, fcount_mode = false;
byte info_mode = 3; // Text information display mode
int trigger_ad;
const double sys_clk = (double)F_CPU;
volatile bool wfft, wdds;
byte time_mag = 1;  // magnify timebase: 1, 2, 5 or 10
double compensation = 1.0;  // compensation for frequency counter
boolean calib = false;      // calibrate flag for frequency counter

#define LEFTPIN   PB13  // LEFT
#define RIGHTPIN  PB14  // RIGHT
#define UPPIN     PB12  // UP
#define DOWNPIN   PB15  // DOWN
#define CH0DCSW   PB8   // DC/AC switch ch0
#define CH1DCSW   PB5   // DC/AC switch ch1

#define BGCOLOR   ILI9341_BLACK
#define GRIDCOLOR ILI9341_DARKGREY
#define CH1COLOR  ILI9341_GREEN
#define CH2COLOR  ILI9341_YELLOW
#define FRMCOLOR  ILI9341_LIGHTGREY
#define TXTCOLOR  ILI9341_WHITE
#define HIGHCOLOR ILI9341_CYAN
#define OFFCOLOR  ILI9341_DARKGREY
#define REDCOLOR  ILI9341_RED
#define LED_ON    LOW
#define LED_OFF   HIGH
#define INFO_OFF  0x20
#define INFO_BIG  0x10
#define INFO_VOL2 0x08
#define INFO_VOL1 0x04
#define INFO_FRQ2 0x02
#define INFO_FRQ1 0x01

void setup(){
  pinMode(PA0, INPUT_ANALOG);
  pinMode(PA1, INPUT_ANALOG);
  pinMode(CH0DCSW, INPUT_PULLUP);   // CH1 DC/AC
  pinMode(CH1DCSW, INPUT_PULLUP);   // CH2 DC/AC
  pinMode(UPPIN, INPUT_PULLUP);     // up
  pinMode(DOWNPIN, INPUT_PULLUP);   // down
  pinMode(RIGHTPIN, INPUT_PULLUP);  // right
  pinMode(LEFTPIN, INPUT_PULLUP);   // left
  pinMode(LED_BUILTIN, OUTPUT);     // sets the digital pin as output
  display.begin();                    // initialise the library
  display.setRotation(1);
  ts.begin();
  ts.setRotation(1);
  display.fillScreen(BGCOLOR);

//  Serial.begin(115200);
#ifdef EEPROM_START
  if (EEPROM.init(0x801F000, 0x801F800, 0x400) != EEPROM_OK) {  // 1024 byte
    EEPROM.format();
  }
  loadEEPROM();                         // read last settings from EEPROM
#else
  set_default();
#endif
  wfft = fft_mode;
  wdds = dds_mode;
//  DrawGrid();
//  DrawText();
//  display.display();
  if (pulse_mode)
    pulse_init();                       // calibration pulse output
  if (dds_mode)
    dds_setup();
  orate = RATE_DMA + 1;                 // old rate befor change
  adc_calibrate(ADC1);
  adc_calibrate(ADC2);
  adc_set_speed();
}

#ifdef DOT_GRID
void DrawGrid() {
  int disp_leng;
  disp_leng = DISPLNG;
  for (int x=0; x<=disp_leng; x += 5) { // Horizontal Line
    for (int y=0; y<=LCD_YMAX; y += DOTS_DIV) {
      display.drawPixel(XOFF+x, YOFF+y, GRIDCOLOR);
//      CheckSW();
    }
  }
  for (int x=0; x<=disp_leng; x += DOTS_DIV ) { // Vertical Line
    for (int y=0; y<=LCD_YMAX; y += 5) {
      display.drawPixel(XOFF+x, YOFF+y, GRIDCOLOR);
//      CheckSW();
    }
  }
}
#else
void DrawGrid() {
  display.drawFastVLine(XOFF, YOFF, LCD_YMAX, FRMCOLOR);          // left vertical line
  display.drawFastVLine(XOFF+SAMPLES, YOFF, LCD_YMAX, FRMCOLOR);  // right vertical line
  display.drawFastHLine(XOFF, YOFF, SAMPLES, FRMCOLOR);           // top horizontal line
  display.drawFastHLine(XOFF, YOFF+LCD_YMAX, SAMPLES, FRMCOLOR);  // bottom horizontal line

  for (int y = 0; y < LCD_YMAX; y += DOTS_DIV) {
    if (y > 0){
      display.drawFastHLine(XOFF+1, YOFF+y, SAMPLES - 1, GRIDCOLOR);  // Draw 9 horizontal lines
    }
    for (int i = 5; i < DOTS_DIV; i += 5) {
      display.drawFastHLine(XOFF+SAMPLES / 2 - 3, YOFF+y + i, 7, GRIDCOLOR);  // Draw the vertical center line ticks
    }
  }
  for (int x = 0; x < SAMPLES; x += DOTS_DIV) {
    if (x > 0){
      display.drawFastVLine(XOFF+x, YOFF+1, LCD_YMAX - 1, GRIDCOLOR); // Draw 11 vertical lines
    }
    for (int i = 5; i < DOTS_DIV; i += 5) {
      display.drawFastVLine(XOFF+x + i, YOFF+LCD_YMAX / 2 -3, 7, GRIDCOLOR);  // Draw the horizontal center line ticks
    }
  }
}
#endif

void fcount_disp() {
  unsigned long count;
  static double dfreq = 0.0;
  uint8_t status;

  if (!fcount_mode) return;
  if (status = PeriodCount.available()) { // wait finish  restart
    count = PeriodCount.read();
    dfreq = PeriodCount.countToFrequency(count);
    if (calib) calibrate(dfreq);
    calib = false;
    if ((status == 1) && (dfreq > 0.001)) {     // ready
      PeriodCount.adjust(dfreq);
    } else {  // timeout
      PeriodCount.end();
      PeriodCount.begin(1000);
    }
  }
  double freq = dfreq * compensation;
  displayfreq(freq);
}

void displayfreq(double freq) {
  String ss;
  int i, f, l;
  for (f = 7; f >= 0; --f) {
    ss = String(freq, f);
    if (ss.length() < 10) break;
  }
  if ((i = ss.indexOf('.')) > 7)  // i = integer part
    ss.remove(i, 2);              // remove fractional part for >= 10,000,000Hz
  l = ss.length();
  if (i > 3) {  // greater than or equal to 1,000Hz
    for (int j = 0; j < (i - 3); ++j) {
      display.print(ss.substring(j, j + 1));
      if ((i-j) == 4 || (i-j) == 7) { // small thousand separator
        display.print(',');
      }
    }
    display.print(ss.substring(i - 3, l));
    display.print("Hz");
    if (i != 7 && i != 9) display.print(' ');
  } else {  // below 1000Hz
    display.print(ss);
    display.print("Hz  ");
  }
}

void calibrate(double freq) {
  float references[] = {25e6, 24e6, 20e6, 16e6, 12e6, 10e6, 8e6, 6e6, 5e6,
          4e6, 3e6, 2e6, 1e6, 1e5, 32768.0, 1e4, 1e3, 100.0, 10.0, 1.0};
  int num = sizeof(references) / sizeof(float);
  for (int i = 0; i < num; ++i) {
    double ref = (double) references[i];
    if ((ref * 0.99995) < freq && freq < (ref * 1.00005)) { // 50ppm
      compensation = ref / (double) freq;
      break;
    }
  }
}

void display_range(byte rng) {
  display.print(Ranges[rng]);
}

void display_rate(void) {
  display.print(Rates[rate]);
}

void display_mode(byte chmode) {
  display.print(Modes[chmode]); 
}

void display_trig_mode(void) {
  display.print(TRIG_Modes[trig_mode]); 
}

void display_ac(byte pin) {
  if (digitalRead(pin) == LOW) display.print('~');
}

void DrawGrid(int x) {
  if ((x % DOTS_DIV) == 0) {
    for (int y=0; y<=LCD_YMAX; y += 5)
      display.drawPixel(XOFF+x, YOFF+y, GRIDCOLOR);
  } else if ((x % 5) == 0)
    for (int y=0; y<=LCD_YMAX; y += DOTS_DIV)
      display.drawPixel(XOFF+x, YOFF+y, GRIDCOLOR);
}

void ClearAndDrawGraph() {
  int clear;
  byte *p1, *p2, *p3, *p4, *p5, *p6, *p7, *p8;
  int disp_leng;
  disp_leng = DISPLNG-1;
  bool ch1_active = ch1_mode != MODE_OFF && !(rate < RATE_DUAL && ch0_mode != MODE_OFF);
  if (sample == 0)
    clear = 2;
  else
    clear = 0;
  p1 = data[clear+0];
  p2 = p1 + 1;
  p3 = data[sample+0];
  p4 = p3 + 1;
  p5 = data[clear+1];
  p6 = p5 + 1;
  p7 = data[sample+1];
  p8 = p7 + 1;
#if 0
  for (int x=0; x<disp_leng; x++) {
    display.drawPixel(XOFF+x, YOFF+LCD_YMAX-data[sample+0][x], CH1COLOR);
    display.drawPixel(XOFF+x, YOFF+LCD_YMAX-data[sample+1][x], CH2COLOR);
  }
#else
  for (int x=0; x<disp_leng; x++) {
    if (ch0_mode != MODE_OFF) {
      display.drawLine(XOFF+x, YOFF+LCD_YMAX-*p1++, XOFF+x+1, YOFF+LCD_YMAX-*p2++, BGCOLOR);
      display.drawLine(XOFF+x, YOFF+LCD_YMAX-*p3++, XOFF+x+1, YOFF+LCD_YMAX-*p4++, CH1COLOR);
    }
    if (ch1_active) {
      display.drawLine(XOFF+x, YOFF+LCD_YMAX-*p5++, XOFF+x+1, YOFF+LCD_YMAX-*p6++, BGCOLOR);
      display.drawLine(XOFF+x, YOFF+LCD_YMAX-*p7++, XOFF+x+1, YOFF+LCD_YMAX-*p8++, CH2COLOR);
    }
  }
#endif
}

void ClearAndDrawDot(int i) {
  DrawGrid(i);
#if 0
  for (int x=0; x<DISPLNG; x++) {
    display.drawPixel(XOFF+i, YOFF+LCD_YMAX-odat01, BGCOLOR);
    display.drawPixel(XOFF+i, YOFF+LCD_YMAX-odat11, BGCOLOR);
    display.drawPixel(XOFF+i, YOFF+LCD_YMAX-data[sample+0][i], CH1COLOR);
    display.drawPixel(XOFF+i, YOFF+LCD_YMAX-data[sample+1][i], CH2COLOR);
  }
#else
  if (i < 1) {
    return;
  }
  if (ch0_mode != MODE_OFF) {
    display.drawLine(XOFF+i-1, YOFF+LCD_YMAX-odat00,   XOFF+i, YOFF+LCD_YMAX-odat01, BGCOLOR);
    display.drawLine(XOFF+i-1, YOFF+LCD_YMAX-data[0][i-1], XOFF+i, YOFF+LCD_YMAX-data[0][i], CH1COLOR);
  }
  if (ch1_mode != MODE_OFF) {
    display.drawLine(XOFF+i-1, YOFF+LCD_YMAX-odat10,   XOFF+i, YOFF+LCD_YMAX-odat11, BGCOLOR);
    display.drawLine(XOFF+i-1, YOFF+LCD_YMAX-data[1][i-1], XOFF+i, YOFF+LCD_YMAX-data[1][i], CH2COLOR);
  }
#endif
}

void scaleDataArray(byte ad_ch, int trig_point)
{
  byte *pdata, ch_mode, range, ch;
  short ch_off;
  uint16_t *idata, *qdata;
  long a, b;

  if (ad_ch == ad_ch1) {
    ch_off = ch1_off;
    ch_mode = ch1_mode;
    range = range1;
    pdata = data[sample+1];
    idata = &cap_buf1[trig_point];
    ch = 1;
  } else {
    ch_off = ch0_off;
    ch_mode = ch0_mode;
    range = range0;
    pdata = data[sample+0];
    idata = &cap_buf[trig_point];
    ch = 0;
  }
  for (int i = 0; i < SAMPLES; i++) {
    a = ((*idata + ch_off) * VREF[range] + 2048) >> 12;
    if (a > LCD_YMAX) a = LCD_YMAX;
    else if (a < 0) a = 0;
    if (ch_mode == MODE_INV)
      a = LCD_YMAX - a;
    *pdata++ = (byte) a;
    ++idata;
  }
  if (rate < RATE_ROLL) {
    switch (time_mag) {
    case 2:
    case 5:
    case 10:
      mag(data[sample+ch], time_mag); // magnify timebase for display
      break;
    default:  // do nothing
      time_mag = 1;   // fix odd value
      break;
    }
  }
}

byte adRead(byte ch, byte mode, int off, int i)
{
  int16_t aa = analogRead(ch);
  long a = (((long)aa+off)*VREF[ch == ad_ch0 ? range0 : range1]+2048) >> 12;
  if (a > LCD_YMAX) a = LCD_YMAX;
  else if (a < 0) a = 0;
  if (mode == MODE_INV)
    a = LCD_YMAX - a;
  if (ch == ad_ch1) {
    cap_buf1[i] = aa;
  } else {
    cap_buf[i] = aa;
  }
  return a;
}

int advalue(int value, long vref, byte mode, int offs) {
  if (mode == MODE_INV)
    value = LCD_YMAX - value;
  return ((long)value << 12) / vref - offs;
}

void set_trigger_ad() {
  if (trig_ch == ad_ch0) {
    trigger_ad = advalue(trig_lv, VREF[range0], ch0_mode, ch0_off);
  } else {
    trigger_ad = advalue(trig_lv, VREF[range1], ch1_mode, ch1_off);
  }
}

void loop() {
  int oad, ad;
  unsigned long auto_time;

  timeExec = 100;
  digitalWrite(LED_BUILTIN, LED_ON);
  if (rate > RATE_DMA) {
    set_trigger_ad();
    auto_time = pow(10, rate / 3) + 5;
    if (trig_mode != TRIG_SCAN) {
      unsigned long st = millis();
      oad = analogRead(trig_ch);
      for (;;) {
        ad = analogRead(trig_ch);

        if (trig_edge == TRIG_E_UP) {
          if (ad > trigger_ad && trigger_ad > oad)
            break;
        } else {
          if (ad < trigger_ad && trigger_ad < oad)
            break;
        }
        oad = ad;

        if (rate > RATE_SLOW)
          CheckSW();      // no need for fast sampling
        if (trig_mode == TRIG_SCAN)
          break;
        if (trig_mode == TRIG_AUTO && (millis() - st) > auto_time)
          break; 
      }
    }
  }

  // sample and draw depending on the sampling rate
  if (rate < RATE_ROLL && Start) {
    // change the index for the double buffer
    if (sample == 0)
      sample = 2;
    else
      sample = 0;

    if (rate <= RATE_ILV) {   // DMA interleave, min 0.19us sampling (5.14Msps)
      interleave_setup();
      if (ch0_mode != MODE_OFF)
        takeSamples_ilv(ad_ch0);
      else if (ch1_mode != MODE_OFF)
        takeSamples_ilv(ad_ch1);
    } else if (rate <= RATE_DMA) {   // DMA, min 0.39us sampling (2.57Msps)
      takeSamples();
    } else if (rate < RATE_SLOW) {   // dual channel 8us, 20us, 40us, 80us, 200us sampling
      sample_dual_us(HREF[rate] / 10);
    } else {                // dual channel 0.4ms, 0.8ms, 2ms sampling
      sample_dual_ms(HREF[rate] / 10);
    }
    digitalWrite(LED_BUILTIN, LED_OFF);
    draw_screen();
  } else if (Start) { // 4ms - 400ms sampling
    timeExec = 5000;
    static const unsigned long r_[] PROGMEM = {4000, 8000, 20000, 40000, 80000, 200000, 400000};
    unsigned long r;
    int disp_leng;
    disp_leng = DISPLNG;
//    unsigned long st0 = millis();
    unsigned long st = micros();
    for (int i=0; i<disp_leng; i ++) {
      r = r_[rate - RATE_ROLL];  // rate may be changed in loop
      while((st - micros())<r) {
        CheckSW();
        if (rate < RATE_ROLL)
          break;
      }
      if (rate<RATE_ROLL) { // sampling rate has been changed
        display.fillScreen(BGCOLOR);
        break;
      }
      st += r;
      if (st - micros()>r)
          st = micros(); // sampling rate has been changed to shorter interval
      if (!Start) {
         i --;
         continue;
      }
      odat00 = odat01;      // save next previous data ch0
      odat10 = odat11;      // save next previous data ch1
      odat01 = data[0][i];  // save previous data ch0
      odat11 = data[1][i];  // save previous data ch1
      if (ch0_mode != MODE_OFF) data[0][i] = adRead(ad_ch0, ch0_mode, ch0_off, i);
      if (ch1_mode != MODE_OFF) data[1][i] = adRead(ad_ch1, ch1_mode, ch1_off, i);
      ClearAndDrawDot(i);
    }
    DrawGrid(disp_leng);  // right side grid
    // Serial.println(millis()-st0);
    digitalWrite(LED_BUILTIN, LED_OFF);
//    DrawGrid();
    DrawText();
  } else {
    DrawText();
  }
  if (trig_mode == TRIG_ONE)
    Start = false;
  CheckSW();
#ifdef EEPROM_START
  saveEEPROM();                         // save settings to EEPROM if necessary
#endif
}

void draw_screen() {
  if (wfft != fft_mode) {
    fft_mode = wfft;
    display.fillScreen(BGCOLOR);
  }
  if (fft_mode) {
    DrawText();
    plotFFT();
  } else {
    DrawGrid();
    ClearAndDrawGraph();
    DrawText();
  }
}

#define textINFO 214
void measure_frequency(int ch) {
  int x1, x2;
  byte y;
  freqDuty(ch);
  if (info_mode & INFO_BIG) {
    x1 = textINFO, x2 = x1+24;        // Big
  } else {
    x1 = textINFO + 48, x2 = x1+12;   // Small
  }
  if (ch == 0) {
    y = 22;
    display.setTextColor(CH1COLOR, BGCOLOR);
  } else {
    y = 122;
    display.setTextColor(CH2COLOR, BGCOLOR);
  }
  TextBG(&y, x1, 8);
  float freq = waveFreq[ch];
  if (freq < 999.5)
    display.print(freq);
  else if (freq < 999999.5)
    display.print(freq, 0);
  else {
    display.print(freq/1000.0, 0);
    display.print('k');
  }
  display.print("Hz");
  if (fft_mode) return;
  TextBG(&y, x2, 6);
  float duty = waveDuty[ch];
  if (duty > 99.9499) duty = 99.9;
  display.print(duty, 1);  display.print('%');
}

void measure_voltage(int ch) {
  int x;
  byte y;
  if (fft_mode) return;
  float vavr = VRF * dataAve[ch] / 40950.0;
  float vmax = VRF * dataMax[ch] / 4095.0;
  float vmin = VRF * dataMin[ch] / 4095.0;
  if (info_mode & INFO_BIG) {
    x = textINFO, y = 62;       // Big
  } else {
    x = textINFO + 48, y = 42;  // Small
  }
  if (ch == 0) {
    display.setTextColor(CH1COLOR, BGCOLOR);
  } else {
    y += 100;
    display.setTextColor(CH2COLOR, BGCOLOR);
  }
  TextBG(&y, x, 8);
  display.print("max");  display.print(vmax); if (vmax >= 0.0) display.print('V');
  TextBG(&y, x, 8);
  display.print("avr");  display.print(vavr); if (vavr >= 0.0) display.print('V');
  TextBG(&y, x, 8);
  display.print("min");  display.print(vmin); if (vmin >= 0.0) display.print('V');
}

void sample_us(unsigned int r) { // single channel
  byte ch;
  uint16_t *p;
  if (ch0_mode == MODE_OFF && ch1_mode != MODE_OFF) {
    ch = ad_ch1;
    p = cap_buf1;
  } else {
    ch = ad_ch0;
    p = cap_buf;
  }
  unsigned long st = micros();
  for (int i=0; i<SAMPLES; i ++) {
    while(micros() - st < r) ;
    p[i] = analogRead(ch);
    st += r;
  }
  scaleDataArray(ch, 0);
}

void sample_dual_us(unsigned int r) { // dual channel. r > 67
  if (ch0_mode != MODE_OFF && ch1_mode == MODE_OFF) {
    unsigned long st = micros();
    for (int i=0; i<SAMPLES; i ++) {
      while(micros() - st < r) ;
      cap_buf[i] = analogRead(ad_ch0);
      st += r;
    }
    scaleDataArray(ad_ch0, 0);
  } else if (ch0_mode == MODE_OFF && ch1_mode != MODE_OFF) {
    unsigned long st = micros();
    for (int i=0; i<SAMPLES; i ++) {
      while(micros() - st < r) ;
      cap_buf1[i] = analogRead(ad_ch1);
      st += r;
    }
    scaleDataArray(ad_ch1, 0);
  } else {
    unsigned long st = micros();
    for (int i=0; i<SAMPLES; i ++) {
      while(micros() - st < r) ;
      cap_buf[i] = analogRead(ad_ch0);
      cap_buf1[i] = analogRead(ad_ch1);
      st += r;
    }
    scaleDataArray(ad_ch0, 0);
    scaleDataArray(ad_ch1, 0);
  }
}

void sample_dual_ms(unsigned int r) { // dual channel. r > 500
// .5ms, 1ms or 2ms sampling
  unsigned long st = micros();
  for (int i=0; i<SAMPLES; i ++) {
    while(micros() - st < r) ;
    st += r;
    if (ch0_mode != MODE_OFF) {
      cap_buf[i] = analogRead(ad_ch0);
    }
    if (ch1_mode != MODE_OFF) {
      cap_buf1[i] = analogRead(ad_ch1);
    }
  }
  scaleDataArray(ad_ch0, 0);
  scaleDataArray(ad_ch1, 0);
}

void plotFFT() {
  byte *lastplot, *newplot;
  int ylim = 200;

  int clear = (sample == 0) ? 2 : 0;
  for (int i = 0; i < FFT_N; i++) {
    vReal[i] = cap_buf[i];
    vImag[i] = 0.0;
  }
  FFT.dcRemoval();
  FFT.windowing(FFTWindow::Hann, FFTDirection::Forward);  // Weigh data
  FFT.compute(FFTDirection::Forward);                     // Compute FFT
  FFT.complexToMagnitude();                               // Compute magnitudes
  newplot = data[sample];
  lastplot = data[clear];
  for (int i = 1; i < FFT_N/2; i++) {
    float db = log10(vReal[i]);
    int dat = constrain((int)(50.0 * (db - 1.6)), 0, ylim);
    display.drawFastVLine(i * 2, ylim - lastplot[i], lastplot[i], BGCOLOR); // erase old
    display.drawFastVLine(i * 2, ylim - dat, dat, CH1COLOR);
    newplot[i] = dat;
  }
  draw_scale();
}

void draw_scale() {
  int ylim = 204;
  float fhref, nyquist;
  display.setTextColor(TXTCOLOR);
  display.setCursor(0, ylim); display.print("0Hz"); 
  fhref = freqhref();
  nyquist = 5.0e6 / fhref; // Nyquist frequency
  if (nyquist > 999.0) {
    nyquist = nyquist / 1000.0;
    if (nyquist > 99.5) {
      display.setCursor(116, ylim); display.print(nyquist/2,0);display.print('k');
      display.setCursor(232, ylim); display.print(nyquist,0);
    } else if (nyquist > 9.95) {
      display.setCursor(122, ylim); display.print(nyquist/2,0);display.print('k');
      display.setCursor(238, ylim); display.print(nyquist,0);
    } else {
      display.setCursor(122, ylim); display.print(nyquist/2,1);display.print('k');
      display.setCursor(232, ylim); display.print(nyquist,1);
    }
    display.print('k');
  } else {
    display.setCursor(116, ylim); display.print(nyquist/2,0);
    display.setCursor(238, ylim); display.print(nyquist,0);
  }
}

float freqhref() {
  float fhref;
  if (rate <= RATE_DMA) {   // DMA sampling
    fhref = dmahref[rate];
  } else {
    fhref = (float) HREF[rate];
  }
  return fhref;
}

#ifdef EEPROM_START

void saveEEPROM() {                   // Save the setting value in EEPROM after waiting a while after the button operation.
  uint16 p = EEPROM_START;
  if (saveTimer > 0) {                // If the timer value is positive
    saveTimer = saveTimer - timeExec; // Timer subtraction
    if (saveTimer <= 0) {             // if time up
      EEPROM.update(p++, range0);      // save current status to EEPROM
      EEPROM.update(p++, ch0_mode);
      EEPROM.update(p++, ch0_off);
      EEPROM.update(p++, range1);
      EEPROM.update(p++, ch1_mode);
      EEPROM.update(p++, ch1_off);
      EEPROM.update(p++, rate);
      EEPROM.update(p++, trig_mode);
      EEPROM.update(p++, trig_lv);
      EEPROM.update(p++, trig_edge);
      EEPROM.update(p++, trig_ch);
      EEPROM.update(p++, fft_mode);
      EEPROM.update(p++, info_mode);
      EEPROM.update(p++, item);
      EEPROM.update(p++, pulse_mode);
      EEPROM.update(p++, duty);
      EEPROM.update(p++, p_range);
      EEPROM.update(p++, count);
      EEPROM.update(p++, dds_mode);
      EEPROM.update(p++, wave_id);
      EEPROM.update(p++, ifreq & 0xffff);
      EEPROM.update(p++, (ifreq >> 16) & 0xffff);
      EEPROM.update(p++, time_mag);
      uint16_t *q = (uint16_t *) &compensation;
      EEPROM.update(p++, *q++); EEPROM.update(p++, *q++);
      EEPROM.update(p++, *q++); EEPROM.update(p++, *q++);
    }
  }
}
#endif

void set_default() {
  range0 = RANGE_MIN;
  ch0_mode = MODE_ON;
  ch0_off = 0;
  range1 = RANGE_MIN;
  ch1_mode = MODE_ON;
  ch1_off = 2048;
  rate = 6;
  trig_mode = TRIG_AUTO;
  trig_lv = 20;
  trig_edge = TRIG_E_UP;
  trig_ch = ad_ch0;
  fft_mode = false;
  info_mode = 1;  // display frequency and duty.  Voltage display is off
  item = 0;       // menu item
  pulse_mode = true;
  duty = 128;     // PWM 50%
  p_range = 1;    // PWM range
  count = 35999;  // PWM 1kHz
  dds_mode = false;
  wave_id = 0;    // sine wave
  ifreq = 23841;  // 238.41Hz
  time_mag = 1;   // magnify timebase
  compensation = 1.0; // frequency counter
}

extern const byte wave_num;

#ifdef EEPROM_START
void loadEEPROM() { // Read setting values from EEPROM (abnormal values will be corrected to default)
  uint16 p = EEPROM_START, error = 0;

  range0 = EEPROM.read(p++);                // range0
  if ((range0 < RANGE_MIN) || (range0 > RANGE_MAX)) ++error;
  ch0_mode = EEPROM.read(p++);              // ch0_mode
  if (ch0_mode > 2) ++error;
  ch0_off = EEPROM.read(p++);               // ch0_off
  if ((ch0_off < -8192) || (ch0_off > 8191)) ++error;

  range1 = EEPROM.read(p++);                // range1
  if ((range1 < RANGE_MIN) || (range1 > RANGE_MAX)) ++error;
  ch1_mode = EEPROM.read(p++);              // ch1_mode
  if (ch1_mode > 2) ++error;
  ch1_off = EEPROM.read(p++);               // ch1_off
  if ((ch1_off < -8192) || (ch1_off > 8191)) ++error;

  rate = EEPROM.read(p++);                  // rate
  if ((rate < RATE_MIN) || (rate > RATE_MAX)) ++error;
//  if (ch0_mode == MODE_OFF && rate < 5) ++error;  // correct ch0_mode
  trig_mode = EEPROM.read(p++);             // trig_mode
  if (trig_mode > TRIG_SCAN) ++error;
  trig_lv = EEPROM.read(p++);               // trig_lv
  if (trig_lv > LCD_YMAX) ++error;
  trig_edge = EEPROM.read(p++);             // trig_edge
  if (trig_edge > 1) ++error;
  trig_ch = EEPROM.read(p++);               // trig_ch
  if (trig_ch != ad_ch0 && trig_ch != ad_ch1) ++error;
  fft_mode = EEPROM.read(p++);              // fft_mode
  info_mode = EEPROM.read(p++);             // info_mode
  if (info_mode > 63) ++error;
  item = EEPROM.read(p++);                  // item
  if (item > ITEM_MAX) ++error;
  pulse_mode = EEPROM.read(p++);            // pulse_mode
  duty = EEPROM.read(p++);                  // duty
  p_range = EEPROM.read(p++);               // p_range
  if (p_range > 16) ++error;
  count = EEPROM.read(p++);                 // count
  dds_mode = EEPROM.read(p++);              // DDS mode
  wave_id = EEPROM.read(p++);               // DDS wave id
  if (wave_id >= wave_num) ++error;
  *((uint16 *)&ifreq) = EEPROM.read(p++);     // ifreq low
  *((uint16 *)&ifreq + 1) = EEPROM.read(p++); // ifreq high
  if (ifreq > 999999L) ++error;
  time_mag = EEPROM.read(p++);               // magnify timebase
  uint16_t *q = (uint16_t *) &compensation;
  *q++ = EEPROM.read(p++); *q++ = EEPROM.read(p++);
  *q++ = EEPROM.read(p++); *q++ = EEPROM.read(p++);
  if (compensation < 1.002 && compensation > 0.998) ; // OK
  else ++error;
  if (error > 0) {
    EEPROM.format();
    set_default();
  }
}
#endif
