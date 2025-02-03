/* =================================================================
 * MATRIX32: 400 LED Matrix with 100 LEDs/m for ESP32
 * February, 2025
 * Version 0.25
 * Copyright ResinChemTech - released under the Apache 2.0 license
 * ================================================================= */

#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <LittleFS.h>
#include "time.h"
#include <HTTPClient.h>         //For syncing weather from OpenWeatherMap (HTTP Get command)
#include "esp_sntp.h"
#include <ESP32Time.h>          //For setting and getting RTC time from ESP32: https://github.com/fbiego/ESP32Time (v2.0.6)
#include <ArduinoJson.h>        //Needed for saved config file: https://arduinojson.org/ (v7.2.0)
#include <Wire.h>
#include <WiFiUdp.h>
#include <DFRobot_AHT20.h>      //AHT20 temp/humidity sensor: https://github.com/DFRobot/DFRobot_AHT20 (v1.0.0)
#include <ESP32RotaryEncoder.h> //Rotary Encoder: https://github.com/MaffooClock/ESP32RotaryEncoder (v1.1.0)
#include <ArduinoOTA.h>         //OTA Updates via Arduino IDE
#include <Update.h>             //OTA Updates via web page
#include "html.h"               //html code for the firmware update page
#define FASTLED_INTERNAL        //Suppress FastLED SPI/bitbanged compiler warnings (only applies after first compile)
#include <FastLED.h>

#define VERSION "v0.25 (ESP32)"
#define APPNAME "MATRIX CLOCK"
#define TIMEZONE "EST+5EDT,M3.2.0/2,M11.1.0/2"        // Set your custom time zone from this list: https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
#define GMT_OFFSET -5                                 // Manually set time zone offset hours from GMT (e.g EST = -5) - only used if useCustomOffsets is true below
#define DST_OFFSET 1                                  // Manually set DST hour adjustment (add one hour) - only used if useCustomOffsets is true below
#define SYNC_INTERVAL 15                              // How often, in minutes, to sync to NTP server (15-60 minutes recommended) - can be changed via web app
#define WIFIMODE 2                                    // 0 = Only Soft Access Point, 1 = Only connect to local WiFi network with UN/PW, 2 = Both (both needed for onboarding)
#define SERIAL_DEBUG 0                                // 0 = Disable (must be disabled if using RX/TX pins), 1 = enable
#define FORMAT_LITTLEFS_IF_FAILED true                // DO NOT CHANGE
#define NUMELEMENTS(x) (sizeof(x) / sizeof(x[0]))     // DO NOT CHANGE - number of elements in array
// ==============================================================================================
//  *** REVIEW AND UPDATE THESE VARIABLES TO MATCH YOUR ENVIRONMENT/BUILD ***
// ==============================================================================================
//Pin Definitions - Update if your build is different
#define BUS1_SDA 21       //I2C Bus 1 Data (GY-302 Light Sensor)
#define BUS1_SCL 22       //I2C Bus 1 Clock (GY-302 Light Sensor)
#define BUS2_SDA 17       //I2C Bus 2 Data (AHT20 Temp/Humidity Sensor)
#define BUS2_SCL 19       //I2C Bus 2 Clock (AHT20 Temp/Humidity Sensor)
#define BUZZER_OUTPUT 13  //Output pin to drive buzzer or other device upon countdown expiration
#define LED_DATA_PIN 16   //LED Data Output
#define MODE_PIN 25       //Push button (white): Change Mode
#define GREEN_PIN 26      //Push button (green - old V0):
#define RED_PIN 27        //Push button (red - old V1):
#define ENCODER_A 32      //Rotary Encoder CLK
#define ENCODER_B 33      //Rotary Encoder DAT
#define ENCODER_SW 35     //Rotary Encoder Switch (push button)
#define NUM_LEDS 400      //Total of 400 LED's if matrix built as shown
#define MILLI_AMPS 5000   //Update to match <80% max milliamp output of your power suppy (e.g. 20A supply = 16000 max milliamps). Do not set above 15000.

/* ================ Default Starting Values ================
 * All of these following values can be modified and saved via
 * the web interface and the values below normally do not need to be modified.
 * Using the web settings is the preferred way to change these default values.  
 * Values listed here are just starting values before the config file exists or
 * if it can't be read.
 */

String ntpServer = "pool.ntp.org";      // Default server for syncing time
String owmKey = "NA";                   // OpenWeatherMap API key (can be entered after onboarding via web app)
String owmLat = "39.8083";              // Your Latitude for OpenWeatherMap (can be entered after onboarding
String owmLong = "-98.555";             // Your Longitude for OpenWeatherMap (can be entered after onboarding)
bool tempExtUseApi = false;             // Use local API for external temperature instead of OWM
bool tempIntUseApi = false;             // Use local API for internal temperature instead of onboard sensor

String timeZone = TIMEZONE;             // Loaded from #define above.  Change #define if you wish to change this value.
bool useCustomOffsets = false;          // Set to true to use custom offsets instead of timezone (need for some zones that use 1/2 hours - no auto-DST adjustments)
long gmtOffsetHours = -5;               // Example: EST is -5 hours from GMT for standard hours (only used if useCustomOffsets = true)
int dstOffsetHours = 1;                 // Example: 1 'springs forward' one hour.  Set to 0 if your zone does not observe DST (only used if useCustomOffsets = true)
byte defaultClockMode = 0;              // Default mode at boot: 0=Clock, 1=Countdown, 2=Scoreboard, 3=Text
bool binaryClock = false;               // Show clock in binary mode (no temperature, 24-hour mode only)
byte brightness = 64;                   // Default starting brightness at boot. 255=max brightness (based on milliamp rating of power supply)
byte numFont = 1;                       // Default Large number font: 0-Original 7-segment (21 pixels), 1-modern (31 pixels), 2-hybrid (28 pixels)
byte temperatureSymbol = 13;            // Default temp display: 12=Celcius, 13=Fahrenheit
byte temperatureSource = 0;             // 0 Dual inside/outside temp, 1 Exterior temp only (provided via OpenWeatherMap), 2 Internal only (ATH20 sensor)
float temperatureCorrection = 0;        // Temp from RTC module.  Generally runs "hot" due to heat from chip.  Adjust as needed. Does not apply if external source selected.
byte hourFormat = 12;                   // Change this to 24 if you want default 24 hours format instead of 12
String scoreboardTeamLeft = "&V";       // Default visitor (left) team name on scoreboard
String scoreboardTeamRight = "&H";      // Default home (right) team name on scoreboard
bool useBuzzer = true;                  // Sound buzzer when countdown time expires
String textTop = "HELLO";               // Default top line for text display
String textBottom = "WORLD";            // Default bottom line for text display
byte textEffect = 0;                    // Text effect to supply (0 = none, see documentation for others)
byte textEffectSpeed = 1;               // 1 = slow (1 sec) to max of 10 (.1 second)

bool autoSync = true;                   //Auto-sync to NTP server
int autoSyncInterval = 60;              //How often, in minutes, to sync the time.  Ignored if autoSync=false.

String wledAddress = "0.0.0.0";         // IP Address of WLED Controller (set to 0.0.0.0 if not used)
byte wledMaxPreset = 0;                //Max preset number to use for button cycle
             

//Define Color Arrays
//If adding new colors, increase array sizes and add to void defineColors()
CRGB ColorCodes[25];
String WebColors[25];

//===========================================================================================
// Do not change any values below this point unless you are sure you know what you are doing!
//===========================================================================================
CRGB clockColor = CRGB::Blue;              // Main time display color
CRGB temperatureColorInt = CRGB::Red;      // External temperature color
CRGB temperatureColorExt = CRGB::Blue;     // Internal temperature color
CRGB countdownColor = CRGB::Green;         // Active countdown timer color
CRGB countdownColorPaused = CRGB::Orange;  // If different from countdownColor, countdown color will change to this when paused/stopped.
CRGB countdownColorFinalMin = CRGB::Red;   // If different from countdownColor, countdown color will change to this for final 60 seconds.
CRGB scoreboardColorLeft = CRGB::Green;    // Color for left (visitor) score
CRGB scoreboardColorRight = CRGB::Red  ;   // Color for right (home) score
CRGB textColorTop = CRGB::Red;             // Color for top text row
CRGB textColorBottom = CRGB::Green;        // Color for bottom text row
CRGB alternateColor = CRGB::Black;         // Recommend to leave as Black. Otherwise unused pixels will be lit in digits

//For web interface.  The value should match the webColor[] index for the matching color above.  These values can be change via web app.
byte webColorClock = 0;              //Blue
byte webColorTemperatureInt = 3;     //Red
byte webColorTemperatureExt = 0;     //Blue
byte webColorCountdown = 1;          //Green
byte webColorCountdownPaused = 2;    //Orange
byte webColorCountdownFinalMin = 3;  //Red
byte webColorScoreboardLeft = 1;     //Green
byte webColorScoreboardRight = 3;    //Red
byte webColorTextTop = 3;            //Red
byte webColorTextBottom = 1;         //Green

//Local Variables (wifi/onboarding)
String deviceName = "MatrixClock";  //Default Device Name - 16 chars max, no spaces.
String wifiHostName = deviceName;
String wifiSSID = "";
String wifiPW = "";
byte macAddr[6];                //Array of device mac address as hex bytes (reversed)
String strMacAddr;              //Formatted string of device mac address
String baseIPAddress;           //Device assigned IP Address
bool onboarding = false;        //Will be set to true if no config file or wifi cannot be joined
long daylightOffsetHours = 0;   //EDT: offset of 1 hour (GMT -4) during DST dates (set in code when time obtained)
int milliamps = MILLI_AMPS;     //Limited to 5,000 - 25,000 milliamps.

//OTA Variables
String otaHostName = deviceName + "_OTA";  // Will be updated by device name from onboarding + _OTA
bool ota_flag = true;                      // Must leave this as true for board to broadcast port to IDE upon boot
uint16_t ota_boot_time_window = 2500;      // minimum time on boot for IP address to show in IDE ports, in millisecs
uint16_t ota_time_window = 20000;          // time to start file upload when ota_flag set to true (after initial boot), in millsecs
uint16_t ota_time_elapsed = 0;             // Counter when OTA active
uint16_t ota_time = ota_boot_time_window;

uint8_t web_otaDone = 0;        //Web OTA Update

byte clockMode = defaultClockMode;
byte oldMode = 0;
int oldTemp = 0;

//Misc. App Variables
String modeCallingPage = "";
byte holdBrightness = brightness;         //Hold variable for toggling LEDs off/on
bool ledsOn = true;                       //Set to false when LEDs turned off via web or rotary knob.
byte scoreboardLeft = 0;                  // Starting "Visitor" (left) score on scoreboard
byte scoreboardRight = 0;                 // Starting "Home" (right) score on scoreboard
unsigned long tempUpdatePeriod = 60;      //Seconds to elapse before updating temp display in clock mode. Default 1 minute if update period not specified in Settings
unsigned long tempUpdateCount = 0;
unsigned long tempUpdatePeriodExt = 600;  //Seconds to elapse before updating external temp from OWM.  Min. 600 recommended so as not to exceed daily API call allotment
unsigned long tempUpdateCountExt = 0;
int externalTemperature = 0;              // Will only be used if external temp selected in Settings
int internalTemperature = 0;
long lastReconnectAttempt = 0;
unsigned long prevTime = 0;
bool useWLED = false;                     // Indicates whether secondary WLED controller is present (based on non-zero IP address)
bool wledStateOn = false;                 // Current state of WLED (can get out of sync if changed via WLED interface)
byte wledCurPreset = 0;                   // Current active preset (used and set by local pushbuttons)

//Flags for rotary encoder turns
volatile bool turnedRightFlag = false;
volatile bool turnedLeftFlag = false;

//Rotary encoder button debouncing
unsigned long lastDebounceTime = 0;         
unsigned long debounceDelay = 250;          //millisecond delay
int startButtonState;                       //For tracking state
int lastButtonState = HIGH;                 //Set initial state (HIGH = not pressed)


byte r_val = 255;  // RGB values for randomizing colors when using rainbow effect
byte g_val = 0;
byte b_val = 0;
bool dotsOn = true;
byte defaultCountdownMin = 0;
byte defaultCountdownSec = 0;
unsigned long countdownMilliSeconds;
unsigned long endCountDownMillis;
bool timerRunning = false;
unsigned long remCountdownMillis = 0;   // Stores value on pause/resume
unsigned long initCountdownMillis = 0;  // Stores initial last countdown value for reset
//Variables for processing text effects
unsigned int textEffectPeriod = 1000;  // Update period for text effects in millis (min 250 max 2500)
bool effectTextHide = false;           // For text Flash and FlashAlternate effects
int effectBrightness = 0;              // For text Fade In and Fade Out effects
byte oldTextEffect = 0;                // For determining effect switch and to setup new effect
int appearCount = 99;                  // Counter for Appear and Appear Flash effects

//Instaniate objects
time_t now;
tm timeinfo;
WiFiUDP ntpUDP;
HTTPClient http;
ESP32Time rtc(0);  // offset handled via NTP time server

TwoWire bus1 = TwoWire(0);     //I2C Bus 1
TwoWire bus2 = TwoWire(1);     //I2C Bus 2
DFRobot_AHT20 aht(bus2);

//RotaryEncoder rotaryEncoder(ENCODER_A, ENCODER_B, ENCODER_SW);
RotaryEncoder rotaryEncoder(ENCODER_A, ENCODER_B);

WebServer server(80);
CRGB LEDs[NUM_LEDS];

//---- Create arrays for characters: These turn on indiviual pixels to create letters/numbers
//Large Numbers
unsigned long numbers[3][18] = {
  // Original 7-Segment Font
  {
    0b00000000001110111011101111110111,  // [0,0] 0
    0b00000000001110000000000000000111,  // [1] 1
    0b00000011101110111000001111110000,  // [2] 2
    0b00000011101110111000000001110111,  // [3] 3
    0b00000011101110000011100000000111,  // [4] 4
    0b00000011100000111011100001110111,  // [5] 5
    0b00000011100000111011101111110111,  // [6] 6
    0b00000000001110111000000000000111,  // [7] 7
    0b00000011101110111011101111110111,  // [8] 8
    0b00000011101110111011100001110111,  // [9] 9
    0b00000000000000000000000000000000,  // [10] off
    0b00000011101110111011100000000000,  // [11] degrees symbol
    0b00000000000000111011101111110000,  // [12] C(elsius)
    0b00000011100000111011101110000000,  // [13] F(ahrenheit)
    0b00000000001110000011101111110111,  // [14] U
    0b00000011101110111011101110000000,  // [15] P
    0b00000000000000000011101111110000,  // [16] L
    0b00000011101110000000001111110111,  // {17] d
  },
  // Modern Font
  {
    0b00000000011110111011111111110111,  // [1,0] 0
    0b11111101000000000000000000000000,  // [1] 1
    0b00000011101110111010001111111000,  // [2] 2
    0b00000011101110111010000011110111,  // [3] 3
    0b00000011111111000111100000001111,  // [4] 4
    0b00000011100000111111100011110111,  // [5] 5
    0b00000011100000111011111111110111,  // [6] 6
    0b11100000101111111000000000100000,  // [7] 7
    0b00000011101110111011101111110111,  // [8] 8
    0b00000011101110111011100011110111,  // [9] 9
    0b00000000000000000000000000000000,  // [10] off
    0b00000011101110111011100000000000,  // [11] degrees symbol
    0b00000000000000111011101111110000,  // [12] C(elsius)
    0b00000011100000111011101110000000,  // [13] F(ahrenheit)
    0b00000000001110000011101111110111,  // [14] U
    0b00000011101110111011101110000000,  // [15] P
    0b00000000000000000011101111110000,  // [16] L
    0b00000011101110000000001111110111,  // {17] d
  },
  // Hybrid Font
  {
    0b00000000011110111011111111110111,  // [0] 0
    0b11111101000000000000000000000000,  // [1] 1
    0b00000011101110111000001111110000,  // [2] 2
    0b00000011101110111000000001110111,  // [3] 3
    0b00000011111110000011100000000111,  // [4] 4
    0b00000011100000111011100001110111,  // [5] 5
    0b00000011100000111011111111110111,  // [6] 6
    0b00000000011110111000000000000111,  // [7] 7
    0b00000011101110111011101111110111,  // [8] 8
    0b00000011101110111011100001110111,  // [9] 9
    0b00000000000000000000000000000000,  // [10] off
    0b00000011101110111011100000000000,  // [11] degrees symbol
    0b00000000000000111011101111110000,  // [12] C(elsius)
    0b00000011100000111011101110000000,  // [13] F(ahrenheit)
    0b00000000001110000011101111110111,  // [14] U
    0b00000011101110111011101110000000,  // [15] P
    0b00000000000000000011101111110000,  // [16] L
    0b00000011101110000000001111110111,  // {17] d
  }
};

//Define small numbers (3 x 5) - 13 pixels total - For temp display (right-to-left)
long smallNums[] = {
  0b0111111111111,  // [0] 0
  0b0111000000011,  // [1] 1
  0b1111110111110,  // [2] 2
  0b1111110101111,  // [3] 3
  0b1111011100011,  // [4] 4
  0b1101111101111,  // [5] 5
  0b1101111111111,  // [6] 6
  0b0111110000011,  // [7] 7
  0b1111111111111,  // [8] 8
  0b1111111100011,  // [9] 9
  0b1110101111011,  // [10] A
  0b1010111111000,  // [11] P
  0b0001111111110,  // [12] C
  0b1001111111000,  // [13] F
  0b0111011110101,  // [14] V
  0b1111011111011,  // [15] H
  0b0000000000000,  // [16] Off
};

//Define small letters (3 x 5) - 15 pixels total - for text display mode (right-to-left)
//Valid ascii values: 32-122 (both upper and lower case letters render the same)
//Must use & or $ for leading blanks - these will always render as blank
long letters[] = {
  0b000000000000000,  // [0]  ' ' ascii 32 (space)
  0b000000011101000,  // [1]  '!' ascii 33 (exclamation)
  0b000011011000000,  // [2]  '"' ascii 34 (quote)
  0b111010101010101,  // [3]  '#' ascii 35 (hashtag)
  0b000000000000000,  // [4]  '$' ascii 36 (dollar - render as blank)
  0b001010010010010,  // [5]  '%' ascii 37 (percent)
  0b000000000000000,  // [6]  '&' ascii 38 (ampersand - render as blank)
  0b000000101000000,  // [7]  ''' ascii 39 (apostrophe)
  0b111001000000010,  // [8]  '(' ascii 40 (left paren)
  0b111000010001000,  // [9]  ')' ascii 41 (right paren)
  0b010101010100000,  // [10] '*' ascii 42 (asterisk)
  0b111100000100000,  // [11] '+' ascii 43 (plus)
  0b100000000001000,  // [12] ',' ascii 44 (comma)
  0b001100000100000,  // [13] '-' ascii 45 (minus/dash)
  0b000000000001000,  // [14] '.' ascii 46 (period)
  0b001010000010000,  // [15] '/' ascii 47 (slash)
  0b000110101110101,  // [16] '0' ascii 48       //numbers
  0b111000100000100,  // [17] '1' ascii 49
  0b001111110111110,  // [18] '2' ascii 50
  0b001111110001111,  // [19] '3' ascii 51
  0b001111011100011,  // [20] '4' ascii 52
  0b001101111101111,  // [21] '5' ascii 53
  0b001101111111111,  // [22] '6' ascii 54
  0b000111110000011,  // [23] '7' ascii 55
  0b001111111111111,  // [24] '8' ascii 56
  0b001111111100011,  // [25] '9' ascii 57
  0b110000000000000,  // [26] ':' ascii 58 (colon)
  0b110000000001000,  // [27] ';' ascii 59 (semicolon)
  0b110001000100010,  // [28] '<' ascii 60 (less than)
  0b110010001010001,  // [29] '=' ascii 61 (equal)
  0b110100010001000,  // [30] '>' ascii 62 (greater than)
  0b101111110000100,  // [31] '?' ascii 63 (question)
  0b001111111110110,  // [32] '@' ascii 64 (at)
  0b001110101111011,  // [33] 'A' ascii 65/96    //alphas
  0b001100011111111,  // [34] 'B' ascii 66/98
  0b000001111111110,  // [35] 'C' ascii 67/99
  0b000110111111101,  // [36] 'D' ascii 68/100
  0b001001111111110,  // [37] 'E' ascii 69/101
  0b001001111111000,  // [38] 'F' ascii 70/102
  0b001101101111101,  // [39] 'G' ascii 71/103
  0b001111011111011,  // [40] 'H' ascii 72/104
  0b111000100000100,  // [41] 'I' ascii 73/105
  0b000111000010101,  // [42] 'J' ascii 74/106
  0b001011011111011,  // [43] 'K' ascii 75/107
  0b000000011111110,  // [44] 'L' ascii 76/108
  0b010111011111011,  // [45] 'M' ascii 77/109
  0b010100011111011,  // [46] 'N' ascii 78/110
  0b000111111111111,  // [47] 'O' ascii 79/111
  0b001111111111000,  // [48] 'P' ascii 80/112
  0b100110101110111,  // [49] 'Q' ascii 81/113
  0b001010111111011,  // [50] 'R' ascii 82/114
  0b001101111101111,  // [51] 'S' ascii 83/115
  0b111001110000100,  // [52] 'T' ascii 84/116
  0b000111011111111,  // [53] 'U' ascii 85/117
  0b000111011110101,  // [54] 'V' ascii 86/118
  0b100111011111011,  // [55] 'W' ascii 87/119
  0b001011011011011,  // [56] 'X' ascii 88/120
  0b101111011100100,  // [57] 'Y' ascii 89/121
  0b001011110011110,  // [58] 'Z' ascii 90/122
  0b111001100000110,  // [59] '[' ascii 91 (left bracket)
  0b001000001000001,  // [60] '\' ascii 92 (back slash)
  0b111000110001100,  // [61] ']' ascii 93 (right bracket)
  0b000010101000000,  // [62] '^' ascii 94 (carat)
  0b000000000001110,  // [63] '_' ascii 95 (underscore)
  0b000010100000000,  // [64] '`' ascii 96 (tick)
};

//Define Binary Number blocks (3x3) x 4 positions - 36 pixels total. Displayed right-to-left (seconds to hours)
long binaryNums[] = {
  0b000000000000000000000000000000000000,  //[0] 0
  0b000000000000000000000000000111111111,  //[1] 1
  0b000000000000000000111111111000000000,  //[2] 2
  0b000000000000000000111111111111111111,  //[3] 3
  0b000000000111111111000000000000000000,  //[4] 4
  0b000000000111111111000000000111111111,  //[5] 5
  0b000000000111111111111111111000000000,  //[6] 6
  0b000000000111111111111111111111111111,  //[7] 7
  0b111111111000000000000000000000000000,  //[8] 8
  0b111111111000000000000000000111111111,  //[9] 9
};
//Pixel Locations for full digits.  First index is segment position on matrix. (0-3 Clock, 4-7 countdown, 8-11 score)
// Second index is the pixel position within the segment
unsigned int fullnumPixelPos[12][33] = {
  { 226, 225, 176, 175, 174, 173, 172, 180, 221, 230, 271, 280, 321, 330, 371, 372, 373, 374, 375, 326, 325, 276, 275, 274, 273, 272, 328, 323, 278, 228, 223, 178 },  //[0]  Segment Pos 0
  { 232, 219, 182, 169, 168, 167, 166, 186, 215, 236, 265, 286, 315, 336, 365, 366, 367, 368, 369, 332, 319, 282, 269, 268, 267, 266, 334, 317, 284, 234, 217, 184 },  //[1]  Segment Pos 1
  { 240, 211, 190, 161, 160, 159, 158, 194, 207, 244, 257, 294, 307, 344, 357, 358, 359, 360, 361, 340, 311, 290, 261, 260, 259, 258, 342, 309, 292, 242, 209, 192 },  //[2]  Segment Pos 2
  { 246, 205, 196, 155, 154, 153, 152, 200, 201, 250, 251, 300, 301, 350, 351, 352, 353, 354, 355, 346, 305, 296, 255, 254, 253, 252, 348, 303, 298, 248, 203, 198 },  //[3]  Segment Pos 3
  { 176, 175, 126, 125, 124, 123, 122, 130, 171, 180, 221, 230, 271, 280, 321, 322, 323, 324, 325, 276, 275, 226, 225, 224, 223, 222, 278, 273, 228, 178, 173, 128 },  //[4]  Segment Pos 4
  { 182, 169, 132, 119, 118, 117, 116, 136, 165, 186, 215, 236, 265, 286, 315, 316, 317, 318, 319, 282, 269, 232, 219, 218, 217, 216, 284, 267, 234, 184, 167, 134 },  //[5]  Segment Pos 5
  { 190, 161, 140, 111, 110, 109, 108, 144, 157, 194, 207, 244, 257, 294, 307, 308, 309, 310, 311, 290, 261, 240, 211, 210, 209, 208, 292, 259, 242, 192, 159, 142 },  //[6]  Segment Pos 6
  { 196, 155, 146, 105, 104, 103, 102, 150, 151, 200, 201, 250, 251, 300, 301, 302, 303, 304, 305, 296, 255, 246, 205, 204, 203, 202, 298, 253, 248, 198, 153, 148 },  //[7]  Segment Pos 7
  { 125, 76, 75, 26, 27, 28, 29, 71, 80, 121, 130, 171, 180, 221, 230, 229, 228, 227, 226, 225, 176, 175, 126, 127, 128, 129, 223, 178, 173, 123, 78, 73 },            //[8]  Segment Pos 8
  { 119, 82, 69, 32, 33, 34, 35, 65, 86, 115, 136, 165, 186, 215, 236, 235, 234, 233, 232, 219, 182, 169, 132, 133, 134, 135, 217, 184, 167, 117, 84, 67 },            //[9]  Segment Pos 9
  { 111, 90, 61, 40, 41, 42, 43, 57, 94, 107, 144, 157, 194, 207, 244, 243, 242, 241, 240, 211, 190, 161, 140, 141, 142, 143, 209, 192, 159, 109, 92, 59 },            //[10] Segment Pos 10
  { 105, 96, 55, 46, 47, 48, 49, 51, 100, 101, 150, 151, 200, 201, 250, 249, 248, 247, 246, 205, 196, 155, 146, 147, 148, 149, 203, 198, 153, 103, 98, 53 },           //[11] Segment Pos 11

};

//Pixel locations for temp digits. First index is segment position on matrix (0=ones, 1=tens)
// Second index is the pixel position within the segment
unsigned int smallPixelPos[6][13] = {
  { 42, 9, 8, 7, 44, 57, 94, 107, 108, 109, 92, 59, 58 },               //[0] Segment Pos Temp ones
  { 46, 5, 4, 3, 48, 53, 98, 103, 104, 105, 96, 55, 54 },               //[1] Segment Pos Temp tens
  { 36, 15, 14, 13, 38, 63, 88, 113, 114, 115, 86, 65, 64 },            //[2] Segment for Temp C/F symbol
  { 27, 24, 23, 22, 29, 72, 79, 122, 123, 124, 77, 74, 73 },            //[3] Segment for Clock AM/PM symbol
  { 307, 294, 295, 296, 305, 346, 355, 396, 395, 394, 357, 344, 345 },  //[4] Segment for Scoreboard V
  { 321, 280, 281, 282, 319, 332, 369, 382, 381, 380, 371, 330, 331 },  //[5] Segment for Scoreboard H
};

//Pixel locations for dual temp digits. First index is segment position on matrix (int temp: 0=ones, 1=tens ext temp: 3=ones, 4=tens)
// Second index is the pixel position within the segment
unsigned int tempPixelPos[4][13] = {
  { 42, 9, 8, 7, 44, 57, 94, 107, 108, 109, 92, 59, 58 },                //[0] Segment Pos Inside temp ones
  { 46, 5, 4, 3, 48, 53, 98, 103, 104, 105, 96, 55, 54 },                //[1] Segment Pos Inside temp tens
  { 28, 23, 22, 21, 30, 71, 80, 121, 122, 123, 78, 73, 72},              //[2] Segment Pos Outside temp ones
  { 32, 19, 18, 17, 34, 67, 84, 117, 118, 119, 82, 69, 68},              //[3] Segment Pos Outside temp tens 
};

// Pixel locations for small letters.  First index is segment position on matrix (0-5 top row, right to left, 6-11 bottom row, right to left)
// Second index is the pixel position within the segment (0-14)
unsigned int letterPixelPos[18][15] = {
  { 274, 227, 228, 229, 272, 279, 322, 329, 328, 327, 324, 277, 278, 323, 273 },  //[0]  Top row, rightmost
  { 270, 231, 232, 233, 268, 283, 318, 333, 332, 331, 320, 281, 282, 319, 269 },  //[1]  Top row, second from right
  { 266, 235, 236, 237, 264, 287, 314, 337, 336, 335, 316, 285, 286, 315, 265 },  //[2]  Top row, third from right
  { 262, 239, 240, 241, 260, 291, 310, 341, 340, 339, 312, 289, 290, 311, 261 },  //[3]  Top row, fourth from right
  { 258, 243, 244, 245, 256, 295, 306, 345, 344, 343, 308, 293, 294, 307, 257 },  //[4]  Top row, fifth from right
  { 254, 247, 248, 249, 252, 299, 302, 349, 348, 347, 304, 297, 298, 303, 253 },  //[5]  Top row, sixth from right
  { 77, 74, 73, 72, 79, 122, 129, 172, 173, 174, 127, 124, 123, 128, 78 },        //[6]  Bottom row, rightmost
  { 81, 70, 69, 68, 83, 118, 133, 168, 169, 170, 131, 120, 119, 132, 82 },        //[7]  Bottom row, second from right
  { 85, 66, 65, 64, 87, 114, 137, 164, 165, 166, 135, 116, 115, 136, 86 },        //[8]  Botton row, third from right
  { 89, 62, 61, 60, 91, 110, 141, 160, 161, 162, 139, 112, 111, 140, 90 },        //[9]  Bottom row, fourth from right
  { 93, 58, 57, 56, 95, 106, 145, 156, 157, 158, 143, 108, 107, 144, 94 },        //[10] Bottom row, fifth from right
  { 97, 54, 53, 52, 99, 102, 149, 152, 153, 154, 147, 104, 103, 148, 98 },        //[11] Bottom row, sixth from right
  { 325, 276, 277, 278, 323, 328, 373, 378, 377, 376, 375, 326, 327, 374, 324 },  //[12] Scoreboard team right, rightmost
  { 321, 280, 281, 282, 319, 332, 369, 382, 381, 380, 371, 330, 331, 370, 320 },  //[13] Scoreboard team right, middle
  { 317, 284, 285, 286, 315, 336, 365, 386, 385, 384, 367, 334, 335, 366, 316 },  //[14] Scoreboard team right, leftmost
  { 311, 290, 291, 292, 309, 342, 359, 392, 391, 390, 361, 340, 341, 360, 310 },  //[15] Socreboard team left, rightmost
  { 307, 294, 295, 296, 305, 346, 355, 396, 395, 394, 357, 344, 345, 356, 306 },  //[16] Scoreboard team left, middle
  { 303, 298, 299, 300, 301, 350, 351, 400, 399, 398, 353, 348, 349, 352, 302 },  //[17] Scoreboard team left, leftmost
};

// Pixel Positions for Binary Clock Mode.  First index is segment position on matrix (0-5 left to right, seconds to hours - 4 "blocks" in each column)
// Second index is the pixel position within the segment (1-36)
unsigned int binaryPixelPos[6][36] = {
  { 22, 23, 24, 27, 28, 29, 72, 73, 74, 122, 123, 124, 127, 128, 129, 172, 173, 174, 222, 223, 224, 227, 228, 229, 272, 273, 274, 322, 323, 324, 327, 328, 329, 372, 373, 374 }, //[0] - Seconds (ones)
  { 18, 19, 20, 31, 32, 33, 68, 69, 70, 118, 119, 120, 131, 132, 133, 168, 169, 170, 218, 219, 220, 231, 232, 233, 268, 269, 270, 318, 319, 320, 331, 332, 333, 368, 369, 370 }, //[1] - Seconds (tens)
  { 14, 15, 16, 35, 36, 37, 64, 65, 66, 114, 115, 116, 135, 136, 137, 164, 165, 166, 214, 215, 216, 235, 236, 237, 264, 265, 266, 314, 315, 316, 335, 336, 337, 364, 365, 366 }, //[2] - Minutes (ones)
  { 10, 11, 12, 39, 40, 41, 60, 61, 62, 110, 111, 112, 139, 140, 141, 160, 161, 162, 210, 211, 212, 239, 240, 241, 260, 261, 262, 310, 311, 312, 339, 340, 341, 360, 361, 362 }, //[3] - Minutes (tens)
  { 6, 7, 8, 43, 44, 45, 56, 57, 58, 106, 107, 108, 143, 144, 145, 156, 157, 158, 206, 207, 208, 243, 244, 245, 256, 257, 258, 306, 307, 308, 343, 344, 345, 356, 357, 358 }, //[4] - Hours (ones)
  { 2, 3, 4, 47, 48, 49, 52, 53, 54, 102, 103, 104, 147, 148, 149, 152, 153, 154, 202, 203, 204, 247, 248, 249, 252, 253, 254, 302, 303, 304, 347, 348, 349, 352, 353, 354 }, //[5] - Hours (tens)
};
//===========================
// Populate Color Arrays
//===========================
void defineColors() {
  //  Increase array sizes (ColorCodes[] and WebColors[]) in declarations above if adding new
  //  Color must be defined as a CRGB::Named Color or as a CRGB RGB value: CRGB(r, g, b);
  //  Each ColorCode[] value must have a matching WebColor[] value with the color plain text description

  ColorCodes[0] = CRGB::Blue;
  ColorCodes[1] = CRGB::Green;
  ColorCodes[2] = CRGB::Orange;
  ColorCodes[3] = CRGB::Red;
  ColorCodes[4] = CRGB::Yellow;
  ColorCodes[5] = CRGB::White;
  ColorCodes[6] = CRGB::Black;
  ColorCodes[7] = CRGB::Aqua;
  ColorCodes[8] = CRGB::CadetBlue;
  ColorCodes[9] = CRGB::Coral;
  ColorCodes[10] = CRGB::Crimson;
  ColorCodes[11] = CRGB::Cyan;
  ColorCodes[12] = CRGB::Fuchsia;
  ColorCodes[13] = CRGB::Gold;
  ColorCodes[14] = CRGB::Lavender;
  ColorCodes[15] = CRGB::LightBlue;
  ColorCodes[16] = CRGB::Lime;
  ColorCodes[17] = CRGB::Magenta;
  ColorCodes[18] = CRGB::Maroon;
  ColorCodes[19] = CRGB::Navy;
  ColorCodes[20] = CRGB::Pink;
  ColorCodes[21] = CRGB::Purple;
  ColorCodes[22] = CRGB::Salmon;
  ColorCodes[23] = CRGB::Teal;
  ColorCodes[24] = CRGB::Turquoise;

  WebColors[0] = "Blue";
  WebColors[1] = "Green";
  WebColors[2] = "Orange";
  WebColors[3] = "Red";
  WebColors[4] = "Yellow";
  WebColors[5] = "White";
  WebColors[6] = "Black (off)";
  WebColors[7] = "Aqua";
  WebColors[8] = "Cadet Blue";
  WebColors[9] = "Coral";
  WebColors[10] = "Crimson";
  WebColors[11] = "Cyan";
  WebColors[12] = "Fuchsia";
  WebColors[13] = "Gold";
  WebColors[14] = "Lavender";
  WebColors[15] = "Light Blue";
  WebColors[16] = "Lime";
  WebColors[17] = "Magenta";
  WebColors[18] = "Maroon";
  WebColors[19] = "Navy";
  WebColors[20] = "Pink";
  WebColors[21] = "Purple";
  WebColors[22] = "Salmon";
  WebColors[23] = "Teal";
  WebColors[24] = "Turquoise";
}

//=======================================
// Read config file from flash (LittleFS)
//=======================================
void readConfigFile() {

  if (LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)) {
    #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
        Serial.println("mounted file system");
    #endif
    if (LittleFS.exists("/config.json")) {
      //file exists, reading and loading
      #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
            Serial.println("reading config file");
      #endif
      File configFile = LittleFS.open("/config.json", "r");
      if (configFile) {
        #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
                Serial.println("opened config file");
        #endif
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonDocument json(1024);
        auto deserializeError = deserializeJson(json, buf.get());
        serializeJson(json, Serial);
        if (!deserializeError) {

          #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
                    Serial.println("\nparsed json");
          #endif
          // Read values here from LittleFS (use defaults for all values in case they don't exist to avoid potential boot loop)
          //DON'T NEED TO STORE OR RECALL WIFI INFO - Written to flash automatically by library when successful connection.
          deviceName = json["device_name"] | "MatrixClock";
          defaultClockMode = json["clock_mode"] | 0;
          int binary = json["binary_clock"] | 0;
          if (binary == 1) {
            binaryClock = true;
          } else {
            binaryClock = false;
          }
          milliamps = json["max_milliamps"] | 5000;
          brightness = json["led_brightness"] | 64;
          numFont = json["num_font"] | 1;
          owmKey = json["owm_key"] | "NA";                        // OpenWeatherMap API Key
          owmLat = json["owm_lat"] | "39.8083";                   // Latitude for OWM (stored as string)
          owmLong = json["owm_long"] | "-98.555";                 // Longitude for OWM (stored as string)
          int useApiExt = json["temp_ext_api"] | 0;               // Use API for external temperature instead of OWM
          if (useApiExt == 1) {
            tempExtUseApi = true;
          } else {
            tempExtUseApi = false;
          }
          int useApiInt = json["temp_int_api"] | 0;               // Use API for internal temperature instead of onboard internal temp sensor
          if (useApiInt == 1) {
            tempIntUseApi = true;
          } else {
            tempIntUseApi = false;
          }
          temperatureSymbol = json["temp_symbol"] | 13;           // 12 celcius, 13 fahrenheit
          temperatureSource = json["temp_source"] | 0;            // 0 dual, 1 external only, 2 internal only
          temperatureCorrection = json["temp_correction"] | 0.00; // Correction (in degrees) value to add to internal temp sensor
          tempUpdatePeriod = json["temp_upd_int"] | 5;            // How often, in minutes, to get a new internal temp reading from sensor (minimum = 1)
          tempUpdatePeriodExt = json["temp_upd_ext"] | 15;        // How often, in minutes, to poll for new external temp value (minimum = 10... otherwise API may exceed daily limit)
          hourFormat = json["hour_format"] | 12;                  // 12 or 24
          timeZone = json["time_zone"] | TIMEZONE;                // Custom tz string to use instead of offsets
          int custom = json["use_custom_tz"] | 0;
          if (custom == 1) {                                      // 1 = use custom offsets instead of timezone string (no auto-DST adjustments)
            useCustomOffsets = true;
          } else {
            useCustomOffsets = false;
          }
          gmtOffsetHours = json["gmt_offset"] | -5;               // -5 = Eastern Standard Time (DST will be handled via NTP server)
          dstOffsetHours = json["dst_offset"] | 1;                // 1 = 'spring forward'. Set to 0 if not observing Daylight Savings time.
          int sync = json["auto_sync"] | 1;                       // Auto-sync to NTP server (1=true 0=false)
          if (sync == 1) {
            autoSync = true;
          } else {
            autoSync = false;
          }
          ntpServer = json["ntp_server"] | "us.pool.ntp.org";     //Default ntp server to use for time sync
          autoSyncInterval =  json["sync_interval"] | 60;         //How often, in minutes, to sync to server: min 15 - 1,440 max (24 hr)
          scoreboardTeamLeft = json["score_team_left"] | "&V";    
          scoreboardTeamRight = json["score_team_right"] | "&H";  
          textEffect = json["text_effect"] | 0;
          textEffectSpeed = json["text_speed"] | 5;
          //textFull = json["default_text"] | "";
          textTop = json["text_top"] | "HELLO";
          textBottom = json["text_bottom"] | "WORLD";
          defaultCountdownMin = json["countdown_min"] | 0;
          defaultCountdownSec = json["countdown_sec"] | 0;
          int buzzer = json["use_buzzer"] | 1;
          if (buzzer == 1) {
            useBuzzer = true;
          } else {
            useBuzzer = false;
          }
          wledAddress = json["wled_address"] | "0.0.0.0";
          wledMaxPreset = json["wled_max_preset"] | 0;
          webColorClock = json["clock_color"] | 0;
          webColorTemperatureInt = json["temp_color_int"] | 3;
          webColorTemperatureExt = json["temp_color_ext"] | 0;
          webColorCountdown = json["count_color_active"] | 1;
          webColorCountdownPaused = json["count_color_paused"] | 2;
          webColorCountdownFinalMin = json["count_color_final_min"] | 3;
          webColorScoreboardLeft = json["score_color_left"] | 3;
          webColorScoreboardRight = json["score_color_right"] | 1;
          webColorTextTop = json["text_color_top"] | 3;
          webColorTextBottom = json["text_color_bottom"] | 1;

          clockColor = ColorCodes[webColorClock];
          temperatureColorInt = ColorCodes[webColorTemperatureInt];
          temperatureColorExt = ColorCodes[webColorTemperatureExt];
          countdownColor = ColorCodes[webColorCountdown];
          countdownColorPaused = ColorCodes[webColorCountdownPaused];
          countdownColorFinalMin = ColorCodes[webColorCountdownFinalMin];
          scoreboardColorLeft = ColorCodes[webColorScoreboardLeft];
          scoreboardColorRight = ColorCodes[webColorScoreboardRight];
          textColorTop = ColorCodes[webColorTextTop];
          textColorBottom = ColorCodes[webColorTextBottom];

          //=== Set or calculate other globals =====
          wifiHostName = deviceName;
          otaHostName = deviceName + "_OTA";
          clockMode = defaultClockMode;
          //Assure milliamps between 5000 - 25000
          if (milliamps > 25000) {
            milliamps = 25000;
          } else if (milliamps < 5000) {
            milliamps = 5000;
          }

          //Default countdown minutes
          if (defaultCountdownMin > 59) defaultCountdownMin = 59;
          if (defaultCountdownSec > 59) defaultCountdownSec = 59;

          //Convert refresh periods to seconds
          tempUpdatePeriod = (tempUpdatePeriod * 60);
          tempUpdatePeriodExt = (tempUpdatePeriodExt * 60); 
          //Do not allow external refresh more than once per 10 minutes
          if (tempUpdatePeriodExt < 600) tempUpdatePeriodExt = 600;

          initCountdownMillis = ((defaultCountdownMin * 60) + defaultCountdownSec) * 1000;
          countdownMilliSeconds = initCountdownMillis;
          remCountdownMillis = initCountdownMillis;

          //Default Team Names
          if (scoreboardTeamLeft.length() > 3) {
            scoreboardTeamLeft = scoreboardTeamLeft.substring(0, 4);
          } else if (scoreboardTeamLeft.length() < 1) {
            scoreboardTeamLeft = "&V";
          }
          if (scoreboardTeamRight.length() > 3) {
            scoreboardTeamRight = scoreboardTeamRight.substring(0, 4);
          } else if (scoreboardTeamRight.length() < 1) {
            scoreboardTeamRight = "&H";
          }

          //WLED Interface
          if (wledAddress == "0.0.0.0") {
            useWLED = false;
           } else {
            useWLED = true;
          }

          defaultClockMode = clockMode;
           
        } else {
         #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
            Serial.println("failed to load json config");
         #endif
          onboarding = true;
        }
        configFile.close();
      } else {
        onboarding = true;
      }
    } else {
      // No config file found - set to onboarding
      onboarding = true;
    }

    LittleFS.end();  //End - need to prevent issue with OTA updates
  } else {
    //could not mount filesystem
    #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
        Serial.println("failed to mount FS");
        Serial.println("LittleFS Formatted. Restarting ESP.");
    #endif
    onboarding = true;
  }
}

//======================================
// Write config file to flash (LittleFS)
//======================================
void writeConfigFile(bool restart_ESP) {
  //Write settings to LittleFS (reboot to save)

  if (LittleFS.begin()) {
    DynamicJsonDocument doc(1024);
    doc.clear();
    //Add any values to save to JSON document
    doc["device_name"] = deviceName;
    doc["clock_mode"] = defaultClockMode;
    if (binaryClock) {
      doc["binary_clock"] = 1;
    } else {
      doc["binary_clock"] = 0;
    }
    doc["max_milliamps"] = milliamps;
    doc["led_brightness"] = brightness;
    doc["num_font"] = numFont;
    doc["owm_key"] = owmKey;
    doc["owm_lat"] = owmLat;
    doc["owm_long"] = owmLong;
    if (tempExtUseApi) {
      doc["temp_ext_api"]  = 1;
    } else {
      doc["temp_ext_api"]  = 0;
    }
    if (tempIntUseApi) {
      doc["temp_int_api"] = 1;
    } else {
      doc["temp_int_api"] = 0;
    }
    doc["temp_symbol"] = temperatureSymbol;
    doc["temp_source"] = temperatureSource;
    doc["temp_correction"] = temperatureCorrection;
    doc["temp_upd_int"] = (tempUpdatePeriod / 60);    //convert back to minutes
    doc["temp_upd_ext"] = (tempUpdatePeriodExt / 60); //convert back to minutes
    doc["hour_format"] = hourFormat;
    doc["time_zone"] = timeZone;
    doc["gmt_offset"] = gmtOffsetHours;
    doc["dst_offset"] = dstOffsetHours;
    if (useCustomOffsets) {
      doc["use_custom_tz"] = 1;
    } else {
      doc["use_custom_tz"] = 0;
    }
    if (autoSync) {
      doc["auto_sync"] = 1;
    } else {
      doc["auto_sync"] = 0;
    }
    doc["ntp_server"] = ntpServer;
    doc["sync_interval"] = autoSyncInterval;
    doc["score_team_left"] = scoreboardTeamLeft;
    doc["score_team_right"] = scoreboardTeamRight;
    doc["text_effect"] = textEffect;
    doc["text_speed"] = textEffectSpeed;
    doc["text_top"] = textTop;
    doc["text_bottom"] = textBottom;
    doc["countdown_min"] = defaultCountdownMin;
    doc["countdown_sec"] = defaultCountdownSec;
    if (useBuzzer) {
      doc["use_buzzer"] = 1;
    } else {
      doc["use_buzzer"] = 0;
    }
    doc["wled_address"] = wledAddress;
    doc["wled_max_preset"] = wledMaxPreset;
    doc["clock_color"] = webColorClock;
    doc["temp_color_int"] = webColorTemperatureInt;
    doc["temp_color_ext"] = webColorTemperatureExt;
    doc["count_color_active"] = webColorCountdown;
    doc["count_color_paused"] = webColorCountdownPaused;
    doc["count_color_final_min"] = webColorCountdownFinalMin;
    doc["score_color_left"] = webColorScoreboardLeft;
    doc["score_color_right"] = webColorScoreboardRight;
    doc["text_color_top"] = webColorTextTop;
    doc["text_color_bottom"] = webColorTextBottom;

    File configFile = LittleFS.open("/config.json", "w");
    if (!configFile) {
      #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
        Serial.println("failed to open config file for writing");
      #endif
      configFile.close();
      return;
    } else {
      #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
        serializeJson(doc, Serial);
      #endif
      serializeJson(doc, configFile);
      #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
        Serial.println("Settings saved.");
      #endif
      configFile.close();
      LittleFS.end();
      if (restart_ESP) {
        ESP.restart();
      }
    }
  } else {
//could not mount filesystem
    #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
      Serial.println("failed to mount FS");
    #endif
  }
}

/* =========================
    PRIMARY WEB PAGES
   =========================*/
void webMainPage() {
  String mainPage = "<html><head>";
  modeCallingPage = "";
  mainPage += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";  //make page responsive
  if (onboarding) {
    //Show portal/onboarding page
    mainPage += "<title>VAR_APP_NAME Onboarding</title>\
    <style>\
      body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000000; }\
    </style>\
    </head>\
    <body>";
    mainPage += "<h1>VAR_APP_NAME Onboarding</h1>";
    mainPage += "Please enter your WiFi information below. These are CASE-SENSITIVE and limited to 64 characters each.<br><br>";
    mainPage += "<form method=\"post\" enctype=\"application/x-www-form-urlencoded\" action=\"/onboard\">\
      <table>\
      <tr>\
      <td><label for=\"ssid\">SSID:</label></td>\
      <td><input type=\"text\" name=\"ssid\" maxlength=\"64\" value=\"";
    mainPage += wifiSSID;
    mainPage += "\"></td></tr>\
        <tr>\
        <td><label for=\"wifipw\">Password:</label></td>\
        <td><input type=\"password\" name=\"wifipw\" maxlength=\"64\" value=\"";
    mainPage += wifiPW;
    mainPage += "\"></td></tr></table><br>";
    mainPage += "<b>Device Name: </b>Please give this device a unique name from all other devices on your network, including other installs of VAR_APP_NAME. ";
    mainPage += "This will be used to set the WiFi and OTA hostnames.<br><br>";
    mainPage += "16 alphanumeric (a-z, A-Z, 0-9) characters max, no spaces:";
    mainPage += "<table>\
        <tr>\
        <td><label for=\"devicename\">Device Name:</label></td>\
        <td><input type=\"text\" name=\"devicename\" maxlength=\"16\" value=\"";
    mainPage += deviceName;
    mainPage += "\"></td></tr>";
    mainPage += "</table><br><br>";
    mainPage += "<b>Max Milliamps: </b>Enter the max current the LEDs are allowed to draw.  This should be about 80% of the rated peak max of the power supply. ";
    mainPage += "Valid values are 5000 to 25000.  See documentation for more info.<br><br>";
    mainPage += "<table>\
        <tr>\
        <td><labelfor=\"maxmilliamps\">Max Milliamps:</label></td>\
        <td><input type=\"number\" name=\"maxmilliamps\" min=\"5000\" max=\"25000\" step=\"1\" value=\"";
    mainPage += String(milliamps);
    mainPage += "\"></td></tr>";
    mainPage += "</table><br><br>";
    mainPage += "<input type=\"submit\" value=\"Submit\">";
    mainPage += "</form>";

  } else {
    //Normal Settings Page
    //Get current WLED state if used
    if (useWLED) {
      if (wledOn()) {
        wledStateOn = true;
      } else {
        wledStateOn = false;
      }
    }
    mainPage += "<title>VAR_APP_NAME Main Page</title>\
    <style>\
      body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #0000ff; }\
    </style>\
    </head>\
    <body>";
    mainPage += "<H1>VAR_APP_NAME Settings and Options</H1>";
    mainPage += "Firmware Version: VAR_CURRENT_VER<br><br>";
    mainPage += "<table border=\"1\" >";
    mainPage += "<tr><td>Device Name:</td><td>" + deviceName + "</td</tr>";
    mainPage += "<tr><td>WiFi Network:</td><td>" + WiFi.SSID() + "</td</tr>";
    mainPage += "<tr><td>MAC Address:</td><td>" + strMacAddr + "</td</tr>";
    mainPage += "<tr><td>IP Address:</td><td>" + baseIPAddress + "</td</tr>";
    mainPage += "<tr><td>Max Milliamps:</td><td>" + String(milliamps) + "</td></tr>";
    mainPage += "</table><br>";
    //Standard mode button header
    mainPage += "<H2>Mode Display & Control</H2>";
    mainPage += "<table><tr><td>";
    mainPage += "<button type=\"button\" id=\"btnback\" style=\"font-size: 16px; border-radius: 8px; width: 140px; height: 40px; "; 
    
    if (ledsOn) {
      mainPage += "background-color: #63de3e;\" onclick=\"location.href = './toggleleds';\">";
      mainPage += "Power: ON";
    } else {
      mainPage += "background-color: #c9535a;\" onclick=\"location.href = './toggleleds';\">";
      mainPage += "Power: OFF";
    }
    mainPage += "</button></td>";

    if (useWLED) {
      mainPage += "<td>";
      if (wledStateOn) {
        mainPage += "<a href=\"./togglewled\"\
          style=\"width:90%;display:block;text-align:center;\
          padding:.5em;background-color:#0073e2;color:#fff;border-radius:8px;outline: 2px solid black; box-shadow:0 4px 6px rgba(50,50,93,.11), 0 1px 3px rgba(0,0,0,.08);text-decoration:none;\">\
          WLED: On</a>";
      } else {
        mainPage += "<a href=\"./togglewled\" target=\"_blank\"\
          style=\"width:90%;display:block;text-align:center;\
          padding:.5em;background-color:#d6c9d6;color:#000;border-radius:8px;outline: 2px solid black; box-shadow:0 4px 6px rgba(50,50,93,.11), 0 1px 3px rgba(0,0,0,.08);text-decoration:none;\">\
          WLED: Off</a>";
      }
      mainPage += "</td>";
    }
    mainPage += "<td>&nbsp;</td><td>&nbsp;</td>";
    mainPage += "</tr><tr><td>&nbsp;</td></tr><tr>";

    mainPage += "<td><button id=\"btnclock\" style=\"font-size: 20px; ";
    if (clockMode == 0) {
      mainPage += "background-color: #95f595; font-weight: bold; ";
    } else {
      mainPage += "background-color: #d6c9d6; ";
    }
    mainPage += "text-align: center; border-radius: 8px; width: 140px; height: 60px;\" onclick=\"location.href = './clock';\">Clock</button></td>";
    mainPage += "<td><button id=\"btncount\" style=\"font-size: 20px; ";
    if (clockMode == 1) {
      mainPage += "background-color: #87d9e8; font-weight: bold; ";
    } else {
      mainPage += "background-color: #d6c9d6; ";
    }
    mainPage += "text-align: center; border-radius: 8px; width: 140px; height: 60px;\" onclick=\"location.href = './countdown';\">Countdown</button></td>";
    mainPage += "<td><button id=\"btnscore\" style=\"font-size: 20px; ";
    if (clockMode == 2) {
      mainPage += "background-color: #ebd588; font-weight: bold; ";
    } else {
      mainPage += "background-color: #d6c9d6; ";
    }
    mainPage += "text-align: center; border-radius: 8px; width: 140px; height: 60px;\" onclick=\"location.href = './scoreboard';\">Scoreboard</button></td>";
    mainPage += "<td><button id=\"btntext\" style=\"font-size: 20px; ";
    if (clockMode == 3) {
      mainPage += "background-color: #eb8ae6; font-weight: bold; ";
    } else {
      mainPage += "background-color: #d6c9d6; ";
    }
    mainPage += "text-align: center; border-radius: 8px; width: 140px; height: 60px;\" onclick=\"location.href = './text';\">Text Display</button></td>";
    mainPage += "</tr><tr>";
    mainPage += "<td style=\"text-align: center\"><a href= \"http://" + baseIPAddress + "/clockedit\">Manage</a></td>";
    mainPage += "<td style=\"text-align: center\"><a href= \"./countdownedit\">Manage</a></td>";
    mainPage += "<td style=\"text-align: center\"><a href= \"./scoreedit\">Manage</a></td>";
    mainPage += "<td style=\"text-align: center\"><a href= \"./textedit\">Manage</a></td>";
    mainPage += "</tr></table>";
    //Rest of page
    mainPage += "<H2>Settings and Options</H2>";
    mainPage += "Changes made below will be used <b><i>until the controller is restarted</i></b>, unless the box to save the settings as new boot defaults is checked. \
      To test settings, leave the box unchecked and click 'Update'. Once you have settings you'd like to keep, check the box and click 'Update' to write the settings as the new boot defaults. \
      If you want to change wifi settings or the device name, you must use the 'Reset All' command.\
      <form method=\"post\" enctype=\"application/x-www-form-urlencoded\" action=\"/applysettings\"><br>";

    mainPage += "<b><u>Color Defaults</u></b><br><br>\
      <table border=\"0\">\
      <tr>\
      <td><label for=\"clockcolor\">Clock/Time:</label></td>\
      <td><select name=\"clockcolor\">";
    for (byte i = 0; i < NUMELEMENTS(WebColors); i++) {
      mainPage += "<option value = \"" + String(i) + "\"";
      if (i == webColorClock) {
        mainPage += " selected";
      }
      mainPage += ">" + WebColors[i] + "</option>";
    }
    mainPage += "</td><td>&nbsp;(Binary Clock Hours)</td>\
      </tr><tr>\
      <td><label for=\"tempcolorint\">Internal Temperature:</label></td>\
      <td><select name=\"tempcolorint\">";
    for (byte i = 0; i < NUMELEMENTS(WebColors); i++) {
      mainPage += "<option value = \"" + String(i) + "\"";
      if (i == webColorTemperatureInt) {
        mainPage += " selected";
      }
      mainPage += ">" + WebColors[i] + "</option>";
    }
    mainPage += "</td><td>&nbsp;(Binary Clock Minutes)</td>\
      </tr><tr>\
      <td><label for=\"tempcolorext\">External Temperature:</label></td>\
      <td><select name=\"tempcolorext\">";
    for (byte i = 0; i < NUMELEMENTS(WebColors); i++) {
      mainPage += "<option value = \"" + String(i) + "\"";
      if (i == webColorTemperatureExt) {
        mainPage += " selected";
      }
      mainPage += ">" + WebColors[i] + "</option>";
    }
    mainPage += "</td><td>&nbsp;(Binary Clock Seconds)</td>\
      </tr><tr>\
      <td><label for=\"countdownactive\">Countdown Active:</label></td>\
      <td><select name=\"countdownactive\">";
    for (byte i = 0; i < NUMELEMENTS(WebColors); i++) {
      mainPage += "<option value = \"" + String(i) + "\"";
      if (i == webColorCountdown) {
        mainPage += " selected";
      }
      mainPage += ">" + WebColors[i] + "</option>";
    }
    mainPage += "</td></tr>\
      <tr>\
      <td><label for=\"countdownpaused\">Countdown Paused:</label></td>\
      <td><select name=\"countdownpaused\">";
    for (byte i = 0; i < NUMELEMENTS(WebColors); i++) {
      mainPage += "<option value = \"" + String(i) + "\"";
      if (i == webColorCountdownPaused) {
        mainPage += " selected";
      }
      mainPage += ">" + WebColors[i] + "</option>";
    }
    mainPage += "</td></tr>\
      <tr>\
      <td><label for=\"countdownfinalmin\">Countdown Final Min:</label></td>\
      <td><select name=\"countdownfinalmin\">";
    for (byte i = 0; i < NUMELEMENTS(WebColors); i++) {
      mainPage += "<option value = \"" + String(i) + "\"";
      if (i == webColorCountdownFinalMin) {
        mainPage += " selected";
      }
      mainPage += ">" + WebColors[i] + "</option>";
    }
    mainPage += "</td></tr>\
      <tr>\
      <td><label for=\"scorecolorleft\">Scoreboard Left Score:</label></td>\
      <td><select name=\"scorecolorleft\">";
    for (byte i = 0; i < NUMELEMENTS(WebColors); i++) {
      mainPage += "<option value = \"" + String(i) + "\"";
      if (i == webColorScoreboardLeft) {
        mainPage += " selected";
      }
      mainPage += ">" + WebColors[i] + "</option>";
    }
    mainPage += "</td></tr>\
      <tr>\
      <td><label for=\"scorecolorright\">Scoreboard Right Score:</label></td>\
      <td><select name=\"scorecolorright\">";
    for (byte i = 0; i < NUMELEMENTS(WebColors); i++) {
      mainPage += "<option value = \"" + String(i) + "\"";
      if (i == webColorScoreboardRight) {
        mainPage += " selected";
      }
      mainPage += ">" + WebColors[i] + "</option>";
    }
    mainPage += "</td></tr>\
      <tr>\
      <td><label for=\"textcolortop\">Text Top Row:</label></td>\
      <td><select name=\"textcolortop\">";
    for (byte i = 0; i < NUMELEMENTS(WebColors); i++) {
      mainPage += "<option value = \"" + String(i) + "\"";
      if (i == webColorTextTop) {
        mainPage += " selected";
      }
      mainPage += ">" + WebColors[i] + "</option>";
    }
    mainPage += "</td></tr>\
      <tr>\
      <td><label for=\"textcolorbottom\">Text Bottom Row:</label></td>\
      <td><select name=\"textcolorbottom\">";
    for (byte i = 0; i < NUMELEMENTS(WebColors); i++) {
      mainPage += "<option value = \"" + String(i) + "\"";
      if (i == webColorTextBottom) {
        mainPage += " selected";
      }
      mainPage += ">" + WebColors[i] + "</option>";
    }
    mainPage += "</td></tr></table><br>";

    mainPage += "<b><u>Misc. Global Settings</u></b> - Affects all modes<br><br>\
      <table border=\"0\">\
      <tr>\
      <td style=\"vertical-align:top\">Default Starting Mode:</td>\
      <td>\
      <input type=\"radio\" id=\"clock\" name=\"clockmode\" value=\"0\"";
    if (defaultClockMode == 0) mainPage += " checked";
    mainPage += ">\
      <label for=\"clock\">Standard Clock & Temperature</label><br>\
      <input type=\"radio\" id=\"count\" name=\"clockmode\" value=\"1\"";
    if (defaultClockMode == 1) mainPage += " checked";
    mainPage += ">\
      <label for=\"count\">Countdown Timer</label><br>\
      <input type=\"radio\" id=\"score\" name=\"clockmode\" value=\"2\"";
    if (defaultClockMode == 2) mainPage += " checked";
    mainPage += ">\
      <label for=\"score\">Scoreboard</label><br>\
      <input type=\"radio\" id=\"text\" name=\"clockmode\" value=\"3\"";
    if (defaultClockMode == 2) mainPage += " checked";
    mainPage += ">\
      <label for=\"text\">Text Display</label>\
      </td>\
      </tr></table><br>";

    mainPage += "<table border=\"0\">\
      <tr>\
      <td style=\"vertical-align:top\"><label for=\"ledbrightness\">LED Brightness (5-255):</label></td>\
      <td><input type=\"number\" style=\"width:4em\" min=\"0\" max=\"255\" step=\"1\" name=\"ledbrightness\" value=\"";
    mainPage += String(brightness);
    mainPage += "\"><br>&nbsp;</td>\
      </tr><tr>\
      <td style=\"vertical-align:top\">Large Number Font:</td>\
      <td>\
      <input type=\"radio\" id=\"sevenseg\" name=\"numfont\" value=\"0\"";
    if (numFont == 0) mainPage += " checked";
    mainPage += ">\
      <label for=\"sevenseg\">Seven-Segment</label><br>\
      <input type=\"radio\" id=\"modern\" name=\"numfont\" value=\"1\"";
    if (numFont == 1) mainPage += " checked";
    mainPage += ">\
      <label for=\"modern\">Modern</label><br>\
      <input type=\"radio\" id=\"hybrid\" name=\"numfont\" value=\"2\"";
    if (numFont == 2) mainPage += " checked";
    mainPage += ">\
      <label for=\"hybrid\">Hybrid</label>\
      </td>\
      </tr></table><br>";

    mainPage += "<b><u>Clock & Time Default Settings</u></b><br><br>";
    mainPage += "<table border=\"0\">\
      <tr><td>Show as Binary Clock:&nbsp;</td>\
      <td colspan=\"2\"><input type=\"checkbox\" name=\"binaryclock\" value=\"binary\"";
    if (binaryClock) {
      mainPage += " checked";
    }  
    mainPage += ">\
      &nbsp;(24-hour time with no temperatures)</td>\
      </tr>\
      </table><table border=\"0\"><tr>\
      <td>Standard Hour Display:</td>\
      <td>\
      <input type=\"radio\" id=\"12hr\" name=\"hourformat\" value=\"12\"";
    if (hourFormat == 12) mainPage += " checked";
    mainPage += ">\
      <label for=\"12hr\">12-hour</label>\
      </td><td>\
      <input type=\"radio\" id=\"24hr\" name=\"hourformat\" value=\"24\"";
    if (hourFormat == 24) mainPage += " checked";
    mainPage += ">\
      <label for=\"24hr\">24-hour</label>\
      </td></tr></table><br>";

    mainPage += "<u>Time Sync Settings</u> - **Changing any of these values require that you save defaults and reboot.\
      <table border=\"0\">\
      <tr><td>**Auto-Sync Time to NTP Server:</td>\
      <td><input type=\"checkbox\" id=\"autosync\" name=\"autosync\" value=\"autosync\"";
    if (autoSync) mainPage += " checked";
    mainPage += "></td>\
      </tr></table>\
      <table border=\"0\"><tr>\
      <td><label for=\"ntpserver\">NTP Server URL:</label></td>\
      <td colspan=\"2\"><input type=\"text\" name=\"ntpserver\" maxlength=\"50\" style=\"width: 15em\" value=\"";
    mainPage += ntpServer;
    mainPage += "\">&nbsp;\
      <a href=\"https://www.ntppool.org\" target=\"_blank\">Lookup...</a></td>\
      </tr><tr>\
      <td><label for=\"timezone\">Time Zone (posix):</label></td>\
      <td colspan=\"2\"><input type=\"text\" name=\"timezone\" maxlength=\"50\" style=\"width: 15em\" value=\"";
    mainPage += timeZone;
    mainPage += "\">&nbsp;\
      <a href=\"https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv\" target=\"_blank\">Lookup...</a></td>\
      </tr><tr>\
      <td><label for=\"syncinterval\">Sync Interval (in minutes):</label></td>\
      <td><input type=\"number\" name=\"syncinterval\" min=\"15\" max=\"1440\" step=\"15\" style=\"width: 4em;\" value=\"";
    mainPage += String(autoSyncInterval);
    mainPage += "\"";
    if (!autoSync) {
      mainPage += " disabled></td>\
        <td>&nbsp;(requires Auto-Sync)</td>";
    } else {
      mainPage += "></td>\
        <td>&nbsp;15 to 1440 minutes (1440 = once per day)</td>";
    }
    mainPage += "</tr></table><br>\
      <u><i>You can specify custom offsets instead of using the time zone string above</i></u>\
      <table border=\"0\"><tr>\
      <td>**Use Custom Offsets:</td>\
      <td><input type=\"checkbox\" id=\"useoffsets\" name=\"useoffsets\" value=\"useoffsets\"";
    if (useCustomOffsets) mainPage += " checked";
    mainPage += "></td>\
      </tr><tr>\
      <td><label for=\"gmtoffset\">GMT Offset Hours:</label></td>\
      <td><input type=\"number\" style=\"width:3em\" min=\"-12\" max=\"14\" step=\"1\" name=\"gmtoffset\" value=\"";
    mainPage += String(gmtOffsetHours);
    mainPage += "\"";
    if ((autoSync) && (useCustomOffsets)) {
      mainPage += "></td>\
        <td>&nbsp;</td>";
    } else {
      mainPage += " disabled></td>\
        <td>&nbsp;(requires Auto-Sync & Custom Offsets)</td>";
    }
    mainPage += "</td>\
      </tr><tr>\
      <td><label for=\"dstoffset\">DST Offset Hours:</label></td>\
      <td><input type=\"number\" style=\"width:3em\" min=\"-12\" max=\"14\" step=\"1\" name=\"dstoffset\" value=\"";
    mainPage += String(dstOffsetHours);
    mainPage += "\"";
    if ((autoSync) && (useCustomOffsets)) {
      mainPage += "></td>\
        <td>&nbsp;</td>";
    } else {
      mainPage += " disabled></td>\
        <td>&nbsp;(requires Auto-Sync & Custom Offsets)</td>";
    }
    mainPage += "</tr>\
      </table><br>";
      
    mainPage += "<u><b>Temperature Default Settings</b></u><br><br>\
      <table border=\"0\">\
      <tr>\
      <td>Temperature Units:</td>\
      <td>\
      <input type=\"radio\" id=\"degF\" name=\"tempsymbol\" value=\"13\"";
    if (temperatureSymbol == 13) mainPage += " checked";
    mainPage += ">\
      <label for=\"degF\">Fahrenheit&nbsp;</label>\
      </td><td>\
      <input type=\"radio\" id=\"degC\" name=\"tempsymbol\" value=\"12\"";
    if (temperatureSymbol == 12) mainPage += " checked";
    mainPage += ">\
      <label for=\"degF\">Celsius</label>\
      </td></tr>\
      <tr>\
      <td>Temperature Display:</td>\
      <td>\
      <input type=\"radio\" id=\"external\" name=\"tempsource\" value=\"1\"";
    if (temperatureSource == 1) mainPage += " checked";
    mainPage += ">\
      <label for=\"external\">Outside Only</label>\
      </td><td>\
      <input type=\"radio\" id=\"internal\" name=\"tempsource\" value=\"2\"";
    if (temperatureSource == 2) mainPage += " checked";
    mainPage += ">\
      <label for=\"internal\">Inside Only</label>\  
      </td><td>\
      <input type=\"radio\" id=\"sensor\" name=\"tempsource\" value=\"0\"";
    if (temperatureSource == 0) mainPage += " checked";
    mainPage += ">\
      <label for=\"sensor\">Dual (both)</label>\
      </td></tr>\
      </table><br>\
      <u>Inside Temperature</u> - If API is used, temperature will report 0&deg until update is received<br>\
      <table border=\"0\">\
      <tr><td>\
      Inside Temp Source:\
      </td><td>\
      <input type=\"radio\" id=\"sensor\" name=\"tempintapi\" value=\"sensor\"";
      if (!tempIntUseApi) mainPage += " checked";
    mainPage += ">\
      <label for=\"sensor\">Onboard Sensor (AHT20)</label>\
      </td><td>\
      <input type=\"radio\" id=\"apiint\" name=\"tempintapi\" value=\"apiint\"";
      if (tempIntUseApi) mainPage += " checked";
    mainPage += ">\
      <label for=\"apiint\">Local API</label>\
      </td></tr></table><br>\
      <i>These settings only applicable to the onboard sensor - ignored if local API is used</i>\
      <table border=\"0\">\
      <tr>\
      <td><label for=\"tempcorrection\">Inside Temp Correction:</label></td>\
      <td><input type=\"number\" style=\"width:4em\" min=\"-10\" max=\"10\" step=\"0.1\" name=\"tempcorrection\" value=\"";
    mainPage += String(temperatureCorrection);
    mainPage += "\">&deg&nbsp;(-10&deg to +10&deg)</td>\
      </tr><tr>\
      <td><label for=\"tempupdint\">Inside Refresh Interval:</label></td>\
      <td><input type=\"number\" name=\"tempupdint\" min=\"1\" step=\"1\" style=\"width:4em\" value=\"";
    mainPage += String((tempUpdatePeriod / 60));  
    mainPage += "\">&nbsp;minutes (minimum 1)</td>\  
      </tr>\  
      </table><br>\
      <u>Outside Temperature</u> - Temperature will always report 0&deg until an update is received<br>\
      <table border=\"0\">\
      <tr><td>\
      Outside Temp Source:\
      </td><td>\
      <input type=\"radio\" id=\"owm\" name=\"tempextapi\" value=\"owm\"";
      if (!tempExtUseApi) mainPage += " checked";
    mainPage += ">\
      <label for=\"owm\">OpenWeatherMap (OWM)</label>\
      </td><td>\
      <input type=\"radio\" id=\"api\" name=\"tempextapi\" value=\"api\"";
      if (tempExtUseApi) mainPage += " checked";
    mainPage += ">\
      <label for=\"api\">Local API</label>\
      </td></tr></table><br>\
      <i>These values only applicable if using OWM - ignored if local API is used</i>\
      <table border=\"0\">\
      <tr>\
      <td><label for=\"owmkey\">OWM API Key:&nbsp;&nbsp;</label></td>\
      <td><input type=\"text\" name=\"owmkey\" maxlength=\"34\" style=\"width:20em\" value=\"";
    mainPage += owmKey;
    mainPage += "\"></td>\
      </tr><tr>\
      <td>OWM Latitude:</td>\
      <td><input type=\"number\" name=\"owmlat\" min=\"-90\" max=\"90\" step=\"0.0001\" style=\"width:6em\" value=\"";
    mainPage += String(owmLat);
    mainPage += "\"></td></tr>\
      <td>OWM Longitude:&nbsp;&nbsp\</td>\
      <td><input type=\"number\" name=\"owmlong\" min=\"-180\" max=\"180\" step=\"0.0001\" style=\"width:6em\" value=\"";
    mainPage += String(owmLong);
    mainPage += "\"></td></tr>\
      <tr>\
      <td><label for=\"tempupdext\">OWM Refresh Interval:</label></td>\
      <td><input type=\"number\" name=\"tempupdext\" min=\"10\" step=\"1\" style=\"width:3em\" value=\"";
    mainPage += String((tempUpdatePeriodExt / 60));
    mainPage += "\">&nbsp;minutes (minimum 10)</td>\
      </tr>\
      </table><br>";

    mainPage += "<b><u>Countdown Timer Default Settings</u></b><br><br>\
      <table border=\"0\">\
      <tr>\
      <td><label for=\"countdownmin\">Starting Countdown time (mm:ss):</td>\
      <td><input type=\"number\" style=\"width:3em\" min=\"0\" max=\"59\" step=\"1\" name=\"countdownmin\" value=\"";
    if (defaultCountdownMin < 10) {
      mainPage += "0" + String(defaultCountdownMin);
    } else {
      mainPage += String(defaultCountdownSec);
    }
    mainPage += "\"> :\
      <input type=\"number\" style=\"width:3em\" min=\"0\" max=\"59\" step=\"1\" name=\"countdownsec\" value=\"";
    if (defaultCountdownSec < 10) {
      mainPage += "0" + String(defaultCountdownSec);
    } else {
      mainPage += String(defaultCountdownSec);
    }
    mainPage += "\"> (max 59:59)</td>\
      </tr><tr>\
      <td><label for=\"usebuzzer\">Sound Buzzer at Time Expiration:</td>\
      <td><input type=\"checkbox\" id=\"usebuzzer\" name=\"usebuzzer\" value=\"usebuzzer\"";
    if (useBuzzer) mainPage += " checked";
    mainPage += "></td>\
      </tr></table><br>";

    mainPage += "<u><b>Scoreboard Default Settings</b></u><br>\
      Max 3 characters each.  Use '&' for a space.<br><br>\
      <table border=\"0\">\
      <tr>\
      <td><label for=\"scoreteamleft\">Left Team Name:</td>\
      <td><input type=\"text\" name=\"scoreteamleft\" maxlength=\"3\" style=\"width:4em\" value=\"";
    mainPage += scoreboardTeamLeft;
    mainPage += "\">\
      </td></tr>\
      <tr>\
      <td><label for=\"scoreteamright\">Right Team Name:</td>\
      <td><input type=\"text\" name=\"scoreteamright\" maxlength=\"3\" style=\"width:4em\" value=\"";
    mainPage += scoreboardTeamRight;
    mainPage += "\">\
      </td></tr></table><br><br>";
    
    mainPage += "<u><b>Text Display Defaults Settings</b></u><br>\
      Max 6 characters per row.  Use an '&' symbol to insert a space<br><br>\
      <table border=\"0\">\
      <tr>\
      <td><label for=\"texttop\">Top Row:</td>\
      <td><input type=\"text\" name=\"texttop\" maxlength=\"6\" style=\"width:7em\" value=\"";
    mainPage += textTop;
    mainPage += "\">\
      </td></tr>\
      <tr>\
      <td><label for=\"textbottom\">Bottom Row:</td>\
      <td><input type=\"text\" name=\"textbottom\" maxlength=\"6\" style=\"width:7em\" value=\"";
    mainPage += textBottom;
    mainPage += "\">\
      </td></tr>\
      <tr>\
      <td><label for=\"texteffect\">Effect:</td>\
      <td>";
    mainPage += getEffectList();
    mainPage += "</td></tr>\
      <tr>\
      <td><label for=\"textspeed\">Speed:</td>\
      <td><input type=\"number\" name=\"textspeed\" min=\"1\" max=\"10\" step=\"1\" style=\"width:3em\" value=\"";
    mainPage += String(textEffectSpeed);
    mainPage += "\">\
      </td></tr>\
      </table><br><br>";

    //WLED Controller IP Address & max preset
    mainPage += "<b>Secondary WLED Controller</b><br>\
      <i>Optional</i>: Set IP address to '0.0.0.0' if not using WLED Controller<br><br>\
      <table><tr>\
      <td><label for=\"wledaddress\">IP Address: http://</label></td>\
      <td><input type=\"text\" name=\"wledaddress\" maxlength=\"15\" style=\"width:9em\" value=\"";
    mainPage += wledAddress;
    mainPage += "\">\
      </td></tr>\
      <tr>\
      <td><label for=\"wledmapreset\">Max. Preset Num:</label></td>\
      <td><input type=\"number\" name=\"wledmaxpreset\" min=\"0\" max=\"20\" step=\"1\" style=\"width:3em\" value=\"";
    mainPage += String(wledMaxPreset);
    mainPage += "\"> (0 - 20)\  
      </table><br><br>";

    //Save as boot defaults checkbox
    mainPage += "<b><u>Boot Defaults</u></b><br><br>\
      <input type=\"checkbox\" name=\"chksave\" value=\"save\">Save all settings as new boot defaults (controller will reboot)<br><br>\
      <input type=\"submit\" value=\"Update\">\
      </form>\
      <h2>Controller Commands</h2>\
      <b>Caution</b>: Restart and Reset are executed <i>immediately</i> when the button is clicked.<br>\
      <table border=\"1\" cellpadding=\"10\">\
      <tr>\
      <td><button type=\"button\" id=\"btnrestart\" onclick=\"location.href = './restart';\">Restart</button></td><td>This will reboot controller and reload default boot values.</td>\
      </tr><tr>\
      <td><button type=\"button\" id=\"btnreset\" style=\"background-color:#FAADB7\" onclick=\"location.href = './reset';\">RESET ALL</button>\
      </td><td><b><font color=red>WARNING</font></b>: This will clear all settings, including WiFi! You must complete initial setup again.</td>\
      </tr><tr>\
      <td><button type=\"button\" id=\"btnupdate\" onclick=\"location.href = './webupdate';\">Firmware Upgrade</button></td><td>\
      Upload and apply new firmware from a compiled .bin file.&nbsp;<i>(beta feature)</i></td>\
      </tr><tr>\
      <td><button type=\"button\" id=\"btnotamode\" onclick=\"location.href = './otaupdate';\">Arudino OTA</button></td><td>\
      Put system in Arduino OTA mode for approx. 20 seconds to flash modified firmware from IDE.</td>\
      </tr>\
      </table><br>";
  }
  mainPage += "</body></html>";
  mainPage.replace("VAR_APP_NAME", APPNAME);
  mainPage.replace("VAR_CURRENT_VER", VERSION);
  server.send(200, "text/html", mainPage);
}

void webClockPage() {
  clockMode = 0;  //switch to clock mode for edit
  modeCallingPage = "clockedit";
  String message = "<html><head>";
  String tempSymbol = "F";
  byte dispMin = 0;
  byte dispSec = 0;
  if (temperatureSymbol == 12) {
    tempSymbol = "C";
  } else {
    tempSymbol = "F";
  }
  message += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";  //make page responsive
  message += "<title>Clock and Temp Controls</title>\
    <style>\
    body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #0000ff; }\
    </style>\
    </head>\
    <body>";
  message += "<H1>VAR_APP_NAME - Clock and Temperature</H1>";
  message += "Firmware Version: VAR_CURRENT_VER<br><br>";
  message += "<table border=\"1\" >";
  message += "<tr><td>Device Name:</td><td>" + deviceName + "</td</tr>";
  message += "<tr><td>WiFi Network:</td><td>" + WiFi.SSID() + "</td</tr>";
  message += "<tr><td>MAC Address:</td><td>" + strMacAddr + "</td</tr>";
  message += "<tr><td>IP Address:</td><td>" + baseIPAddress + "</td</tr>";
  message += "<tr><td>Max Milliamps:</td><td>" + String(milliamps) + "</td></tr>";
  message += "</table><br>";
  message += "<button type=\"button\" id=\"btnback\" style=\"font-size: 16px; border-radius: 8px; width: 100px; height: 30px;\" onclick=\"location.href = './';\"><< Back</button>";
  message += "<H2>Clock & Temperature Control</H2>\
      <table border=\"0\">\
      <tr><td>\
      Current Time:</td><td>";
  message += rtc.getDate() + " " + rtc.getTime();
  message += " (static: <a href= \"./clockedit\">Click here</a> to refresh time)</td></tr>\
      <tr><td>Interior Temp:</td><td>";
  message += String(internalTemperature) + "&deg" + tempSymbol;
  message += "</td></tr>\
      <tr><td>Exterior Temp:</td><td>";
  message += String(externalTemperature) + "&deg" + tempSymbol;
  message += "</td></tr>\
      </table><br<br>";

  message += "<H3>Adjust Date and Time</H3>\
      <table border=\"0\">\
      <tr><td>\
      Resync to NTP Server:</td>\
      <td><button type=\"button\" id=\"btnsync\" style=\"font-size: 16px; background-color: #bdf2d1;\
       border-radius: 8px; width: 100px; height: 30px;\" onclick=\"location.href = './timesync';\"";
  if (!autoSync) {
    message += " disabled>Sync Off</button>&nbsp;(enable Auto-Sync to sync time)";  
  } else {
    message += ">Sync Now</button>&nbsp;(You cannot sync more frequently than the defined sync interval)";
  }
  message += "</td></tr></table><br><br>";

  message += "<u><b>Manually Set Date and Time</b></u><br>\
      <form method=\"post\" enctype=\"application/x-www-form-urlencoded\" action=\"/clockupdate\">\
      <table border=\"0\">\
      <tr><td>Date (mm/dd/yyyy):</td>\
      <td><input type=\"number\" name=\"month\" style=\"width:3em\" min=\"1\" max=\"12\" step=\"1\" value=\"";
  //Month via ESPTime is zero-based, so add one
  message += String(rtc.getMonth() + 1) + "\">\  
      /<input type=\"number\" name=\"day\" style=\"width:3em\" min=\"1\" max=\"31\" step=\"1\" value=\"";
  message += String(rtc.getDay()) + "\">\
      /<input type=\"number\" name=\"year\" style=\"width:4em\" min=\"2020\" max=\"2100\" step=\"1\" value=\"";
  message += String(rtc.getYear()) + "\"></td>\
      </tr><tr>\
      <td>Time (hh:mm:ss):</td>\
      <td style=\"text-align: center;\"><input type=\"number\" name=\"hour\" style=\"width:3em\" min=\"0\" max=\"23\" step=\"1\" value=\"";
  message += String(rtc.getHour(true)) + "\">\
      :<input type=\"number\" name=\"minute\" style=\"width:3em\" min=\"0\" max=\"59\" step=\"1\" value=\"";
  dispMin = rtc.getMinute();
  if (dispMin < 10) {
    message += "0" + String(dispMin);
  } else {
    message += String(dispMin);
  }
  message += "\">\:<input type=\"number\" name=\"second\" style=\"width:3em\" min=\"0\" max=\"59\" step=\"1\" value=\"";
  dispSec = rtc.getSecond();
  if (dispSec < 10) {
    message += "0" + String(dispSec);
  } else {  
    message += String(dispSec);
  }
  message += "\">&nbsp;(enter hours in 24-hour/military format)</td></tr>";
  message += "</table><br>\
      <input type=\"submit\" style=\"font-size: 16px; background-color: #b8e7f5;\
       border-radius: 8px; width: 100px; height: 30px;\" value=\"Set Time\">";

  message += "</body></html>";
  message.replace("VAR_APP_NAME", APPNAME);
  message.replace("VAR_CURRENT_VER", VERSION);
  server.send(200, "text/html", message);
}

void webCountdownPage() {
  clockMode = 1;  //switch display to countdown mode
  modeCallingPage = "countdownedit";

  String message = "<html><head>";
  message += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";  //make page responsive
  message += "<title>Countdown Control</title>\
    <style>\
    body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #0000ff; }\
    </style>\
    </head>\
    <body>";
  message += "<H1>VAR_APP_NAME - Countdown Timer</H1>";
  message += "Firmware Version: VAR_CURRENT_VER<br><br>";
  message += "<table border=\"1\" >";
  message += "<tr><td>Device Name:</td><td>" + deviceName + "</td</tr>";
  message += "<tr><td>WiFi Network:</td><td>" + WiFi.SSID() + "</td</tr>";
  message += "<tr><td>MAC Address:</td><td>" + strMacAddr + "</td</tr>";
  message += "<tr><td>IP Address:</td><td>" + baseIPAddress + "</td</tr>";
  message += "<tr><td>Max Milliamps:</td><td>" + String(milliamps) + "</td></tr>";
  message += "</table><br>";
  message += "<button type=\"button\" id=\"btnback\" style=\"font-size: 16px; border-radius: 8px; width: 100px; height: 30px;\" onclick=\"location.href = './';\"><< Back</button>";
  message += "<H2>Countdown Control</H2>\
      Changes made here are temporary. To permanently change the starting time, use the option on main settings page.<br><br>\
      <form method=\"post\" enctype=\"application/x-www-form-urlencoded\" action=\"/countdownupdate\">\
      <table border=\"0\">\
      <tr>\
      <td><label for=\"countdownmin\">Starting Countdown time (mm:ss):</td>\
      <td><input type=\"number\" style=\"width:3em\" min=\"0\" max=\"59\" step=\"1\" name=\"countdownmin\" value=\"";
  if (defaultCountdownMin < 10) {
    message += "0" + String(defaultCountdownMin);
  } else {
    message += String(defaultCountdownSec);
  }
  message += "\">:\
    <input type=\"number\" style=\"width:3em\" min=\"0\" max=\"59\" step=\"1\" name=\"countdownsec\" value=\"";
  if (defaultCountdownSec < 10) {
    message += "0" + String(defaultCountdownSec);
  } else {
    message += String(defaultCountdownSec);
  }
  message += "\"> (max 59:59)</td>\
    <td><input type=\"submit\" style=\"font-size: 16px; background-color: #b8e7f5;\
      border-radius: 8px; width: 100px; height: 30px;\" value=\"Set Time\">\
    </tr></table></form><br><br>";

  message += "<tableborder=\"0\">\
   <tr><td><button id=\"btnstart\"  onclick=\"location.href = './countdowntoggle';\" \
    style=\"font-size: 20px; text-align: center; border-radius: 8px; width: 140px; height: 40px; ";
  if (timerRunning == 0) {
    message += "background-color: #95f595;\"\
      >Start";
  } else {
    message += "background-color: #faa2a2;\"\
      >Stop";
  }
  message += "</button></td>\
    <td><button id=\"btnstart\"  onclick=\"location.href = './countdownbuzzer';\" \
    style=\"font-size: 20px; text-align: center; border-radius: 8px; width: 140px; height: 40px;\"\
    >Buzzer</button></td></tr></table>";

  message += "</body></html>";
  message.replace("VAR_APP_NAME", APPNAME);
  message.replace("VAR_CURRENT_VER", VERSION);
  server.send(200, "text/html", message);
}

void webScorePage() {
  clockMode = 2;   //Switch display to scoreboard mode
  modeCallingPage = "scoreedit";

  String message = "<html><head>";
  message += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";  //make page responsive
  message += "<title>Scoreboard Controls</title>\
    <style>\
    body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #0000ff; }\
    </style>\
    </head>\
    <body>";
  message += "<H1>VAR_APP_NAME - Scoreboard</H1>";
  message += "Firmware Version: VAR_CURRENT_VER<br><br>";
  message += "<table border=\"1\" >";
  message += "<tr><td>Device Name:</td><td>" + deviceName + "</td</tr>";
  message += "<tr><td>WiFi Network:</td><td>" + WiFi.SSID() + "</td</tr>";
  message += "<tr><td>MAC Address:</td><td>" + strMacAddr + "</td</tr>";
  message += "<tr><td>IP Address:</td><td>" + baseIPAddress + "</td</tr>";
  message += "<tr><td>Max Milliamps:</td><td>" + String(milliamps) + "</td></tr>";
  message += "</table><br>";
  message += "<button type=\"button\" id=\"btnback\" style=\"font-size: 16px; border-radius: 8px; width: 100px; height: 30px;\" \
    onclick=\"location.href = './';\"><< Back</button>";
  message += "<H2>Scoreboard Control</H2>\
    Team names are 3 characters max. Use '&' to insert a space in team names.<br><br>\
    <form method=\"post\" enctype=\"application/x-www-form-urlencoded\" action=\"/scoreupdate\">\
    <table border=\"0\"><tr>\
    <td>&nbsp;</td>\
    <td style=\"text-align: center;\">LEFT</td>\
    <td style=\"width: 2em\">&nbsp;</td>\
    <td style=\"text-align: center;\">RIGHT</td>\
    <td>&nbsp;</td>\
    </tr><tr>\
    <td style=\"text-align: center;\">Team Name:</td>\
    <td style=\"text-align: center;\"><input name=\"leftteam\" style=\" width: 4em;\" type=\"text\" maxlength=\"3\" value=\"";
  message += scoreboardTeamLeft;
  message += "\"></td>\
    <td>&nbsp;</td>\
    <td style=\"text-align: center;\"><input name=\"rightteam\" style=\"width: 4em;\" type=\"text\" maxlength=\"3\" value=\"";
  message += scoreboardTeamRight;
  message += "\"></td>\
    <td><input type=\"submit\" style=\"font-size: 14px; border-radius: 8px; width: 110px; height: 30px; background-color: #f5f1b5;\" value=\"Update Teams\"></td>\
    </tr><tr>\
    <td>&nbsp;</td>\
    <td style=\"text-align: center;\"><input type=\"number\" name=\"scoreleft\" \
      style=\"font-size: 36px; text-align: center; width: 75px; height: 75px;\" min=\"0\" max=\"99\" step=\"1\" value=\"";
  message += String(scoreboardLeft);
  message += "\"></td>\
    <td>&nbsp;</td>\
    <td style=\"text-align: center;\"><input type=\"number\" name=\"scoreright\" \
      style=\"font-size: 36px; text-align: center; width: 75px; height: 75px;\" min=\"0\" max=\"99\" step=\"1\" value=\"";
  message += String(scoreboardRight);
  message += "\"></td>\
    <td>&nbsp;</td>\    
    </tr><tr>\
    <td>&nbsp;</td>\
    <td style=\"text-align: center;\">\
      <input type=\"submit\" style= \"font-size: 24px; width: 2em; text-align: center;\" formaction=\"/scoreleftminus\" value=\"-\">\
      <input type=\"submit\" style= \"font-size: 24px; width: 2em; text-align: center;\" formaction=\"/scoreleftplus\" value=\"+\"></td>\
    <td>&nbsp;</td>\
    <td style=\"text-align: center;\">\
      <input type=\"submit\" style= \"font-size: 24px; width: 2em; text-align: center;\" formaction=\"/scorerightminus\" value=\"-\">\
      <input type=\"submit\" style= \"font-size: 24px; width: 2em; text-align: center;\"  formaction=\"/scorerightplus\"value=\"+\"></td>\
    <td>&nbsp;</td>\
    </tr><tr>\
    <td>&nbsp;</td><td>&nbsp;</td><td>&nbsp;</td><td>&nbsp;</td><td>&nbsp;</td>\
    </tr><tr>\
    <td>&nbsp;</td>\
    <td><input type=\"submit\" style=\"font-size: 14px; \
      border-radius: 8px; width: 110px; height: 30px; background-color: #b1f2d6;\" value=\"Update Scores\"></td>\    
    <td>&nbsp;</td>\
    <td><input type=\"submit\" style=\"font-size: 14px; border-radius: 8px; width: 110px; height: 30px; background-color: #f5bfbf;\" \
      formaction=\"/scorereset\" value=\"Reset Scores\"></td>\
    <td>&nbsp;</td>\
    </tr>\ 
    </table>";

  message += "</body></html>";
  message.replace("VAR_APP_NAME", APPNAME);
  message.replace("VAR_CURRENT_VER", VERSION);
  server.send(200, "text/html", message);
}

void webTextPage() {
  clockMode = 3;   //Switch display to scoreboard mode
  modeCallingPage = "textedit";

  String message = "<html><head>";
  message += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";  //make page responsive
  message += "<title>Text Display Controls</title>\
    <style>\
    body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #0000ff; }\
    </style>\
    </head>\
    <body>";
  message += "<H1>VAR_APP_NAME - Text Display</H1>";
  message += "Firmware Version: VAR_CURRENT_VER<br><br>";
  message += "<table border=\"1\" >";
  message += "<tr><td>Device Name:</td><td>" + deviceName + "</td</tr>";
  message += "<tr><td>WiFi Network:</td><td>" + WiFi.SSID() + "</td</tr>";
  message += "<tr><td>MAC Address:</td><td>" + strMacAddr + "</td</tr>";
  message += "<tr><td>IP Address:</td><td>" + baseIPAddress + "</td</tr>";
  message += "<tr><td>Max Milliamps:</td><td>" + String(milliamps) + "</td></tr>";
  message += "</table><br>";
  message += "<button type=\"button\" id=\"btnback\" style=\"font-size: 16px; border-radius: 8px; width: 100px; height: 30px;\" \
    onclick=\"location.href = './';\"><< Back</button>";
  message += "<H2>Text Display Control</H2>\
    Each row of text can hold up to six characters.  Use an '&' to insert a space anywhere in the string.<br>\
    <form method=\"post\" enctype=\"application/x-www-form-urlencoded\" action=\"/textupdate\">\
    <table border=\"0\">\
    <tr>\
    <td><label for=\"texttop\">Top Text Row:</td>\
      <td><input type=\"text\" name=\"texttop\" maxlength=\"6\" style=\"width:7em\" value=\"";
  message += textTop;
  message += "\">\
    </td></tr>\
    <tr>\
    <td><label for=\"textbottom\">Bottom Row:</td>\
    <td><input type=\"text\" name=\"textbottom\" maxlength=\"6\" style=\"width:7em\" value=\"";
  message += textBottom;
  message += "\">\
    </td></tr>\
    <tr>\
    <td><label for=\"texteffect\">Effect:</td>\
    <td>";
  message += getEffectList();
  message += "</td></tr>\
    <tr>\
    <td><label for=\"textspeed\">Speed (1-10):</td>\
    <td><input type=\"number\" name=\"textspeed\" min=\"1\" max=\"10\" step=\"1\" style=\"width:3em\" value=\"";
  message += String(textEffectSpeed);
  message += "\">\
    </td></tr>\
    <tr>\
    <td><input type=\"submit\" style=\"font-size: 14px; \
      border-radius: 8px; width: 110px; height: 30px; background-color: #b1f2d6;\" value=\"Update Display\"></td><td>&nbsp;</td>\    
    </tr>\
    </table><br><br>";
  message += "</body></html>";
  message.replace("VAR_APP_NAME", APPNAME);
  message.replace("VAR_CURRENT_VER", VERSION);
  server.send(200, "text/html", message);
}

void webFirmwareUpdate() {
  String page = String(updateHtml);       //from html.h
  page.replace("VAR_APP_NAME", APPNAME);
  page.replace("VAR_CURRENT_VER", VERSION);
  server.send(200, "text/html", page);
}

// ============================
//  Web Page Handlers
// ============================
void handleOnboard() {
  byte count = 0;
  bool wifiConnected = true;
  uint32_t currentMillis = millis();
  uint32_t pageDelay = currentMillis + 5000;
  String webPage = "";
  //Output web page to show while trying wifi join
  webPage = "<html><head>\
    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\ 
    <meta http-equiv=\"refresh\" content=\"1\">";  //make page responsive and refresh once per second
  webPage += "<title>VAR_APP_NAME Onboarding</title>\
      <style>\
        body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000000; }\
      </style>\
      </head>\
      <body>";
  webPage += "<h3>Attempting to connect to Wifi</h3><br>";
  webPage += "Please wait...  If WiFi connection is successful, device will reboot and you will be disconnected from the VAR_APP_NAME AP.<br><br>";
  webPage += "Reconnect to normal WiFi, obtain the device's new IP address and go to that site in your browser.<br><br>";
  webPage += "If this page does remains after one minute, reset the controller and attempt the onboarding again.<br>";
  webPage += "</body></html>";
  webPage.replace("VAR_APP_NAME", APPNAME);
  server.send(200, "text/html", webPage);
  while (pageDelay > millis()) {
    yield();
  }

  //Handle initial onboarding - called from main page
  //Get vars from web page
  wifiSSID = server.arg("ssid");
  wifiPW = server.arg("wifipw");
  deviceName = server.arg("devicename");
  milliamps = server.arg("maxmilliamps").toInt();
  wifiHostName = deviceName;

  //Attempt wifi connection
#if defined(ESP8266)
  WiFi.setSleepMode(WIFI_NONE_SLEEP);  //Disable WiFi Sleep
#elif defined(ESP32)
  WiFi.setSleep(false);
#endif
  WiFi.mode(WIFI_STA);
  WiFi.hostname(wifiHostName);
  WiFi.begin(wifiSSID, wifiPW);
#if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
  Serial.print("SSID:");
  Serial.println(wifiSSID);
  Serial.print("password: ");
  Serial.println(wifiPW);
  Serial.print("Connecting to WiFi (onboarding)");
#endif
  while (WiFi.status() != WL_CONNECTED) {
#if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
    Serial.print(".");
#endif
    // Stop if cannot connect
    if (count >= 60) {
// Could not connect to local WiFi
#if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
      Serial.println();
      Serial.println("Could not connect to WiFi during onboarding.");
#endif
      wifiConnected = false;
      break;
    }
    delay(500);
    yield();
    count++;
  }

  if (wifiConnected) {
    //Save settings to LittleFS and reboot
    writeConfigFile(true);
  }
}

void handleSettingsUpdate() {
  //Update local variables
  bool owmChanged = false;
  modeCallingPage = "";  //Changing modes from this page will return to settings
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
  } else {
    String saveSettings;
    timerRunning = false;          //stop any active timers
    bool updateExtTemp = false;    //flag for changes to external temp settings and to refresh display
    bool updateTimeServer = false; //flag for setting new server params if NTP or offsets changed
    //Update vars here
    defaultClockMode = server.arg("clockmode").toInt();
    if (server.arg("binaryclock") == "binary") {
      binaryClock = true;
    } else {
      binaryClock = false;
      //Change to mode if different than current
      if (clockMode != defaultClockMode) clockMode = defaultClockMode;
    }
    brightness = server.arg("ledbrightness").toInt();
    if (brightness > 255) brightness = 255;
    if (brightness > 5) ledsOn = true;
    FastLED.setBrightness(brightness);

    numFont = server.arg("numfont").toInt();
    //Check for any changes to external temperaure settings and force display update
    if ((server.arg("owmkey") != owmKey) || (server.arg("owmlat") != owmLat) || (server.arg("owmlong") != owmLong) || ((server.arg("tempsymbol").toInt()) != temperatureSymbol)) {
      updateExtTemp = true;
    }
    //Check for any changes related to syncing time/time server
    if (autoSync) {
      if ((server.arg("ntpserver") != ntpServer) || (server.arg("timezone") != timeZone) || ((server.arg("syncinterval").toInt()) != autoSyncInterval)) {  
        updateTimeServer = true;
        autoSyncInterval = server.arg("syncinterval").toInt();
        ntpServer = server.arg("ntpserver");
        timeZone = server.arg("timezone");
      } 
      if ((autoSync) && (useCustomOffsets)) {
        if (((server.arg("gmtoffset").toInt()) != gmtOffsetHours) || ((server.arg("dstOffset").toInt()) != dstOffsetHours)) {
          updateTimeServer = true;
          gmtOffsetHours = server.arg("gmtoffset").toInt();
          dstOffsetHours = server.arg("dstoffset").toInt();
        }
      }
    }
    //Internal temperature source
    if (server.arg("tempintapi") == "apiint") {
      if (!tempIntUseApi) {
        //Set internal temp initially to 0 when first switching to local API and force update
        internalTemperature = 0;
        tempUpdateCount = 0;
      }
      tempIntUseApi = true;
    } else {
      if (tempIntUseApi) {
        //Force refresh from OWM when switching back from local API
        tempUpdateCount = 0;
      }
      tempIntUseApi = false;
    }
    //External temperature source
    if (server.arg("tempextapi") == "api") {
      if (!tempExtUseApi) {
        //Set external temp initially to 0 when first switching to local API and force update
        externalTemperature = 0;
        tempUpdateCountExt = 0;
      }
      tempExtUseApi = true;
    } else {
      if (tempExtUseApi) {
        //Force refresh from OWM when switching back from local API
        tempUpdateCountExt = 0;
      }
      tempExtUseApi = false;
    }
    owmKey = server.arg("owmkey");
    if (owmKey == "") owmKey = "NA";
    owmLat = server.arg("owmlat");
    owmLong = server.arg("owmlong");
    temperatureSymbol = server.arg("tempsymbol").toInt();
    temperatureSource = server.arg("tempsource").toInt();
    temperatureCorrection = server.arg("tempcorrection").toFloat();
    tempUpdatePeriod = (server.arg("tempupdint").toInt() * 60);    //convert to seconds
    tempUpdatePeriodExt = (server.arg("tempupdext").toInt() * 60); //convert to seconds
    hourFormat = server.arg("hourformat").toInt();

    //Now set autosync and manual offset flags
    if (server.arg("autosync") == "autosync") {
      autoSync = true;
      if (server.arg("useoffsets") == "useoffsets") {
        useCustomOffsets = true;
      } else {
        useCustomOffsets = false;
      }
    } else {
      autoSync = false;
      useCustomOffsets = false;
    }
    //Update time configuration of any sync settings changed
    if ((updateTimeServer) && (autoSync)) {
      sntp_set_sync_interval(autoSyncInterval * 60000);
      if (useCustomOffsets) {
        configTime(gmtOffsetHours * 3600, dstOffsetHours * 3600, ntpServer.c_str());  
      } else {
        configTime(0, 0, ntpServer.c_str());  
        setenv("TZ", timeZone.c_str(), 1);
        tzset();
      }
      syncTime();
    }
    scoreboardTeamLeft = server.arg("scoreteamleft");    //string
    scoreboardTeamRight = server.arg("scoreteamright");  //string
    textTop = server.arg("texttop");
    textBottom = server.arg("textbottom");
    textEffect = server.arg("texteffect").toInt();
    textEffectSpeed = server.arg("textspeed").toInt();
    textEffectPeriod = int(1000 / textEffectSpeed);

    //Get min/sec and convert to millseconds
    defaultCountdownMin = server.arg("countdownmin").toInt();
    defaultCountdownSec = server.arg("countdownsec").toInt();
    if (defaultCountdownMin > 59) defaultCountdownMin = 59;
    if (defaultCountdownSec > 59) defaultCountdownSec = 59;
    initCountdownMillis = ((defaultCountdownMin * 60) + defaultCountdownSec) * 1000;
    countdownMilliSeconds = initCountdownMillis;
    remCountdownMillis = initCountdownMillis;
    endCountDownMillis = countdownMilliSeconds + millis();
    //Auto-buzzer on time expire
    if (server.arg("usebuzzer") == "usebuzzer") {
      useBuzzer = true;
    } else {
      useBuzzer = false;
    }
    //WLED Controller
    wledAddress = server.arg("wledaddress");
    if (wledAddress == "0.0.0.0") {
      useWLED = false;
    } else {
      useWLED = true;
    }
    wledMaxPreset = server.arg("wledmaxpreset").toInt();

    //LED Colors
    webColorClock = server.arg("clockcolor").toInt();
    webColorTemperatureInt = server.arg("tempcolorint").toInt();
    webColorTemperatureExt = server.arg("tempcolorext").toInt();
    webColorCountdown = server.arg("countdownactive").toInt();
    webColorCountdownPaused = server.arg("countdownpaused").toInt();
    webColorCountdownFinalMin = server.arg("countdownfinalmin").toInt();
    webColorScoreboardLeft = server.arg("scorecolorleft").toInt();
    webColorScoreboardRight = server.arg("scorecolorright").toInt();
    webColorTextTop = server.arg("textcolortop").toInt();
    webColorTextBottom = server.arg("textcolorbottom").toInt();

    clockColor = ColorCodes[webColorClock];
    temperatureColorInt = ColorCodes[webColorTemperatureInt];
    temperatureColorExt = ColorCodes[webColorTemperatureExt];
    countdownColor = ColorCodes[webColorCountdown];
    countdownColorPaused = ColorCodes[webColorCountdownPaused];
    countdownColorFinalMin = ColorCodes[webColorCountdownFinalMin];
    scoreboardColorLeft = ColorCodes[webColorScoreboardLeft];
    scoreboardColorRight = ColorCodes[webColorScoreboardRight];
    textColorTop = ColorCodes[webColorTextTop];
    textColorBottom = ColorCodes[webColorTextBottom];

    saveSettings = server.arg("chksave");
    //Update displays with any new values
    //This will also force an update to temperatures if any temperature settings changed
    updateDisplay(updateExtTemp);

    //Web page output
    String message = "<html>\
      </head>\
        <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\
        <title>Current System Settings</title>\
        <style>\
          body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
        </style>\
      </head>\
      <body>";
    message += "<H1>VAR_APP_NAME Settings Updated</H1>";
    message += "Firmware Version: VAR_CURRENT_VER<br><br>";
    message += "<table border=\"1\" >";
    message += "<tr><td>Device Name:</td><td>" + deviceName + "</td</tr>";
    message += "<tr><td>WiFi Network:</td><td>" + WiFi.SSID() + "</td</tr>";
    message += "<tr><td>MAC Address:</td><td>" + strMacAddr + "</td</tr>";
    message += "<tr><td>IP Address:</td><td>" + baseIPAddress + "</td</tr>";
    message += "<tr><td>Max Milliamps:</td><td>" + String(milliamps) + "</td></tr>";
    message += "</table><br>";
    message += "<button type=\"button\" id=\"btnback\" style=\"font-size: 16px; border-radius: 8px; width: 100px; height: 30px;\" onclick=\"location.href = './';\"><< Back</button>";

    //Standard mode button header
    message += "<H2>Mode Display & Control</H2>";
    message += "(changing modes here will return you to the main settings page)";
    message += "<table><tr>";
    message += "<td><button id=\"btnclock\" style=\"font-size: 20px; ";
    if (clockMode == 0) {
      message += "background-color: #95f595; font-weight: bold; ";
    } else {
      message += "background-color: #d6c9d6; ";
    }
    message += "text-align: center; border-radius: 8px; width: 140px; height: 60px;\" onclick=\"location.href = './clock';\">Clock</button></td>";
    message += "<td><button id=\"btncount\" style=\"font-size: 20px; ";
    if (clockMode == 1) {
      message += "background-color: #87d9e8; font-weight: bold; ";
    } else {
      message += "background-color: #d6c9d6; ";
    }
    message += "text-align: center; border-radius: 8px; width: 140px; height: 60px;\" onclick=\"location.href = './countdown';\">Countdown</button></td>";
    message += "<td><button id=\"btnscore\" style=\"font-size: 20px; ";
    if (clockMode == 2) {
      message += "background-color: #ebd588; font-weight: bold; ";
    } else {
      message += "background-color: #d6c9d6; ";
    }
    message += "text-align: center; border-radius: 8px; width: 140px; height: 60px;\" onclick=\"location.href = './scoreboard';\">Scoreboard</button></td>";
    message += "<td><button id=\"btntext\" style=\"font-size: 20px; ";
    if (clockMode == 3) {
      message += "background-color: #eb8ae6; font-weight: bold; ";
    } else {
      message += "background-color: #d6c9d6; ";
    }
    message += "text-align: center; border-radius: 8px; width: 140px; height: 60px;\" onclick=\"location.href = './text';\">Text Display</button></td>";
    message += "</tr><tr>";
    message += "<td style=\"text-align: center\"><a href= \"./clockedit\">Manage</a></td>";
    message += "<td style=\"text-align: center\"><a href= \"./countdownedit\">Manage</a></td>";
    message += "<td style=\"text-align: center\"><a href= \"./scoreedit\">Manage</a></td>";
    message += "<td style=\"text-align: center\"><a href= \"./textedit\">Manage</a></td>";
    message += "</tr></table>";
    //Rest of page
    message += "<H2>Settings updated!</H2>";
    message += "<H3>Current values are:</H3>";
    message += "<u><b>Colors</b></u><br>";
    message += "<table border=\"0\">";
    message += "<tr><td>Main Clock:</td><td>" + WebColors[webColorClock] + "</td><td>&nbsp;(binary clock hours)</td></tr>";
    message += "<tr><td>Outside Temperature:</td><td>" + WebColors[webColorTemperatureInt] + "</td><td>&nbsp;(binary clock minutes)</td></tr>";
    message += "<tr><td>Inside Temperature:</td><td>" + WebColors[webColorTemperatureExt] + "</td><td>&nbsp;(binary clock seconds)</td></tr>";
    message += "<tr><td>Countdown Active:</td><td>" + WebColors[webColorCountdown] + "</td></tr>";
    message += "<tr><td>Countdown Paused:</td><td>" + WebColors[webColorCountdownPaused] + "</td></tr>";
    message += "<tr><td>Countdown Final Min:</td><td>" + WebColors[webColorCountdownFinalMin] + "</td></tr>";
    message += "<tr><td>Scoreboard - Left:</td><td>" + WebColors[webColorScoreboardLeft] + "</td></tr>";
    message += "<tr><td>Scoreboard - Right:</td><td>" + WebColors[webColorScoreboardRight] + "</td></tr>";
    message += "<tr><td>Text - Top Row:</td><td>" + WebColors[webColorTextTop] + "</td></tr>";
    message += "<tr><td>Text - Bottom Row:</td><td>" + WebColors[webColorTextBottom] + "</td></tr></table><br>";

    message += "<u><b>Misc. Global Options</b></u><br>";
    message += "<table border=\"0\">";
    message += "<tr><td>Default Boot Mode:</td><td>";
    switch (defaultClockMode) {
      case 0:
        message += "Standard Clock & Temperature</td></tr>";
        break;
      case 1:
        message += "Countdown Timer</td></tr>";
        break;
      case 2:
        message += "Scoreboard</td></tr>";
        break;
      case 3:
        message += "Text Display</td></tr>";
        break;
      default:
        message += "&nbsp;</td></tr>";
        break;
    }
    message += "<tr><td>LED Brightness:</td><td>" + String(brightness) + "</td></tr>";
    message += "<tr><td>Large Number Font:</td><td>";
    switch (numFont) {
      case 0:
        message += "7-Segment</td></tr>";
        break;
      case 1:
        message += "Modern</td></tr>";
        break;
      case 2:
        message += "Hybrid</td></tr>";
        break;
      default:
        message += "&nbsp;</td></tr>";
        break;
    }
    message += "</table><br>";

    message += "<u><b>Clock & Time Default Settings</b></u><br>";
    message += "<table border=\"0\">";
    message += "<tr><td>Binary Clock Display:</td><td>";
    if (binaryClock) {
      message += "YES";
    } else {
      message += "No";
    }
    message += "</td></tr>";
    message += "<tr><td>Hour Display:</td><td>";
    if (hourFormat == 12) {
      message += "12-hour";
    } else if (hourFormat == 24) {
      message += "24-hour";
    } else {
      message += "&nbsp;";
    }
    message += "</td></tr>";
    message += "<tr><td>Auto-Sync Time:</td><td>";
    if (autoSync) {
      message += "YES";
    } else {
      message += "No";
    }
    message += "</td></tr>";
    if (autoSync) {
      message += "<tr><td>NTP Server:</td>";
      message += "<td>" + ntpServer + "</td></tr>";
      if (!useCustomOffsets) {
        message += "<tr>\
          <td>Time Zone (posix):</td>\
          <td>";
        message += timeZone + "</td></tr>";
      }  
      message += "<tr><td>Sync Interval:</td>";
      message += "<td>" + String(autoSyncInterval) + "&nbsp;minutes</td></tr>";
      message += "<tr><td>Use Custom Offsets:</td>";
      if (useCustomOffsets) {
        message += "<td>YES</td>\
          </tr><tr>\
          <td>GMT Offset (hours):</td>\
          <td>";
        message += String(gmtOffsetHours) + "</td>\
          </tr><tr>\
          <td>DST Offset (hours):</td>\
          <td>";
        message += String(dstOffsetHours) + "</td></tr>";  
      } else {
        message += "<td>No</td></tr>";
      }
    }
    message += "</table><br>";
    message += "<u><b>Temperature Default Settings</b></u>";
    message += "<table border=\"0\">";
    message += "<tr><td>Temperature Units:</td><td>";
    if (temperatureSymbol == 12) {
      message += "Celsius";
    } else if (temperatureSymbol == 13) {
      message += "Fahrenheit";
    } else {
      message += "&nbsp;";
    }
    message += "</td></tr>";
    message += "<tr><td>Temperature Display:</td><td>";
    if (temperatureSource == 0) {
      message += "Dual (inside and outside)";
    } else if (temperatureSource == 1) {
      message += "Outside Only";
    } else {
      message += "Inside Only";
    }
    message += "</td></tr>";

    message += "<tr><td>Temperature Correction:</td><td>" + String(temperatureCorrection) + "&deg</td></tr>";
    message += "<tr><td>Inside Temp Refresh:</td><td>" + String((tempUpdatePeriod / 60)) + "&nbsp;minute(s)</td></tr>";
    message += "<tr><td>Outside Temp Source:</td><td>";
    if (tempExtUseApi) {
      message += "Local API";
    } else {
      message += "OpenWeatherMap (OWM)";
    }
    message += "</td></tr>";
    if (tempExtUseApi) {
      message += "<tr><td>OWM API Key:</td><td>NA (local API in use)</td></tr>";
      message += "<tr><td>OWM Latitude:</td><td>NA</td></tr>";
      message += "<tr><td>OWM Longitude:</td><td>NA</td></tr>";
      message += "<tr><td>OWM Temp Refresh:</td><td>NA</td></tr>";
    } else {
      message += "<tr><td>OWM API Key:</td><td>" + owmKey + "</td></tr>";
      message += "<tr><td>OWM Latitude:</td><td>" + owmLat + "</td></tr>";
      message += "<tr><td>OWM Longitude:</td><td>" + owmLong + "</td></tr>";
      message += "<tr><td>OWM Temp Refresh:</td><td>" + String((tempUpdatePeriodExt / 60)) + "&nbsp;minutes</td></tr>";
    }
    message += "</table><br>";
 
    message += "<u><b>Countdown Timer Default Settings</b></u><br>";
    message += "<table border=\"0\">";
    message += "<tr><td>Starting Countdown Time:</td><td>";
    if (defaultCountdownMin < 10) {
      message += "0" + String(defaultCountdownMin);
    } else {
      message += String(defaultCountdownMin);
    }
    message += ":";
    if (defaultCountdownSec < 10) {
      message += "0" + String(defaultCountdownSec);
    } else {
      message += String(defaultCountdownSec);
    }
    message += "</td></tr>";
    message += "<tr><td>Sound Buzzer at Expiration:</td><td>";
    if (useBuzzer) {
      message += "YES";
    } else {
      message += "No";
    }
    message += "</td></tr></table><br>";

    message += "<u><b>Scoreboard Default Settings</b></u><br>";
    message += "<table border=\"0\">";
    message += "<tr><td>Left Team Name:</td><td>" + scoreboardTeamLeft + "</td></tr>";
    message += "<tr><td>Right Team Name:</td><td>" + scoreboardTeamRight + "</td></tr>";
    message += "</table><br>";

    message += "<u><b>Text Display Default Settings</b></u><br>";
    message += "<table border=\"0\">";
    message += "<tr><td>Top Text Row:</td><td>" + textTop + "</td></tr>";
    message += "<tr><td>Bottom Text Row:</td><td>" + textBottom + "</td></tr>";
    message += "</table><br>";

    message += "<u><b>Optional WLED Controller</b></u><br>";
    message += "Controller IP Address: " + wledAddress + "<br>";
    message += "Max. Preset Number: " + String(wledMaxPreset) + "<br><br>";
    //If update checked, write new values to flash
    if (saveSettings == "save") {
      message += "<b>New settings saved as new boot defaults.</b> Controller will now reboot.<br>";
      message += "You can return to the settings page after the boot complete (clock will show default mode when boot is finished).<br><br>";
    } else {
      message += "<i>*Current settings are temporary and will reset back to boot defaults when controller is restarted.\
        If you wish to make the current settings the new boot defaults, return to the Settings page and check the box to save the current settings as the new boot values.<br><br>";
    }

    message += "</body></html>";
    message.replace("VAR_APP_NAME", APPNAME);
    message.replace("VAR_CURRENT_VER", VERSION);
    server.send(200, "text/html", message);
    delay(1000);
    yield();

    if (saveSettings == "save") {
      writeConfigFile(true);
    }
  }
}

void handleRestart() {
  String restartMsg = "<HTML>\
      <head>\
        <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\
        <title>Controller Restart</title>\
        <style>\
          body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
        </style>\
      </head>\
      <body>\
      <H1>Controller restarting...</H1><br>\
      <H3>Please wait</H3><br>\
      After the controller completes the boot process, you may click the following link to return to the main page:<br><br>\
      <a href=\"http://";
  restartMsg += baseIPAddress;
  restartMsg += "\">Return to settings</a><br>";
  restartMsg += "</body></html>";
  server.send(200, "text/html", restartMsg);
  delay(1000);
  ESP.restart();
}

void handleLEDToggle() {
  toggleLEDs(!ledsOn);
  String page = "<HTML><head>";
  page += "<meta http-equiv=\"refresh\" content=\"0; url='http://" + baseIPAddress;
  if (modeCallingPage != "") {
    page += "/" + modeCallingPage;
  }
  page += "'\"><title>Toggling Power</title>\</head><body>";
  page += "Toggling Display...<br><br>";
  page += "If you are not automatically redirected, follow this to <a href='http://" + baseIPAddress + "'> return to settings page</a>.";
  page += "</body></html>";
  server.send(200, "text/html", page);
}

void handleReset() {
  String resetMsg = "<HTML>\
      </head>\
        <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\
        <title>Controller Reset</title>\
        <style>\
          body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
        </style>\
      </head>\
      <body>\
      <H1>Controller Resetting...</H1><br>\
      <H3>After this process is complete, you must setup your controller again:</H3>\
      <ul>\
      <li>Connect a device to the controller's local access point: VAR_APP_NAME_AP</li>\
      <li>Open a browser and go to: 192.168.4.1</li>\
      <li>Enter your WiFi information and set other default settings values</li>\
      <li>Click Save. The controller will reboot and join your WiFi</li>\
      </ul><br>\
      Once the above process is complete, you can return to the main settings page by rejoining your WiFi and entering the IP address assigned by your router in a browser.<br>\
      You will need to reenter all of your settings for the system as all values will be reset to original defaults<br><br>\
      <b>This page will NOT automatically reload or refresh</b>\
      </body></html>";
  resetMsg.replace("VAR_APP_NAME", APPNAME);
  server.send(200, "text/html", resetMsg);
  delay(1000);
  digitalWrite(2, LOW);
  LittleFS.begin();
  LittleFS.format();
  LittleFS.end();
  WiFi.disconnect(false, true);
  delay(1000);
  ESP.restart();
}

void handleTimeSync() {
  //manually set local time
  syncTime();
  String page = "<HTML><head>";
  page += "<meta http-equiv=\"refresh\" content=\"0; url='http://" + baseIPAddress;
  if (modeCallingPage != "") {
    page += "/" + modeCallingPage;
  }
  page += "'\"><title>Syncing Time</title>\</head><body>";
  page += "Syncing time to NTP Server...<br><br>";
  page += "If you are not automatically redirected, follow this to <a href='http://" + baseIPAddress + "'> return to settings page</a>.";
  page += "</body></html>";
  server.send(200, "text/html", page);
}

void handleClockEdit() {
  int month = server.arg("month").toInt();
  int day = server.arg("day").toInt();
  int year = server.arg("year").toInt();
  int hour = server.arg("hour").toInt();
  int minute = server.arg("minute").toInt();
  int second = server.arg("second").toInt();
  //Output values to serial monitor if enabled
  #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
    Serial.println("Manual Time Set - web parms passed");
    Serial.print("month: ");
    Serial.println(month);
    Serial.print("day: ");
    Serial.println(day);
    Serial.print("year: ");
    Serial.println(year);
    Serial.print("hour: ");
    Serial.println(hour);
    Serial.print("min: ");
    Serial.println(minute);
    Serial.print("sec: ");
    Serial.println(second);
    Serial.print("gmt_offset: ");
    Serial.println(gmtOffsetHours);
    Serial.print("dst_offset: ");
    Serial.println(daylightOffsetHours);
  #endif
  //Update local time
  struct tm t;
  t.tm_year = year - 1900;
  t.tm_mon = month - 1;
  t.tm_mday = day;
  if (autoSync) {
    t.tm_hour = hour + (daylightOffsetHours * 1);  //- (gmtOffsetHours + daylightOffsetHours);  //need to deal with DST
  } else {
    t.tm_hour = hour;
  }
  t.tm_min = minute;
  t.tm_sec = second;
  rtc.setTimeStruct(t);

  String page = "<HTML><head>";
  page += "<meta http-equiv=\"refresh\" content=\"0; url='http://" + baseIPAddress;
  if (modeCallingPage != "") {
    page += "/" + modeCallingPage;
  }
  page += "'\"><title>Setting Time</title>\</head><body>";
  page += "Manaully setting time ...<br><br>";
  page += "If you are not automatically redirected, follow this to <a href='http://" + baseIPAddress + "'> return to settings page</a>.";
  page += "</body></html>";
  server.send(200, "text/html", page);
}

void handleCountdownEdit() {
  timerRunning = false; //stop any running time
  defaultCountdownMin = server.arg("countdownmin").toInt();
  defaultCountdownSec = server.arg("countdownsec").toInt();
  if (defaultCountdownMin > 59) defaultCountdownMin = 59;
  if (defaultCountdownSec > 59) defaultCountdownSec = 59;
  initCountdownMillis = ((defaultCountdownMin * 60) + defaultCountdownSec) * 1000;
  countdownMilliSeconds = initCountdownMillis;
  remCountdownMillis = initCountdownMillis;
  //Web output - just redirect back to calling page
  String page = "<HTML><head>";
  page += "<meta http-equiv=\"refresh\" content=\"0; url='http://" + baseIPAddress;
  if (modeCallingPage != "") {
    page += "/" + modeCallingPage;
  }
  page += "'\"><title>Setting Countdown Time</title>\</head><body>";
  page += "Manaully setting countdown time ...<br><br>";
  page += "If you are not automatically redirected, follow this to <a href='http://" + baseIPAddress + "'> return to settings page</a>.";
  page += "</body></html>";
  server.send(200, "text/html", page);
}

void handleCountdownToggle() {
  if (!timerRunning && remCountdownMillis > 0) {
    endCountDownMillis = millis() + remCountdownMillis;
    timerRunning = true;
  } else {
    timerRunning = false;
  }

  //Web output - just redirect back to calling page
  String page = "<HTML><head>";
  page += "<meta http-equiv=\"refresh\" content=\"0; url='http://" + baseIPAddress;
  if (modeCallingPage != "") {
    page += "/" + modeCallingPage;
  }
  page += "'\"><title>Toggling Countdown Timer</title>\</head><body>";
  page += "Manaully toggling countdown timer ...<br><br>";
  page += "If you are not automatically redirected, follow this to <a href='http://" + baseIPAddress + "'> return to settings page</a>.";
  page += "</body></html>";
  server.send(200, "text/html", page);
}

void handleCountdownBuzzer() {
  soundBuzzer(2000);  //Sound buzzer for 2000 milliseconds
  //Web output - just redirect back to calling page
  String page = "<HTML><head>";
  page += "<meta http-equiv=\"refresh\" content=\"0; url='http://" + baseIPAddress;
  if (modeCallingPage != "") {
    page += "/" + modeCallingPage;
  }
  page += "'\"><title>Sounding Buzzer</title>\</head><body>";
  page += "Manaully sounding buzzer...<br><br>";
  page += "If you are not automatically redirected, follow this to <a href='http://" + baseIPAddress + "'> return to settings page</a>.";
  page += "</body></html>";
  server.send(200, "text/html", page);
}

void handleScoreEdit() {
  scoreboardTeamLeft = server.arg("leftteam");
  scoreboardTeamRight = server.arg("rightteam");  
  scoreboardLeft = server.arg("scoreleft").toInt();
  scoreboardRight = server.arg("scoreright").toInt();
  //Web output - just redirect back to calling page
  String page = "<HTML><head>";
  page += "<meta http-equiv=\"refresh\" content=\"0; url='http://" + baseIPAddress;
  if (modeCallingPage != "") {
    page += "/" + modeCallingPage;
  }
  page += "'\"><title>Updating Scoreboard</title>\</head><body>";
  page += "Updating scoreboard...<br><br>";
  page += "If you are not automatically redirected, follow this to <a href='http://" + baseIPAddress + "'> return to settings page</a>.";
  page += "</body></html>";
  server.send(200, "text/html", page);
}
void handleScoreLeftPlus() {
  if (scoreboardLeft < 99) {
    scoreboardLeft++;
  }
  //Web output - just redirect back to calling page
  String page = "<HTML><head>";
  page += "<meta http-equiv=\"refresh\" content=\"0; url='http://" + baseIPAddress;
  if (modeCallingPage != "") {
    page += "/" + modeCallingPage;
  }
  page += "'\"><title>Updating Scoreboard</title>\</head><body>";
  page += "Updating scoreboard...<br><br>";
  page += "If you are not automatically redirected, follow this to <a href='http://" + baseIPAddress + "'> return to settings page</a>.";
  page += "</body></html>";
  server.send(200, "text/html", page);
}
void handleScoreLeftMinus() {
  if (scoreboardLeft > 0) {
    scoreboardLeft--;
  }
  //Web output - just redirect back to calling page
  String page = "<HTML><head>";
  page += "<meta http-equiv=\"refresh\" content=\"0; url='http://" + baseIPAddress;
  if (modeCallingPage != "") {
    page += "/" + modeCallingPage;
  }
  page += "'\"><title>Updating Scoreboard</title>\</head><body>";
  page += "Updating scoreboard...<br><br>";
  page += "If you are not automatically redirected, follow this to <a href='http://" + baseIPAddress + "'> return to settings page</a>.";
  page += "</body></html>";
  server.send(200, "text/html", page);
}
void handleScoreRightPlus() {
  if (scoreboardRight < 99) {
    scoreboardRight++;
  }
  //Web output - just redirect back to calling page
  String page = "<HTML><head>";
  page += "<meta http-equiv=\"refresh\" content=\"0; url='http://" + baseIPAddress;
  if (modeCallingPage != "") {
    page += "/" + modeCallingPage;
  }
  page += "'\"><title>Updating Scoreboard</title>\</head><body>";
  page += "Updating scoreboard...<br><br>";
  page += "If you are not automatically redirected, follow this to <a href='http://" + baseIPAddress + "'> return to settings page</a>.";
  page += "</body></html>";
  server.send(200, "text/html", page);
}
void handleScoreRightMinus() {
  if (scoreboardRight > 0) {
    scoreboardRight--;
  }
  //Web output - just redirect back to calling page
  String page = "<HTML><head>";
  page += "<meta http-equiv=\"refresh\" content=\"0; url='http://" + baseIPAddress;
  if (modeCallingPage != "") {
    page += "/" + modeCallingPage;
  }
  page += "'\"><title>Updating Scoreboard</title>\</head><body>";
  page += "Updating scoreboard...<br><br>";
  page += "If you are not automatically redirected, follow this to <a href='http://" + baseIPAddress + "'> return to settings page</a>.";
  page += "</body></html>";
  server.send(200, "text/html", page);
}
void handleScoreReset() {
  scoreboardLeft = 0;
  scoreboardRight = 0;
  //Web output - just redirect back to calling page
  String page = "<HTML><head>";
  page += "<meta http-equiv=\"refresh\" content=\"0; url='http://" + baseIPAddress;
  if (modeCallingPage != "") {
    page += "/" + modeCallingPage;
  }
  page += "'\"><title>Updating Scoreboard</title>\</head><body>";
  page += "Updating scoreboard...<br><br>";
  page += "If you are not automatically redirected, follow this to <a href='http://" + baseIPAddress + "'> return to settings page</a>.";
  page += "</body></html>";
  server.send(200, "text/html", page);
}

void handleTextEdit() {
  textTop = server.arg("texttop");
  textBottom = server.arg("textbottom");
  textEffect = server.arg("texteffect").toInt();
  textEffectSpeed = server.arg("textspeed").toInt();
  textEffectPeriod = int(1000 / textEffectSpeed);
  //Web output - just redirect back to calling page
  String page = "<HTML><head>";
  page += "<meta http-equiv=\"refresh\" content=\"0; url='http://" + baseIPAddress;
  if (modeCallingPage != "") {
    page += "/" + modeCallingPage;
  }
  page += "'\"><title>Updating Text Display</title>\</head><body>";
  page += "Updating text display...<br><br>";
  page += "If you are not automatically redirected, follow this to <a href='http://" + baseIPAddress + "'> return to settings page</a>.";
  page += "</body></html>";
  server.send(200, "text/html", page);
}

void handleWLEDToggle() {
  int success = 0;
  if (useWLED) {
    success = toggleWLED(!wledStateOn);
    String page = "<HTML><head>";
    if ((success >= 200) && (success <= 299)) {
      wledStateOn = !wledStateOn;
      if (wledStateOn) {
      //redirect to WLED web site
        page += "<meta http-equiv=\"refresh\" content=\"0; url='http://" + wledAddress;
        page += "'\"></head><body>";
        page += "Launching WLED Interface...<br><br>";
        page += "If you are not automatically redirected, follow this to <a href='http://" + wledAddress + "'> open the WLED controller page</a>.";
      } else {
        page += "<meta http-equiv=\"refresh\" content=\"0; url='http://" + baseIPAddress;
        page += "'\"></head><body>";
        page += "Powering off WLED...<br><br>";
        page += "If you are not automatically redirected, follow this to <a href='http://" + baseIPAddress + "'> return to the main settings</a>.";

      }
      page += "</body></html>";
      server.send(200, "text/html", page);

    } else {
      page += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";  //make page responsive
      page += "</head><body>";
      page += "<H2>Unable to Reach WLED Controller</H2><br>";
      page += "WLED Controller did not respond to power request.  Be sure to check the following:<br><br>\
        <ul>\
          <li>The WLED controller is powered on</li>\
          <li>The WLED controller is on the same WiFi network as the clock</li>\
          <li>The WLED controller IP address is correct (this may change depending upon your router/config)</li>\
        </ul><br><br>";
      page += "Status Code: " + String(success) + "<br><br>";

      page += "Return to <a href='http://" + baseIPAddress + "'> Main Clock Page</a>";
      page += "</body></html>";
      server.send(200, "text/html", page);
    }
  }
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

void handleOTAUpdate() {
  String ota_message = "<HTML>\
      <head>\
        <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\
        <title>Arduino OTA Mode</title>\
        <style>\
          body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
        </style>\
      </head>\
      <body>";
  ota_message += "<h1>VAR_APP_NAME Ready for upload...<h1><h3>Start upload from IDE now</h3><br>";
  ota_message += "If no communication is received after approximately 20 seconds, the system will exit OTA mode and return to normal operation.<br><br>\
      If a firmware update is successfully delivered, the controller will reboot.  You can return to the settings after the boot process completes.<br><br>";
  ota_message += "<a href=\"http://";
  ota_message += baseIPAddress;
  ota_message += "\">Return to settings</a><br>";
  ota_message += "</body></html>";

  ota_message.replace("VAR_APP_NAME", APPNAME);
  server.send(200, "text/html", ota_message);
  ota_flag = true;
  ota_time = ota_time_window;
  ota_time_elapsed = 0;
}

void handleFirmwareUpdate() {
  size_t fsize = UPDATE_SIZE_UNKNOWN;
  if (server.hasArg("size")) {
    fsize = server.arg("size").toInt();
  }
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("Receiving Update: %s, Size: %d\n", upload.filename.c_str(), fsize);
    if (!Update.begin(fsize)) {
      web_otaDone = 0;
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    } else {
      web_otaDone = 100 * Update.progress() / Update.size();
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.printf("Update Success: %u bytes\nRebooting...\n", upload.totalSize);
    } else {
      Serial.printf("%s\n", Update.errorString());
      web_otaDone = 0;
    }
  }
}

void handleWebUpdateEnd() {
  server.sendHeader("Connection", "close");
  if (Update.hasError()) {
    server.send(502, "text/plain", Update.errorString());
  } else {
    String result = "";
    result += "<html><head></head>";
    result += "<body>";
    result += "Success!  Board will now reboot.";
    result += "</body></html>";
    //server.send(200, "text/html", result);
    server.sendHeader("Refresh", "10");
    server.sendHeader("Location", "/");
    server.send(307, "text/html", result);
    ESP.restart();
  }
}

void handleWebUpdate() {
  size_t fsize = UPDATE_SIZE_UNKNOWN;
  if (server.hasArg("size")) {
    fsize = server.arg("size").toInt();
  }
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
      Serial.printf("Receiving Update: %s, Size: %d\n", upload.filename.c_str(), fsize);
    #endif
    if (!Update.begin(fsize)) {
      web_otaDone = 0;
      #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
        Update.printError(Serial);
      #endif
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)  
        Update.printError(Serial);
      #endif
    } else {
      web_otaDone = 100 * Update.progress() / Update.size();
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
        Serial.printf("Update Success: %u bytes\nRebooting...\n", upload.totalSize);
      #endif
    } else {
      #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
        Serial.printf("%s\n", Update.errorString());
      #endif
      web_otaDone = 0;
    }
  }
}

// Handle Mode Changes (and return to calling page)
void changeModeClock() {
  clockMode = 0;
  allBlank();
  if (!binaryClock) {
    updateTemperature();
    updateTemperatureExt(false);
  }
  String page = "<HTML><head>";
  page += "<meta http-equiv=\"refresh\" content=\"0; url='http://" + baseIPAddress;
  if (modeCallingPage != "") {
    page += "/" + modeCallingPage;
  }
  page += "'\"></head><body>";
  page += "Changing to clock mode...<br><br>";
  page += "If you are not automatically redirected, follow this to <a href='http://" + baseIPAddress + "'> return to settings page</a>.";
  page += "</body></html>";
  server.send(200, "text/html", page);
}

void changeModeCountdown() {
  clockMode = 1;
  String page = "<HTML><head>";
  page += "<meta http-equiv=\"refresh\" content=\"0; url='http://" + baseIPAddress;
  if (modeCallingPage != "") {
    page += "/" + modeCallingPage;
  }
  page += "'\"></head><body>";
  page += "Changing to coutdown mode...<br><br>";
  page += "If you are not automatically redirected, follow this to <a href='http://" + baseIPAddress + "'> return to settings page</a>.";
  page += "</body></html>";
  server.send(200, "text/html", page);
}

void changeModeScoreboard() {
  clockMode = 2;
  String page = "<HTML><head>";
  page += "<meta http-equiv=\"refresh\" content=\"0; url='http://" + baseIPAddress;
  if (modeCallingPage != "") {
    page += "/" + modeCallingPage;
  }
  page += "'\"></head><body>";
  page += "Changing to scoreboard mode...<br><br>";
  page += "If you are not automatically redirected, follow this to <a href='http://" + baseIPAddress + "'> return to settings page</a>.";
  page += "</body></html>";
  server.send(200, "text/html", page);
}

void changeModeText() {
  clockMode = 3;
  String page = "<HTML><head>";
  page += "<meta http-equiv=\"refresh\" content=\"0; url='http://" + baseIPAddress;
  if (modeCallingPage != "") {
    page += "/" + modeCallingPage;
  }
  page += "'\"></head><body>";
  page += "Changing to text mode...<br><br>";
  page += "If you are not automatically redirected, follow this to <a href='http://" + baseIPAddress + "'> return to settings page</a>.";
  page += "</body></html>";
  server.send(200, "text/html", page);
}

// ----------------------------
//  Handle API (http) Requests
// ----------------------------
void handleApiBrightness(String value) {
  String result = "Invalid command value received: " + value ;
  int response = 400;
  if (isValidNumber(value)) {
    int newBrightness = value.toInt();
    if ((newBrightness >= 0) && (newBrightness <= 255)) {
      brightness = newBrightness;
      result = "Brightness request sent successfully";
      response = 200;
    }
  }
  server.send(response, "text/html", result);
}

void handleApiMode(String value) {
  String result = "Invalid command value received: " + value;
  int response = 400;
  if (isValidNumber(value)) {
    int newMode = value.toInt();
    if ((newMode >= 0) && (newMode <= 3)) {
      clockMode = newMode;
      result = "Display Mode request successfully sent";
      response = 200;
    }
  }
  server.send(response, "text/html", result);
}

void handleApiWled(String value) {
  //Will accept 0/1 or on/off
  String result = "Invalid command value received: " + value;
  int response = 400;
  bool turnOn = false;
  if (value == "on") {
    turnOn = true;
    response = 200;
  } else if (value == "off") {
    turnOn = false;
    response = 200;
  } else if (value == "toggle") {
    turnOn = !(wledOn());
    response = 200;
  } else if (isValidNumber(value)) {
    if ((value.toInt()) == 1) {
      turnOn = true;
      response = 200;
    } else if ((value.toInt()) == 0) {
      turnOn = false;
      response = 200;
    } else {
      //Just toggle with any numeric value other than 0 or 1
      turnOn = !(wledOn());
      response = 200;
    }
  }
  if (response == 200) {
    toggleWLED(turnOn);
    result = ">WLED command sent successfully";
  }
  server.send(response, "text/html", result);
}

void handleApiBinary (String value) {
  String result = "Invalid command value received: " + value;
  int response = 400;
  //Only apply if display is in clock mode (0)
  if (clockMode == 0) {
    oldMode = 99;   //Clears display and redraws temperature(s) when binary switched off
    if (value == "on") {
      binaryClock = true;
      response = 200;
    } else if (value == "off") {
      binaryClock = false;
      response = 200;
    } else if (value == "toggle") {
      binaryClock = !binaryClock;
      response = 200;
    }
  }
  if (response == 200) {
    result = "Binary command sent successfully";
  }
  server.send(response, "text/html", result);

}

void handleApiCountdown (String value) {
  //valid params are "start", "stop" and "reset"
  String result = "Invalid command value received: " + value;
  int response = 400;
  if (clockMode == 1) {
    if (value == "start") {
      //Only start if not already running
      if ((!timerRunning && remCountdownMillis > 0)) {
        endCountDownMillis = millis() + remCountdownMillis;
        timerRunning = true;
        response = 200;
      } else {
        result = "Countdown is already running or no time remains";
      }
    } else if (value == "stop") {
      timerRunning = false;
      response = 200;
    } else if (value == "toggle") {
      if ((!timerRunning && remCountdownMillis > 0)) {
        endCountDownMillis = millis() + remCountdownMillis;
        timerRunning = true;
        response = 200;
      } else {
        timerRunning = false;
        response = 200;
      }
    } else if (value == "reset") {
      timerRunning = false;
      initCountdownMillis = ((defaultCountdownMin * 60) + defaultCountdownSec) * 1000;
      countdownMilliSeconds = initCountdownMillis;
      remCountdownMillis = initCountdownMillis;
      response = 200;
    }  
  } else {
    result = "System not in required countdown mode";
  }
  if (response == 200) {
    result = "Countdown request sent successfully.</h2>";
  }
  server.send(response, "text/html", result);
}

void handleApiBuzzer(String value) {
  String result = "Invalid command value received: " + value;
  int response = 400;
  if (isValidNumber(value)) {
    int length = value.toInt();
    if (length >= 0) {
      if (length > 5) length = 5;
      soundBuzzer(length * 1000);  //length in milliseconds
      result = "Buzzer command sent successfully";
      response = 200;
    }
  }
  server.send(response, "text/html", result);
}

void handleApiLeftScore(String value) {
  String result = "Invalid command value received: " + value;
  int response = 400;
  if (isValidNumber(value)) {
    int newScore = value.toInt();
    if ((newScore >= 0) && (newScore <= 99)) {
      scoreboardLeft = newScore;
      response = 200;
      result = "Left score value sent successfully";
    }
  }
  server.send(response, "text/html", result);
}

void handleApiRightScore(String value) {
  String result = "Invalid command value received: " + value;
  int response = 400;
  if (isValidNumber(value)) {
    int newScore = value.toInt();
    if ((newScore >= 0) && (newScore <= 99)) {
      scoreboardRight = newScore;
      response = 200;
      result = "Right score value sent successfully";
    }
  }
  server.send(response, "text/html", result);
}

void handleApiTextTop(String value) {
  String result = "Invalid command value received: " + value;
  int response = 400;
  if (value == "\"\"") {  //clear out value
    textTop = "";
    response = 200;
  } else {
    textTop = value.substring(0, 6);
    response = 200;
  }
  if (response = 200) {
    result = "Top text value sent successfully";
  }
  server.send(response, "text/html", result);
}

void handleApiTextBottom(String value) {
  String result = "Invalid command value received: " + value;
  int response = 400;
  if (value == "\"\"") {  //clear out value
    textBottom = "";
    response = 200;
  } else {
    textBottom = value.substring(0, 6);
    response = 200;
  }
  if (response = 200) {
    result = "Bottom text value sent successfully";
  }
  server.send(response, "text/html", result);
}

void handleApiTextEffect (String value) {
  String result = "Invalid command value received: " + value;
  int response = 400;
  if (isValidNumber(value)) {
    int newEffect = value.toInt();
    if ((newEffect >= 0) && (newEffect <= 7)) {
      textEffect = newEffect;
      response = 200;
      result = "Text effect request sent successfully";
    }
  }
  server.send(response, "text/html", result);
}

void handleApiTextSpeed (String value) {
  String result = "Invalid command value received: " + value;
  int response = 400;
  if (isValidNumber(value)) {
    int newSpeed = value.toInt();
    if ((newSpeed > 0) && (newSpeed <= 10)) {
      textEffectSpeed = newSpeed;
      textEffectPeriod = int(1000 / textEffectSpeed);
      response = 200;
      result = "Text speed request sent successfully";
    }
  }
  server.send(response, "text/html", result);
}

void handleApiIntTemp(String value) {
  String result = "Invalid command value received: " + value;
  int response = 400;
  if (tempIntUseApi) {
    if (isValidNumber(value)) {
      int newTemp = value.toInt();
      if ((newTemp > -100) && (newTemp < 200)) {
        internalTemperature = newTemp;
        //Set refresh counter to zero so that temp updates on next display cycle
        tempUpdateCount = 0;
        response = 200;
        result = "Internal temperature update sent successfully";
      }
    }
  } else {
    result = "Local API for internal temperature not enabled in settings.  Command ignored.";
  }
  server.send(response, "text/html", result);

}

void handleApiExtTemp(String value) {
  String result = "Invalid command value received: " + value;
  int response = 400;
  if (tempExtUseApi) {
    if (isValidNumber(value)) {
      int newTemp = value.toInt();
      if ((newTemp > -100) && (newTemp < 200)) {
        externalTemperature = newTemp;
        //Set refresh counter to zero so that temp updates on next display cycle
        tempUpdateCountExt = 0;
        response = 200;
        result = "External temperature update sent successfully";
      }
    }
  } else {
    result = "Local API for external temperature not enabled in settings.  Command ignored.";
  }
  server.send(response, "text/html", result);
}

void handleApiSystem(String value) {
  String result = "Invalid command value received: " + value;
  int response = 400;
  if (value == "off") {
    toggleLEDs(false);
    response = 200;
    result = "System command 'off' sent successfully";
  } else if (value == "on") {
    toggleLEDs(true);
    response = 200;
    result = "System command 'on' sent successfully";
  } else if (value == "toggle") {
    toggleLEDs(!ledsOn);
    response = 200;
    result = "System command 'off' sent successfully";
  } else if (value == "restart") {
    response = 200;
    result = "System command 'restart' sent successfully";
    server.send(response, "text/html", result);  //must send before reboot
    delay(1000);
    ESP.restart(); 
  } else if (value == "otaupdate") {
    ota_flag = true;
    ota_time = ota_time_window;
    ota_time_elapsed = 0;
    response = 200;
    result = "System command 'OTA Update' sent successfully";
  }
  server.send(response, "text/html", result);
}

void handleApiState() {
  String result = "";
  int response = 400;
  //Build response as JSON with current system states
  //Current running state
  result += "{\"power\":\"";
  if (ledsOn) {
    result += "on\",";
  } else {
    result += "off\",";
  }
  result += "\"displaymode\":\"" + String(clockMode) + "\",";
  result += "\"wled\":\"";
  if (wledOn()) {
    result += "on\",";
  } else {
    result += "off\",";
  }
  result += "\"binaryclock\":\"";
  if (binaryClock) {
    result += "on\",";
  } else {
    result += "off\",";
  }
  result += "\"brightness\":\"" + String(brightness) + "\",";
  result += "\"countdown\":\"";
  if (timerRunning) {
    result += "running\",";
  } else {
    result += "stopped\",";
  }
  result += "\"scoreleft\":\"" + String(scoreboardLeft) + "\",";
  result += "\"scoreright\":\"" + String(scoreboardRight) + "\",";
  result += "\"texttop\":\"" + textTop + "\",";
  result += "\"textbottom\":\"" + textBottom + "\",";
  result += "\"texteffect\":\"" + String(textEffect) + "\",";
  result += "\"textspeed\":\"" + String(textEffectSpeed) + "\",";
  //Config values as nested 'config' object/key
  result += "\"config\": {";
    result += "\"devicename\":\"" + deviceName + "\",";
    result += "\"maxmilliamps\":\"" + String(milliamps) + "\",";
    result += "\"bootmode\":\"" + String(defaultClockMode) + "\",";
    result += "\"numfont\":\"" + String(numFont) + "\",";
    result += "\"hours\":\"" + String(hourFormat) + "\",";
    if (useCustomOffsets) {
      result += "\"timezone\":\"custom\",";
      result += "\"gmtoffset\":\"" + String(gmtOffsetHours) + "\",";
      result += "\"dstoffset\":\"" + String(dstOffsetHours) + "\",";
    } else {
      result += "\"timezone\":\"" + timeZone + "\",";
      result += "\"gmtoffset\":\"na\",";
      result += "\"dstoffset\":\"na\",";
    } 
    if (autoSync) {
      result += "\"autosync\":\"on\",";
      result += "\"ntpserver\":\"" + ntpServer + "\",";
      result += "\"timesyncinterval\":\"" + String(autoSyncInterval) + "\",";
    } else {
      result += "\"autosync\":\"off\",";
      result += "\"ntpserver\":\"na\",";
      result += "\"timesyncinterval\":\"0\",";
    }
    //Temperature settings
    result += "\"tempunits\":\"";
    if (temperatureSymbol == 12) {
      result += "C\",";
    } else {
      result += "F\",";
    }
    result += "\"tempdisplay\":\"";
    if (temperatureSource == 0) {
      result += "dual\",";
    } else if (temperatureSource == 1) {
      result += "outside\",";
    } else {
      result += "inside\",";
    }
    //Temperature sources
    result += "\"inttempsource\":\"";
    if (tempIntUseApi) {
      result += "api\",";
    } else {
      result += "sensor\",";
    }
    result += "\"tempcorrection\":\"" + String(temperatureCorrection) + "\",";
    result += "\"inttempinterval\":\"" + String(tempUpdatePeriod) + "\",";

    result += "\"exttempsource\":\"";
    if (tempExtUseApi) {
      result += "api\",";
    } else {
      result += "owm\",";
    }
    //Mask OWM Key
    result += "\"owmkey\":\"";
    if (owmKey.length() > 4) {
      result += owmKey.substring(0, 4) + "********\",";
    } else {
      result += owmKey + "\",";
    }
    result += "\"owmlat\":\"" + String(owmLat) + "\",";
    result += "\"owmlong\":\"" + String(owmLong) + "\",";
    result += "\"exttempinterval\":\"" + String(tempUpdatePeriodExt) + "\",";

    result += "\"countmin\":\"" + String(defaultCountdownMin) + "\",";
    result += "\"countsec\":\"" + String(defaultCountdownSec) + "\",";
    result += "\"buzzer\":\"";
    if (useBuzzer) {
      result += "on\",";
    } else {
      result += "off\",";      
    }

    result += "\"teamleft\":\"" + scoreboardTeamLeft + "\",";
    result += "\"teamright\":\"" + scoreboardTeamRight + "\",";
  
    result += "\"wledip\":\"" + wledAddress + "\",";
    result += "\"wledpresets\":\"" + String(wledMaxPreset) + "\",";
   //Colors - nested under [config][colors]
    result += "\"colors\": {";
      result += "\"time\":\"" + WebColors[webColorClock] + "\",";
      result += "\"tempint\":\"" + WebColors[webColorTemperatureInt] + "\",";
      result += "\"tempext\":\"" + WebColors[webColorTemperatureExt] + "\",";
      result += "\"countactive\":\"" + WebColors[webColorCountdown] + "\",";
      result += "\"countpaused\":\"" + WebColors[webColorCountdownPaused] + "\",";
      result += "\"countfinalmin\":\"" + WebColors[webColorCountdownFinalMin] + "\",";
      result += "\"scoreleft\":\"" + WebColors[webColorScoreboardLeft] + "\",";
      result += "\"scoreright\":\"" + WebColors[webColorScoreboardRight] + "\",";
      result += "\"texttop\":\"" + WebColors[webColorTextTop] + "\",";
      result += "\"textbottom\":\"" + WebColors[webColorTextBottom] + "\"";
  result += "}}}";
  response = 200;
  server.send(response, "text/html", result);

}

// ----------------------------
//  Setup Web Handlers
// -----------------------------
void setupWebHandlers() {
  //Main pages
  server.on("/", webMainPage);
  server.on("/clockedit", webClockPage);
  server.on("/countdownedit", webCountdownPage);
  server.on("/scoreedit", webScorePage);
  server.on("/textedit", webTextPage);
  server.on("/webupdate", webFirmwareUpdate);
  server.on(
    "/update", HTTP_POST,
    []() {
      handleWebUpdateEnd();
    },
    []() {
      handleWebUpdate();
    }
  );

 //Change modes
  server.on("/clock", changeModeClock);
  server.on("/countdown", changeModeCountdown);
  server.on("/scoreboard", changeModeScoreboard);
  server.on("/text", changeModeText);
  //Handlers/process actions
  server.on("/toggleleds", handleLEDToggle);
  server.on("/applysettings", handleSettingsUpdate);
  server.on("/onboard", handleOnboard);
  server.on("/restart", handleRestart);
  server.on("/reset", handleReset);
  server.on("/timesync", handleTimeSync);
  server.on("/clockupdate", handleClockEdit);
  server.on("/countdownupdate", handleCountdownEdit);
  server.on("/countdowntoggle", handleCountdownToggle);
  server.on("/countdownbuzzer", handleCountdownBuzzer);
  server.on("/scoreupdate", handleScoreEdit);
  server.on("/scoreleftplus", handleScoreLeftPlus);
  server.on("/scoreleftminus", handleScoreLeftMinus);
  server.on("/scorerightplus", handleScoreRightPlus);
  server.on("/scorerightminus", handleScoreRightMinus);
  server.on("/scorereset", handleScoreReset);
  server.on("/textupdate", handleTextEdit);
  server.on("/togglewled", handleWLEDToggle);

  //API URL Calls
  server.on("/api/state", handleApiState);
  server.on("/api", HTTP_GET, []()
    {
      int queryNum = 0;
      String queryString = "";
      for (int i = 0; i < server.args(); i++) {
        if (server.argName(i) == "brightness") {
          handleApiBrightness(server.arg(i));
        } else if (server.argName(i) == "mode") {
          handleApiMode(server.arg(i));
        } else if (server.argName(i) == "binaryclock") {
          handleApiBinary(server.arg(i));
        } else if ((server.argName(i) == "wled") && (useWLED)) {
          handleApiWled(server.arg(i));
        } else if (server.argName(i) == "countdown") {
          handleApiCountdown(server.arg(i));
        } else if (server.argName(i) == "buzzer") {
          handleApiBuzzer(server.arg(i));
        } else if (server.argName(i) == "leftscore") {
          handleApiLeftScore(server.arg(i));
        } else if (server.argName(i) == "rightscore") {
          handleApiRightScore(server.arg(i));
        } else if (server.argName(i) == "texttop") {
          handleApiTextTop(server.arg(i));
        } else if (server.argName(i) == "textbottom") {
          handleApiTextBottom(server.arg(i));
        } else if (server.argName(i) == "texteffect") {
          handleApiTextEffect(server.arg(i));
        } else if (server.argName(i) == "textspeed") {
          handleApiTextSpeed(server.arg(i));
        } else if (server.argName(i) == "tempext") {
          handleApiExtTemp(server.arg(i));
        } else if (server.argName(i) == "tempint") {
          handleApiIntTemp(server.arg(i));
        } else if (server.argName(i) == "system") {
          handleApiSystem(server.arg(i)); 
        } else {
          //Just return error to client
          server.send(400, "text/html", "<h2>Invalid parameter or value passed<h2>");
        }
      }
    }

  );

  server.onNotFound(handleNotFound);
  //OTAUpdate via IDE
  server.on("/otaupdate", handleOTAUpdate);
}

/* ------------------------------------
    Rotary Knob/Click Callbacks
   ----------------------------------- */
void rotaryKnobCallback(long value) {
  //Don't do anything if either flag is set (no action yet)
	if( turnedRightFlag || turnedLeftFlag )
		return;
  //Set a flag that will be watched for in Loop();
	switch( value )
	{
		case 1:
	  		turnedRightFlag = true;
		break;

		case -1:
	  		turnedLeftFlag = true;
		break;
	}
  //Set tracked value back to zero for next tracking
  rotaryEncoder.setEncoderValue( 0 );
}

/* =====================================
    WIFI SETUP 
   =====================================
*/
void setupSoftAP() {
  //for onboarding
  WiFi.mode(WIFI_AP);
  WiFi.softAP(deviceName + "_AP");
  IPAddress Ip(192, 168, 4, 1);
  IPAddress NMask(255, 255, 255, 0);
  WiFi.softAPConfig(Ip, Ip, NMask);
#if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
  Serial.println("SoftAP Created");
  Serial.println("Web server starting...");
#endif
  server.begin();
}

bool setupWifi() {
  byte count = 0;
  //attempt connection
  //if successful, return true else false
  delay(200);
  WiFi.hostname(wifiHostName);
  WiFi.begin();
  while (WiFi.status() != WL_CONNECTED) {
#if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
    Serial.print(".");
#endif
    // Stop if cannot connect
    if (count >= 60) {
// Could not connect to local WiFi
#if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
      Serial.println();
      Serial.println("Could not connect to WiFi.");
#endif
      return false;
    }
    delay(500);
    yield();
    count++;
  }
  //Successfully connected
  baseIPAddress = WiFi.localIP().toString();
  WiFi.macAddress(macAddr);
  strMacAddr = WiFi.macAddress();
#if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
  Serial.println("Connected to wifi... yay!");
  Serial.print("MAC Address: ");
  Serial.println(strMacAddr);
  Serial.print("IP Address: ");
  Serial.println(baseIPAddress);
  Serial.println("Starting web server...");
#endif
  server.begin();
  return true;
}

// ============================================
//   MAIN SETUP
// ============================================
void setup() {
#if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
  Serial.begin(115200);
  Serial.println("Starting setup...");
#endif
  esp_netif_init();
  setupWebHandlers();
  delay(500);

  defineColors();  //This must be done before reading config file
  readConfigFile();

  if (onboarding) {
    #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
        Serial.println("Entering Onboarding setup...");
    #endif
        setupSoftAP();
  } else if (!setupWifi()) {
    #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
        Serial.println("Wifi connect failed. Reentering onboarding...");
    #endif
    setupSoftAP();
    onboarding = true;
  } else {
    //Connected to Wifi
    //Rest of normal setup here
    //Output for onboard LED - indicates WiFi connection
    pinMode(2, OUTPUT);
    digitalWrite(2, HIGH);
    //If WLED controller defined, assure WLED starts in OFF state (fix for WLED issue powering on at boot despite settings)
    if (useWLED) {
      int success = toggleWLED(false);
      wledStateOn = false;
      delay(100);
    }
  }
  //Setup hardware GPIO pins
  pinMode(BUZZER_OUTPUT, OUTPUT);
  pinMode(MODE_PIN, INPUT_PULLUP);   //white
  pinMode(GREEN_PIN, INPUT_PULLUP);  //green
  pinMode(RED_PIN, INPUT_PULLUP);    //red
  pinMode(ENCODER_SW, INPUT);        //Rotary button push
  delay(200);
  //Setup LEDs
  FastLED.addLeds<WS2812B, LED_DATA_PIN, GRB>(LEDs, NUM_LEDS);
  FastLED.setDither(false);
  FastLED.setCorrection(TypicalLEDStrip);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, milliamps);
  FastLED.setBrightness(brightness);
  fill_solid(LEDs, NUM_LEDS, CRGB::Black);
  FastLED.show();

  if (onboarding) {
    //Display "Join WiFi" on display so user knows onboarding is required
    String holdTop = textTop;
    String holdBottom = textBottom;
    textTop = "&JOIN";
    textBottom = "&WIFI";
    updateText();
    textTop = holdTop;
    textBottom = holdBottom;
    FastLED.show();
    delay(1000);

  } else {
    //OTA Updates
    ArduinoOTA.setHostname(otaHostName.c_str());
    ArduinoOTA.onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH) {
        type = "sketch";
      } else {  // U_FS
        type = "filesystem";
      }
      // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    });
    ArduinoOTA.begin();

    //I2C Buses:
    bus1.begin(BUS1_SDA, BUS1_SCL, 100000);  //light level
    bus2.begin(BUS2_SDA, BUS2_SCL, 100000);  //temp/humidity

    //Initialize time server
    if (autoSync) {
      sntp_set_sync_interval(autoSyncInterval * 60000);  //convert to ms
      if (useCustomOffsets) {
        configTime(gmtOffsetHours * 3600, DST_OFFSET * 3600, ntpServer.c_str());
      } else {
        configTime(0, 0, ntpServer.c_str());  //offset in seconds
        setenv("TZ", timeZone.c_str(), 1);
        tzset();
      }
      //Get initial time
      syncTime();
    } else {
      //Manually set time to midnight, Jan 1, 2024
      manualTimeSet(0, 0, 0, 1, 1, 2024);
    }
    delay(200);

    //Initialize AHT20 Temp/Humidity
    uint8_t status = aht.begin();
    #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
      Serial.print("AHT20 Begin Status: ");
      Serial.println(status);
    #endif  
    delay(100);
    aht.startMeasurementReady(true);

    // Set Text Effect Speed
    if (textEffectSpeed <= 10) {  //10 is max speed
      textEffectPeriod = int(1000 / textEffectSpeed);
    } else {
      textEffectPeriod = 100;
    }

    //Rotary Encoder
    rotaryEncoder.setEncoderType( EncoderType::HAS_PULLUP );  //Encoder board has pullup resistors
    rotaryEncoder.setBoundaries( -1, 1, false );              //Will return -1, 0, 1 and will not wrap
    rotaryEncoder.onTurned( &rotaryKnobCallback );            //Callback whenever knob is moved
    rotaryEncoder.begin();

    //Set buzzer GPIO pin to low (off)
    digitalWrite(BUZZER_OUTPUT, LOW);

    //Briefly display IP Address
    String holdTop = textTop;
    String holdBottom = textBottom;
    textTop = "IP:";
    int ipLen = baseIPAddress.length();
    if (ipLen > 6) {
      int dispLen = (baseIPAddress.length() - 6);
      textBottom = baseIPAddress.substring(dispLen);
    } else {
      textBottom = baseIPAddress;
    }
    updateText();
    FastLED.show();
    delay(1500);
    textTop = holdTop;
    textBottom = holdBottom;
    fill_solid(LEDs, NUM_LEDS, CRGB::Black);
    FastLED.show();

    //Update temperatures if showing clock (and not binary mode)
    if ((clockMode == 0) && (!binaryClock)) {
      updateTemperature();
      updateTemperatureExt(true);
    }
  }
#if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
  Serial.println("Setup complete. Entering main loop...");
#endif
};

// ===============================================================
//   MAIN LOOP
// ===============================================================
void loop() {
  // When OTA flag set via HTML call, time to upload set at 20 sec. via server callback above.  Alter there if more time desired.
  if ((ota_flag) && (!onboarding)) {
    displayOTA();
    uint16_t ota_time_start = millis();
    while (ota_time_elapsed < ota_time) {
      ArduinoOTA.handle();
      ota_time_elapsed = millis() - ota_time_start;
      delay(10);
    }
    ota_flag = false;
    tempUpdateCount = 0;
    updateDisplay(false);  //don't poll external temp
  }

  server.handleClient();

  if (!onboarding) {
    int modeReading = digitalRead(MODE_PIN);       //White (top) button
    int v1Reading = digitalRead(GREEN_PIN);        //Green (center) button
    int h1_Reading = digitalRead(RED_PIN);         //Red (bottom) button
    int rotBtn_Reading = digitalRead(ENCODER_SW);  //Rotary button

    //Debounce rotary button
    if (rotBtn_Reading != lastButtonState) {
      lastDebounceTime = millis();
    }
    //If pressed past debounce, then toggle LEDs off/on
    if ((millis() - lastDebounceTime) > debounceDelay) {
      startButtonState = rotBtn_Reading;
      if (!startButtonState) {
        if ((wledStateOn) || (wledOn())) {
          toggleWLED(false);
          wledStateOn = false;
        } else {
          toggleLEDs(!ledsOn);
        }
      }
    }
    lastButtonState = rotBtn_Reading;

    if (clockMode != 1) timerRunning = false;
    // Mode Button (White/top)
    if (modeReading == LOW) { 
      if ((wledStateOn) || (wledOn())) {
        //Set to first preset
        int success = wledPresetFirst();
      } else {
        clockMode = clockMode + 1; 
        delay(500);
      }
    }
    if (clockMode > 3) {         
      clockMode = 0;
      tempUpdateCount = 0;
      tempUpdateCountExt = 0;
    }

    //V+ Button (Green/middle): Increase visitor score / Toggle timer (run/stop) - Previous preset if WLED active
    if (v1Reading == LOW && h1_Reading == !LOW) {
      if ((wledStateOn) || (wledOn())) {
         int success = wledPresetAdvance(false);
      } else {
        if (clockMode == 2) {
          scoreboardLeft = scoreboardLeft + 1;
          if (scoreboardLeft > 99)
            scoreboardLeft = 0;
        } else if (clockMode == 1) {
          if (!timerRunning && remCountdownMillis > 0) {
            endCountDownMillis = millis() + remCountdownMillis;
            timerRunning = true;
          } else if (timerRunning) {
            timerRunning = false;  
          }  
        }
      }
    delay(500);
    }

    //H+ Button (Red/bottom): Increase home score / Reset timer to starting value - Next preset if WLED active
    if (h1_Reading == LOW && v1Reading == !LOW) {
      if ((wledStateOn) || (wledOn())) {
        int success = wledPresetAdvance(true);
      } else {
        if (clockMode == 2) {
          scoreboardRight = scoreboardRight + 1;
          if (scoreboardRight > 99) 
            scoreboardRight = 0;
        } else if (clockMode == 1) {
          if (!timerRunning) {
            // Reset timer to last start value
            countdownMilliSeconds = initCountdownMillis;
            remCountdownMillis = initCountdownMillis;
            endCountDownMillis = countdownMilliSeconds + millis();
          } else {
            timerRunning = false;
          }
        }
      }
      delay(500);
    }

    //V+ and H+ buttons (green + red): Reset scoreboard scores / clear starting time - Not used for WLED
    if (v1Reading == LOW && h1_Reading == LOW) {
      if (clockMode == 2) {
        scoreboardLeft = 0;
        scoreboardRight = 0;
      } else if (clockMode == 1  && !timerRunning) {
        countdownMilliSeconds = 0;
        endCountDownMillis = 0;
        remCountdownMillis = 0;
        initCountdownMillis = 0;
      }
    delay(500);
    }
    
    //Look for rotary knob (brightness) change
    if (turnedRightFlag)
      rotaryTurnedRight();
    else if (turnedLeftFlag)
      rotaryTurnedLeft();

    unsigned long currentMillis = millis(); 
    // Text Effect processing
    //Flash, Flash Alternate, Fade In and Fade Out
    if ((clockMode == 3) && (textEffect > 0)) {
      yield();
      if ((currentMillis - prevTime) >= textEffectPeriod) {
        prevTime = currentMillis;
        switch (textEffect) {
          case 1:   //Flash
            if (textEffect != oldTextEffect){
              allBlank();
              oldTextEffect = textEffect;
            }
            updateTextFlash();
            FastLED.setBrightness(brightness);
            break;
          case 2:   //FlashAlternate
            if (textEffect != oldTextEffect){
              allBlank();
              oldTextEffect = textEffect;
            }
            updateTextFlashAlternate();
            FastLED.setBrightness(brightness);
            break;
          case 3:  //Fade In
            if (textEffect != oldTextEffect) {
              allBlank();
              effectBrightness = 0;
              oldTextEffect = textEffect;
            }
            updateTextFadeIn();
            FastLED.setBrightness(effectBrightness);
            break;
          case 4:  //Fade Out
            if (textEffect != oldTextEffect) {
              allBlank();
              effectBrightness = brightness;
              oldTextEffect = textEffect;
            }
            updateTextFadeOut();
            FastLED.setBrightness(effectBrightness);
            break;
          case 5:  //Appear
            if (textEffect != oldTextEffect) {
              allBlank();
              appearCount = 99;
              oldTextEffect = textEffect;
            }
            updateTextAppear(false);
            FastLED.setBrightness(brightness);
            break;
          case 6:  //Appear Flash
            if (textEffect != oldTextEffect) {
              allBlank();
              appearCount = 99;
              oldTextEffect = textEffect;
            }
            updateTextAppear(true);
            FastLED.setBrightness(brightness);
            break;
          case 7:  //Rainbow (random letter colors)
            if (textEffect != oldTextEffect){
              allBlank();
              oldTextEffect = textEffect;
            }
            updateTextRainbow();
            FastLED.setBrightness(brightness);
            break;
          default:
            textEffect = 0;
            break;
        }
        FastLED.show();
      }
    } else if (currentMillis - prevTime >= 1000) {
      prevTime = currentMillis;
      
      if (oldMode != clockMode) {  //If mode has changed, clear the display first
        allBlank();
        oldMode = clockMode;
        if ((clockMode == 0) && (!binaryClock)) {
          updateTemperature();
          updateTemperatureExt(false);
        }
      }

      if (clockMode == 0) {
        if (binaryClock) {
          updateBinaryClock();
        } else { 
          updateClock();
          if (tempUpdateCount <= 0) {
            updateTemperature(); 
          }
          tempUpdateCount -= 1;
          if (tempUpdateCountExt <= 0) {
            updateTemperatureExt(true);
          }
          tempUpdateCountExt -= 1;
        }
      } else if (clockMode == 1) {
        updateCountdown();
      } else if (clockMode == 2) {
        updateScoreboard();            
      } else if (clockMode == 3) {
        updateText();
      }

      FastLED.setBrightness(brightness);
      FastLED.show();
    }
  }
}

// ===============================================================
//   UPDATE FUNCTIONS
// ===============================================================
void syncTime() {
  byte count = 0;
  byte maxCount = 10;
  bool gotTime = false;
  time(&now);            //read current time
  localtime_r(&now, &timeinfo);
  if ( timeinfo.tm_isdst ) {
    daylightOffsetHours = 1;
  } else {
    daylightOffsetHours = 0;
  }
  gotTime = true;

  if (gotTime) {
    rtc.setTimeStruct(timeinfo);
    #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
        Serial.println("Values from RTC:");
        Serial.print("Full Date/Time: ");
        Serial.println(rtc.getDateTime());
        Serial.print("Month: ");
        Serial.println(rtc.getMonth()+1);
        Serial.print("Day of Month: ");
        Serial.println(rtc.getDay());
        Serial.print("Year: ");
        Serial.println(rtc.getYear());
        Serial.print("Hour: ");
        Serial.println(rtc.getHour(true));
        Serial.print("Minute: ");
        Serial.println(rtc.getMinute());
        Serial.print("Second: ");
        Serial.println(rtc.getSecond());
        Serial.print("DST Offset: ");
        Serial.println(daylightOffsetHours);
    #endif
  } else {
    #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
        Serial.println("Failed to obtain time");
    #endif
  }
}

void manualTimeSet(int sec, int min, int hr, int day, int mon, int yr) {
  rtc.setTime(sec, min, hr, day, mon, yr);
}

void updateDisplay(bool refreshTemp) {
  //Called from settings page to refresh display with any changed values
  allBlank();
  switch (clockMode) {
    case 0:
      if (binaryClock) {
        updateBinaryClock();
      } else {
        updateClock();
        updateTemperature();
        updateTemperatureExt(refreshTemp);  //don't requery due to API limits, just refresh display if refreshTemp = false
      }
      break;
    case 1:
      updateCountdown();
      break;
    case 2:
      updateScoreboard();
      break;
    case 3:
      updateText();
      break;
  }
  FastLED.show();
}
void updateClock() {
  int hour = rtc.getHour(true);
  int mins = rtc.getMinute();
  int secs = rtc.getSecond();
  byte AmPm = 16;  // Default to no symbol

  //Get Am/Pm symbol if 12 hour mode and not dual-temp mode
  if (temperatureSource > 0) {   //AmPm not shown in dual temp mode (0)
    if (hourFormat == 12 && hour < 12) {
      AmPm = 10;
    } else if (hourFormat == 12 && hour >= 12) {
      AmPm = 11;
    }
  }

  if (hourFormat == 12 && hour > 12) {
    hour = hour - 12;
  } else if (hourFormat == 12 && hour == 0) {  //fix for midnight - 1 am, where previously showed "0" for hour
    hour = 12;
  }

  byte h1 = hour / 10;
  byte h2 = hour % 10;
  byte m1 = mins / 10;
  byte m2 = mins % 10;
  byte s1 = secs / 10;
  byte s2 = secs % 10;

  CRGB color = clockColor;  //CRGB(r_val, g_val, b_val);

  if (h1 > 0 || (hourFormat == 24 && hour == 0))  //display leading zero for midnight when 24-hour display
    displayNumber(h1, 3, color);
  else
    displayNumber(10, 3, color);  // Blank

  displayNumber(h2, 2, color);
  displayNumber(m1, 1, color);
  displayNumber(m2, 0, color);
  //Display A/P symbol if in 12-hour mode
  if (temperatureSource > 0) {
    displaySmallNum(AmPm, 3, color);
  }
  displayDots(color);
}

void updateBinaryClock() {
  int hour = rtc.getHour(true);
  int mins = rtc.getMinute();
  int secs = rtc.getSecond();
  byte hTen = hour / 10;
  byte hOne = hour % 10;
  byte mTen = mins / 10;
  byte mOne = mins % 10;
  byte sTen = secs / 10;
  byte sOne = secs % 10;

  CRGB colorHr = clockColor;
  CRGB colorMin = temperatureColorInt;
  CRGB colorSec = temperatureColorExt;
  displayBinaryNumber(hTen, 5, colorHr);
  displayBinaryNumber(hOne, 4, colorHr);
  displayBinaryNumber(mTen, 3, colorMin);
  displayBinaryNumber(mOne, 2, colorMin);
  displayBinaryNumber(sTen, 1, colorSec);
  displayBinaryNumber(sOne, 0, colorSec);
}

void updateCountdown() {
  if (countdownMilliSeconds == 0 && endCountDownMillis == 0) {
    displayNumber(0, 7, countdownColorPaused);
    displayNumber(0, 6, countdownColorPaused);
    displayNumber(0, 5, countdownColorPaused);
    displayNumber(0, 4, countdownColorPaused);
    LEDs[237] = countdownColorPaused;
    LEDs[187] = countdownColorPaused;
    return;
  }

  if (!timerRunning) {
    unsigned long restMillis = remCountdownMillis;
    unsigned long hours = ((restMillis / 1000) / 60) / 60;
    unsigned long minutes = (restMillis / 1000) / 60;
    unsigned long seconds = restMillis / 1000;
    int remSeconds = seconds - (minutes * 60);
    int remMinutes = minutes - (hours * 60);
    hours = 0;
    byte h1 = hours / 10;
    byte h2 = hours % 10;
    byte m1 = remMinutes / 10;
    byte m2 = remMinutes % 10;
    byte s1 = remSeconds / 10;
    byte s2 = remSeconds % 10;
    if (hours > 0) {
      // hh:mm
      displayNumber(h1, 7, countdownColorPaused);
      displayNumber(h2, 6, countdownColorPaused);
      displayNumber(m1, 5, countdownColorPaused);
      displayNumber(m2, 4, countdownColorPaused);
    } else {
      // mm:ss
      displayNumber(m1, 7, countdownColorPaused);
      displayNumber(m2, 6, countdownColorPaused);
      displayNumber(s1, 5, countdownColorPaused);
      displayNumber(s2, 4, countdownColorPaused);
    }
    LEDs[237] = countdownColorPaused;
    LEDs[187] = countdownColorPaused;
    return;
  }

  unsigned long restMillis = endCountDownMillis - millis();
  unsigned long hours = ((restMillis / 1000) / 60) / 60;
  unsigned long minutes = (restMillis / 1000) / 60;
  unsigned long seconds = restMillis / 1000;
  int remSeconds = seconds - (minutes * 60);
  int remMinutes = minutes - (hours * 60);

  byte h1 = hours / 10;
  byte h2 = hours % 10;
  byte m1 = remMinutes / 10;
  byte m2 = remMinutes % 10;
  byte s1 = remSeconds / 10;
  byte s2 = remSeconds % 10;

  remCountdownMillis = restMillis;  //Store current remaining in event of pause

  CRGB color = countdownColor;
  if (restMillis <= 60000) {
    color = countdownColorFinalMin;
  }

  if (hours > 0) {
    // hh:mm
    displayNumber(h1, 7, color);
    displayNumber(h2, 6, color);
    displayNumber(m1, 5, color);
    displayNumber(m2, 4, color);
  } else {
    // mm:ss
    displayNumber(m1, 7, color);
    displayNumber(m2, 6, color);
    displayNumber(s1, 5, color);
    displayNumber(s2, 4, color);
  }

  displayDots(color);

  if (hours <= 0 && remMinutes <= 0 && remSeconds <= 0) {
    //endCountdown();
    countdownMilliSeconds = 0;
    endCountDownMillis = 0;
    remCountdownMillis = 0;
    timerRunning = false;
    FastLED.setBrightness(brightness);
    FastLED.show();
    if (useBuzzer) {
      digitalWrite(BUZZER_OUTPUT, HIGH);
      delay(2000);  //sound for 2 seconds
      digitalWrite(BUZZER_OUTPUT, LOW);
    }
    return;
  }
}

void updateTemperature() {
  //Internal Temperature (only shown if temperatureSource = 0 dual temp or 2 Internal Only)
  if ((temperatureSource == 0) || (temperatureSource == 2)) {
    float ctemp = 0;
    float ftemp = 0;
    bool isNegative = false;
    bool hasHundreds = false;
    if (tempIntUseApi) {
       //Local API in use. Just set temp to last passed in value.
       ctemp = internalTemperature;
    } else {
      //Get temperature from onboard sensor
      if (aht.startMeasurementReady()) {  
        ftemp = aht.getTemperature_C();
      }
      #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
            Serial.print("Got Temperature: ");
            Serial.println(ftemp);
      #endif
      if (temperatureSymbol == 13)
        ftemp = (ftemp * 1.8000) + 32.0;
      ctemp = round(ftemp + temperatureCorrection);
    }

    internalTemperature = ctemp;
    if (ctemp < 0) {  //Flip sign and set isNegative to true since byte cannot contain negative num
      ctemp = ctemp * -1;
      isNegative = true;
    } else if (ctemp >= 100) {
      ctemp = ctemp - 100;
      hasHundreds = true;
      //Show leading one (hardcoded)
      LEDs[0] = temperatureColorInt;
      LEDs[49] = temperatureColorInt;
      LEDs[50] = temperatureColorInt;
      LEDs[99] = temperatureColorInt;
      LEDs[100] = temperatureColorInt;
    }
    byte t1 = int(ctemp) / 10;
    byte t2 = int(ctemp) % 10;

    //Hide leading zero
    if ((t1 == 0) && (!hasHundreds))
      t1 = 16;  //blank
    displaySmallNum(t1, 1, temperatureColorInt);
    displaySmallNum(t2, 0, temperatureColorInt);
    if (temperatureSource > 0) displaySmallNum(temperatureSymbol, 2, temperatureColorInt);
    LEDs[110] = temperatureColorInt;  //degree dot
    if (isNegative) {
      //show negative
      LEDs[50] = temperatureColorInt;
      LEDs[51] = temperatureColorInt;
    } else {
      LEDs[50] = CRGB::Black;
      LEDs[51] = CRGB::Black;
    }
    //reset counter
    tempUpdateCount = tempUpdatePeriod;
  }
}
void updateTemperatureExt(bool getData) {
  //External Temperature (only shown if temperatureSource = 0 dual temp or 1 External Only)  
  float rtemp = -99.9;
  int ctemp = -99;
  bool isNegative = false;
  bool hasHundreds = false;
  if ((temperatureSource < 2) && (!tempExtUseApi)) {
    if ((owmKey == "NA") || (owmKey == "")) {   //No API key defined
      ctemp = 0;
    } else if (getData) {
      //Get external temperature from Open Weather Map
      String jsonBuffer;
      String serverPath = "https://api.openweathermap.org/data/3.0/onecall?lat=" + owmLat + "&lon=" + owmLong + "&exclude=minutely,hourly,daily,alerts&appid=" + owmKey;
      http.useHTTP10(true);
      http.begin(serverPath);
      int httpCode = http.GET();
      if (httpCode > 0) {
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, http.getStream());
        rtemp = doc["current"]["temp"] | -88.0;
        //Temperature returned in Kelvin. Convert to C then F
        if (rtemp > -88.0) {
          rtemp = (rtemp - 273.15);           //celcius
          if (temperatureSymbol == 13) {
            rtemp = ((rtemp * 1.8) + 32.0);  //fahrenheit
          }
        }
        ctemp = round(rtemp);
      }
      //Reset counter so temp isn't pulled second time
      tempUpdateCountExt = tempUpdatePeriodExt;
    } else {
      //just refresh display with current data
      ctemp = externalTemperature;
    }
  } else {
    //Local API in use.  Just use last passed value
    ctemp = externalTemperature;
  }  
  if (temperatureSource < 2) {
    //Round decimal temperature to integer
    externalTemperature = ctemp;  //save for later non-update refreshes
    if (ctemp < 0) {              //Flip sign and set isNegative to true since byte cannot contain negative num
      ctemp = ctemp * -1;
      isNegative = true;
    } else if (ctemp >= 100) {
      ctemp = ctemp - 100;
      hasHundreds = true;
      if (temperatureSource == 0) {     //Dual temp mode
      //Show leading one (hardcoded)
        LEDs[14] = temperatureColorExt;
        LEDs[35] = temperatureColorExt;
        LEDs[64] = temperatureColorExt;
        LEDs[85] = temperatureColorExt;
        LEDs[114] = temperatureColorExt;
      } else if (temperatureSource == 1) {  //Ext only - show on left
        LEDs[0] = temperatureColorExt;
        LEDs[49] = temperatureColorExt;
        LEDs[50] = temperatureColorExt;
        LEDs[99] = temperatureColorExt;
        LEDs[100] = temperatureColorExt;
      }
    }
    byte t1 = int(ctemp) / 10;
    byte t2 = int(ctemp) % 10;

    //Hide leading zero
    if ((t1 == 0) && (!hasHundreds))
      t1 = 16;  //blank
      if (temperatureSource == 0) {
        displayTempNum(t1, 3, temperatureColorExt);
        displayTempNum(t2, 2, temperatureColorExt);
        LEDs[124] = temperatureColorExt;  //degree dot
        if (isNegative) {
          //show negative
          LEDs[64] = temperatureColorExt;
          LEDs[65] = temperatureColorExt;
        } else {
          LEDs[64] = CRGB::Black;
          LEDs[65] = CRGB::Black;
        }
      } else if (temperatureSource == 1) {
        displaySmallNum(t1, 1, temperatureColorExt);
        displaySmallNum(t2, 0, temperatureColorExt);
        displaySmallNum(temperatureSymbol, 2, temperatureColorExt);
        LEDs[110] = temperatureColorExt;  //degree dot
        if (isNegative) {
          //show negative
          LEDs[50] = temperatureColorExt;
          LEDs[51] = temperatureColorExt;
        } else {
          LEDs[50] = CRGB::Black;
          LEDs[51] = CRGB::Black;
        }
      }
  }    
}

void updateScoreboard() {
  byte sl1 = scoreboardLeft / 10;
  byte sl2 = scoreboardLeft % 10;
  byte sr1 = scoreboardRight / 10;
  byte sr2 = scoreboardRight % 10;
  byte vLen = scoreboardTeamLeft.length() + 1;
  byte hLen = scoreboardTeamRight.length() + 1;
  char v_array[vLen];
  char h_array[hLen];
  byte letternum = 0;  //Default to blank space
  scoreboardTeamLeft.toCharArray(v_array, vLen);
  scoreboardTeamRight.toCharArray(h_array, hLen);

  displayNumber(sl1, 11, scoreboardColorLeft);
  displayNumber(sl2, 10, scoreboardColorLeft);
  displayNumber(sr1, 9, scoreboardColorRight);
  displayNumber(sr2, 8, scoreboardColorRight);

  //Show Team Names
  // Left - visitor
  for (byte i = 0; i < 3; i++) {
    yield();
    if (i <= (vLen - 1)) {
      letternum = getLetterIndex(v_array[i]);
    } else {
      letternum = 0;
    }
    displayLetter(letternum, (17 - i), scoreboardColorLeft);
  }
  // Right - home
  for (byte i = 0; i < 3; i++) {
    yield();
    if (i <= (hLen - 1)) {
      letternum = getLetterIndex(h_array[i]);
    } else {
      letternum = 0;
    }
    displayLetter(letternum, (14 - i), scoreboardColorRight);
  }
  hideDots();
}

void updateText() {
  byte topLen = textTop.length() + 1;
  byte botLen = textBottom.length() + 1;
  char top_array[topLen];
  char bottom_array[botLen];
  textTop.toCharArray(top_array, topLen);
  textBottom.toCharArray(bottom_array, botLen);
  byte letternum = 0;  //Default to blank space

  //Top row
  for (byte i = 0; i < 6; i++) {
    yield();
    if (i <= (topLen - 1)) {
      letternum = getLetterIndex(top_array[i]);
    } else {
      letternum = 0;
    }
    displayLetter(letternum, (5 - i), textColorTop);
  }
  //Bottom row
  for (byte i = 0; i < 6; i++) {
    yield();
    if (i <= (botLen - 1)) {
      letternum = getLetterIndex(bottom_array[i]);
    } else {
      letternum = 0;
    }
    displayLetter(letternum, (11 - i), textColorBottom);
  }
}

//Text Effect Mathods
//Flash (both rows on/off together)
void updateTextFlash() {
  yield();
  if (effectTextHide) {
    allBlank();
  } else {
    updateText();
  }
  effectTextHide = !effectTextHide;
}

//Flash Alternate (one row on, one row off)
void updateTextFlashAlternate() {
  byte letternum = 0;
  allBlank();
  //Show 1st row when effectTextHide false
  if (effectTextHide) {
    byte topLen = textTop.length() + 1;
    char top_array[topLen];
    textTop.toCharArray(top_array, topLen);
    for (byte i = 0; i < 6; i++) {
      yield();
      if (i <= (topLen - 1)) {
        letternum = getLetterIndex(top_array[i]);
      } else {
        letternum = 0;
      }
      displayLetter(letternum, (5 - i), textColorTop);
    }
  } else {
    byte botLen = textBottom.length() + 1;
    char bottom_array[botLen];
    textBottom.toCharArray(bottom_array, botLen);
    for (byte i = 0; i < 6; i++) {
      yield();
      if (i <= (botLen - 1)) {
        letternum = getLetterIndex(bottom_array[i]);
      } else {
        letternum = 0;
      }
      displayLetter(letternum, (11 - i), textColorBottom);
    }
  }
  effectTextHide = !effectTextHide;
}

void updateTextFadeIn() {
  if (effectBrightness == 0) {
    allBlank();
    effectBrightness += int(brightness / 5);
  } else if (effectBrightness > brightness) {
    effectBrightness = 0;
  } else {
    updateText();
    effectBrightness += int(brightness / 5);
  }
}
//Fade Out
void updateTextFadeOut() {
  if (effectBrightness <= 0) {
    allBlank();
    effectBrightness = brightness;
  } else {
    updateText();
    effectBrightness = (effectBrightness - int((brightness / 5)));
  }
}

//Appear and Appear Flash
void updateTextAppear(bool flash) {
  byte topLen = textTop.length() + 1;
  byte botLen = textBottom.length() + 1;
  byte letternum = 0;
  char top_array[topLen];
  char bottom_array[botLen];
  textTop.toCharArray(top_array, topLen);
  textBottom.toCharArray(bottom_array, botLen);
  yield();
  if (appearCount >= 99) {
    allBlank();
    appearCount = 5;
  } else if ((appearCount >= 14) && (appearCount <= 20)) {
    // just skip and hold or start flash
    if (flash) {  //Start flash for 6 loops (3 on 3 off)
      updateTextFlash();
      appearCount = appearCount - 1;
    } else {
      appearCount = 99;
    }
  } else if ((appearCount == 12) || (appearCount == 13)) {
    appearCount = 99;
  } else if ((appearCount >= 6) && (appearCount <= 11)) {  //Bottom row
    letternum = getLetterIndex(bottom_array[11 - appearCount]);
    displayLetter(letternum, appearCount, textColorBottom);
    if (appearCount == 6) {
      appearCount = 20;  //finished display
    } else {
      appearCount = appearCount - 1;
    }

  } else if ((appearCount >= 0) && (appearCount <= 5)) {  //Top row
    letternum = getLetterIndex(top_array[5 - appearCount]);
    displayLetter(letternum, appearCount, textColorTop);
    if (appearCount == 0) {
      appearCount = 11;  //Move to bottom row
    } else {
      appearCount = appearCount - 1;
    }
  }
  return;
}

//Rainbow (random colors)
void updateTextRainbow() {
  byte topLen = textTop.length() + 1;
  byte botLen = textBottom.length() + 1;
  char top_array[topLen];
  char bottom_array[botLen];
  textTop.toCharArray(top_array, topLen);
  textBottom.toCharArray(bottom_array, botLen);
  byte letternum = 0;  //Default to blank space
  byte r_val = 255;
  byte g_val = 255;
  byte b_val = 255;
  //Top row
  for (byte i = 0; i < 6; i++) {
    yield();
    if (i <= (topLen - 1)) {
      letternum = getLetterIndex(top_array[i]);
    } else {
      letternum = 0;
    }
    r_val = getRandomColor();
    g_val = getRandomColor();
    b_val = getRandomColor();
    displayLetter(letternum, (5 - i), CRGB(r_val, g_val, b_val));
  }
  //Bottom row
  for (byte i = 0; i < 6; i++) {
    yield();
    if (i <= (botLen - 1)) {
      letternum = getLetterIndex(bottom_array[i]);
    } else {
      letternum = 0;
    }
    r_val = getRandomColor();
    g_val = getRandomColor();
    b_val = getRandomColor();
    displayLetter(letternum, (11 - i), CRGB(r_val, g_val, b_val));
  }
  return;
}

byte getLetterIndex(byte letterval) {
  byte index = 0;
  if ((letterval >= 32) && (letterval <= 96)) {  //Symbols, numbers & uppercase letters
    index = letterval - 32;
  } else if ((letterval >= 97) && (letterval <= 122)) {  //lowercase letters
    index = letterval - 64;
  }
  return index;
}

byte getRandomColor() {
  byte colorVal = 0;
  colorVal = random(0, 256);
  return colorVal;
}
// --------Rotary Knob Functions----------
void rotaryTurnedRight() {
  //Increase brightness.  Commands depend upon whether clock or WLED controller is active
  if (wledOn()) {
    //Send increase brightness by 25 to WLED controller
    int success = wledBrightness(true);
  } else if (ledsOn) {
    //Increase Clock brightness by 10
    if (brightness <= 240) {
      brightness += 10;

    } else {
      brightness = 250;
    }
    FastLED.setBrightness(brightness);
  } else {
    brightness = 5;
    holdBrightness = brightness;
    toggleLEDs(true);
  }
  turnedRightFlag = false;
}

void rotaryTurnedLeft() {
  //Decrease brightness. Commands depend upon whether clock or WLED controller is active
  if (wledOn()) {
    //Send decrease brightness by 25 to WLED controller
    int success = wledBrightness(false);
  } else if (ledsOn) {
    if (brightness >= 15) {
      brightness -= 10;
    } else if (brightness < 10) {
      brightness = 5;
      toggleLEDs(false);
    } else {
      brightness = 5;
    }
    FastLED.setBrightness(brightness);
  }
  turnedLeftFlag = false;
}
// ===============================================================
//   DISPLAY FUNCTIONS
//   For all LED[x] calls, subtract 1 since array is 0-based,
//   but segments are based on actual pixel number which is 1-based.
// ===============================================================
void displayNumber(byte number, byte segment, CRGB color) {
  /*
    number passed from numbers[x][y] array.  [x] defines 'font' number. [y] is the digit/character
    Segment is defined in the fullnumPixelPos[] array 
   */
  unsigned int pixelPos = 0;
  for (byte i = 0; i < 32; i++) {
    yield();
    pixelPos = (fullnumPixelPos[segment][i] - 1);
    LEDs[pixelPos] = ((numbers[numFont][number] & 1 << i) == 1 << i) ? color : alternateColor;
  }
}

void displaySmallNum(byte number, byte segment, CRGB color) {
  /*
   *      7  8  9  Segments 0,1 Temperature digits
   *      6    10  Segment  2 Temperature Symbol (F/C)
   *      5 12 11  Segment  3 A/P time symbol
   *      4     0  Segment  4 V(istor) symbol
   *      3  2  1  Segment  5 H(ome) symbol
   */
  unsigned int pixelPos = 0;
  for (byte i = 0; i < 13; i++) {
    yield();
    pixelPos = (smallPixelPos[segment][i] - 1);
    LEDs[pixelPos] = ((smallNums[number] & 1 << i) == 1 << i) ? color : alternateColor;
  }
}

void displayBinaryNumber(byte number, byte segment, CRGB color) {
/*
 *   6 7 8   Segments 0, 1 - Seconds ones, tens
 *   5 4 3   Segments 2, 3 - Minutes ones, tens
 *   0 1 2   Segments 4, 5 - Hours ones, tens
 *   There are four of the above blocks for each of the six columns (hour tens & ones, min tens & ones, sec tens & ones )
 *   Each column consists of 36 pixel positions
 */
    unsigned int pixelPos = 0;
 if (number < 9 ) {
    for (byte i = 0; i < 32; i++) {
      yield();
      pixelPos = (binaryPixelPos[segment][i] - 1);
      LEDs[pixelPos] = ((binaryNums[number] & 1 << i) == 1 << i) ? color : alternateColor;
    }
    for (byte j = 32; j < 36; j++) { //32 thru 35
      pixelPos = (binaryPixelPos[segment][j] - 1);
      if (number == 8) {
        LEDs[pixelPos] = color;
      } else {
        LEDs[pixelPos] = alternateColor;
      }
    }

 } else {
    for (byte i = 0; i < 36; i++) {
      yield();
      pixelPos = (binaryPixelPos[segment][i] - 1);
      LEDs[pixelPos] = ((binaryNums[number] & 1 << i) == 1 << i) ? color : alternateColor;
    }
 }
}

void displayTempNum(byte number, byte segment, CRGB color) {
  //For dual-temp display in clock mode (internal left - external right
  /*
   *      7  8  9  Segments 0,1 Internal Temperature digits (ones, tens)
   *      6    10  Segment  2,3 External Temperature digits (ones, tens)
   *      5 12 11  Segment  
   *      4     0  Segment  
   *      3  2  1  Segment  
   */
  unsigned int pixelPos = 0;
  for (byte i = 0; i < 13; i++) {
    yield();
    pixelPos = (tempPixelPos[segment][i] - 1);
    LEDs[pixelPos] = ((smallNums[number] & 1 << i) == 1 << i) ? color : alternateColor;
  }

}
void displayLetter(byte number, byte segment, CRGB color) {
  /*
   *      7  8  9  Segments 0-5 Top row, right to left
   *      6 13 10  Segments 6-11 Bottom row, right to left    
   *      5 12 11  
   *      4 14  0  
   *      3  2  1  
   */
  unsigned int pixelPos = 0;
  for (byte i = 0; i < 15; i++) {
    yield();
    pixelPos = (letterPixelPos[segment][i] - 1);
    LEDs[pixelPos] = ((letters[number] & 1 << i) == 1 << i) ? color : alternateColor;
  }
}

void allBlank() {
  for (int i = 0; i < NUM_LEDS; i++) {
    yield();
    LEDs[i] = CRGB::Black;
  }
  FastLED.show();
}

void toggleLEDs(bool turnOn) {
  //Function for turning display "off", while allowing controller to run
  //Equivalent to setting brightness to 0 or non-zero 
  if (turnOn) {
    if ((holdBrightness > 250) || (holdBrightness < 5)) {
      holdBrightness = 25;   //provide default value
    } 
    ledsOn = true;
    brightness = holdBrightness;
  } else {
    holdBrightness = brightness;
    ledsOn = false;
    brightness = 0;
  }
  FastLED.setBrightness(brightness);
}

void displayOTA() {
  // Turn on LEDs if off
  if (!ledsOn) {
    toggleLEDs(true);
  }

  CRGB color = CRGB::Red;
  allBlank();
  // Subtract 1 from physical pixel number
  // U - 0b0111011111111
  LEDs[196] = color;
  LEDs[153] = color;
  LEDs[152] = color;
  LEDs[151] = color;
  LEDs[198] = color;
  LEDs[201] = color;
  LEDs[248] = color;
  LEDs[251] = color;
  LEDs[253] = color;
  LEDs[246] = color;
  LEDs[203] = color;
  //P - 0b1111111111000
  LEDs[155] = color;
  LEDs[194] = color;
  LEDs[205] = color;
  LEDs[244] = color;
  LEDs[255] = color;
  LEDs[256] = color;
  LEDs[257] = color;
  LEDs[242] = color;
  LEDs[207] = color;
  LEDs[206] = color;
  //L - 0b0000011111110
  LEDs[161] = color;
  LEDs[160] = color;
  LEDs[159] = color;
  LEDs[190] = color;
  LEDs[209] = color;
  LEDs[240] = color;
  LEDs[259] = color;
  //O - 0b0111111111111
  LEDs[184] = color;
  LEDs[165] = color;
  LEDs[164] = color;
  LEDs[163] = color;
  LEDs[186] = color;
  LEDs[213] = color;
  LEDs[236] = color;
  LEDs[263] = color;
  LEDs[264] = color;
  LEDs[265] = color;
  LEDs[234] = color;
  LEDs[215] = color;
  //A - 0b1111111111011
  LEDs[180] = color;
  LEDs[169] = color;
  LEDs[167] = color;
  LEDs[182] = color;
  LEDs[217] = color;
  LEDs[232] = color;
  LEDs[267] = color;
  LEDs[268] = color;
  LEDs[269] = color;
  LEDs[230] = color;
  LEDs[219] = color;
  LEDs[218] = color;
  //D - 0b0110111111101
  LEDs[176] = color;
  LEDs[172] = color;
  LEDs[171] = color;
  LEDs[178] = color;
  LEDs[221] = color;
  LEDs[228] = color;
  LEDs[271] = color;
  LEDs[272] = color;
  LEDs[226] = color;
  LEDs[223] = color;

  FastLED.setBrightness(brightness);
  FastLED.show();
}

void displayDots(CRGB color) {
  if (dotsOn) {
    LEDs[237] = color;
    if (clockMode == 0) {
      LEDs[287] = color;
    } else if (clockMode = 1) {
      LEDs[187] = color;
    }
  } else {
    LEDs[187] = CRGB::Black;
    LEDs[237] = CRGB::Black;
    LEDs[287] = CRGB::Black;
  }
  dotsOn = !dotsOn;
}

void hideDots() {
  LEDs[187] = CRGB::Black;
  LEDs[237] = CRGB::Black;
  LEDs[287] = CRGB::Black;
}

void soundBuzzer(unsigned int buzzLength) {
  digitalWrite(BUZZER_OUTPUT, HIGH);
  delay(buzzLength);  //time in millis -  do not exceed about 3000 millis
  digitalWrite(BUZZER_OUTPUT, LOW);
}

String getEffectList() {
  String retVal = "<select name=\"texteffect\" id=\"texteffect\">\
    <option value=\"0\"";
  if (textEffect == 0) retVal += " selected";
  retVal += ">None</option>\
    <option value=\"1\"";
  if (textEffect == 1) retVal += " selected";
  retVal += ">Flash</option>\
    <option value=\"2\"";
  if (textEffect == 2) retVal += " selected";  
  retVal += ">Flash Alternate</option>\
    <option value=\"3\"";
  if (textEffect == 3) retVal += " selected";
  retVal += ">Fade In</option>\
    <option value=\"4\"";
  if (textEffect == 4) retVal += " selected";
  retVal += ">Fade Out</option>\
    <option value=\"5\"";
  if (textEffect == 5) retVal += " selected";
  retVal += ">Appear</option>\
    <option value=\"6\"";
  if (textEffect == 6) retVal += " selected";
  retVal += ">Appear Flash</option>\
    <option value=\"7\"";
  if (textEffect == 7) retVal += " selected";
  retVal += ">Rainbow</option>\
    </select>";
  return retVal;
}

// =========================
//  WLED Functions/Inteface 
// =========================
bool wledOn() {
  //Call to determine current state of WLED (on/off)
  bool retVal = false;
  if (useWLED) {
    String serverName = "http://" + wledAddress + "/json";
    WiFiClient client;
    HTTPClient http;
    http.begin(client, serverName);
    int httpResponse = http.GET();
    String payload = "{}";
    payload = http.getString();
    if (httpResponse > 0) {
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, payload);
      if (error) {
        retVal = false;
      } else {
        JsonObject state = doc["state"];
        retVal = state["on"];
      }
    } else {
      retVal = false;
    } 
    http.end(); 
  }
  wledStateOn = retVal;    //Local variable for tracking state
  return retVal;
}

int toggleWLED(bool stateOn) {
  int httpResponseCode = 503;
  if (useWLED) {
    //Set on/off state of WLED
    WiFiClient client;
    HTTPClient wled;
    String serverName = "http://" + wledAddress + "/";
    String httpRequestData = "win&T=";
    if (stateOn) {
      httpRequestData += "1";
    } else {
      httpRequestData += "0";
    }
    serverName = serverName + httpRequestData;
    wled.begin(client, serverName.c_str());
    wled.addHeader("Content-Type", "text/plain");
    httpResponseCode = wled.POST("");
    wled.end();
  }
  return httpResponseCode;
}

int wledBrightness(bool increase) {
  int httpResponseCode = 503;
  if (useWLED) {
    //Set WLED brightness
    //Passing true for increase will increase brightness by 20
    //Passing false for increase will DECREASE brightness by 20
    WiFiClient client;
    HTTPClient wled;
    String serverName = "http://" + wledAddress + "/";
    String httpRequestData = "";
    if (increase) {
      httpRequestData += "win&A=~10";
    } else {
      httpRequestData += "win&A=~-10";
    }
    serverName = serverName + httpRequestData;
    wled.begin(client, serverName.c_str());
    wled.addHeader("Content-Type", "text/plain");
    httpResponseCode = wled.POST("");
    wled.end();
  }
  return httpResponseCode;
}

int wledPresetFirst() {
  int httpResponseCode = 503;
  if ((useWLED) && (wledMaxPreset > 0)) {
    //Set WLED to first preset (preset #1) - white/top button on dispaly
    WiFiClient client;
    HTTPClient wled;
    String serverName = "http://" + wledAddress + "/";
    String httpRequestData = "";
    httpRequestData += "win&PL=1";
    wledCurPreset = 1;
    serverName = serverName + httpRequestData;
    wled.begin(client, serverName.c_str());
    wled.addHeader("Content-Type", "text/plain");
    httpResponseCode = wled.POST("");
    wled.end();
  }
  return httpResponseCode;
}

int wledPresetAdvance(bool increase) {
  int httpResponseCode = 503;
  if ((useWLED) && (wledMaxPreset > 1)) {
    WiFiClient client;
    HTTPClient wled;
    String serverName = "http://" + wledAddress + "/";
    String httpRequestData = "";
    
    if (increase) {
      //move to next preset
      if (wledCurPreset >= wledMaxPreset) {
        wledCurPreset = 1;
      } else {
        wledCurPreset++;
      }
    } else {
      //move to previous preset 
      if (wledCurPreset <= 1) {
        wledCurPreset = wledMaxPreset;
      } else {
        wledCurPreset--;
      }
    }
    httpRequestData += "win&PL=" + String(wledCurPreset);
    serverName = serverName + httpRequestData;
    wled.begin(client, serverName.c_str());
    wled.addHeader("Content-Type", "text/plain");
    httpResponseCode = wled.POST("");
    wled.end();
  }
  return httpResponseCode;
}

// ===========================
//  Misc Functions
// ===========================
boolean isValidNumber(String str){
  for(byte i=0;i<str.length();i++) {
    if(isDigit(str.charAt(i))) return true;
  }
  return false;
}