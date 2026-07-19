/**********************************************************************************
 * JsonCleaner - In-place JSON cleaning functions for CharBufferStream
 *
 * They operate directly on the buffer without copying memory. Extracted from the
 * main sketch so they can be shared with PatchStreamFilter and unit-tested on
 * desktop builds.
 *
 * Part of NES RA Adapter - ESP32 Firmware
 **********************************************************************************/

#ifndef JSON_CLEANER_H
#define JSON_CLEANER_H

#include "CharBufferStream.h"

// Remove spaces, newlines, and tabs outside of strings (in-place on CharBufferStream)
inline void remove_space_new_lines_buffer(CharBufferStream &buf)
{
  char* data = buf.data();
  size_t len = buf.length();
  bool inside_quotes = false;
  size_t write_idx = 0;

  for (size_t read_idx = 0; read_idx < len; read_idx++)
  {
    char c = data[read_idx];

    if (c == '"' && (read_idx == 0 || data[read_idx - 1] != '\\')) {
      inside_quotes = !inside_quotes;
    }

    if (inside_quotes || (c != ' ' && c != '\n' && c != '\r' && c != '\t')) {
      data[write_idx++] = c;
    }
  }
  buf.setLength(write_idx);
}

// Remove a complete JSON field (in-place on CharBufferStream)
inline void remove_json_field_buffer(CharBufferStream &buf, const char* field_to_remove)
{
  remove_space_new_lines_buffer(buf);

  char* data = buf.data();
  size_t len = buf.length();
  size_t field_len = strlen(field_to_remove);

  bool inside_quotes = false;
  bool inside_array = false;
  bool skip_field = false;
  size_t read_idx = 0, write_idx = 0, skip_init = 0;

  while (read_idx < len)
  {
    char c = data[read_idx];

    if (c == '"' && (read_idx == 0 || data[read_idx - 1] != '\\')) {
      inside_quotes = !inside_quotes;
    }

    if (c == '[' && skip_field) inside_array = true;
    if (c == ']' && skip_field) inside_array = false;

    // Detect start of field to remove
    if (inside_quotes &&
        read_idx + 1 + field_len + 1 < len &&
        strncmp(data + read_idx + 1, field_to_remove, field_len) == 0 &&
        data[read_idx + field_len + 1] == '"')
    {
      skip_field = true;
      skip_init = read_idx;
    }

    if (!skip_field) {
      data[write_idx++] = c;
    }

    // End of field
    if (skip_field && read_idx + 1 < len && data[read_idx + 1] == '}') {
      skip_field = false;
      if (skip_init > 0 && data[skip_init - 1] == ',') {
        write_idx--;
      }
    }
    else if (skip_field && data[read_idx] == ',' && !inside_array && !inside_quotes) {
      skip_field = false;
    }

    read_idx++;
  }
  buf.setLength(write_idx);
}

// Clean the string value of a field - replace with "" (in-place on CharBufferStream)
inline void clean_json_field_str_value_buffer(CharBufferStream &buf, const char* field_to_remove)
{
  remove_space_new_lines_buffer(buf);

  char* data = buf.data();
  size_t len = buf.length();
  size_t field_len = strlen(field_to_remove);

  bool inside_quotes = false;
  bool skip_field = false;
  bool remove_next_str = false;
  size_t read_idx = 0, write_idx = 0, skip_init = 0;

  while (read_idx < len)
  {
    char c = data[read_idx];

    if (c == '"' && (read_idx == 0 || data[read_idx - 1] != '\\'))
    {
      inside_quotes = !inside_quotes;
      if (inside_quotes && remove_next_str) {
        skip_field = true;
        data[write_idx++] = '"';
      }
      if (!inside_quotes && remove_next_str && read_idx > skip_init) {
        remove_next_str = false;
        skip_field = false;
      }
    }

    if (inside_quotes &&
        read_idx + 1 + field_len + 1 < len &&
        strncmp(data + read_idx + 1, field_to_remove, field_len) == 0 &&
        data[read_idx + field_len + 1] == '"')
    {
      remove_next_str = true;
      skip_init = read_idx + field_len + 2;
    }

    if (!skip_field) {
      data[write_idx++] = c;
    }
    read_idx++;
  }
  buf.setLength(write_idx);
}

// Clean the array value of a field - replace with [] (in-place on CharBufferStream)
inline void clean_json_field_array_value_buffer(CharBufferStream &buf, const char* field_to_remove)
{
  remove_space_new_lines_buffer(buf);

  char* data = buf.data();
  size_t len = buf.length();
  size_t field_len = strlen(field_to_remove);

  bool inside_quotes = false;
  bool skip_field = false;
  bool remove_next_array = false;
  int array_depth = 0;
  size_t read_idx = 0, write_idx = 0;

  while (read_idx < len)
  {
    char c = data[read_idx];

    // Update quote state first (handling escaped quotes)
    if (c == '"' && (read_idx == 0 || data[read_idx - 1] != '\\')) {
      inside_quotes = !inside_quotes;
    }

    // Only process [ and ] when NOT inside quotes
    if (!inside_quotes) {
      if (c == '[') {
        if (remove_next_array) {
          if (array_depth == 0) {
            skip_field = true;
            data[write_idx++] = '[';
          }
          array_depth++;
        }
      }
      if (c == ']') {
        if (remove_next_array) {
          array_depth--;
          if (array_depth == 0) {
            remove_next_array = false;
            skip_field = false;
          }
        }
      }
    }

    // Detect the field name to remove
    if (inside_quotes &&
        read_idx + 1 + field_len + 1 < len &&
        strncmp(data + read_idx + 1, field_to_remove, field_len) == 0 &&
        data[read_idx + field_len + 1] == '"')
    {
      remove_next_array = true;
    }

    if (!skip_field) {
      data[write_idx++] = c;
    }
    read_idx++;
  }
  buf.setLength(write_idx);
}

// Remove achievements with flags 5 (unofficial) - in-place on CharBufferStream
inline void remove_achievements_with_flags_5_buffer(CharBufferStream &buf)
{
  remove_space_new_lines_buffer(buf);

  char* data = buf.data();
  int achvStart = buf.indexOf("\"Achievements\":[");
  if (achvStart == -1) return;

  int arrayStart = buf.indexOf("[", achvStart);
  int arrayEnd = buf.indexOf("]", arrayStart);
  if (arrayStart == -1 || arrayEnd == -1) return;

  int objCount = 0;
  int pos = arrayStart + 1;

  while (pos < arrayEnd)
  {
    int objStart = buf.indexOf("{", pos);
    if (objStart == -1 || objStart > arrayEnd) break;

    int objEnd = objStart;
    int braces = 1;
    bool inString = false;
    while (braces > 0 && objEnd < arrayEnd) {
      objEnd++;
      char ch = data[objEnd];
      if (ch == '"' && (objEnd == 0 || data[objEnd - 1] != '\\')) {
        inString = !inString;
      }
      if (!inString) {
        if (ch == '{') braces++;
        else if (ch == '}') braces--;
      }
    }

    if (objEnd >= arrayEnd) break;
    objCount++;

    // Search for "Flags":5 inside the object
    bool hasFlags5 = false;
    for (int i = objStart; i < objEnd - 8; i++) {
      if (strncmp(data + i, "\"Flags\":5", 9) == 0) {
        hasFlags5 = true;
        break;
      }
    }

    if (hasFlags5)
    {
      int removeStart = objStart;
      while (removeStart > arrayStart && (data[removeStart - 1] == ' ' || data[removeStart - 1] == '\n'))
        removeStart--;
      if (data[removeStart - 1] == ',')
        removeStart--;
      if (objCount == 1 && objEnd + 1 < (int)buf.length() && data[objEnd + 1] == ',')
        objEnd++;

      buf.removeRange(removeStart, objEnd - removeStart + 1);
      arrayEnd = buf.indexOf("]", arrayStart);
      pos = removeStart;
    }
    else
    {
      pos = objEnd + 1;
    }
  }
}

// Helper: find closing quote handling escaped quotes
inline int findClosingQuote(char* data, int start, int limit) {
  for (int i = start; i < limit; i++) {
    if (data[i] == '"' && (i == 0 || data[i - 1] != '\\')) {
      return i;
    }
  }
  return -1;
}

// Remove achievements with very large MemAddr - in-place on CharBufferStream
inline void remove_achievements_with_long_MemAddr_buffer(CharBufferStream &buf, uint32_t maxSize)
{
  char* data = buf.data();
  int achievementsPos = buf.indexOf("\"Achievements\"");
  if (achievementsPos == -1) return;

  int arrayStart = buf.indexOf("[", achievementsPos);
  int arrayEnd = buf.indexOf("]", arrayStart);
  if (arrayStart == -1 || arrayEnd == -1) return;

  int pos = arrayStart + 1;

  while (pos < arrayEnd)
  {
    int objStart = buf.indexOf("{", pos);
    if (objStart == -1 || objStart >= arrayEnd) break;

    int objEnd = objStart;
    int braceCount = 1;
    bool inString = false;
    while (braceCount > 0 && objEnd + 1 < (int)buf.length()) {
      objEnd++;
      char ch = data[objEnd];
      if (ch == '"' && (objEnd == 0 || data[objEnd - 1] != '\\')) {
        inString = !inString;
      }
      if (!inString) {
        if (ch == '{') braceCount++;
        else if (ch == '}') braceCount--;
      }
    }

    if (objEnd >= arrayEnd) break;

    // Search for "MemAddr"
    int memAddrPos = -1;
    for (int i = objStart; i < objEnd - 8; i++) {
      if (strncmp(data + i, "\"MemAddr\"", 9) == 0) {
        memAddrPos = i;
        break;
      }
    }

    if (memAddrPos == -1) {
      pos = objEnd + 1;
      continue;
    }

    int valueStart = buf.indexOf("\"", memAddrPos + 9);
    if (valueStart == -1 || valueStart > objEnd) {
      pos = objEnd + 1;
      continue;
    }

    int valueEnd = findClosingQuote(data, valueStart + 1, objEnd);
    if (valueEnd == -1 || valueEnd > objEnd) {
      pos = objEnd + 1;
      continue;
    }

    int memAddrLength = valueEnd - valueStart - 1;

    if (memAddrLength > (int)maxSize)
    {
      int removeStart = objStart;
      int removeEnd = objEnd + 1;

      if (removeEnd < (int)buf.length() && data[removeEnd] == ',')
        removeEnd++;
      else if (removeStart > arrayStart + 1 && data[removeStart - 1] == ',')
        removeStart--;

      buf.removeRange(removeStart, removeEnd - removeStart);
      arrayEnd -= (removeEnd - removeStart);
      pos = removeStart;
    }
    else
    {
      pos = objEnd + 1;
    }
  }
}

// ============================================================================
// Pico load-memory budget
//
// rcheevos on the Pico (ARM32) expands every parsed trigger condition into
// ~66 bytes of rc_condition_t/memref structs, so a big achievement set can
// OOM the RP2040 even when the JSON payload itself fits comfortably (FF3:
// 83KB of JSON -> ~202KB of parsed triggers -> PANIC). During rc_client load
// the Pico holds simultaneously: the JSON payload (serial buffer, shrunk to
// the payload size), the parsed triggers, the rich presence script (parses to
// ~1.8x its length) and per-achievement strings/structs. These helpers
// estimate that total need and drop the most expensive achievements until it
// fits the Pico's heap. Achievements typed "progression" or "win_condition"
// are never dropped, so finishing the game remains detectable.
// ============================================================================

// Parsed trigger bytes per condition. Measured 66.5 on FF3 with the vendored
// rcheevos compiled for 32-bit; 68 adds a small margin.
#define PICO_TRIGGER_BYTES_PER_COND 68
// Arena strings (title/description/author) + achievement structs, per
// achievement. Measured ~177 (SMB3) / ~225 (FF3).
#define PICO_BYTES_PER_ACHIEVEMENT 200

// Count rcheevos conditions in a MemAddr: '_' separates conditions, 'S'
// separates alt groups. An 'S' right after 'x' is a memory-size char
// (e.g. 0xS607d), not a separator.
inline uint32_t count_trigger_conditions(const char* memaddr, size_t len)
{
  if (len == 0) return 0;
  uint32_t conds = 1;
  for (size_t i = 0; i < len; i++) {
    char c = memaddr[i];
    if (c == '_') conds++;
    else if (c == 'S' && (i == 0 || memaddr[i - 1] != 'x')) conds++;
  }
  return conds;
}

// Find the next {...} object scanning from *pos (string-aware). Returns true
// and sets *objStart/*objEnd (inclusive braces); false on ']' at array level
// or end of buffer.
inline bool find_next_achievement_object(CharBufferStream &buf, int pos, int* objStart, int* objEnd)
{
  char* data = buf.data();
  int len = (int)buf.length();
  bool inString = false;

  while (pos < len) {
    char c = data[pos];
    if (c == '"' && (pos == 0 || data[pos - 1] != '\\')) inString = !inString;
    if (!inString) {
      if (c == '{') break;
      if (c == ']') return false;
    }
    pos++;
  }
  if (pos >= len) return false;

  *objStart = pos;
  int depth = 1;
  inString = false;
  while (depth > 0 && pos + 1 < len) {
    pos++;
    char c = data[pos];
    if (c == '"' && data[pos - 1] != '\\') inString = !inString;
    if (!inString) {
      if (c == '{') depth++;
      else if (c == '}') depth--;
    }
  }
  if (depth != 0) return false;
  *objEnd = pos;
  return true;
}

// Estimated parse cost of one achievement object's trigger
inline uint32_t achievement_trigger_cost(CharBufferStream &buf, int objStart, int objEnd)
{
  int memAddrPos = buf.indexOf("\"MemAddr\":\"", objStart);
  if (memAddrPos == -1 || memAddrPos > objEnd) return 0;
  int valueStart = memAddrPos + 11; // strlen("\"MemAddr\":\"")
  int valueEnd = findClosingQuote(buf.data(), valueStart, objEnd + 1);
  if (valueEnd == -1) return 0;
  return PICO_TRIGGER_BYTES_PER_COND *
         count_trigger_conditions(buf.data() + valueStart, valueEnd - valueStart);
}

// Achievements required to beat the game are never dropped
inline bool is_protected_achievement(CharBufferStream &buf, int objStart, int objEnd)
{
  char* data = buf.data();
  for (int i = objStart; i < objEnd - 8; i++) {
    if (strncmp(data + i, "\"Type\":\"", 8) == 0) {
      return strncmp(data + i + 8, "progression\"", 12) == 0 ||
             strncmp(data + i + 8, "win_condition\"", 14) == 0;
    }
  }
  return false;
}

// Length of a string field's value ("field":"value") or 0 if absent
inline uint32_t get_json_str_field_value_len_buffer(CharBufferStream &buf, const char* field)
{
  char pattern[48];
  snprintf(pattern, sizeof(pattern), "\"%s\":\"", field);
  int pos = buf.indexOf(pattern);
  if (pos == -1) return 0;
  int valueStart = pos + (int)strlen(pattern);
  int valueEnd = findClosingQuote(buf.data(), valueStart, (int)buf.length());
  if (valueEnd == -1) return 0;
  return (uint32_t)(valueEnd - valueStart);
}

// Estimated peak Pico heap need during rc_client load: JSON payload (held in
// the serial buffer) + parsed triggers + rich presence parse (~1.8x script,
// rounded to 2x) + per-achievement strings/structs.
inline uint32_t estimate_pico_load_need_buffer(CharBufferStream &buf)
{
  uint32_t need = buf.length();
  need += 2 * get_json_str_field_value_len_buffer(buf, "RichPresencePatch");

  int achvPos = buf.indexOf("\"Achievements\":[");
  if (achvPos == -1) return need;
  int pos = achvPos + 16;
  int objStart, objEnd;
  while (find_next_achievement_object(buf, pos, &objStart, &objEnd)) {
    need += achievement_trigger_cost(buf, objStart, objEnd) + PICO_BYTES_PER_ACHIEVEMENT;
    pos = objEnd + 1;
  }
  return need;
}

// Bring the estimated load need under the limit: drop rich presence first,
// then the most expensive unprotected achievements. Returns how many
// achievements were removed.
inline int trim_achievements_to_pico_budget_buffer(CharBufferStream &buf, uint32_t limit)
{
  int removed = 0;

  while (estimate_pico_load_need_buffer(buf) > limit) {
    // Rich presence is the cheapest thing to give up
    if (get_json_str_field_value_len_buffer(buf, "RichPresencePatch") > 0) {
      remove_json_field_buffer(buf, "RichPresencePatch");
      continue;
    }

    int achvPos = buf.indexOf("\"Achievements\":[");
    if (achvPos == -1) return removed;
    int arrayStart = achvPos + 15; // index of '['

    // Most expensive unprotected achievement (parse cost + its payload bytes)
    uint32_t bestCost = 0;
    int bestStart = -1, bestEnd = -1;
    int pos = arrayStart + 1;
    int objStart, objEnd;
    while (find_next_achievement_object(buf, pos, &objStart, &objEnd)) {
      uint32_t cost = achievement_trigger_cost(buf, objStart, objEnd) +
                      (uint32_t)(objEnd - objStart) + PICO_BYTES_PER_ACHIEVEMENT;
      if (cost > bestCost && !is_protected_achievement(buf, objStart, objEnd)) {
        bestCost = cost;
        bestStart = objStart;
        bestEnd = objEnd;
      }
      pos = objEnd + 1;
    }

    if (bestStart == -1) return removed; // only protected ones left

    char* data = buf.data();
    int removeStart = bestStart;
    int removeEnd = bestEnd + 1;
    if (removeEnd < (int)buf.length() && data[removeEnd] == ',')
      removeEnd++;
    else if (removeStart > arrayStart + 1 && data[removeStart - 1] == ',')
      removeStart--;
    buf.removeRange(removeStart, removeEnd - removeStart);
    removed++;
  }

  return removed;
}

#endif // JSON_CLEANER_H
