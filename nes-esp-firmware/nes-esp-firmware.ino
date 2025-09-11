/**********************************************************************************
                    NES RA Adapter - ESP32 Firmware

   This project is the ESP32 firmware for the NES RA Adapter, a device that connects
   to an NES console and enables users to connect to the RetroAchievements server to
   unlock achievements in games played on original hardware.

   The ESP32 provides Internet connectivity to the NES and also manages a TFT screen
   to display unlocked achievements, along with a buzzer for sound effects. It stores
   Wi-Fi and RA credentials in EEPROM and allows users to configure settings via a
   smartphone connected to the ESP32's access point. Users can reset the configuration
   by holding the reset button for 5 seconds during startup. Additionally, it utilizes
   LittleFS to store a CRC32-to-MD5 hash table (for cartridge identification) and game
   images. The firmware communicates with a Raspberry Pi Pico through Serial0 to retrieve
   the game CRC, send HTTP requests to RetroAchievements, and forward HTTP responses.
   Finally, it orchestrates the opening and closing of the bus between the NES and the
   cartridge by controlling analog switches.

   Date:             2025-09-11
   Version:          1.0
   By odelot

   Arduino IDE ESP32 Boards: v3.0.7

   Libs used:
   WifiManager: https://github.com/tzapu/WiFiManager v2.0.17
   PNGdec: https://github.com/bitbank2/PNGdec v1.1.2
   TFT_eSPI lib: https://github.com/Bodmer/TFT_eSPI v2.5.43
   ESP Async WebServer: https://github.com/ESP32Async/ESPAsyncWebServer v.3.7.6
   Async TCP: https://github.com/ESP32Async/AsyncTCP v3.4.0

   Compiled with Arduino IDE 2.3.4

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.

**********************************************************************************/

/*********************************************************************************
* FEATURE FLAGS
**********************************************************************************/

#define ENABLE_RESET 1

/**
 * defines to use a aws lambda function to shrink the JSON response with the list of achievements
 * remember: you need to deploy the lambda and inform its URL
 */

#define ENABLE_SHRINK_LAMBDA 0 // 0 - disable / 1 - enable
#define SHRINK_LAMBDA_URL "https://xxxxxxxxxx.execute-api.us-east-1.amazonaws.com/default/NES_RA_ADAPTER?"

/**
 enable internal web app (comment to disable)
*/

#define ENABLE_INTERNAL_WEB_APP_SUPPORT

/**
* enable LCD (comment to disable)
*/
#define ENABLE_LCD

/**
* flip screen upside down
*/
#define FLIP_SCREEN

#include <WiFiManager.h>
#include <EEPROM.h>

#include <StreamString.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include "HardwareSerial.h"
#include "LittleFS.h"
#include <esp_sleep.h>
#include <esp_system.h>
#include <driver/uart.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <Wire.h>
#include <Ticker.h>

#ifdef ENABLE_LCD
  #include <PNGdec.h>
  #include "SPI.h"
  #include <TFT_eSPI.h>
#endif

#define VERSION "1.0"

/**
 * defines for the LittleFS
 */
#define FileSys LittleFS
#define FORMAT_LITTLEFS_IF_FAILED true

/**
 * defines for the EEPROM
 */
#define EEPROM_SIZE 2048
#define EEPROM_ID_1 142
#define EEPROM_ID_2 208

/**
 * defines for the analog switch
 */
#define ANALOG_SWITCH_PIN 3
#define ANALOG_SWITCH_ENABLE_BUS HIGH
#define ANALOG_SWITCH_DISABLE_BUS LOW

/**
 * defines for sending big serial data into chunks with a delay
 */
#define SERIAL_COMM_CHUNK_SIZE 32
#define SERIAL_COMM_TX_DELAY_MS 5

#define SERIAL_MAX_PICO_BUFFER 32768
/**
 * defines for the fifo used to store achievements to be showed on screen
 */
#define ACHIEVEMENTS_FIFO_SIZE 5

/**
 * defines for the png decoder
 */
#define MAX_IMAGE_WIDTH 240

/**
 * defines for the LCD
 */
#define LCD_BRIGHTNESS_PIN 7

/**
 * defines for status led
 */
#define LED_STATUS_RED_PIN 6
#define LED_STATUS_GREEN_PIN 5

/**
 * defines for the buzzer
 */

#define NOTE_CS6 1109
#define NOTE_D6 1175
#define NOTE_D5 587
#define NOTE_C4 262
#define NOTE_G4 392
#define NOTE_G5 784
#define NOTE_A5 880
#define NOTE_B5 988
#define NOTE_E6 1319
#define NOTE_G6 1568
#define NOTE_C7 2093
#define NOTE_D7 2349
#define NOTE_E7 2637
#define NOTE_F7 2794
#define NOTE_G7 3136

#define SOUND_PIN 10

/**
 * defines for the reset button
 */

// time that reset button should be pressed to kick off reset operation
#define RESET_PRESSED_TIME 5000L

// pin to the reset button
#define RESET_PIN 8

// types for http request
typedef enum HttpRequestMethod
{
  GET = 0,
  POST = 1
};

typedef enum HttpRequestResult
{
  HTTP_SUCCESS = 0,
  HTTP_ERR_NO_WIFI = -1,
  HTTP_ERR_REQUEST_FAILED = -2,
  HTTP_ERR_HTTP_4XX = -3,
  HTTP_ERR_TIMEOUT = -4,
  HTTP_ERR_REPONSE_TOO_BIG = -5,
};

// type to store the achievements to be showed on screen
typedef struct
{
  uint32_t id;
  String title;
  String url;
} achievements_t;

// type to store the achievement fifo
typedef struct
{
  achievements_t buffer[ACHIEVEMENTS_FIFO_SIZE];
  int head;
  int tail;
  int count;
} achievements_FIFO_t;

// types for LED control

enum LedMode {
  LED_OFF,
  LED_ON,
  LED_BLINK_SLOW,
  LED_BLINK_MEDIUM,
  LED_BLINK_FAST
};

enum LedColor {
  LED_RED,
  LED_GREEN,
  LED_YELLOW  
};

struct Led {
  uint8_t pin;
  LedMode mode;
  bool state; // HIGH ou LOW atual
  Ticker ticker;
};

// global variables for LED control
Led ledRed   = {LED_STATUS_RED_PIN, LED_OFF, false, Ticker()};
Led ledGreen = {LED_STATUS_GREEN_PIN, LED_OFF, false, Ticker()};


#ifdef ENABLE_LCD
// global variables for the PNG decoder
PNG png;
int16_t x_pos = 0;
int16_t y_pos = 0;
File png_file;

// global variables for the TFT screen
TFT_eSPI tft = TFT_eSPI();
#endif

// global variables for the achievements fifo
achievements_FIFO_t achievements_fifo;

// custom html and parameters for the configuration portal
const char head[] PROGMEM = "<style>#l,#i,#z{text-align:center}#i,#z{margin:15px auto}button{background-color:#0000FF;}#l{margin:0 auto;width:100%; font-size: 28px;}p{margin-bottom:-5px}[type='checkbox']{height: 20px;width: 20px;}</style><script>var w=window,d=document,e=\"password\";function cc(){z=gE(this.w);z.type!=e?z.type=e:z.type='text';z.focus();}function iB(a,b,c){a.insertBefore(b,c)}function gE(a){return d.getElementById(a)}function cE(a){return d.createElement(a)};\"http://192.168.1.1/\"==w.location.href&&(w.location.href+=\"wifi\");</script>\0";
const char html_p1[] PROGMEM = "<p id='z' style='width: 80%;'>Enter the RetroAchievements credentials below:</p>\0";
const char html_p2[] PROGMEM = "<p>&#8226; RetroAchievements user name: </p>\0";
const char html_p3[] PROGMEM = "<p>&#8226; RetroAchievements user password: </p>\0";
const char html_s[] PROGMEM = "<script>gE(\"s\").required=!0;l=cE(\"div\");l.innerHTML=\"NES RetroAchievements Adapter\",l.id=\"l\";m=d.body.childNodes[0];iB(m,l,m.childNodes[0]);p=cE(\"p\");p.id=\"i\",p.innerHTML=\"Choose the network you want to connect with:\",iB(m,p,m.childNodes[1]),a=d.createTextNode(\" show \"+e),sp=(s=d.getElementById(\"up\").nextSibling).parentNode,(c1=d.createElement(\"input\")).type=\"checkbox\",c1.onclick=cc,c1.w=\"up\",iB(sp,c1,s),iB(sp,a,s);</script>\0";

// ws variables
#ifdef ENABLE_INTERNAL_WEB_APP_SUPPORT
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
uint32_t last_ws_cleanup = millis();
AsyncWebSocketClient *ws_client = NULL;
#endif

// global variables for the state machine
uint8_t state = 255; // TODO: change to enum

// global variables for storing states, timestamps and useful information
String base_url = "https://retroachievements.org/dorequest.php?";
String ra_user_token = "";
StreamString buffer;
String md5 = "";
String game_name = "not identified";
String game_image = "";
String game_id = "0";
String game_session;
bool go_back_to_title_screen = false;
bool already_showed_title_screen = false;
long go_back_to_title_screen_timestamp;

/**
 * functions related to LED status and control
 */

// function called by the ticker to update the LED state
void updateLed(Led* led) {
  switch (led->mode) {
    case LED_OFF:
      led->state = false;
      digitalWrite(led->pin, LOW);
      break;

    case LED_ON:
      led->state = true;
      digitalWrite(led->pin, HIGH);
      break;

    case LED_BLINK_SLOW:
    case LED_BLINK_MEDIUM:
    case LED_BLINK_FAST:
      led->state = !led->state;
      digitalWrite(led->pin, led->state);
      break;
  }
}

// configure the LED ticker based on the mode
void configureTicker(Led* led) {
  led->ticker.detach();  // Para evitar duplicatas

  float interval = 0;

  switch (led->mode) {
    case LED_BLINK_SLOW:   interval = 0.9; break;
    case LED_BLINK_MEDIUM: interval = 0.6; break;
    case LED_BLINK_FAST:   interval = 0.3; break;
    case LED_ON:
    case LED_OFF:
      updateLed(led);  // atualiza o estado imediatamente
      return;
  }

  led->ticker.attach(interval, updateLed, led);
}

// set the LED mode and color
void setSemaphore (LedMode mode, LedColor color) {
  ledRed.mode = LED_OFF;
  ledGreen.mode = LED_OFF;
  switch (color) {
    case LED_RED:
      ledRed.mode = mode;
      break;
    case LED_GREEN:
      ledGreen.mode = mode;      
      break;
    case LED_YELLOW:
      ledRed.mode = mode;
      ledGreen.mode = mode;
      break;
  }
  configureTicker(&ledRed);
  configureTicker(&ledGreen);
}


/**
 * functions to identify the cartridge
 */

// look for the md5 hash in the crc to md5 hash table (games.txt)
// first_bank is true if the crc is from the first bank of the cartridge,
// otherwise it look for the crc from the last bank
String get_MD5(String crc, bool first_bank)
{
  if (crc == "BD7BC39F")
  {                                                 // CRC from regions filled with 0xFF - normally the last bank - useless to identify the cartridge
    Serial.println("CRC is 0xBD7BC39F - skipping"); // debug
    return "";
  }
  if (crc == "B2AA7578")
  {                                                 // CRC with many collisions 
    Serial.println("CRC is 0xB2AA7578 - skipping"); // debug
    return "";
  }
  

  const char *filePath = "/games.txt";

  File file = LittleFS.open(filePath, "r");
  if (!file)
  {
    Serial.println("Error opening file");
    return "";
  }

  StreamString line;
  StreamString crc1;
  StreamString crc2;
  StreamString md5;
  line.reserve(54);
  crc1.reserve(10);
  crc2.reserve(10);
  md5.reserve(33);

  while (file.available())
  {
    line.clear();
    crc1.clear();
    crc2.clear();
    md5.clear();
    line += file.readStringUntil('\n');
    int index_after_comma = line.indexOf(',');
    int index_after_equal_sign = line.indexOf('=');

    if (index_after_comma != -1 && index_after_equal_sign != -1)
    {
      crc1 += line.substring(0, index_after_comma);
      crc2 += line.substring(index_after_comma + 1, index_after_equal_sign);
      md5 += line.substring(index_after_equal_sign + 1);

      if ((first_bank && crc1.equalsIgnoreCase(crc)) || (!first_bank && crc2.equalsIgnoreCase(crc)))
      {
        file.close();
        return md5; // Retorna o MD5 se um dos CRCs for encontrado
      }
    }
  }

  file.close();
  return ""; // Retorna string vazia se não encontrar
}

// remove spaces, new lines and tabs from the json string
void remove_space_new_lines_in_json(String &json)
{
  bool inside_quotes = false;
  int write_index = 0;

  for (int read_index = 0; read_index < json.length(); read_index++)
  {
    char c = json[read_index];

    if (c == '"')
    {
      if (json[read_index - 1] != '\\')
        inside_quotes = !inside_quotes;
    }

    if (inside_quotes || (c != ' ' && c != '\n' && c != '\r' && c != '\t'))
    {
      json[write_index++] = c;
    }
  }

  json.remove(write_index); // Reduz o tamanho da string sem cópias extras
}

// remove a field from the json string
void remove_json_field(String &json, const String &field_to_remove)
{
  remove_space_new_lines_in_json(json);

  bool inside_quotes = false;
  bool inside_array = false;
  bool skip_field = false;
  int field_len = field_to_remove.length();
  int read_index = 0, write_index = 0, skip_init = 0;

  while (read_index < json.length())
  {
    char c = json[read_index];

    if (c == '"')
    {
      // if (json[readIndex - 1] != '\\')
      inside_quotes = !inside_quotes;
    }

    if (c == '[' && skip_field)
    {
      inside_array = true;
    }
    if (c == ']' && skip_field)
    {
      inside_array = false;
    }

    if (inside_quotes && json.substring(read_index + 1, read_index + 1 + field_len) == field_to_remove && json[read_index + field_len + 1] == '"')
    {
      skip_field = true;
      skip_init = read_index;
    }

    if (!skip_field)
    {
      json[write_index++] = c;
    }

    if (skip_field && json[read_index + 1] == '}')
    {
      skip_field = false;
      if (json[skip_init - 1] == ',')
      {
        write_index--;
      }
    }
    else if (skip_field && json[read_index] == ',' && (inside_array == false && inside_quotes == false))
    {
      skip_field = false;
    }

    read_index++;
  }

  json.remove(write_index);
}

// transform a string field from the json into a empty string
void clean_json_field_str_value(String &json, const String &field_to_remove)
{
  remove_space_new_lines_in_json(json);

  bool inside_quotes = false;
  bool inside_array = false;
  bool skip_field = false;
  int field_len = field_to_remove.length();
  int read_index = 0, write_index = 0, skip_init = 0;
  bool remove_next_str = false;
  while (read_index < json.length())
  {
    char c = json[read_index];

    if (c == '"')
    {
      if (json[read_index - 1] != '\\')
      {
        inside_quotes = !inside_quotes;
        if (inside_quotes && remove_next_str)
        {
          skip_field = true;
          json[write_index++] = '"';
        }
        if (!inside_quotes && remove_next_str && read_index > skip_init)
        {
          remove_next_str = false;
          skip_field = false;
        }
      }
    }

    if (inside_quotes && json.substring(read_index + 1, read_index + 1 + field_len) == field_to_remove && json[read_index + field_len + 1] == '"')
    {
      remove_next_str = true;
      skip_init = read_index + field_len + 2;
    }

    if (!skip_field)
    {
      json[write_index++] = c;
    }
    read_index++;
  }

  json.remove(write_index);
}

// remove achievements with flags 5 (unofficial) from the json string
void remove_achievements_with_flags_5(String &json)
{
  remove_space_new_lines_in_json(json);
  int achvStart = json.indexOf("\"Achievements\":[");
  if (achvStart == -1)
    return;

  int arrayStart = json.indexOf('[', achvStart);
  int arrayEnd = json.indexOf(']', arrayStart);
  if (arrayStart == -1 || arrayEnd == -1)
    return;
  int objCount = 0;
  int pos = arrayStart + 1;
  while (pos < arrayEnd)
  {
    int objStart = json.indexOf('{', pos);
    if (objStart == -1 || objStart > arrayEnd)
      break;

    int objEnd = objStart;
    int braces = 1;
    while (braces > 0 && objEnd < arrayEnd)
    {
      objEnd++;
      if (json.charAt(objEnd) == '{')
        braces++;
      else if (json.charAt(objEnd) == '}')
        braces--;
    }

    if (objEnd >= arrayEnd)
      break;
    objCount++;

    int flagsPos = json.indexOf("\"Flags\":5", objStart);
    if (flagsPos != -1 && flagsPos < objEnd)
    {
      
      int removeStart = objStart;
      while (removeStart > arrayStart && isspace(json.charAt(removeStart - 1)))
        removeStart--;
      if (json.charAt(removeStart - 1) == ',')
        removeStart--;
      if (objCount == 1 && json.charAt(objEnd + 1) == ',' ) {
        objEnd++; // remove the comma after the first object
      }
      
      json.remove(removeStart, objEnd - removeStart + 1);
      arrayEnd = json.indexOf(']', arrayStart); // refresh arrayEnd after removal
      pos = removeStart;                        // reset pos to the start of the removed object
    }
    else
    {
      pos = objEnd + 1;
    }
  }
}

// remove achievements with MemAddr longer than size from the json string
// used just if needed
void remove_achievements_with_long_MemAddr(String &json, uint32_t size)
{
  int achievementsPos = json.indexOf("\"Achievements\"");
  if (achievementsPos == -1)
    return;

  int arrayStart = json.indexOf('[', achievementsPos);
  int arrayEnd = json.indexOf(']', arrayStart);
  if (arrayStart == -1 || arrayEnd == -1)
    return;

  int pos = arrayStart + 1;
  while (pos < arrayEnd)
  {
    int objStart = json.indexOf('{', pos);
    if (objStart == -1 || objStart >= arrayEnd)
      break;

    int objEnd = objStart;
    int braceCount = 1;
    while (braceCount > 0 && objEnd + 1 < json.length())
    {
      objEnd++;
      if (json[objEnd] == '{')
        braceCount++;
      else if (json[objEnd] == '}')
        braceCount--;
    }

    if (objEnd >= arrayEnd)
      break;

    // Procura o campo "MemAddr"
    int memAddrPos = json.indexOf("\"MemAddr\"", objStart);
    if (memAddrPos == -1 || memAddrPos > objEnd)
    {
      pos = objEnd + 1;
      continue;
    }

    int valueStart = json.indexOf('"', memAddrPos + 9);
    if (valueStart == -1 || valueStart > objEnd)
    {
      pos = objEnd + 1;
      continue;
    }

    int valueEnd = json.indexOf('"', valueStart + 1);
    if (valueEnd == -1 || valueEnd > objEnd)
    {
      pos = objEnd + 1;
      continue;
    }

    int length = valueEnd - valueStart - 1;
    if (length > size)
    {

      int removeStart = objStart;
      int removeEnd = objEnd + 1;

      if (json[removeEnd] == ',')
        removeEnd++;
      else if (removeStart > arrayStart + 1 && json[removeStart - 1] == ',')
        removeStart--;

      json.remove(removeStart, removeEnd - removeStart);
      arrayEnd -= (removeEnd - removeStart);
      pos = removeStart;
    }
    else
    {
      pos = objEnd + 1;
    }
  }
}

// transform a array field from the json into a empty array
void clean_json_field_array_value(String &json, const String &field_to_remove)
{
  remove_space_new_lines_in_json(json);

  bool inside_quotes = false;
  bool inside_array = false;
  bool skip_field = false;
  int field_len = field_to_remove.length();
  int read_index = 0, write_index = 0, skip_init = 0;
  bool remove_next_array = false;
  while (read_index < json.length())
  {
    char c = json[read_index];
    if (c == '[')
    {
      inside_array = true;
      if (remove_next_array)
      {
        skip_field = true;
        json[write_index++] = '[';
      }
    }
    if (c == ']')
    {
      inside_array = false;
      if (remove_next_array)
      {
        remove_next_array = false;
        skip_field = false;
      }
    }

    if (c == '"')
    {
      if (json[read_index - 1] != '\\')
      {
        inside_quotes = !inside_quotes;
      }
    }

    if (inside_quotes && json.substring(read_index + 1, read_index + 1 + field_len) == field_to_remove && json[read_index + field_len + 1] == '"')
    {
      remove_next_array = true;
      skip_init = read_index + field_len + 2;
    }

    if (!skip_field)
    {
      json[write_index++] = c;
    }
    read_index++;
  }

  json.remove(write_index);
}

#ifdef ENABLE_LCD
/*
 * functions related to the png decoder and file management
 */

// open a png file from the LittleFS
void *png_open(const char *file_name, int32_t *size)
{
  Serial.printf("Attempting to open %s\n", file_name);
  png_file = FileSys.open(file_name, "r");
  *size = png_file.size();
  return &png_file;
}

// close a png file from the LittleFS
void png_close(void *handle)
{
  File png_file = *((File *)handle);
  if (png_file)
    png_file.close();
}

// read a png file from the LittleFS
int32_t png_read(PNGFILE *page, uint8_t *buffer, int32_t length)
{
  if (!png_file)
    return 0;
  page = page; // Avoid warning
  return png_file.read(buffer, length);
}

// seek a position in a png file from the LittleFS
int32_t png_seek(PNGFILE *page, int32_t position)
{
  if (!png_file)
    return 0;
  page = page; // Avoid warning
  return png_file.seek(position);
}

// callback function to draw pixels to the display
void png_draw(PNGDRAW *pDraw)
{
  uint16_t line_buffer[MAX_IMAGE_WIDTH];
  png.getLineAsRGB565(pDraw, line_buffer, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
  tft.pushImage(x_pos, y_pos + pDraw->y, pDraw->iWidth, 1, line_buffer);
}

void initLCD () {
  tft.begin(); // initialize the TFT screen
  #ifdef FLIP_SCREEN
    tft.setRotation(2);
  #endif

  //  draw the title screen
  tft.fillScreen(TFT_YELLOW);
  tft.setTextColor(TFT_BLACK, TFT_YELLOW, true);

  tft.setCursor(10, 10, 4);
  tft.setTextSize(1);
  tft.println("RetroAchievements");
  tft.setCursor(140, 40, 4);
  tft.println("Adapter");

  tft.fillRoundRect(16, 76, 208, 128, 15, TFT_BLUE);
  tft.fillRoundRect(20, 80, 200, 120, 12, TFT_BLACK);

  tft.setCursor(75, 220, 2);
  tft.println("by Odelot & GH");
}
#endif

/**
 * functions related to manage the fifo of achievements
 */

// initialize the fifo
void fifo_init(achievements_FIFO_t *fifo)
{
  fifo->head = 0;
  fifo->tail = 0;
  fifo->count = 0;
}

// check if the fifo is empty
bool fifo_is_empty(achievements_FIFO_t *fifo)
{
  return fifo->count == 0;
}

// check if the fifo is full
bool fifo_is_full(achievements_FIFO_t *fifo)
{
  return fifo->count == ACHIEVEMENTS_FIFO_SIZE;
}

// enqueue a achievement in the fifo
bool fifo_enqueue(achievements_FIFO_t *fifo, achievements_t value)
{
  if (fifo_is_full(fifo))
  {
    return false; // FIFO is full
  }
  fifo->buffer[fifo->tail] = value;
  fifo->tail = (fifo->tail + 1) % ACHIEVEMENTS_FIFO_SIZE;
  fifo->count++;
  return true;
}

// dequeue a achievement from the fifo
bool fifo_dequeue(achievements_FIFO_t *fifo, achievements_t *value)
{
  if (fifo_is_empty(fifo))
  {
    return false; // FIFO is empty
  }
  *value = fifo->buffer[fifo->head];
  fifo->head = (fifo->head + 1) % ACHIEVEMENTS_FIFO_SIZE;
  fifo->count--;
  return true;
}

/**
 * functions related to the sound
 */

// play a sound to get the user attention
void play_attention_sound()
{
  delay(35);
  tone(SOUND_PIN, NOTE_G4, 35);
  delay(35);
  tone(SOUND_PIN, NOTE_G5, 35);
  delay(35);
  tone(SOUND_PIN, NOTE_G6, 35);
  delay(35);
  noTone(SOUND_PIN);
  tone(SOUND_PIN, NOTE_G4, 35);
  delay(35);
  tone(SOUND_PIN, NOTE_G5, 35);
  delay(35);
  tone(SOUND_PIN, NOTE_G6, 35);
  delay(35);
  noTone(SOUND_PIN);
}

// play a sound to indicate a success
void play_success_sound()
{

  delay(130);
  tone(SOUND_PIN, NOTE_E6, 125);
  delay(130);
  tone(SOUND_PIN, NOTE_G6, 125);
  delay(130);
  tone(SOUND_PIN, NOTE_E7, 125);
  delay(130);
  tone(SOUND_PIN, NOTE_C7, 125);
  delay(130);
  tone(SOUND_PIN, NOTE_D7, 125);
  delay(130);
  tone(SOUND_PIN, NOTE_G7, 125);
  delay(125);
  noTone(SOUND_PIN);
}

// play a sound to indicate an error
void play_error_sound()
{
  delay(100);
  tone(SOUND_PIN, NOTE_G4);
  delay(250);
  tone(SOUND_PIN, NOTE_C4);
  delay(500);
  noTone(SOUND_PIN);
}

// play a sound to indicate an achievement unlocked
void play_sound_achievement_unlocked()
{
  int snd_notes_achievement_unlocked[] = {
      NOTE_D5, NOTE_D5, NOTE_CS6, NOTE_D6};

  int snd_velocity_achievement_unlocked[] = {
      52,
      37,
      83,
      74,
  };

  int snd_notes_duration_achievement_unlocked[] = {
      2,
      4,
      3,
      12,
  };
  for (int this_note = 0; this_note < 4; this_note++)
  {
    int note_duration = snd_notes_duration_achievement_unlocked[this_note] * 16;
    tone(SOUND_PIN, snd_notes_achievement_unlocked[this_note], snd_velocity_achievement_unlocked[this_note]);
    delay(note_duration);
    noTone(SOUND_PIN);
  }
}


/**
 * functions related to the TFT screen and PNG decoder
 */

// print a line of text in the TFT screen
void print_line(String text, int line, int line_status)
{
  print_line(text, line, line_status, 0);
}

// print a line of text in the TFT screen with a delta (left to right)
void print_line(String text, int line, int line_status, int delta)
{
  #ifdef ENABLE_LCD
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK, true);
  tft.setCursor(20, 90 + line * 22, 2);
  tft.println("                                 ");
  tft.setCursor(46 + delta, 90 + line * 22, 2);
  if (text.length() == 0)
  {
    return;
  }
  tft.println(text);
  if (line_status == 2)
  {
    tft.fillCircle(32, 98 + line * 22, 7, TFT_RED);
  }
  else if (line_status == 1)
  {
    tft.fillCircle(32, 98 + line * 22, 7, TFT_YELLOW);
  }
  else if (line_status == 0)
  {
    tft.fillCircle(32, 98 + line * 22, 7, TFT_GREEN);
  }
  #endif
}

// clean the text in the TFT screen
void clean_screen_text()
{
  print_line("", 0, 0);
  print_line("", 1, 0);
  print_line("", 2, 0);
  print_line("", 3, 0);
  print_line("", 4, 0);
}





// show the title screen
void show_title_screen()
{
  setSemaphore(LED_ON, LED_GREEN);
#ifdef ENABLE_LCD  
  analogWrite(LCD_BRIGHTNESS_PIN, 100); // set the brightness of the TFT screen
  setCpuFrequencyMhz(160);
  tft.setTextColor(TFT_BLACK, TFT_YELLOW, true);

  tft.setTextSize(1);
  tft.setTextPadding(240);
  tft.drawString("", 0, 10, 4);
  tft.drawString("", 0, 40, 4);
  tft.setTextDatum(MC_DATUM);

  // if game name is longer than 18 chars, add ... at the end
  String esp_game_name = game_name;
  if (esp_game_name.length() > 18)
  {
    esp_game_name = esp_game_name.substring(0, 18 - 3) + "...";
  }

  tft.drawString(esp_game_name, 120, 40, 4);
  tft.setTextPadding(0);

  tft.fillRoundRect(20, 80, 200, 120, 12, TFT_BLACK);
  x_pos = 72;
  y_pos = 92;
  String file_name = "/title_" + game_id + ".png";
  int16_t rc = png.open(file_name.c_str(), png_open, png_close, png_read, png_seek, png_draw);
  if (rc == PNG_SUCCESS)
  {
    tft.startWrite();
    uint32_t dt = millis();
    rc = png.decode(NULL, 0);
    tft.endWrite();
    png.close();
  }
  already_showed_title_screen = true;
  setCpuFrequencyMhz(80);
#endif
}

// show the achievement screen
void show_achievement(achievements_t achievement)
{
  setSemaphore(LED_BLINK_FAST, LED_GREEN);
  
#ifdef ENABLE_INTERNAL_WEB_APP_SUPPORT
  char aux[256];
  sprintf(aux, "A=%s;%s;%s", "0", achievement.title.c_str(), achievement.url.c_str());
  send_ws_data(aux);
#endif
#ifdef ENABLE_LCD  
  analogWrite(LCD_BRIGHTNESS_PIN, 200); // set the brightness of the TFT screen
  // if achievement title is longer than 26 chars, add ... at the end
  if (achievement.title.length() > 26)
  {
    achievement.title = achievement.title.substring(0, 26 - 3) + "...";
  }

  setCpuFrequencyMhz(160);
  tft.fillRoundRect(20, 80, 200, 120, 12, TFT_BLACK);
  print_line("New Achievement Unlocked!", 0, 0);
  print_line("", 1, 0);
  print_line("", 2, 0);
  print_line("", 3, 0);
  print_line(achievement.title, 4, -1);
  Serial.println(achievement.title);
  x_pos = 50;
  y_pos = 110;
  char file_name[64];
  sprintf(file_name, "/achievement_%d.png", achievement.id);
  try_download_file(achievement.url, file_name);
  int16_t rc = png.open(file_name, png_open, png_close, png_read, png_seek, png_draw);
  if (rc == PNG_SUCCESS)
  {
    Serial.print("Successfully opened png file");
    Serial.printf("image specs: (%d x %d), %d bpp, pixel type: %d\n", png.getWidth(), png.getHeight(), png.getBpp(), png.getPixelType());
    tft.startWrite();
    uint32_t dt = millis();
    rc = png.decode(NULL, 0);
    Serial.print(millis() - dt);
    Serial.print("ms");
    tft.endWrite();
    png.close();
  }
  play_sound_achievement_unlocked();
  go_back_to_title_screen = true;
  go_back_to_title_screen_timestamp = millis();
  setCpuFrequencyMhz(80);
#endif
}


/**
 * functions related to the EEPROM
 */

// initialize the EEPROM
void init_EEPROM(bool force)
{
  EEPROM.begin(EEPROM_SIZE);
  if (EEPROM.read(0) != EEPROM_ID_1 || EEPROM.read(1) != EEPROM_ID_2 || force == true)
  {
    EEPROM.write(0, EEPROM_ID_1);
    EEPROM.write(1, EEPROM_ID_2);
    for (int i = 2; i < EEPROM_SIZE; i++)
    {
      EEPROM.write(i, 0);
    }
    EEPROM.commit();
    Serial.print("eeprom initialized");
  }
}

// check if the EEPROM is configured
bool is_eeprom_configured()
{
  return EEPROM.read(2) == 1;
}

// save the RA credentials in the EEPROM
void save_configuration_info_eeprom(String ra_user, String ra_pass)
{
  
  uint8_t user_len = ra_user.length();
  uint8_t pass_len = ra_pass.length();
  EEPROM.write(2, 1);
  EEPROM.write(3, user_len);
  for (int i = 0; i < user_len; i++)
  {
    EEPROM.write(4 + i, ra_user[i]);
  }
  EEPROM.write(4 + user_len, pass_len);
  for (int i = 0; i < pass_len; i++)
  {
    EEPROM.write(5 + user_len + i, ra_pass[i]);
  }
  EEPROM.write(6 + user_len + pass_len, 0);
  EEPROM.commit();
}

// read the RA user from the EEPROM
String read_ra_user_from_eeprom()
{
  uint8_t len = EEPROM.read(3);
  String ra_user = "";
  for (int i = 0; i < len; i++)
  {
    ra_user += (char)EEPROM.read(4 + i);
  }
  return ra_user;
}

// read the RA pass from the EEPROM
String read_ra_pass_from_eeprom()
{
  uint8_t ra_user_len = EEPROM.read(3);
  uint8_t len = EEPROM.read(4 + ra_user_len);
  String ra_pass = "";
  for (int i = 0; i < len; i++)
  {
    ra_pass += (char)EEPROM.read(5 + ra_user_len + i);
  }
  return ra_pass;
}

/**
 * functions related to the wifi manager
 */

void wifi_manager_init(WiFiManager &wm)
{
  // wm.setDebugOutput(true);
  wm.setHostname("nes-ra-adapter");
  wm.setBreakAfterConfig(true);
  wm.setCaptivePortalEnable(true);
  wm.setMinimumSignalQuality(40);
  wm.setConnectTimeout(30);
  wm.setCustomHeadElement(head);
  wm.setDarkMode(true);
  wm.setAPStaticIPConfig(IPAddress(192, 168, 1, 1), IPAddress(192, 168, 1, 1), IPAddress(255, 255, 255, 0));
}

// remove Arduino JSON and save some RAM
String extractToken(String &json)
{
  String key = "\"Token\":";
  int start = json.indexOf(key);
  if (start == -1)
    return "";

  // Avança até o início do valor (pula o ':', espaços e aspas)
  start = json.indexOf("\"", start + key.length());
  if (start == -1)
    return "";

  int end = json.indexOf("\"", start + 1);
  if (end == -1)
    return "";

  return json.substring(start + 1, end);
}

/**
 * functions related to the RA login
 */
String try_login_RA(String ra_user, String ra_pass)
{
  String login_path = "r=login&u=@USER@&p=@PASS@";
  login_path.replace("@USER@", ra_user);
  login_path.replace("@PASS@", ra_pass);

  StreamString response;
  response.reserve(256);
  int ret = perform_http_request(base_url + login_path, GET, login_path, response, true, 3, 5000, 500);
  if (ret < 0)
  {
    Serial.print("request error: ");
    Serial.println(http_request_result_to_string(ret));
    state = 253; // error - login failed
    Serial0.print("ERROR=253-LOGIN_FAILED\r\n");
    Serial.print("ERROR=253-LOGIN_FAILED\r\n");
    return String("null");
  }

  String ra_token = extractToken(response);

  return ra_token;
}

/**
 * functions related to reset the adapter configuration
 */

// handle the reset routine
void handle_reset()
{
  if (ENABLE_RESET == 0)
  {
    return;
  }
  unsigned long start = millis();
  setSemaphore(LED_BLINK_FAST, LED_YELLOW);
  while (digitalRead(RESET_PIN) == LOW && (millis() - start) < 10000)
  {
    yield();
    print_line("Resetting in progress...", 0, 1);
  }
  if ((millis() - start) >= RESET_PRESSED_TIME)
  {
    setSemaphore(LED_BLINK_SLOW, LED_YELLOW);
    Serial.print("reset");
    init_EEPROM(true);
    print_line("Reset successful!", 1, 0);
    print_line("Reboot in 5 seconds...", 2, 0);
    delay(5000);
    ESP.restart();
  }
  setSemaphore(LED_BLINK_MEDIUM, LED_YELLOW);
  print_line("Reset aborted!", 0, 1);
}

/**
 * functions related to the littleFS
 */

// retry download a file up to 3 times with exponential backoff
int try_download_file(String url, String file_name)
{
  int attempt = 0;
  int maxRetries = 3;
  int retryDelayMs = 250;
  while (attempt < maxRetries)
  {
    int ret = download_file_to_littleFS(url, file_name);
    if (ret == 0)
    {
      return 0; // File downloaded successfully
    }
    else
    {
      Serial.printf("Attempt %d failed. Retrying...\n", attempt + 1);
    }
    attempt++;
    if (attempt <= maxRetries)
    {
      delay(retryDelayMs * pow(2, attempt)); // Exponential backoff
    }
  }
  return -1;
}

// Fetch a file from the URL given and save it in LittleFS
// Return 1 if a web fetch was needed or 0 if file already exists
int download_file_to_littleFS(String url, String file_name)
{
  int ret = 0;
  // If it exists then no need to fetch it
  if (LittleFS.exists(file_name) == true)
  {
    Serial.print("Found " + file_name + " in LittleFS\n"); // debug
    return ret;
  }

  // before use LittleFS, check if we have enough space and delete files if necessary
  check_free_space_littleFS();

  Serial.print("Downloading " + file_name + " from " + url + "\n"); // debug

  // Check WiFi connection
  if ((WiFi.status() == WL_CONNECTED))
  {
    NetworkClientSecure client;
    HTTPClient http;
    client.setInsecure();
    http.begin(client, url);

    // Start connection and send HTTP header
    int http_code = http.GET();
    if (http_code == 200)
    {
      fs::File f = LittleFS.open(file_name, "w+");
      if (!f)
      {
        Serial.print("file open failed\n"); // debug
        return -1;
      }

      // File found at server
      if (http_code == HTTP_CODE_OK)
      {

        // Get length of document (is -1 when Server sends no Content-Length header)
        int total = http.getSize();
        int len = total;

        // Create buffer for read
        uint8_t buff[128] = {0};

        // Get tcp stream
        WiFiClient *stream = http.getStreamPtr();

        // Read all data from server
        while (http.connected() && (len > 0 || len == -1))
        {
          // Get available data size
          size_t size = stream->available();

          if (size)
          {
            // Read up to 128 bytes
            int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));

            // Write it to file
            f.write(buff, c);

            // Calculate remaining bytes
            if (len > 0)
            {
              len -= c;
            }
          }
          yield();
        }
      }
      f.close();
    }
    else
    {
      Serial.printf("download failed, error: %s\n", http.errorToString(http_code).c_str()); // debug
      ret = -1;
    }
    http.end();
  }
  return ret;
}

// auxiliary function to implement startsWith for char*
bool prefix(const char *pre, const char *str)
{
  return strncmp(pre, str, strlen(pre)) == 0;
}

// monitor wifi events - connection
void onWiFiConnect(WiFiEvent_t event, WiFiEventInfo_t info)
{
  Serial.println(F("WiFi connected."));
}

// monitor wifi events - disconnection
void onWiFiDisconnect(WiFiEvent_t event, WiFiEventInfo_t info)
{
  Serial.println(F("WiFi disconnected."));
  WiFi.reconnect();
}

// monitor wifi events - got IP address
void onWiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info)
{
  Serial.print(F("Got IP: "));
  Serial.println(IPAddress(info.got_ip.ip_info.ip.addr));
#ifdef ENABLE_INTERNAL_WEB_APP_SUPPORT
  config_mDNS();
#endif
}

// calculate how much free space we have on littleFS in percentage
float get_free_space_littleFS()
{
  float free_space = (float)LittleFS.totalBytes() - (float)LittleFS.usedBytes();
  free_space = (free_space / (float)LittleFS.totalBytes()) * 100.0;
  // Serial.print("Total Bytes: "); //debug
  // Serial.println(LittleFS.totalBytes()); //debug
  // Serial.print("Used Bytes: "); //debug
  // Serial.println(LittleFS.usedBytes()); //debug
  Serial.print("Free space: ");
  Serial.print(free_space);
  Serial.println("%");
  return free_space;
}

// remove all images from littleFS
void remove_all_image_files_littleFS()
{
  fs::File root = LittleFS.open("/");
  fs::File file = root.openNextFile();
  while (file)
  {
    String file_name = file.name();
    file.close();
    if (prefix("achievement_", file_name.c_str()) || prefix("title_", file_name.c_str()))
    {
      Serial.print("Removing " + file_name + "\n"); // debug
      file_name = "/" + file_name;
      LittleFS.remove(file_name);
    }
    file = root.openNextFile();
  }
}

// remove all files if we are running out of space
void check_free_space_littleFS()
{
  float free_space = get_free_space_littleFS();
  if (free_space < 3) // each image uses ~0.5%
  {
    Serial.print("Free space is less than 3%, removing all image files\n"); // debug
    remove_all_image_files_littleFS();
  }
}

/**
 * functions related with mDNS, websocket and the experimental internal web app
 */

#ifdef ENABLE_INTERNAL_WEB_APP_SUPPORT

// configure mDNS
void config_mDNS()
{
  String mdnsName = "nes-ra-adapter";

  MDNS.end();
  if (!MDNS.begin(mdnsName.c_str()))
  {
    Serial.println("error - begin mDNS");
    return;
  }

  Serial.print("mDNS init: ");
  Serial.println(mdnsName + ".local");

  MDNS.addService("nraa", "tcp", 80);
  Serial.println("service mDNS announced");
}

// handle websocket events
void on_websocket_event(AsyncWebSocket *server, AsyncWebSocketClient *client,
                        AwsEventType type, void *arg, uint8_t *data, size_t len)
{

  if (type == WS_EVT_CONNECT)
  {
    // just one simultaneous connection for the time being
    Serial.println("new ws connection");
    if (ws_client != NULL)
    {
      Serial.println("closing last connection");
      ws_client->close();
    }
    ws_client = client;
    client->setCloseClientOnQueueFull(false);
    client->ping();
  }
  else if (type == WS_EVT_PONG)
  {
    Serial.println("ws pong");
    if (game_id != "0") // we have a game running
    {
      // send the game name and image to the client
      char aux[512];
      sprintf(aux, "G=%s;%s;%s;%s", game_session.c_str(), game_id.c_str(), game_name.c_str(), game_image.c_str());
      client->text(aux);
    }
  }
  else if (type == WS_EVT_DISCONNECT)
  {
    Serial.println("ws disconnect");
    if (ws_client != NULL)
    {
      ws_client->close();
      ws_client == NULL;
    }
  }
  else if (type == WS_EVT_DATA)
  {
    data[len] = 0; // garante terminação
    if (strcmp((char *)data, "ping") == 0)
    {
      Serial.println("ws ping");
      client->text("pong");
    }
  }
}

// send websocket data
void send_ws_data(String data)
{
  if (ws_client != NULL)
  {
    ws_client->text(data);
  }
}

// initialize the websocket server
void init_websocket()
{

  // ws begin
  ws.onEvent(on_websocket_event);
  // Servir arquivos estáticos (HTML, CSS, JS)
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  server.serveStatic("/sw.js", LittleFS, "/sw.js")
      .setCacheControl("no-cache, no-store, must-revalidate");
  server.serveStatic("/index.html", LittleFS, "/index.html")
      .setCacheControl("no-cache, no-store, must-revalidate");

  server.serveStatic("/snd.mp3", LittleFS, "/snd.mp3")
      .setCacheControl("max-age=86400");

  server.addHandler(&ws);
  server.begin();

  // // prevent a second WS client to connect - i am not sure if it is really needed since I disconnect the old one before conneceting the new one
  // server.addHandler(&ws).addMiddleware([](AsyncWebServerRequest *request, ArMiddlewareNext next)
  // {
  //   // ws.count() is the current count of WS clients: this one is trying to upgrade its HTTP connection
  //   if (ws.count() > 0) {
  //     // if we have 1 clients prevent the next one to connect
  //     request->send(503, "text/plain", "Server is busy");
  //   } else {
  //     // process next middleware and at the end the handler
  //     next();
  //   }
  // });
}
#endif

/**
 * functions related to the HTTP requests, retry, error handling, etc
 */

// HTTP request result codes to string
String http_request_result_to_string(int code)
{
  switch (code)
  {
  case HTTP_SUCCESS:
    return "HTTP_SUCCESS";
  case HTTP_ERR_NO_WIFI:
    return "HTTP_ERR_NO_WIFI";
  case HTTP_ERR_REQUEST_FAILED:
    return "HTTP_ERR_REQUEST_FAILED";
  case HTTP_ERR_HTTP_4XX:
    return "HTTP_ERR_HTTP_4XX";
  case HTTP_ERR_TIMEOUT:
    return "HTTP_ERR_TIMEOUT";
  case HTTP_ERR_REPONSE_TOO_BIG:
    return "HTTP_ERR_REPONSE_TOO_BIG";
  default:
    return "UNKNOWN_ERROR";
  }
}

// perform an HTTP request with retries and exponential backoff
int perform_http_request(
    const String &url,
    HttpRequestMethod method,
    const String &payload,
    StreamString &response,
    bool isIdempotent,
    int maxRetries,
    int timeoutMs,
    int retryDelayMs)
{
  int attempt = 0;
  int wifiRetries = 3;
  int code = HTTP_ERR_REQUEST_FAILED;

  while (attempt <= maxRetries)
  {
    if (attempt != 0)
    {
      Serial.println("Attemp " + String(attempt) + " to request");
    }
    int wifiAttempt = 0;
    while (WiFi.status() != WL_CONNECTED && wifiAttempt < wifiRetries)
    {
      delay(500);
      wifiAttempt++;
    }

    if (WiFi.status() != WL_CONNECTED)
    {
      code = HTTP_ERR_NO_WIFI;
      delay(retryDelayMs * pow(2, attempt)); // Backoff mesmo para WiFi
      attempt++;
      continue;
    }
    NetworkClientSecure client;
    client.setInsecure(); // Disable SSL certificate verification
    HTTPClient https;
    https.setTimeout(timeoutMs);
    https.begin(client, url);
    const char user_agent[] = "NES_RA_ADAPTER/1.0 rcheevos/11.6";
    https.setUserAgent(user_agent);
    if (method == GET)
    {
      code = https.GET();
    }
    else
    { // POST
      https.addHeader("Content-Type", "application/x-www-form-urlencoded");
      code = https.POST(payload);
    }

    if (code > 0)
    {
      if (code >= 200 && code < 300)
      {
        int ret = https.writeToStream(&response);
        if (ret < 0)
        {
          Serial.print(F("ERROR ON RESPONSE: "));
          Serial.println(ret);
          https.end();
          return HTTP_ERR_REPONSE_TOO_BIG;
        }
        https.end();
        return HTTP_SUCCESS;
      }
      else if (code >= 400 && code < 500)
      {
        https.end();
        return HTTP_ERR_HTTP_4XX;
      }
      else
      {
        // Erros 5xx ou outros
        https.end();
        if (!isIdempotent && method == HTTP_POST)
        {
          return HTTP_ERR_REQUEST_FAILED;
        }
      }
    }
    else
    {
      // Timeout, falha de conexão, etc
      if (code == HTTPC_ERROR_CONNECTION_REFUSED ||
          code == HTTPC_ERROR_READ_TIMEOUT ||
          code == HTTPC_ERROR_CONNECTION_LOST)
      {
        code = HTTP_ERR_TIMEOUT;
      }
      else
      {
        code = HTTP_ERR_REQUEST_FAILED;
      }
    }

    attempt++;
    if (attempt <= maxRetries)
    {
      delay(retryDelayMs * pow(2, attempt)); // Exponential backoff
    }
  }

  return code;
}

// arduino-esp32 entry point
void setup()
{
  
  setCpuFrequencyMhz(80);
  // disable i2c
  Wire.end();
  // BLEDevice::deinit(true);

  // config analog switch control pin
  pinMode(ANALOG_SWITCH_PIN, OUTPUT);
  digitalWrite(ANALOG_SWITCH_PIN, ANALOG_SWITCH_DISABLE_BUS); // isolate the cartridge from NES

  //config lcd brightness
  pinMode(LCD_BRIGHTNESS_PIN, OUTPUT);
  analogWrite(LCD_BRIGHTNESS_PIN, 100); // set the brightness of the TFT screen

  // config the led status pin
  pinMode(LED_STATUS_GREEN_PIN, OUTPUT);
  pinMode(LED_STATUS_RED_PIN, OUTPUT);

  setSemaphore(LED_BLINK_MEDIUM, LED_YELLOW);

  Serial.begin(9600);    // initialize the serial port
  Serial0.setTimeout(2); // 2ms timeout for Serial0
  Serial0.begin(115200);
  // consume any garbage in the serial buffer before communicating with the pico
  if (Serial0.available() > 0)
  {
    Serial0.readString();
    delay(10);
  }

  // hard reset the pico
  Serial0.print("RESET\r\n");

  // config reset pin
  pinMode(RESET_PIN, INPUT);

  // reserve a buffer for the streamstring
  buffer.reserve(256);
  buffer.clear();

  // initialize stuff
  init_EEPROM(false); // initialize the EEPROM

  fifo_init(&achievements_fifo); // initialize the achievement fifo

  

  // delay(250);
  if (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)) // Initialise LittleFS
  {
    Serial.print("LittleFS initialisation failed!"); // debug
    setSemaphore(LED_BLINK_FAST, LED_RED);
    while (1)
      yield(); // Stay here twiddling thumbs waiting
  }

  char esp_version[32];
  sprintf(esp_version, "ESP32_VERSION=%s\r\n", VERSION);
  Serial0.print(esp_version); // send the version to the pico - debug

#ifdef ENABLE_LCD
  initLCD ();
#endif

  // check if the reset button is pressed and handle the reset routine
  handle_reset();

  // check if the EEPROM is configured. If not, start the configuration portal
  int wifi_configuration_tries = 0;
  bool wifi_configured = false;
  bool configured = is_eeprom_configured();
  if (!configured)
  {
    WiFiManager wm;
    WiFiManagerParameter custom_p1(html_p1);
    WiFiManagerParameter custom_p2(html_p2);
    WiFiManagerParameter custom_param_1("un", NULL, "", 20, " required autocomplete='off'");
    WiFiManagerParameter custom_p3(html_p3);
    WiFiManagerParameter custom_param_2("up", NULL, "", 255, " type='password' required");
    WiFiManagerParameter custom_s(html_s);
    wifi_manager_init(wm); // initialize the wifi manager
    wm.addParameter(&custom_p1);
    wm.addParameter(&custom_p2);
    wm.addParameter(&custom_param_1);
    wm.addParameter(&custom_p3);
    wm.addParameter(&custom_param_2);
    wm.addParameter(&custom_s);
    while (!configured)
    {
      if (wifi_configuration_tries == 0 && !wifi_configured)
      {
        setSemaphore(LED_BLINK_MEDIUM, LED_YELLOW); 
        print_line(" Configure the Adapter:", 0, 1);
        print_line("Connect to its wifi network", 1, -1, -16);
        print_line("named \"NES_RA_ADAPTER\",", 2, -1, -16);
        print_line("password: 12345678, and ", 3, -1, -16);
        print_line("then open http://192.168.1.1", 4, -1, -16);
        play_attention_sound();
      }
      else if (wifi_configuration_tries > 0 && !wifi_configured)
      {
        setSemaphore(LED_BLINK_SLOW, LED_RED);
        print_line("Could not connect to wifi", 0, 2);
        print_line("network!", 1, -1, 16);
        print_line("check it and try again", 3, -1, 16);
        play_error_sound();
      }
      else if (wifi_configured)
      {
        setSemaphore(LED_BLINK_MEDIUM, LED_RED);
        print_line("Could not log in", 0, 2);
        print_line("RetroAchievements!", 1, -1, 16);
        print_line("check the credentials", 3, -1, 16);
        print_line("and try again", 4, -1, 16);
        play_error_sound();
      }
      if (wm.startConfigPortal("NES_RA_ADAPTER", "12345678"))
      {
        setSemaphore(LED_BLINK_SLOW, LED_GREEN);
        Serial.print("connected...yeey :)");
        wifi_configured = true;
        clean_screen_text();
        print_line("Wifi OK!", 0, 0);
        String temp_ra_user = custom_param_1.getValue();
        String temp_ra_pass = custom_param_2.getValue();
        ra_user_token = try_login_RA(temp_ra_user, temp_ra_pass);
        Serial.print(ra_user_token);
        if (ra_user_token.compareTo("null") != 0)
        {
          setSemaphore(LED_BLINK_MEDIUM, LED_GREEN);
          print_line("Logged in RA!", 1, 0);
          play_success_sound();
          Serial.println(F("saving configuration info in eeprom"));
          save_configuration_info_eeprom(temp_ra_user, temp_ra_pass);
          ESP.restart(); // just to play safe as we are using all the RAM we can get to receive the achievement list
        }
        else
        {
          setSemaphore(LED_BLINK_FAST, LED_RED);
          Serial.print(F("could not log in RA"));
          clean_screen_text();
        }
      }
      else
      {
        setSemaphore(LED_BLINK_MEDIUM, LED_RED);
        Serial.print(F("not connected...booo :("));
        clean_screen_text();
        wifi_configuration_tries += 1;
      }
      configured = is_eeprom_configured();
    }
  }
  else
  {
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_15dBm);
    print_line("Connecting to Wifi...", 0, 1);
    if (WiFi.status() != WL_CONNECTED)
    {
      WiFi.begin();
      while (WiFi.status() != WL_CONNECTED)
        delay(500);
    }
    setSemaphore(LED_BLINK_SLOW, LED_GREEN);
    print_line("Wifi OK!", 0, 0); // TODO: implement timeout
    String ra_user = read_ra_user_from_eeprom();
    String ra_pass = read_ra_pass_from_eeprom();
    print_line("Logging in RA...", 1, 1);
    ra_user_token = try_login_RA(ra_user, ra_pass);
    if (ra_user_token.compareTo("null") != 0)
    {
      setSemaphore(LED_BLINK_MEDIUM, LED_GREEN);
      print_line("Logged in RA!", 1, 0);
      play_success_sound();
    }
    else
    {
      setSemaphore(LED_BLINK_FAST, LED_RED);
      print_line("Could not log in RA!", 1, 2);
      print_line("Consider reset the adapter", 3, -1, 16);
      play_error_sound();
    }
  }

#ifdef ENABLE_INTERNAL_WEB_APP_SUPPORT
  config_mDNS();
#endif

  // handle reconnects
  WiFi.onEvent(onWiFiConnect, ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(onWiFiDisconnect, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.onEvent(onWiFiGotIP, ARDUINO_EVENT_WIFI_STA_GOT_IP);

  // send the token and user to the pico (needed )
  char token_and_user[512];
  sprintf(token_and_user, "TOKEN_AND_USER=%s,%s\r\n", ra_user_token.c_str(), read_ra_user_from_eeprom().c_str());

  delay(250);                   // make sure pico restarted
  Serial.print(token_and_user); // debug
  Serial0.print(token_and_user);  
  state = 0;

  // modem sleep
  WiFi.setSleep(true);
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
  // esp_wifi_set_ps(WIFI_PS_MAX_MODEM);

  Serial.print(F("setup end")); // debug
}

// arduino-esp32 main loop
void loop()
{

#ifdef ENABLE_INTERNAL_WEB_APP_SUPPORT
  uint32_t delta_ws_cleanup = millis() - last_ws_cleanup;
  last_ws_cleanup = millis();
  if (delta_ws_cleanup > 1000)
  {
    ws.cleanupClients();
  }

  // #TODO last ping was too long ago, closes the connection
#endif

  long go_back_to_title_delta_timestamp = millis() - go_back_to_title_screen_timestamp;
  // check if we can go back to the title screen after showing the achievement for 15 seconds
  if (go_back_to_title_screen == true && go_back_to_title_delta_timestamp > 15000)
  {
    go_back_to_title_screen = false;
    if (fifo_is_empty(&achievements_fifo))
    {
      
      show_title_screen();
    }
  }

  // handle errors during the cartridge identification
  if (state > 200)
  {
    setSemaphore(LED_BLINK_FAST, LED_RED);
    // 200 - 254 are error trying to identify the cartridge
    state = 128; // do nothing - it will not enable the BUS, so the game will not boot
    print_line("Cartridge not identified", 1, 2);
    print_line("Turn off the console", 4, 2);
    play_error_sound();
  }

  if (state == 198)
  {
    setSemaphore(LED_BLINK_FAST, LED_RED);
    // connectivity error - no wifi or no result from the server after some retries
    state = 128; // do nothing - it will not enable the BUS, so the game will not boot
    print_line("Connectivity Error", 1, 2);
    print_line("It's not recording achievs", 2, 2);
    print_line("Turn off the console", 4, 2);
    play_error_sound();
  }

  if (state == 199)
  {
    setSemaphore(LED_BLINK_FAST, LED_RED);
    // json is too big
    state = 128; // do nothing - it will not enable the BUS, so the game will not boot
    print_line("RA response is too big", 1, 2);
    print_line("Turn off the console", 4, 2);
    play_error_sound();
  }

  // handle the cartridge identification
  if (state == 0)
  {
    setSemaphore(LED_BLINK_FAST, LED_GREEN);
    print_line("Identifying cartridge...", 1, 1);

    Serial0.print(F("READ_CRC\r\n")); // send command to read cartridge crc
    Serial.print(F("READ_CRC\r\n"));  // send command to read cartridge crc
    delay(250);
    state = 1;

    // USEFUL FOR TESTING - FORCING SOME GAME, IN THIS CASE, RC PRO AM
    // md5 = "2178cc3772b01c9f3db5b2de328bb992";
    // state = 2;
  }

  // inform the pico to start the process to watch the BUS for memory writes
  if (state == 2)
  {
    Serial0.print(F("START_WATCH\r\n"));
    Serial.print(F("START_WATCH\r\n"));
    state = 3;
  }

  // if there is some achievement to be shown, show it
  if (fifo_is_empty(&achievements_fifo) == false && Serial.available() == 0 && go_back_to_title_screen == false) // show achievements
  {
    achievements_t achievement;
    fifo_dequeue(&achievements_fifo, &achievement);
    show_achievement(achievement);
  }

  // handle the serial communication with the pico
  while (Serial0.available() > 0)
  {
    buffer += Serial0.readString();
    if (buffer.length() > 2048) // max command size from pico - 2048 bytes
    {
      buffer.clear();
      Serial0.print(F("BUFFER_OVERFLOW\r\n"));
      Serial.print(F("BUFFER_OVERFLOW\r\n"));
    }

    // wait for a command terminator \r\n
    while (buffer.indexOf("\r\n") > 0)
    {
      // we have a command \o/
      String command = buffer.substring(0, buffer.indexOf("\n") + 1);
      buffer.remove(0, buffer.indexOf("\n") + 1); // shift string left, removing the command from the buffer

      // handle a http request command
      if (command.startsWith("REQ"))
      {
        // example of request
        // REQ=FF;M:POST;U:https://retroachievements.org/dorequest.php;D:r=login2&u=user&p=pass
        StreamString request;
        request.reserve(command.length());
        request += command.substring(4);
        int pos = request.indexOf(";");
        String request_id = request.substring(0, pos);
        request.remove(0, pos + 1);
        pos = request.indexOf(";");
        String method = request.substring(2, pos);
        request.remove(0, pos + 1);
        pos = request.indexOf(";");
        String url = request.substring(2, pos);
        String data = request.substring(pos + 3);
        // Serial.println("REQ_METHOD=" + method); // debug
        // Serial.println("REQ_URL=" + url); // debug
        // Serial.println("REQ_DATA=" + data); // debug
        int http_code;

        if (prefix("r=patch", data.c_str()) && ENABLE_SHRINK_LAMBDA == 1)
        {
          // change to a temporary aws lambda that shrinks the patch JSON
          Serial.println("ENABLED LAMBDA SHRINK");
          url = SHRINK_LAMBDA_URL;
        }

        int reserve;
        if (prefix("r=patch", data.c_str()))
        {
          data.trim();
          data += "&f=3"; // filter achievements with flag = 5
          // because of this amount of RAM we can only enable the websocket after get the achievement list
          reserve = 65250; // bold - need more tests, but at least supports Final Fantasy for now - could allocate 65250 in tests      
          if (ENABLE_SHRINK_LAMBDA)
          {
            reserve = 32768;
          }
        }
        else
        {
          reserve = 512;
        }

        StreamString response;
        // reserve memory for the response
        if (!response.reserve(reserve))
        {
          Serial.print(F("could not allocate: "));
          Serial.print(reserve);
          Serial.println(F(" KB"));
          state = 199; // json to big error
        }
        else
        {
          int ret = perform_http_request(url, POST, data, response, true, 3, 5000, 500);
          if (ret < 0)
          {
            Serial.print(F("ERROR ON RESPONSE: "));
            Serial.println(http_request_result_to_string(ret));
            if (ret == HTTP_ERR_REPONSE_TOO_BIG)
            {
              state = 199; // json to big error
            }
            else
            {
              state = 198; // connectivity error
            }
          }
          else
          {
            // clean the json response to save some bytes and speed up transfer for pico
            if (prefix("r=patch", data.c_str()))
            {
              // print response size and header content size

              Serial.print(F("PATCH LENGTH: ")); // debug
              Serial.println(response.length()); // debug
              clean_json_field_str_value(response, "Description");
              remove_json_field(response, "Warning");
              remove_json_field(response, "BadgeLockedURL");
              remove_json_field(response, "BadgeURL");
              remove_json_field(response, "ImageIconURL");
              remove_json_field(response, "Rarity");
              remove_json_field(response, "RarityHardcore");
              remove_json_field(response, "Author");
              remove_json_field(response, "RichPresencePatch");
              clean_json_field_array_value(response, "Leaderboards");
              remove_achievements_with_flags_5(response);
              if (response.length() > SERIAL_MAX_PICO_BUFFER)
              {
                Serial.println(F("removing achievements with MemAddr > 4KB"));
                remove_achievements_with_long_MemAddr(response, 4096);
              }
              if (response.length() > SERIAL_MAX_PICO_BUFFER)
              {
                Serial.println(F("removing achievements with MemAddr > 2KB"));
                remove_achievements_with_long_MemAddr(response, 2048);
              }
              if (response.length() > SERIAL_MAX_PICO_BUFFER)
              {
                Serial.println(F("removing achievements with MemAddr > 1KB"));
                remove_achievements_with_long_MemAddr(response, 1024);
              }
              if (response.length() > SERIAL_MAX_PICO_BUFFER)
              {
                Serial.println(F("removing achievements with MemAddr > 768B"));
                remove_achievements_with_long_MemAddr(response, 768);
              }
              if (response.length() > SERIAL_MAX_PICO_BUFFER)
              {
                Serial.println(F("removing achievements with MemAddr > 512B"));
                remove_achievements_with_long_MemAddr(response, 512);
              }
              Serial.println(response);
              Serial.print(F("NEW PATCH LENGTH: ")); // debug
              Serial.println(response.length());     // debug
            }
            else if (prefix("r=login", data.c_str()))
            {
              remove_json_field(response, "AvatarUrl");
            }
          }
        }
        if (state < 198 && response.length() < SERIAL_MAX_PICO_BUFFER)
        {
          Serial0.print("RESP=");
          Serial0.print(request_id);
          Serial0.print(";");
          Serial0.print("200");
          Serial0.print(";");

          // send the response to the pico in chunks
          const char *ptr = response.c_str();
          uint32_t len = response.length();
          uint32_t offset = 0;

          while (offset < len)
          {
            uint32_t chunk_len = min((uint32_t)SERIAL_COMM_CHUNK_SIZE, (uint32_t)(len - offset));
            Serial0.write((const uint8_t *)&ptr[offset], chunk_len);
            Serial0.flush();
            offset += chunk_len;
            delay(SERIAL_COMM_TX_DELAY_MS); // delay to avoid buffer overflow
          }
          Serial0.print("\r\n");
          // for debug purposes
          Serial.print(F("RESP="));
          Serial.print(request_id);
          Serial.print(F(";"));
          // Serial.print(http_code_str);
          // Serial.print(F(";"));
          // Serial.print(response);
          Serial.print(F("\r\n"));
        }
      }
      // handle the pico response with the cartridge CRCs
      else if (command.startsWith("READ_CRC="))
      {
        if (state != 1)
        {
          Serial0.print(F("COMMAND_IGNORED_WRONG_STATE\r\n"));
          Serial.print(F("COMMAND_IGNORED_WRONG_STATE\r\n"));
        }
        else
        {
          String data = command.substring(9);
          data.trim();
          Serial.print("READ_CRC=" + data + "\r\n"); // DEBUG
          String begin_CRC = data.substring(0, 8);
          Serial.print("BEGIN_CRC=" + begin_CRC + "\r\n");
          String end_CRC = data.substring(9);
          Serial.print("END_CRC=" + end_CRC + "\r\n");
          md5 = get_MD5(begin_CRC, true);

          // if we didn't find the md5 using the CRC from the first bank, let's try using the CRC from the last bank
          // some mappers will load a random bank as the first bank, but will always point to the last bank
          if (md5.length() == 0)
          {
            md5 = get_MD5(end_CRC, false);
          }
          if (md5.length() == 0)
          {
            Serial0.print(F("CRC_NOT_FOUND\r\n"));
            Serial.print(F("CRC_NOT_FOUND\r\n"));
            state = 254; // error - cartridge not found
          }
          else
          {
            Serial0.print("CRC_FOUND_MD5=" + md5 + "\r\n");
            Serial.print("CRC_FOUND_MD5=" + md5 + "\r\n");
            state = 2;
          }
        }
      }
      // handle command with an achievement to be shown on the screen
      else if (command.startsWith("A="))
      {
        // example A=123456;Cruise Control;https://media.retroachievements.org/Badge/348421.png
        Serial.print(command); // debug
        int pos = command.indexOf(";");
        String achievements_id = command.substring(2, pos);
        command = command.substring(pos + 1);
        pos = command.indexOf(";");
        String achievements_title = command.substring(0, pos);

        command = command.substring(pos + 1);
        String achievements_image = command;
        achievements_image.trim();
        achievements_t achievement;
        achievement.id = achievements_id.toInt();
        achievement.title = achievements_title;
        achievement.url = achievements_image;
        if (fifo_enqueue(&achievements_fifo, achievement) == false)
        {
          Serial.print(F("FIFO_FULL\r\n")); // debug
        }
        else
        {
          Serial.print(F("ACHIEVEMENT_ADDED\r\n")); // debug
        }
      }
      // handle command with the game info
      else if (command.startsWith("GAME_INFO="))
      {
        // example GAME_INFO=1496;R.C. Pro-Am;https://media.retroachievements.org/Images/052570.png
        int pos = command.indexOf(";");
        game_id = command.substring(10, pos);
        command = command.substring(pos + 1);
        pos = command.indexOf(";");
        game_name = command.substring(0, pos);
        command = command.substring(pos + 1);
        game_image = command;
        game_image.trim();

        if (game_id == "0")
        {
          state = 254; // error - cartridge not found
        }
        else
        {
          setSemaphore(LED_ON, LED_GREEN);
          char file_name[64];
          sprintf(file_name, "/title_%s.png", game_id.c_str());
          try_download_file(game_image, file_name);
          // get just the file name from the url
          game_image = game_image.substring(game_image.lastIndexOf("/") + 1, game_image.length());

          // if game name is longer than 18 chars, add ... at the end
          String esp_game_name = game_name;
          if (esp_game_name.length() > 18)
          {
            esp_game_name = esp_game_name.substring(0, 18 - 3) + "...";
          }

          print_line("Cartridge identified:", 1, 0);
          print_line(" * " + esp_game_name + " * ", 2, 0);
          print_line("please RESET the console", 4, 0);
          Serial.print(command);
          // enable the BUS, connection NES to the cartridge
          digitalWrite(ANALOG_SWITCH_PIN, ANALOG_SWITCH_ENABLE_BUS);
        }
      }
      // handle the command whe pico detectes the start of a game
      // (probably the user resets the NES after the game is identified or NES starts the game
      // by itself once the BUS is connected)
      else if (command.startsWith("NES_RESETED"))
      { 
        uint8_t random = (uint8_t)esp_random();
        game_session = random;
#ifdef ENABLE_INTERNAL_WEB_APP_SUPPORT
        init_websocket();
        char aux[512];
        send_ws_data("RESET");
        sprintf(aux, "G=%s;%s;%s;%s", game_session.c_str(), game_id.c_str(), game_name.c_str(), game_image.c_str());
        if (ws_client)
        {
          ws_client->text(aux);
        }
#endif

        show_title_screen();
        // TODO: play some sound?
        state = 128; // do nothing
      }
      else if (command.startsWith("C="))
      {
#ifdef ENABLE_INTERNAL_WEB_APP_SUPPORT
        send_ws_data(command);
#endif
      }
      else if (command.startsWith("P="))
      {
#ifdef ENABLE_INTERNAL_WEB_APP_SUPPORT
        send_ws_data(command);
#endif
      }
      // command unknown
      else
      {
        Serial.print(F("UNKNOWN="));
        Serial.print(command);
      }
    }
  }
  yield();
}
