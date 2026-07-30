#include "mixer.h"

#include "sigmoid.h"

#include <numeric>
#include <utility>
#include <math.h>
#include <sys/resource.h>

namespace {
constexpr unsigned int kContextLimit = 10000;
}

Mixer::Mixer(const std::valarray<float>& inputs,
    const std::valarray<float>& extra_inputs,
    const unsigned long long& context, float learning_rate,
    unsigned int extra_input_size, const bool* active) : inputs_(inputs),
    extra_inputs_vec_(extra_inputs), extra_inputs_size_(extra_input_size),/*extra_inputs_(extra_input_size),*/ p_(0.5),
    learning_rate_(learning_rate), context_(context), /*max_steps_(1),*/ steps_(0),
    context_base_(inputs.size(), extra_inputs_size_), active_data_(nullptr),
    active_(active) {
  context_map_.reserve(kContextLimit);
}

ContextData* Mixer::GetContextData() {
  ContextData* data;
  const unsigned int context = context_;
  auto it = context_map_.find(context);
  if (context_map_.size() >= kContextLimit && it == context_map_.end()) {
    data = &context_base_;
    // data = context_map_[0xDEADBEEF].get();
    // if (data == nullptr) {
    //   context_map_[0xDEADBEEF] = std::unique_ptr<ContextData>(
    //       new ContextData(inputs_.size(), extra_inputs_.size()));
    //   data = context_map_[0xDEADBEEF].get();
    // }
  } else {
    if (it != context_map_.end()) {
      data = &it->second;
    } else {
      auto [it, success] = context_map_.try_emplace(
          context, ContextData(inputs_.size(), extra_inputs_size_));
      data = &it->second;
    }
  }

  return data;
}

float Mixer::Mix() {
  if (active_ && !*active_) {
    active_data_ = nullptr;
    p_ = 0;
    return 0;
  }
  active_data_ = GetContextData();
  float p = 0;
  for (int i = 0; i < inputs_.size(); ++i) {
    p += inputs_[i] * active_data_->weights[i];
  }
  p_ = p;
  // for (unsigned int i = 0; i < extra_inputs_.size(); ++i) {
  //   extra_inputs_[i] = extra_inputs_vec_[i];
  // }
  float e = 0;
  for (unsigned int i = 0; i < extra_inputs_size_; ++i) {
    e += extra_inputs_vec_[i] * active_data_->extra_weights[i];
  }
  p_ += e;
  return p_;
}

void Mixer::Perceive(int bit) {
  if (!active_data_) return;

  float decay=0.2f;
  if ( steps_ < 25000000) {
      decay = 0.3f;
      if ( steps_ < 5000000) { 
          decay = 0.7f;
          if ( steps_ < 1000000)  
              decay = 1.0f;
      }
  }
  ++steps_;
   
  float update =   learning_rate_ * (Sigmoid::Logistic(p_) - bit);
  if(fabs(update)<0.000000000005f && extra_inputs_size_>0) {
      return;
  }
   // ++data->steps;
  update = decay * update;
  for (size_t i = 0; i < active_data_->weights.size(); ++i) {
    active_data_->weights[i] -= update * inputs_[i];
  }
  for (size_t i = 0; i < extra_inputs_size_; ++i) {
    active_data_->extra_weights[i] -= update * extra_inputs_vec_[i];
  }
 /*if ((data->steps & 1023) == 0) {
    data->weights *= 1.0f - 3.0e-6f;
    data->extra_weights *= 1.0f - 3.0e-6f;
  }*/

}
