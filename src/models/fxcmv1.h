//#ifndef FXCM_H
//#define FXCM_H

#include "model.h"
#include <cstdint>
#include <vector>
#include <memory>

#ifndef LATENT_TOPIC_CONTEXT
#define LATENT_TOPIC_CONTEXT 0
#endif

static_assert((LATENT_TOPIC_CONTEXT & ~7) == 0,
    "LATENT_TOPIC_CONTEXT must be a bit mask from 0 to 7");

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
  const std::valarray<uint16_t>& Predict() const;
  unsigned int NumOutputs();
  void Perceive(int bit);
  void ByteUpdate() {};
#if LATENT_TOPIC_CONTEXT
  uint32_t ArticleIndex() const;
#endif

 private:
  std::unique_ptr<fxcmv1::Predictor> predictor_;
};

//#endif
