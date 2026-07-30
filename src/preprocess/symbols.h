#ifndef PREPROCESS_SYMBOLS_H
#define PREPROCESS_SYMBOLS_H

#include <cstdint>

namespace preprocess {

constexpr uint8_t kWrtColon = 'J';
constexpr uint8_t kWrtSemicolon = 'K';
constexpr uint8_t kWrtLessThan = 'L';
constexpr uint8_t kWrtEquals = 'M';
constexpr uint8_t kWrtGreaterThan = 'N';
constexpr uint8_t kWrtQuestion = 'O';
constexpr uint8_t kWrtCurlyOpen = 'P';
constexpr uint8_t kWrtVerticalBar = 'Q';
constexpr uint8_t kWrtCurlyClose = 'R';

inline bool IsSemanticColon(uint8_t byte) {
  return byte == ':' || byte == kWrtColon;
}

inline bool IsSemanticQuestion(uint8_t byte) {
  return byte == '?' || byte == kWrtQuestion;
}

inline bool IsSemanticEquals(uint8_t byte) {
  return byte == '=' || byte == kWrtEquals;
}

inline bool IsSemanticLessThan(uint8_t byte) {
  return byte == '<' || byte == kWrtLessThan;
}

inline bool IsSemanticGreaterThan(uint8_t byte) {
  return byte == '>' || byte == kWrtGreaterThan;
}

inline bool IsSemanticVerticalBar(uint8_t byte) {
  return byte == '|' || byte == kWrtVerticalBar;
}

}

#endif
