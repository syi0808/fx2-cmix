#ifndef URL_RESIDUAL_H
#define URL_RESIDUAL_H

#include "../contexts/url-context.h"
#include "../mixer/sigmoid.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#ifndef URL_RESIDUAL_LEARNING_RATE
#define URL_RESIDUAL_LEARNING_RATE 0.001f
#endif

class UrlResidual {
 public:
  UrlResidual() : context_weights_(kNumContexts * kNumFeatures, 0) {
    role_weights_.fill(0);
  }

  float Predict(float base_probability,
      const std::array<float, 5>& model_probabilities,
      size_t model_count, const UrlState& state, const Sigmoid& sigmoid) {
    active_ = state.confidence == UrlConfidence::UrlConfirmed &&
        IsResidualRole(state.role);
    if (!active_) return base_probability;

    role_index_ = std::min<size_t>(
        static_cast<uint8_t>(state.role), kNumRoles - 1);
    context_index_ =
        (static_cast<size_t>(state.role) * 96 +
         static_cast<uint8_t>(state.component_class) * 8 +
         state.position_bucket) %
        kNumContexts;
    features_.fill(0);
    features_[0] = 1;
    for (size_t index = 0;
         index < model_count && index + 1 < features_.size(); ++index) {
      features_[index + 1] =
          std::max(-4.0f,
              std::min(4.0f, sigmoid.Logit(model_probabilities[index])));
    }

    float delta = 0;
    for (size_t index = 0; index < features_.size(); ++index) {
      delta += features_[index] *
          (role_weights_[role_index_ * features_.size() + index] +
           context_weights_[context_index_ * features_.size() + index]);
    }
    final_probability_ =
        Sigmoid::Logistic(sigmoid.Logit(base_probability) + delta);
    return final_probability_;
  }

  void Perceive(int bit) {
    if (!active_) return;
    const float error = final_probability_ - bit;
    for (size_t index = 0; index < features_.size(); ++index) {
      const float update =
          URL_RESIDUAL_LEARNING_RATE * error * features_[index];
      const size_t role_weight = role_index_ * features_.size() + index;
      const size_t context_weight = context_index_ * features_.size() + index;
      role_weights_[role_weight] -= update;
      context_weights_[context_weight] -= update;
    }
  }

 private:
  static constexpr size_t kNumRoles = 12;
  static constexpr size_t kNumContexts = 1024;
  static constexpr size_t kNumFeatures = 6;

  static bool IsResidualRole(UrlRole role) {
    return role == UrlRole::PathSegment ||
        role == UrlRole::Extension ||
        role == UrlRole::QueryKey ||
        role == UrlRole::QueryValue;
  }

  std::array<float, kNumRoles * kNumFeatures> role_weights_;
  std::vector<float> context_weights_;
  std::array<float, kNumFeatures> features_ = {};
  size_t role_index_ = 0;
  size_t context_index_ = 0;
  float final_probability_ = 0.5f;
  bool active_ = false;
};

#endif
