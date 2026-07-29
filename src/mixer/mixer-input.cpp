#include "mixer-input.h"

MixerInput::MixerInput(const Sigmoid& sigmoid, float eps) :
    inputs_(0.5, 1), sigmoid_(sigmoid), min_(eps), max_(1 - eps),
    stretched_min_(sigmoid.Logit(0)), stretched_max_(sigmoid.Logit(1)) {
  for (unsigned int i = 0; i < 4096; ++i) {
    float p = i * (1.0f / 4095);
    if (p < min_) p = min_;
    else if (p > max_) p = max_;
    input_12_bit_[i] = sigmoid_.Logit(p);
  }
  input_12_bit_[4096] = sigmoid_.Logit(0.5f);
}

void MixerInput::SetNumModels(int num_models) {
  inputs_.resize(num_models, 0.5);
}

void MixerInput::SetInput(int index, float p) {
  if (p < min_) p = min_;
  else if (p > max_) p = max_;
  inputs_[index] = sigmoid_.Logit(p);
}

void MixerInput::SetInputFrom12Bit(int index, uint16_t p) {
  inputs_[index] = input_12_bit_[p];
}

void MixerInput::SetStretchedInput(int index, float p) {
  if (p > stretched_max_) p = stretched_max_;
  else if (p < stretched_min_) p = stretched_min_;
  inputs_[index] = p;
}
void MixerInput::SetZero(int index) {
  
  inputs_[index] = 0.0f;
}

void MixerInput::SetExtraInput(size_t index, float p) {
  if (p > stretched_max_) p = stretched_max_;
  else if (p < stretched_min_) p = stretched_min_;
  extra_inputs_[index] = p;
}
