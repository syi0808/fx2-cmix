#ifndef GATED_MODEL_H
#define GATED_MODEL_H

#include <utility>

template<typename Model>
class GatedModel {
 public:
  template<typename... Args>
  GatedModel(const bool& gate, Args&&... args)
      : gate_(gate), model_(std::forward<Args>(args)...) {}

  const std::valarray<float>& Predict() const {
    if (!gate_) {
      neutral_[0] = 0.5f;
      return neutral_;
    }
    return model_.Predict();
  }

  void Perceive(int bit) {
    if (gate_) model_.Perceive(bit);
  }

  void ByteUpdate() {
    if (gate_) model_.ByteUpdate();
  }

  unsigned int NumOutputs() const {
    return 1;
  }

 private:
  const bool& gate_;
  Model model_;
  mutable std::valarray<float> neutral_ = std::valarray<float>(0.5f, 1);
};

#endif
