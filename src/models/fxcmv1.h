//#ifndef FXCM_H
//#define FXCM_H

#include "model.h"
#include <cstdint>
#include <vector>
#include <memory>

namespace fxcmv1 {
  class Predictor{

public:
  Predictor();
  int p() ;
  void update();
};
}

class FXCM : public Model {
 public:
  FXCM();
  void SetMixerInput(float* inputs, const float* input_12_bit);
  unsigned int NumOutputs();
  void Perceive(int bit);
  void ByteUpdate() {};

 private:
  std::unique_ptr<fxcmv1::Predictor> predictor_;
};

//#endif
