//#ifndef FXCM_H
//#define FXCM_H

#include "model.h"
#include <cstdint>
#include <vector>
#include <memory>

#ifndef LATENT_TOPIC_CONTEXT
#define LATENT_TOPIC_CONTEXT 0
#endif

#ifndef LATENT_TOPIC_SHADOW_EVAL
#define LATENT_TOPIC_SHADOW_EVAL 0
#endif

#ifndef LATENT_TOPIC_COARSE_SHIFT
#define LATENT_TOPIC_COARSE_SHIFT 12
#endif

#ifndef LATENT_TOPIC_MID_SHIFT
#define LATENT_TOPIC_MID_SHIFT 9
#endif

#ifndef LATENT_TOPIC_FINE_SHIFT
#define LATENT_TOPIC_FINE_SHIFT 6
#endif

static_assert((LATENT_TOPIC_CONTEXT & ~7) == 0,
    "LATENT_TOPIC_CONTEXT must be a bit mask from 0 to 7");
static_assert(!LATENT_TOPIC_SHADOW_EVAL || LATENT_TOPIC_CONTEXT == 0,
    "shadow evaluation requires LATENT_TOPIC_CONTEXT=0");
static_assert(LATENT_TOPIC_COARSE_SHIFT > LATENT_TOPIC_MID_SHIFT
        && LATENT_TOPIC_MID_SHIFT > LATENT_TOPIC_FINE_SHIFT,
    "latent topic shifts must be ordered coarse > mid > fine");

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

 private:
  std::unique_ptr<fxcmv1::Predictor> predictor_;
};

//#endif
