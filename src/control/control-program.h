#ifndef CONTROL_PROGRAM_H
#define CONTROL_PROGRAM_H

#include "../mixer/sigmoid.h"

#include <algorithm>
#include <array>
#include <cstdint>

struct ControlFeatures {
  uint8_t confidence = 0;
  uint8_t disagreement = 0;
  uint8_t match_length = 0;
  uint8_t bit_position = 0;
};

struct ControlRule {
  uint8_t confidence_begin;
  uint8_t confidence_end;
  uint8_t disagreement_begin;
  uint8_t disagreement_end;
  uint8_t scale_class;
};

inline float ApplyControlScale(float probability, uint8_t scale_class,
    const Sigmoid& sigmoid) {
  static constexpr std::array<float, 5> kScales = {
      0.75f, 0.875f, 1.0f, 1.125f, 1.25f};
  if (scale_class == 2) return probability;
  const size_t index = std::min<size_t>(scale_class, kScales.size() - 1);
  return Sigmoid::Logistic(sigmoid.Logit(probability) * kScales[index]);
}

inline uint8_t SelectControlScale(const ControlFeatures& features) {
#include "generated-control-program.inc"
  for (const ControlRule& rule : kControlRules) {
    if (features.confidence >= rule.confidence_begin &&
        features.confidence <= rule.confidence_end &&
        features.disagreement >= rule.disagreement_begin &&
        features.disagreement <= rule.disagreement_end) {
      return rule.scale_class;
    }
  }
  return 2;
}

#endif
