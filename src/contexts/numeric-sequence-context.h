#ifndef NUMERIC_SEQUENCE_CONTEXT_H
#define NUMERIC_SEQUENCE_CONTEXT_H

#include "structural-class.h"

#include <cstdint>

enum class NumericPhase : uint8_t {
  Outside,
  AfterDigit,
  AfterSeparator,
  AfterBridge,
};

enum class NumericSeparator : uint8_t {
  None,
  Dash,
  Colon,
  Dot,
  Comma,
  Slash,
  Plus,
  Other,
};

struct NumericSequenceState {
  NumericPhase phase = NumericPhase::Outside;
  uint8_t current_run_length = 0;
  uint8_t previous_run_length_bucket = 0;
  uint8_t segment_index = 0;
  NumericSeparator separator = NumericSeparator::None;
  uint16_t shape_code = 0;
  uint16_t start_semantic_coarse = 0;
  uint16_t start_field_bucket = 0;
  uint8_t bridge_age = 0;
  uint8_t sequence_length = 0;
};

class NumericSequenceContext {
 public:
  NumericSequenceContext();

  void Update(uint8_t byte, uint64_t previous_word, uint64_t wrt_context,
      uint64_t structural_stream, uint8_t line_position);

  const NumericSequenceState& State() const { return state_; }
  const bool& BoundaryActive() const { return boundary_active_; }
  const bool& LinkedActive() const { return linked_active_; }
  const bool& StartActive() const { return start_active_; }
  const uint64_t& BoundaryContext() const { return boundary_context_; }
  const uint64_t& LinkedContext() const { return linked_context_; }
  const uint64_t& StartCoarseContext() const { return start_coarse_context_; }
  const uint64_t& StartFieldContext() const { return start_field_context_; }
  const uint64_t& BoundaryMixerContext() const {
    return boundary_mixer_context_;
  }
  uint8_t StartWordBucket() const { return start_word_bucket_; }
  uint8_t StartWrtBucket() const { return start_wrt_bucket_; }
  StructuralClass PreviousStructuralClass() const {
    return previous_structural_class_;
  }

  static bool IsDigit(uint8_t byte);
  static uint8_t RunLengthBucket(uint8_t length);
  static NumericSeparator SeparatorFor(uint8_t byte);

 private:
  void Reset();
  void Refresh(uint64_t previous_word, uint64_t wrt_context,
      uint64_t structural_stream, uint8_t line_position);
  static bool IsStartCandidate(StructuralClass structural_class);

  NumericSequenceState state_;
  StructuralClass previous_structural_class_ = StructuralClass::Other;
  bool boundary_active_ = false;
  bool linked_active_ = false;
  bool start_active_ = false;
  uint64_t boundary_context_ = 0;
  uint64_t linked_context_ = 0;
  uint64_t start_coarse_context_ = 0;
  uint64_t start_field_context_ = 0;
  uint64_t boundary_mixer_context_ = 64;
  uint8_t start_word_bucket_ = 0;
  uint8_t start_wrt_bucket_ = 0;
};

#endif
