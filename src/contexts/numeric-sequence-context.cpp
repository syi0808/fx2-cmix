#include "numeric-sequence-context.h"

#include <algorithm>

#ifndef NUMERIC_BOUNDARY_SEMANTIC
#define NUMERIC_BOUNDARY_SEMANTIC 0
#endif
#ifndef NUMERIC_LINKED_SHAPE
#define NUMERIC_LINKED_SHAPE 0
#endif

namespace {

uint64_t MixField(uint64_t value) {
  value ^= value >> 30;
  value *= 0xbf58476d1ce4e5b9ULL;
  value ^= value >> 27;
  value *= 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}

}

NumericSequenceContext::NumericSequenceContext() {
  Refresh(0, 0, 0, 0);
}

bool NumericSequenceContext::IsDigit(uint8_t byte) {
  return byte >= '0' && byte <= '9';
}

uint8_t NumericSequenceContext::RunLengthBucket(uint8_t length) {
  if (length <= 4) return length;
  if (length <= 6) return 5;
  if (length <= 8) return 6;
  return 7;
}

NumericSeparator NumericSequenceContext::SeparatorFor(uint8_t byte) {
  if (byte == '-') return NumericSeparator::Dash;
  if (byte == ':' || byte == 'J') return NumericSeparator::Colon;
  if (byte == '.') return NumericSeparator::Dot;
  if (byte == ',') return NumericSeparator::Comma;
  if (byte == '/') return NumericSeparator::Slash;
  if (byte == '+') return NumericSeparator::Plus;
  return NumericSeparator::None;
}

bool NumericSequenceContext::IsStartCandidate(
    StructuralClass structural_class) {
  return structural_class != StructuralClass::Other;
}

void NumericSequenceContext::Reset() {
  state_ = NumericSequenceState();
}

void NumericSequenceContext::Refresh(uint64_t previous_word,
    uint64_t wrt_context, uint64_t structural_stream,
    uint8_t line_position) {
  boundary_active_ = state_.phase == NumericPhase::AfterDigit;
  linked_active_ = state_.phase == NumericPhase::AfterSeparator ||
      state_.phase == NumericPhase::AfterBridge;
  start_active_ = state_.phase == NumericPhase::Outside &&
      IsStartCandidate(previous_structural_class_);

  const uint64_t segment = std::min<uint8_t>(state_.segment_index, 3);
  boundary_context_ = RunLengthBucket(state_.current_run_length);
  boundary_mixer_context_ = boundary_active_
      ? RunLengthBucket(state_.current_run_length) * 8 +
          state_.start_semantic_coarse
      : 64;
#if NUMERIC_BOUNDARY_SEMANTIC
  boundary_context_ = boundary_context_ * 8 + state_.start_semantic_coarse;
  boundary_context_ = boundary_context_ * 4 + segment;
  boundary_context_ =
      boundary_context_ * 8 + state_.previous_run_length_bucket;
#endif

  linked_context_ = static_cast<uint8_t>(state_.separator);
  linked_context_ = linked_context_ * 8 + state_.previous_run_length_bucket;
  linked_context_ = linked_context_ * 4 + segment;
#if NUMERIC_LINKED_SHAPE
  linked_context_ = linked_context_ * 64 + (state_.shape_code & 63);
  linked_context_ = linked_context_ * 8 + state_.start_semantic_coarse;
#endif

  const uint64_t structural =
      static_cast<uint8_t>(previous_structural_class_);
  const uint64_t wrt_coarse =
      (wrt_context & 7) | ((structural_stream & 7) << 3);
  start_wrt_bucket_ = static_cast<uint8_t>(wrt_coarse);
  start_word_bucket_ =
      static_cast<uint8_t>(MixField(previous_word) & 0xff);
  start_coarse_context_ = structural;
  start_coarse_context_ = start_coarse_context_ * 64 + wrt_coarse;
  start_coarse_context_ =
      start_coarse_context_ * 8 + std::min<uint8_t>(line_position / 8, 7);

  uint64_t field = previous_word;
  field ^= wrt_context * 0x9e3779b97f4a7c15ULL;
  field ^= structural_stream * 0x94d049bb133111ebULL;
  start_field_context_ = MixField(field) & 0xfff;
}

void NumericSequenceContext::Update(uint8_t byte, uint64_t previous_word,
    uint64_t wrt_context, uint64_t structural_stream,
    uint8_t line_position) {
  if (IsDigit(byte)) {
    if (state_.phase == NumericPhase::Outside) {
      state_.phase = NumericPhase::AfterDigit;
      state_.current_run_length = 1;
      state_.segment_index = 0;
      state_.sequence_length = 1;
      state_.start_semantic_coarse =
          CoarseStructuralClass(previous_structural_class_);
      state_.start_field_bucket =
          static_cast<uint16_t>(start_field_context_);
    } else if (state_.phase == NumericPhase::AfterDigit) {
      if (state_.current_run_length < 255) ++state_.current_run_length;
      if (state_.sequence_length < 255) ++state_.sequence_length;
    } else {
      if (state_.segment_index >= 5 || state_.sequence_length >= 63) {
        Reset();
        state_.start_semantic_coarse =
            CoarseStructuralClass(previous_structural_class_);
        state_.start_field_bucket =
            static_cast<uint16_t>(start_field_context_);
        state_.segment_index = 0;
      } else {
        ++state_.segment_index;
      }
      state_.phase = NumericPhase::AfterDigit;
      state_.current_run_length = 1;
      if (state_.sequence_length < 255) ++state_.sequence_length;
      state_.bridge_age = 0;
      state_.separator = NumericSeparator::None;
    }
  } else if (state_.phase == NumericPhase::AfterDigit) {
    const NumericSeparator separator = SeparatorFor(byte);
    if (separator != NumericSeparator::None && state_.segment_index < 5 &&
        state_.sequence_length < 63) {
      state_.previous_run_length_bucket =
          RunLengthBucket(state_.current_run_length);
      state_.separator = separator;
      state_.shape_code = static_cast<uint16_t>(
          (state_.shape_code * 64 +
           state_.previous_run_length_bucket * 8 +
           static_cast<uint8_t>(separator)) & 0xfff);
      state_.phase = NumericPhase::AfterSeparator;
      ++state_.sequence_length;
    } else if (byte == 'T' && state_.segment_index >= 2 &&
        state_.segment_index < 5 && state_.sequence_length < 63) {
      state_.previous_run_length_bucket =
          RunLengthBucket(state_.current_run_length);
      state_.separator = NumericSeparator::Other;
      state_.phase = NumericPhase::AfterBridge;
      state_.bridge_age = 1;
      ++state_.sequence_length;
    } else {
      Reset();
    }
  } else if (state_.phase == NumericPhase::AfterSeparator ||
      state_.phase == NumericPhase::AfterBridge) {
    Reset();
  }

  if (!IsDigit(byte)) previous_structural_class_ = ClassifyStructural(byte);
  Refresh(previous_word, wrt_context, structural_stream, line_position);
}
