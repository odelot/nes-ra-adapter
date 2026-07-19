/**********************************************************************************
 * PatchStreamFilter - Streaming cleaner for RetroAchievements patch downloads
 *
 * Cleans the r=patch JSON response while it is being downloaded, so the raw
 * response never has to fit in RAM - only the cleaned result does. This allows
 * payloads larger than the response buffer (e.g. FF3: 129KB raw -> ~83KB clean).
 *
 * How it works: bytes from the HTTP stream pass through a character-level state
 * machine that (1) strips whitespace outside strings, (2) stages one achievement
 * object at a time in a small buffer, cleans it (BadgeURL, Rarity, Author, ...)
 * and drops it entirely when it is unofficial (Flags 5) or its MemAddr exceeds
 * PATCH_FILTER_MAX_MEMADDR, (3) empties the Leaderboards array, and (4) passes
 * everything else (header, RichPresencePatch) through untouched. The existing
 * post-download cleanup in the sketch still runs afterwards as a safety net and
 * for the global size fallbacks.
 *
 * An achievement object that outgrows the staging buffer can only be one with a
 * huge MemAddr (all other fields are small), so overflowing the staging buffer
 * simply switches to skip mode and drops the object - same outcome as the
 * MemAddr rule.
 *
 * Part of NES RA Adapter - ESP32 Firmware
 **********************************************************************************/

#ifndef PATCH_STREAM_FILTER_H
#define PATCH_STREAM_FILTER_H

#include "CharBufferStream.h"
#include "JsonCleaner.h"

// Staging must hold the largest achievement we intend to KEEP:
// PATCH_FILTER_MAX_MEMADDR + a few hundred bytes for the other fields.
#define PATCH_FILTER_STAGING_SIZE 10240
#define PATCH_FILTER_MAX_MEMADDR 8192

class PatchStreamFilter {
public:
  PatchStreamFilter() : _dest(nullptr) { resetState(); }

  // Allocate staging and reset state. Returns false if allocation failed
  // (caller should fall back to the non-streaming path).
  bool begin(CharBufferStream* dest) {
    _dest = dest;
    resetState();
    return _staging.reserve(PATCH_FILTER_STAGING_SIZE);
  }

  // Reset the state machine for a fresh response (e.g. HTTP retry attempt).
  // Keeps the staging allocation.
  void restart() {
    resetState();
    _staging.clear();
  }

  // Release staging memory
  void end() {
    _staging.release();
    _dest = nullptr;
  }

  // Feed raw response bytes. Always consumes len; check overflowed() after.
  size_t write(const uint8_t* buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
      char c = (char)buf[i];
      _rawBytes++;

      // String/escape tracking (parity-correct for backslashes)
      if (_escaped) {
        _escaped = false;
      } else if (_inString && c == '\\') {
        _escaped = true;
      } else if (c == '"') {
        _inString = !_inString;
      }

      // Strip whitespace outside strings
      if (!_inString && (c == ' ' || c == '\n' || c == '\r' || c == '\t')) {
        continue;
      }

      processChar(c);
    }
    return len;
  }

  // Flush pending state after the last byte (truncated objects pass through raw)
  void finish() {
    if (_state == ST_ACH_OBJ && _staging.length() > 0) {
      if (_keptCount > 0) emitChar(',');
      emitBuf((const uint8_t*)_staging.data(), _staging.length());
      _staging.clear();
    }
  }

  bool overflowed() const { return _overflow; }
  uint32_t rawBytes() const { return _rawBytes; }
  uint16_t keptCount() const { return _keptCount; }
  uint16_t droppedCount() const { return _droppedCount; }

private:
  enum State {
    ST_OUTSIDE,   // outside the Achievements array: pass through, watch for markers
    ST_ACH_ARRAY, // between achievement objects
    ST_ACH_OBJ,   // staging one achievement object
    ST_ACH_SKIP,  // discarding an oversized achievement object
    ST_LB_SKIP    // discarding the Leaderboards array content
  };

  CharBufferStream* _dest;
  CharBufferStream _staging;
  State _state;
  bool _inString;
  bool _escaped;
  bool _overflow;
  uint8_t _achMatchPos;
  uint8_t _lbMatchPos;
  int _objDepth;
  int _lbDepth;
  uint16_t _keptCount;
  uint16_t _droppedCount;
  uint32_t _rawBytes;

  void resetState() {
    _state = ST_OUTSIDE;
    _inString = false;
    _escaped = false;
    _overflow = false;
    _achMatchPos = 0;
    _lbMatchPos = 0;
    _objDepth = 0;
    _lbDepth = 0;
    _keptCount = 0;
    _droppedCount = 0;
    _rawBytes = 0;
  }

  // Incremental substring matcher. Both marker patterns start with '"' and have
  // no other repeated prefix, so restarting at 0/1 on mismatch is sufficient.
  static bool matchStep(const char* pattern, uint8_t& pos, char c) {
    if (c == pattern[pos]) {
      pos++;
      if (pattern[pos] == '\0') {
        pos = 0;
        return true;
      }
    } else {
      pos = (c == pattern[0]) ? 1 : 0;
    }
    return false;
  }

  void processChar(char c) {
    switch (_state) {
      case ST_OUTSIDE:
        emitChar(c);
        // Marker bytes are emitted as-is; both stay in the output.
        // Bare quotes cannot occur inside JSON strings, so no in-string false positives.
        if (matchStep("\"Achievements\":[", _achMatchPos, c)) {
          _achMatchPos = 0;
          _lbMatchPos = 0;
          _state = ST_ACH_ARRAY;
        } else if (matchStep("\"Leaderboards\":[", _lbMatchPos, c)) {
          _achMatchPos = 0;
          _lbMatchPos = 0;
          _lbDepth = 1;
          _state = ST_LB_SKIP;
        }
        break;

      case ST_ACH_ARRAY:
        if (c == '{') {
          _staging.clear();
          _staging.write((uint8_t)'{');
          _objDepth = 1;
          _state = ST_ACH_OBJ;
        } else if (c == ']') {
          emitChar(']');
          _state = ST_OUTSIDE;
        }
        // commas between objects are dropped here and re-inserted on emit
        break;

      case ST_ACH_OBJ:
        updateObjDepth(c);
        if (_staging.write((uint8_t)c) == 0) {
          // Staging overflow: object bigger than any we would keep - drop it
          _staging.clear();
          _state = ST_ACH_SKIP;
        }
        if (_objDepth == 0) {
          if (_state == ST_ACH_OBJ) {
            processObject();
          } else {
            _droppedCount++;
          }
          _state = ST_ACH_ARRAY;
        }
        break;

      case ST_ACH_SKIP:
        updateObjDepth(c);
        if (_objDepth == 0) {
          _droppedCount++;
          _state = ST_ACH_ARRAY;
        }
        break;

      case ST_LB_SKIP:
        if (!_inString) {
          if (c == '[') {
            _lbDepth++;
          } else if (c == ']') {
            _lbDepth--;
            if (_lbDepth == 0) {
              emitChar(']');
              _state = ST_OUTSIDE;
            }
          }
        }
        break;
    }
  }

  void updateObjDepth(char c) {
    if (!_inString) {
      if (c == '{') {
        _objDepth++;
      } else if (c == '}') {
        _objDepth--;
      }
    }
  }

  // Clean one complete staged achievement object and emit it (or drop it)
  void processObject() {
    remove_json_field_buffer(_staging, "BadgeLockedURL");
    remove_json_field_buffer(_staging, "BadgeURL");
    remove_json_field_buffer(_staging, "Rarity");
    remove_json_field_buffer(_staging, "RarityHardcore");
    remove_json_field_buffer(_staging, "Author");

    // Unofficial achievement (same match as remove_achievements_with_flags_5_buffer)
    bool drop = (_staging.indexOf("\"Flags\":5") != -1);

    if (!drop) {
      int memAddrPos = _staging.indexOf("\"MemAddr\":\"");
      if (memAddrPos != -1) {
        int valueStart = memAddrPos + 11; // strlen("\"MemAddr\":\"")
        int valueEnd = findClosingQuote(_staging.data(), valueStart, (int)_staging.length());
        if (valueEnd == -1 || (valueEnd - valueStart) > PATCH_FILTER_MAX_MEMADDR) {
          drop = true;
        }
      }
    }

    if (drop) {
      _droppedCount++;
      _staging.clear();
      return;
    }

    if (_keptCount > 0) emitChar(',');
    emitBuf((const uint8_t*)_staging.data(), _staging.length());
    _keptCount++;
    _staging.clear();
  }

  void emitChar(char c) {
    if (_overflow) return;
    if (_dest->write((uint8_t)c) != 1) _overflow = true;
  }

  void emitBuf(const uint8_t* buf, size_t len) {
    if (_overflow || len == 0) return;
    if (_dest->write(buf, len) != len) _overflow = true;
  }
};

#endif // PATCH_STREAM_FILTER_H
