//
// This file is distributed under the Apache License v2.0. See LICENSE for
// details.
//

#include "omvll/passes/string-encoding/Routines.h"
#include <cassert>

namespace {

const char *DecodeRoutines[] = {
  R"delim(
    void decode(char *out, char *in, unsigned long long key, int size) {
      unsigned char *raw_key = (unsigned char*)(&key);
      for (int i = 0; i < size; ++i) {
        unsigned char c = in[i] ^ (unsigned char)(i * 0x6B);
        c = c ^ raw_key[i % sizeof(key)];
        int rot = (i % 7) + 1;
        c = (c << rot) | (c >> (8 - rot));
        
        out[i] = c;
      }
    }
  )delim",
  R"delim(
    void decode(char *out, char *in, unsigned long long key, int size) {
      unsigned char S[256];
      unsigned char *raw_key = (unsigned char*)(&key);
      int key_len = sizeof(key);
      for (int i = 0; i < 256; ++i) S[i] = (unsigned char)i;
      int j = 0;
      for (int i = 0; i < 256; ++i) {
        j = (j + S[i] + raw_key[i % key_len]) & 0xFF;
        unsigned char tmp = S[i]; S[i] = S[j]; S[j] = tmp;
      }
      int si = 0; j = 0;
      for (int i = 0; i < size; ++i) {
        si = (si + 1) & 0xFF;
        j  = (j + S[si]) & 0xFF;
        unsigned char tmp = S[si]; S[si] = S[j]; S[j] = tmp;
        out[i] = in[i] ^ S[(S[si] + S[j]) & 0xFF];
      }
    }
)delim",
};

omvll::EncRoutineFn *EncodeRoutines[] = {
    [] (char *out, const char *in, unsigned long long key, int size) {
    unsigned char *raw_key = (unsigned char *)(&key);
    for (int i = 0; i < size; ++i) {
      unsigned char c = (unsigned char)in[i];
      int rot = (i % 7) + 1;
      c = (c >> rot) | (c << (8 - rot));
      out[i] = c ^ raw_key[i % sizeof(key)] ^ (unsigned char)(i * 0x6B);
    }
  },
    [](char *out, const char *in, unsigned long long key, int size) {
      unsigned char S[256];
      unsigned char *raw_key = (unsigned char *)(&key);
      int key_len = sizeof(key);
      for (int i = 0; i < 256; ++i) S[i] = (unsigned char)i;
      int j = 0;
      for (int i = 0; i < 256; ++i) {
        j = (j + S[i] + raw_key[i % key_len]) & 0xFF;
        unsigned char tmp = S[i]; S[i] = S[j]; S[j] = tmp;
      }
      int si = 0; j = 0;
      for (int i = 0; i < size; ++i) {
        si = (si + 1) & 0xFF;
        j  = (j + S[si]) & 0xFF;
        unsigned char tmp = S[si]; S[si] = S[j]; S[j] = tmp;
        out[i] = in[i] ^ S[(S[si] + S[j]) & 0xFF];
      }
    }};

} // end anonymous namespace

namespace omvll {

template <typename T, unsigned N>
static constexpr unsigned arraySize(const T (&)[N]) noexcept {
  return N;
}

// Encode and decode functions must match in pairs
static_assert(arraySize(EncodeRoutines) == arraySize(DecodeRoutines));

unsigned getNumEncodeDecodeRoutines() { return arraySize(EncodeRoutines); }

EncRoutineFn *getEncodeRoutine(unsigned Idx) {
  assert(Idx < arraySize(EncodeRoutines));
  return EncodeRoutines[Idx];
}

const char *getDecodeRoutine(unsigned Idx) {
  assert(Idx < arraySize(DecodeRoutines));
  return DecodeRoutines[Idx];
}

} // end namespace omvll