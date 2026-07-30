#ifndef NUMERIC_CONTEXT_H
#define NUMERIC_CONTEXT_H

#include <cstdint>

struct NumericState {
  bool in_run = false;
  uint8_t position_bucket = 0;
  uint8_t previous_digit = 10;
  uint8_t previous_two_digits = 100;
  uint8_t left_class = 0;
  uint16_t run_length = 0;
};

class NumericContext {
 public:
  NumericContext();
  void Update(uint8_t byte);

  const uint64_t& LocalContext() const { return local_context_; }
  const NumericState& State() const { return state_; }

 private:
  static uint8_t PositionBucket(uint16_t length);
  static uint8_t LeftClass(uint8_t byte);
  void RefreshContext();

  NumericState state_;
  uint64_t local_context_ = 0;
};

#endif
