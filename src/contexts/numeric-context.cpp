#include "numeric-context.h"

#include <limits>

NumericContext::NumericContext() {
  RefreshContext();
}

uint8_t NumericContext::PositionBucket(uint16_t length) {
  if (length <= 5) return static_cast<uint8_t>(length);
  if (length <= 7) return 6;
  if (length <= 9) return 7;
  if (length <= 15) return 8;
  return 9;
}

uint8_t NumericContext::LeftClass(uint8_t byte) {
  if (byte == 0) return 0;
  if (byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n') return 1;
  if (byte >= 'a' && byte <= 'z') return 2;
  if (byte >= 'A' && byte <= 'Z') return 3;
  if (byte >= '0' && byte <= '9') return 4;
  if (byte == '=') return 5;
  if (byte == '<' || byte == '>') return 6;
  if (byte == '+' || byte == '-') return 7;
  if (byte == '/' || byte == '\\') return 8;
  if (byte == '.' || byte == ',') return 9;
  if (byte == ':' || byte == ';') return 10;
  if (byte == '(' || byte == '[' || byte == '{') return 11;
  if (byte == ')' || byte == ']' || byte == '}') return 12;
  if (byte == '\'' || byte == '"') return 13;
  if (byte < 0x20 || byte == 0x7f) return 14;
  return 15;
}

void NumericContext::RefreshContext() {
  uint64_t context = state_.in_run ? 1 : 0;
  context = context * 10 + state_.position_bucket;
  context = context * 11 + state_.previous_digit;
  context = context * 101 + state_.previous_two_digits;
  context = context * 16 + state_.left_class;
  local_context_ = context;
}

void NumericContext::Update(uint8_t byte) {
  if (byte >= '0' && byte <= '9') {
    const uint8_t digit = byte - '0';
    if (!state_.in_run) {
      state_.in_run = true;
      state_.run_length = 1;
      state_.previous_digit = digit;
      state_.previous_two_digits = digit;
    } else {
      if (state_.run_length != std::numeric_limits<uint16_t>::max()) {
        ++state_.run_length;
      }
      state_.previous_two_digits =
          static_cast<uint8_t>((state_.previous_digit * 10 + digit) % 100);
      state_.previous_digit = digit;
    }
    state_.position_bucket = PositionBucket(state_.run_length);
  } else {
    state_.in_run = false;
    state_.run_length = 0;
    state_.position_bucket = 0;
    state_.previous_digit = 10;
    state_.previous_two_digits = 100;
    state_.left_class = LeftClass(byte);
  }
  RefreshContext();
}
