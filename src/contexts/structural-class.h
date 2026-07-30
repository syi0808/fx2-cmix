#ifndef STRUCTURAL_CLASS_H
#define STRUCTURAL_CLASS_H

#include <cstdint>

enum class StructuralClass : uint8_t {
  Other,
  Whitespace,
  Letter,
  WordToken,
  OpenMarkup,
  CloseMarkup,
  Equals,
  Pipe,
  Colon,
  Semicolon,
  Dash,
  Plus,
  Slash,
  Dot,
  Comma,
  Quote,
  Newline,
};

inline StructuralClass ClassifyStructural(uint8_t byte) {
  if (byte == '\n' || byte == '\r') return StructuralClass::Newline;
  if (byte == ' ' || byte == '\t') return StructuralClass::Whitespace;
  if (byte == ':' || byte == 'J') return StructuralClass::Colon;
  if (byte == ';' || byte == 'K') return StructuralClass::Semicolon;
  if (byte == '<' || byte == 'L' || byte == '{' || byte == 'P') {
    return StructuralClass::OpenMarkup;
  }
  if (byte == '>' || byte == 'N' || byte == '}' || byte == 'R') {
    return StructuralClass::CloseMarkup;
  }
  if (byte == '=' || byte == 'M') return StructuralClass::Equals;
  if (byte == '|' || byte == 'Q') return StructuralClass::Pipe;
  if (byte == '-') return StructuralClass::Dash;
  if (byte == '+') return StructuralClass::Plus;
  if (byte == '/' || byte == '\\') return StructuralClass::Slash;
  if (byte == '.') return StructuralClass::Dot;
  if (byte == ',') return StructuralClass::Comma;
  if (byte == '\'' || byte == '"') return StructuralClass::Quote;
  if ((byte >= 'a' && byte <= 'z') ||
      (byte >= 'A' && byte <= 'Z')) {
    return StructuralClass::Letter;
  }
  if (byte >= 0x80) return StructuralClass::WordToken;
  return StructuralClass::Other;
}

inline uint8_t CoarseStructuralClass(StructuralClass structural_class) {
  switch (structural_class) {
    case StructuralClass::Whitespace:
    case StructuralClass::Newline:
      return 1;
    case StructuralClass::Letter:
    case StructuralClass::WordToken:
      return 2;
    case StructuralClass::OpenMarkup:
    case StructuralClass::CloseMarkup:
      return 3;
    case StructuralClass::Equals:
      return 4;
    case StructuralClass::Pipe:
    case StructuralClass::Colon:
    case StructuralClass::Semicolon:
      return 5;
    case StructuralClass::Dash:
    case StructuralClass::Plus:
      return 6;
    case StructuralClass::Slash:
    case StructuralClass::Dot:
    case StructuralClass::Comma:
      return 7;
    default:
      return 0;
  }
}

#endif
