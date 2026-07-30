#ifndef CONDITIONAL_INDIRECT_H
#define CONDITIONAL_INDIRECT_H

#include "model.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

template<typename StateType>
class ConditionalIndirect : public Model {
 public:
  ConditionalIndirect(const bool& active, const StateType& state,
      const uint64_t& byte_context, const unsigned int& bit_context,
      float delta, std::vector<unsigned char>& map, uint32_t fixed_offset)
      : active_(active), byte_context_(byte_context),
        bit_context_(bit_context), map_offset_(0), divisor_(1.0 / delta),
        state_(state), map_(map) {
    assert(map_.size() > 257);
    map_offset_ = fixed_offset % (map_.size() - 257);
    for (int index = 0; index < 256; ++index) {
      predictions_[index] = state_.InitProbability(index);
    }
  }

  const std::valarray<float>& Predict() const {
    outputs_[0] = active_
        ? predictions_[map_[map_index_ + bit_context_]]
        : 0.5f;
    return outputs_;
  }

  void Perceive(int bit) {
    if (!active_) return;
    map_index_ += bit_context_;
    const int state = map_[map_index_];
    predictions_[state] += (bit - predictions_[state]) * divisor_;
    map_[map_index_] = state_.Next(state, bit);
    map_index_ -= bit_context_;
  }

  void ByteUpdate() {
    if (!active_) return;
    map_index_ =
        (257 * byte_context_ + map_offset_) % (map_.size() - 257);
  }

 private:
  const bool& active_;
  const uint64_t& byte_context_;
  const unsigned int& bit_context_;
  uint64_t map_index_ = 0;
  uint64_t map_offset_;
  float divisor_;
  const StateType& state_;
  std::vector<unsigned char>& map_;
  std::array<float, 256> predictions_;
};

#endif
