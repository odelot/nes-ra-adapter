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

   Date:             2025-03-29
   Version:          0.1
   By odelot

   Arduino IDE ESP32 Boards: v3.0.5
   
   Libs used:
   WifiManager: https://github.com/tzapu/WiFiManager v2.0.17
   ArduinoJson: https://github.com/bblanchon/ArduinoJson v7.3.0
   PNGdec: https://github.com/bitbank2/PNGdec v1.0.3
   TFT_eSPI lib: https://github.com/Bodmer/TFT_eSPI v2.5.43

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

#include <WiFiManager.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <PNGdec.h>
#include <StreamString.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include "SPI.h"
#include <TFT_eSPI.h>
#include "HardwareSerial.h"
#include "LittleFS.h"
#include <esp_sleep.h>
#include <driver/uart.h>
#include <driver/adc.h>
#include <Wire.h>

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

#define ENABLE_RESET 1

/**
 * defines to use a lambda function to shrink  the JSON response with the list of achievements
 * remember: you need to deploy the lambda and inform its URL
 */

#define ENABLE_SHRINK_LAMBDA 0 // 0 - disable / 1 - enable
#define SHRINK_LAMBDA_URL "https://xxxxxxxxxx.execute-api.us-east-1.amazonaws.com/default/NES_RA_ADAPTER?"

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

// global variables for the PNG decoder
PNG png;
int16_t x_pos = 0;
int16_t y_pos = 0;
File png_file;

// global variables for the TFT screen
TFT_eSPI tft = TFT_eSPI();

// global variables for the network
NetworkClientSecure client;
HTTPClient https;

// global variables for the json parser
StaticJsonDocument<512> json_doc;

// global variables for the "achievement unlocked" sound
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

// global variables for the achievements fifo
achievements_FIFO_t achievements_fifo;

// global variable for the wifi manager
WiFiManager wm;

// custom html and parameters for the configuration portal
const char head[] = "<style>#l,#i,#z{text-align:center}#i,#z{margin:15px auto}button{background-color:#0000FF;}#l{margin:0 auto;width:100%; font-size: 28px;}p{margin-bottom:-5px}[type='checkbox']{height: 20px;width: 20px;}</style><script>var w=window,d=document,e=\"password\";function iB(a,b,c){a.insertBefore(b,c)}function gE(a){return d.getElementById(a)}function cE(a){return d.createElement(a)};\"http://192.168.1.1/\"==w.location.href&&(w.location.href+=\"wifi\");</script>\0";
const char html_p1[] = "<p id='z' style='width: 80%;'>Enter the RetroAchievements credentials below:</p>\0";
const char html_p2[] = "<p>&#8226; RetroAchievements user name: </p>\0";
const char html_p3[] = "<p>&#8226; RetroAchievements user password: </p>\0";
const char html_s[] = "<script>gE(\"s\").required=!0;l=cE(\"div\");l.innerHTML=\"NES RetroAchievements Adapter\",l.id=\"l\";m=d.body.childNodes[0];iB(m,l,m.childNodes[0]);p=cE(\"p\");p.id=\"i\",p.innerHTML=\"Choose the network you want to connect with:\",iB(m,p,m.childNodes[1]);</script>\0";
WiFiManagerParameter custom_p1(html_p1);
WiFiManagerParameter custom_p2(html_p2);
WiFiManagerParameter custom_param_1("un", NULL, "", 24, " required autocomplete='off'");
WiFiManagerParameter custom_p3(html_p3);
WiFiManagerParameter custom_param_2("up", NULL, "", 14, " type='password' required");
WiFiManagerParameter custom_s(html_s);

// global variables for the state machine
int state = 255; // TODO: change to enum

// global variables for storing states, timestamps and useful information
String base_url = "https://retroachievements.org/dorequest.php?";
String ra_user_token = "";
StreamString buffer;
String md5 = "";
String game_name = "not identified";
String game_id = "0";
bool go_back_to_title_screen = false;
bool already_showed_title_screen = false;
long go_back_to_title_screen_timestamp;

/**
 * functions to identify the cartridge
 */

// look for the md5 hash in the crc to md5 hash table (games.txt)
// first_bank is true if the crc is from the first bank of the cartridge,
// otherwise it look for the crc from the last bank
String get_MD5(String crc, bool first_bank)
{
  if (crc == "BD7BC39F") { // CRC from regions filled with 0xFF - normally the last bank - useless to identify the cartridge
    Serial.println("CRC is 0xBD7BC39F - skipping"); // debug
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
      if (remove_next_array) {
        skip_field = true;
        json[write_index++] = '[';
      }
    } 
    if (c == ']')
    {
      inside_array = false;
      if (remove_next_array) {
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
  setCpuFrequencyMhz(160);
  tft.setTextColor(TFT_BLACK, TFT_YELLOW, true);

  tft.setTextSize(1);
  tft.setTextPadding(240);
  tft.drawString("", 0, 10, 4);
  tft.drawString("", 0, 40, 4);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(game_name, 120, 40, 4);
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
}

void show_achievement(achievements_t achievement)
{
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
  download_file_to_littleFS(achievement.url, file_name);
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
}

// callback function to draw pixels to the display
void png_draw(PNGDRAW *pDraw)
{
  uint16_t line_buffer[MAX_IMAGE_WIDTH];
  png.getLineAsRGB565(pDraw, line_buffer, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
  tft.pushImage(x_pos, y_pos + pDraw->y, pDraw->iWidth, 1, line_buffer);
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
  int user_len = ra_user.length();
  int pass_len = ra_pass.length();
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
  int len = EEPROM.read(3);
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
  int ra_user_len = EEPROM.read(3);
  int len = EEPROM.read(4 + ra_user_len);
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

void wifi_manager_init()
{
  wm.setBreakAfterConfig(true);
  wm.setCaptivePortalEnable(true);
  wm.setMinimumSignalQuality(40);
  wm.setConnectTimeout(30);
  wm.addParameter(&custom_p1);
  wm.addParameter(&custom_p2);
  wm.addParameter(&custom_param_1);
  wm.addParameter(&custom_p3);
  wm.addParameter(&custom_param_2);
  wm.addParameter(&custom_s);
  wm.setCustomHeadElement(head);
  wm.setDarkMode(true);
  wm.setAPStaticIPConfig(IPAddress(192, 168, 1, 1), IPAddress(192, 168, 1, 1), IPAddress(255, 255, 255, 0));
}

/**
 * functions related to the RA login
 */
String try_login_RA(String ra_user, String ra_pass)
{
  DeserializationError error;
  int http_code;
  client.setInsecure();
  String login_path = "r=login&u=@USER@&p=@PASS@";
  login_path.replace("@USER@", ra_user);
  login_path.replace("@PASS@", ra_pass);
  https.begin(client, base_url + login_path);

  const char *header_keys[] = {"date"};
  const size_t header_keys_count = sizeof(header_keys) / sizeof(header_keys[0]); // Returns 1
  https.collectHeaders(header_keys, header_keys_count);

  http_code = https.GET();
  // TODO: use this timestamp to control temp files on littleFS
  String date_header = https.header("date");
  Serial.println("Header Date: " + date_header); // debug

  if (http_code != HTTP_CODE_OK)
  {
    state = 253; // error - login failed
    Serial0.print("ERROR=253-LOGIN_FAILED\r\n");
    Serial.print("ERROR=253-LOGIN_FAILED\r\n");
    return String("null");
  }
  
  StreamString response;
  response.reserve(256);
  int ret = https.writeToStream(&response);
  if (ret < 0)
  {
    //TODO: handle error
    Serial.print("ERROR ON RESPONSE: ");
    Serial.println(ret);
  }

  https.end();

  error = deserializeJson(json_doc, (String) response);

  if (error)
  {
    state = 252; // error - json parse login failed
    Serial0.print("ERROR=252-JSON_PARSE_ERROR\r\n");
    Serial.print("ERROR=252-JSON_PARSE_ERROR\r\n");
    return String("null");
  }

  String ra_token(json_doc["Token"]);
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
  while (digitalRead(RESET_PIN) == LOW && (millis() - start) < 10000)
  {
    yield();
    print_line("Resetting in progress...", 0, 1);
  }
  if ((millis() - start) >= RESET_PRESSED_TIME)
  {

    Serial.print("reset");
    init_EEPROM(true);
    print_line("Reset successful!", 1, 0);
    print_line("Reboot in 5 seconds...", 2, 0);
    delay(5000);
    ESP.restart();
  }
  print_line("Reset aborted!", 0, 1);
}

/**
 * functions related to the littleFS
 */

// Fetch a file from the URL given and save it in LittleFS
// Return 1 if a web fetch was needed or 0 if file already exists
bool download_file_to_littleFS(String url, String file_name)
{

  // If it exists then no need to fetch it
  if (LittleFS.exists(file_name) == true)
  {
    Serial.print("Found " + file_name + " in LittleFS\n"); // debug
    return 0;
  }

  Serial.print("Downloading " + file_name + " from " + url + "\n"); // debug

  // Check WiFi connection
  if ((WiFi.status() == WL_CONNECTED))
  {

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
        return 0;
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
      // TODO: handle error, maybe retry
    }
    http.end();
  }
  return 1;
}

// auxiliary function to implement startsWith for char*
bool prefix(const char *pre, const char *str)
{
  return strncmp(pre, str, strlen(pre)) == 0;
}

// arduino-esp32 entry point
void setup()
{
  setCpuFrequencyMhz(80);
  // disable i2c
  Wire.end();
 
  // config analog switch control pin
  pinMode(ANALOG_SWITCH_PIN, OUTPUT);
  digitalWrite(ANALOG_SWITCH_PIN, ANALOG_SWITCH_DISABLE_BUS); // isolate the cartridge from NES

  // config reset pin
  pinMode(RESET_PIN, INPUT);

  // reserve a buffer for the streamstring
  buffer.reserve(512);
  buffer.clear();

  // initialize stuff
  init_EEPROM(false); // initialize the EEPROM

  fifo_init(&achievements_fifo); // initialize the achievement fifo

  wifi_manager_init(); // initialize the wifi manager

  tft.begin(); // initialize the TFT screen

  Serial.begin(9600);    // initialize the serial port
  Serial0.setTimeout(2); // 2ms timeout for Serial0
  Serial0.begin(115200);
  delay(250);

  if (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)) // Initialise LittleFS
  {
    Serial.print("LittleFS initialisation failed!"); // debug
    while (1)
      yield(); // Stay here twiddling thumbs waiting
  }

  // consume any garbage in the serial buffer before communicating with the pico
  if (Serial0.available() > 0)
  {
    Serial0.readString();
    delay (10);
  }

  Serial0.print("ESP32_VERSION=0.2\r\n"); // send the version to the pico - debug
  Serial0.print("RESET\r\n");             // reset the pico
  delay(250);

  // draw the title screen
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

  // check if the reset button is pressed and handle the reset routine
  handle_reset();

  // check if the EEPROM is configured. If not, start the configuration portal
  int wifi_configuration_tries = 0;
  bool wifi_configured = false;
  bool configured = is_eeprom_configured();
  if (!configured)
  {
    while (!configured)
    {
      if (wifi_configuration_tries == 0 && !wifi_configured)
      {
        print_line(" Configure the Adapter:", 0, 1);
        print_line("Connect to its wifi network", 1, -1, -16);
        print_line("named \"NES_RA_ADAPTER\",", 2, -1, -16);
        print_line("password: 12345678, and ", 3, -1, -16);
        print_line("then open http://192.186.1.1", 4, -1, -16);
        play_attention_sound();
      }
      else if (wifi_configuration_tries > 0 && !wifi_configured)
      {
        print_line("Could not connect to wifi", 0, 2);
        print_line("network!", 1, -1, 16);
        print_line("check it and try again", 3, -1, 16);
        play_error_sound();
      }
      else if (wifi_configured)
      {
        print_line("Could not log in", 0, 2);
        print_line("RetroAchievements!", 1, -1, 16);
        print_line("check the credentials", 3, -1, 16);
        print_line("and try again", 4, -1, 16);
        play_error_sound();
      }
      if (wm.startConfigPortal("NES_RA_ADAPTER", "12345678"))
      {
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
          print_line("Logged in RA!", 1, 0);
          play_success_sound();
          Serial.println("saving configuration info in eeprom");
          save_configuration_info_eeprom(temp_ra_user, temp_ra_pass);
        }
        else
        {
          Serial.print("could not log in RA");
          clean_screen_text();
        }
      }
      else
      {
        Serial.print("not connected...booo :(");
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
        delay (500);
    }
    print_line("Wifi OK!", 0, 0); // TODO: implement timeout
    String ra_user = read_ra_user_from_eeprom();
    String ra_pass = read_ra_pass_from_eeprom();
    print_line("Logging in RA...", 1, 1);
    ra_user_token = try_login_RA(ra_user, ra_pass);
    if (ra_user_token.compareTo("null") != 0)
    {
      print_line("Logged in RA!", 1, 0);
      play_success_sound();
    }
    else
    {
      print_line("Could not log in RA!", 1, 2);
      print_line("Consider reset the adapter", 3, -1, 16);
      play_error_sound();
    }
  }

  // send the token and user to the pico (needed )
  char token_and_user[512];
  sprintf(token_and_user, "TOKEN_AND_USER=%s,%s\r\n", ra_user_token.c_str(), read_ra_user_from_eeprom().c_str());
  Serial0.print(token_and_user);

  Serial.print(token_and_user); // debug
  Serial.print("TFT_INIT\r\n"); // debug
  delay(250);
  state = 0;
  Serial.print("setup end"); // debug

  // modem sleep
  WiFi.setSleep(true);
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

}

// arduino-esp32 main loop
void loop()
{
  // reconnect if wifi got disconnected
  if (WiFi.status() != WL_CONNECTED)
  {
    WiFi.begin();
    while (WiFi.status() != WL_CONNECTED)
      yield();
  }

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
    // 200 - 254 are error trying to identify the cartridge
    state = 128; // do nothing - it will not enable the BUS, so the game will not boot
    print_line("Cartridge not identified", 1, 2);
    print_line("Turn off the console", 4, 2);
    play_error_sound();
  }

  if (state == 199)
  {
    // json is too big
    state = 128; // do nothing - it will not enable the BUS, so the game will not boot
    print_line("RA response is too big", 1, 2);
    print_line("Turn off the console", 4, 2);
    play_error_sound();
  }

  // handle the cartridge identification
  if (state == 0)
  {
    print_line("Identifying cartridge...", 1, 1);

    Serial0.print("READ_CRC\r\n"); // send command to read cartridge crc
    Serial.print("READ_CRC\r\n");  // send command to read cartridge crc
    delay(250);
    state = 1;

    // USEFUL FOR TESTING - FORCING SOME GAME, IN THIS CASE, RC PRO AM
    // md5 = "2178cc3772b01c9f3db5b2de328bb992";
    // state = 2;
  }

  // inform the pico to start the process to watch the BUS for memory writes
  if (state == 2)
  {
    Serial0.print("START_WATCH\r\n");
    Serial.print("START_WATCH\r\n");
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
      Serial0.print("BUFFER_OVERFLOW\r\n");
      Serial.print("BUFFER_OVERFLOW\r\n");
    }

    // wait for a command terminator \r\n
    if (buffer.indexOf("\r\n") > 0)
    {
      // we have a command \o/
      String command = buffer.substring(0, buffer.indexOf("\n") + 1);
      buffer.remove(0,buffer.indexOf("\n") + 1); // shift string left, removing the command from the buffer
      
      // handle a http request command
      if (command.startsWith("REQ"))
      {
        // example of request
        // REQ=FF;M:POST;U:https://retroachievements.org/dorequest.php;D:r=login2&u=user&p=pass
        const char user_agent[] = "NES_RA_ADAPTER/0.2 rcheevos/11.6";

        String request = command.substring(4);
        // Serial.println("REQ=" + request);
        int pos = request.indexOf(";");
        String request_id = request.substring(0, pos);
        request = request.substring(pos + 1);
        pos = request.indexOf(";");
        String method = request.substring(2, pos);
        request = request.substring(pos + 1);
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
          url = SHRINK_LAMBDA_URL;
        }

        client.setInsecure();
        https.begin(client, url);
        https.setUserAgent(user_agent);

        if (method == "POST")
        {
          https.addHeader("Content-Type", "application/x-www-form-urlencoded");
          http_code = https.POST(data);
        }
        else if (method == "GET")
        {
          http_code = https.GET();
        }
        if (http_code != HTTP_CODE_OK)
        {
          // TODO: handle errors
          Serial0.print("REQ_ERROR=" + String(http_code) + "\r\n");
          Serial.print("REQ_ERROR=" + https.errorToString(http_code) + "\r\n");
        }
        char http_code_str[4];
        sprintf(http_code_str, "%03X", http_code);
        // String response = https.getString();

        int reserve;
        if (prefix("r=patch", data.c_str())) {
          reserve = 61000; // bold - need more tests, but at least supports Super Mario Bros 3
          if (ENABLE_SHRINK_LAMBDA) {
            reserve = 32768; 
          }
        } else {
          reserve = 512;
        }
        
        StreamString response;
        // reserve 32KB of memory for the response
        if (!response.reserve(reserve)) {
          Serial.print ("could not allocate: ");
          Serial.print (reserve);
          Serial.println (" KB");
          state = 199; // json to big error
        } else {
          int ret = https.writeToStream(&response);
          if (ret < 0)
          {
            Serial.print("ERROR ON RESPONSE: ");
            Serial.println(ret);
            state = 199; // json to big error
          }
          else
          {
            // clean the json response to save some bytes and speed up transfer for pico
            if (prefix("r=patch", data.c_str()))
            {
              // print response size and header content size

              Serial.print("PATCH LENGTH: ");    // debug
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
              Serial.print("NEW PATCH LENGTH: "); // debug
              // Serial.println(response.length());  // debug
            }
            else if (prefix("r=login", data.c_str()))
            {
              remove_json_field(response, "AvatarUrl");
            }
          }
        }
        if (state != 199 && response.length() < SERIAL_MAX_PICO_BUFFER) {
          Serial0.print("RESP=");
            Serial0.print(request_id);
            Serial0.print(";");
            Serial0.print(http_code_str);
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
            https.end();

            // for debug purposes
            Serial.print("RESP=");
            Serial.print(request_id);
            Serial.print(";");
            Serial.print(http_code_str);
            Serial.print(";");
            Serial.print(response);
            Serial.print("\r\n");
        } else {
          state = 199;
        }                
      }
      // controls analog switch - useful for debugging send commands via Serial Monitor
      else if (command.startsWith("ANALOG_SWITCH="))
      {
        {
          int analog_switch_command = command.substring(4).toInt();
          if (analog_switch_command == 1)
          {
            digitalWrite(ANALOG_SWITCH_PIN, ANALOG_SWITCH_ENABLE_BUS);
          }
          else
          {
            digitalWrite(ANALOG_SWITCH_PIN, ANALOG_SWITCH_DISABLE_BUS);
          }
          Serial0.print("ANALOG_SWITCH_SET\r\n");
          Serial.print("ANALOG_SWITCH_SET\r\n");
        }
      }
      // handle the pico response with the cartridge CRCs
      else if (command.startsWith("READ_CRC="))
      {
        if (state != 1)
        {
          Serial0.print("COMMAND_IGNORED_WRONG_STATE\r\n");
          Serial.print("COMMAND_IGNORED_WRONG_STATE\r\n");
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
            Serial0.print("CRC_NOT_FOUND\r\n");
            Serial.print("CRC_NOT_FOUND\r\n");
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

        // if achievement title is longer than 26 chars, add ... at the end
        if (achievements_title.length() > 26)
        {
          achievements_title = achievements_title.substring(0, 26 - 3) + "...";
        }

        command = command.substring(pos + 1);
        String achievements_image = command;
        achievements_image.trim();
        achievements_t achievement;
        achievement.id = achievements_id.toInt();
        achievement.title = achievements_title;
        achievement.url = achievements_image;
        if (fifo_enqueue(&achievements_fifo, achievement) == false)
        {
          Serial.print("FIFO_FULL\r\n"); // debug
        }
        else
        {
          Serial.print("ACHIEVEMENT_ADDED\r\n"); // debug
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
        String game_image = command;
        game_image.trim();

        if (game_id == "0")
        {
          state = 254; // error - cartridge not found
        }
        else
        {
          char file_name[64];
          sprintf(file_name, "/title_%s.png", game_id.c_str());
          download_file_to_littleFS(game_image.c_str(), file_name);

          // if game name is longer than 18 chars, add ... at the end
          if (game_name.length() > 18)
          {
            game_name = game_name.substring(0, 18 - 3) + "...";
          }

          print_line("Cartridge identified:", 1, 0);
          print_line(" * " + game_name + " * ", 2, 0);
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

        show_title_screen();
        // TODO: play some sound?
        state = 128; // do nothing
      }
      // command unknown
      else
      {
        Serial.print("UNKNOWN=");
        Serial.print(command);
      }
    }
  }
  yield();
}
