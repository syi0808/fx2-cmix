#ifndef MIXER_INPUT_H
#define MIXER_INPUT_H

#include "sigmoid.h"

#include <array>
#include <cstdint>
#include <valarray>
#include <vector>

class MixerInput {
 public:
  MixerInput(const Sigmoid& sigmoid, float eps);
  void SetNumModels(int num_models);
  void SetInput(int index, float p);
  void SetInputFrom12Bit(int index, uint16_t p);
  void SetStretchedInput(int index, float p);
  void SetZero(int index);
  void SetExtraInput(size_t index, float p);
  void SetExtraInputSize(size_t size) { extra_inputs_.resize(size);};
  //void ClearExtraInputs() { extra_inputs_.clear(); }
  const std::valarray<float>& Inputs() const { return inputs_; }
  float* MutableInputs() { return std::begin(inputs_); }
  const float* Input12BitTable() const { return input_12_bit_.data(); }
  //const std::vector<float>& ExtraInputs() const { return extra_inputs_; }
  const auto& ExtraInputs() const { return extra_inputs_; }

 private:
  std::valarray<float> inputs_;
  //std::vector<float> extra_inputs_;
  std::valarray<float> extra_inputs_;
  const Sigmoid& sigmoid_;
  float min_, max_, stretched_min_, stretched_max_;
  std::array<float, 4097> input_12_bit_;
};

#endif
