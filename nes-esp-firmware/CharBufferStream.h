/**********************************************************************************
 * CharBufferStream - Flexible buffer with malloc/realloc that implements Stream
 * 
 * This class allows allocating large buffers and resizing (shrinking) after use.
 * It's designed for ESP32 memory-constrained environments where avoiding heap
 * fragmentation is critical.
 * 
 * Part of NES RA Adapter - ESP32 Firmware
 **********************************************************************************/

#ifndef CHAR_BUFFER_STREAM_H
#define CHAR_BUFFER_STREAM_H

#include <Arduino.h>
#include <Stream.h>

class CharBufferStream : public Stream {
private:
  char* _buffer;
  size_t _capacity;
  size_t _length;
  size_t _readPos;

public:
  CharBufferStream() : _buffer(nullptr), _capacity(0), _length(0), _readPos(0) {}
  
  ~CharBufferStream() {
    release();
  }

  // Allocate or reallocate the buffer
  bool reserve(size_t size) {
    if (size == 0) {
      release();
      return true;
    }
    
    if (_buffer == nullptr) {
      _buffer = (char*)malloc(size + 1);
      if (_buffer) {
        _buffer[0] = '\0';
        _capacity = size;
        _length = 0;
        _readPos = 0;
        return true;
      }
      return false;
    }
    
    // Reallocate to new size
    char* newBuffer = (char*)realloc(_buffer, size + 1);
    if (newBuffer) {
      _buffer = newBuffer;
      _capacity = size;
      if (_length > size) {
        _length = size;
        _buffer[_length] = '\0';
      }
      return true;
    }
    return false;
  }

  // Free all memory
  void release() {
    if (_buffer) {
      free(_buffer);
      _buffer = nullptr;
    }
    _capacity = 0;
    _length = 0;
    _readPos = 0;
  }

  // Shrink the buffer - free excess memory
  bool shrink(size_t newSize) {
    if (newSize >= _capacity) return true;
    return reserve(newSize);
  }

  // Clear content but keep capacity
  void clear() { 
    _length = 0; 
    _readPos = 0;
    if (_buffer) _buffer[0] = '\0';
  }
  
  // Getters
  size_t length() const { return _length; }
  size_t capacity() const { return _capacity; }
  const char* c_str() const { return _buffer ? _buffer : ""; }
  char* data() { return _buffer; }
  
  // Stream write interface
  size_t write(uint8_t c) override {
    if (_buffer && _length < _capacity) {
      _buffer[_length++] = c;
      _buffer[_length] = '\0';
      return 1;
    }
    return 0;
  }
  
  size_t write(const uint8_t* buf, size_t size) override {
    if (!_buffer) return 0;
    size_t toWrite = (size < (_capacity - _length)) ? size : (_capacity - _length);
    memcpy(_buffer + _length, buf, toWrite);
    _length += toWrite;
    _buffer[_length] = '\0';
    return toWrite;
  }
  
  // Stream read interface
  int available() override { return _length - _readPos; }
  int read() override { 
    if (_readPos < _length) return _buffer[_readPos++];
    return -1;
  }
  int peek() override {
    if (_readPos < _length) return _buffer[_readPos];
    return -1;
  }
  void flush() override {}

  // Update length after direct manipulation
  void setLength(size_t len) {
    if (len <= _capacity) {
      _length = len;
      if (_buffer) _buffer[_length] = '\0';
    }
  }

  // Access by index
  char charAt(size_t index) const {
    return (_buffer && index < _length) ? _buffer[index] : '\0';
  }
  
  char& operator[](size_t index) {
    return _buffer[index];
  }

  // Remove characters (in-place)
  void removeRange(size_t index, size_t count) {
    if (!_buffer || index >= _length) return;
    if (index + count > _length) count = _length - index;
    memmove(_buffer + index, _buffer + index + count, _length - index - count + 1);
    _length -= count;
  }

  // Search substring (returns -1 if not found)
  int indexOf(const char* str, size_t from = 0) const {
    if (!_buffer || !str || from >= _length) return -1;
    const char* found = strstr(_buffer + from, str);
    return found ? (found - _buffer) : -1;
  }

  int indexOf(char c, size_t from = 0) const {
    if (!_buffer || from >= _length) return -1;
    for (size_t i = from; i < _length; i++) {
      if (_buffer[i] == c) return i;
    }
    return -1;
  }
};

#endif // CHAR_BUFFER_STREAM_H
