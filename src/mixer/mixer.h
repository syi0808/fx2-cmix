#ifndef MIXER_H
#define MIXER_H

#include <vector>
#include <valarray>
#include "../ds/emhash_map.hpp"
#include <memory>

struct ContextData {
  ContextData(size_t input_size, size_t extra_input_size)
      : weights(input_size), extra_weights(extra_input_size) {}
  //unsigned long long steps;
  std::vector<float> weights, extra_weights;
};

class Mixer {
 public:
  Mixer(const std::valarray<float>& inputs,
      const std::valarray<float>& extra_inputs, const unsigned long long& context,
      float learning_rate, unsigned int extra_input_size,
      const bool* active = nullptr);
  float Mix();
  void Perceive(int bit);

 private:
  ContextData* GetContextData();
  const std::valarray<float>& inputs_;
  const std::valarray<float>& extra_inputs_vec_;
  // std::valarray<float> extra_inputs_;
  uint16_t extra_inputs_size_;
  float p_, learning_rate_;
  const unsigned long long& context_;
  unsigned long long /*max_steps_,*/ steps_;
  emhash6::HashMap<unsigned int, ContextData> context_map_;
  ContextData context_base_;
  ContextData* active_data_;
  const bool* active_;
};

#endif
