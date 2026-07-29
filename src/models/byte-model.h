#ifndef BYTE_MODEL_H
#define BYTE_MODEL_H

#include "model.h"

#include <valarray>
#include <vector>

class ByteModel : public Model {
 public:
  virtual ~ByteModel() {}
  ByteModel(const std::vector<bool>& vocab, bool track_expected = false);
  const std::valarray<float>& BytePredict();
   std::valarray<float>& Predict() ;
  void Perceive(int bit);
  int ex;
 protected:
  void ByteUpdate();
  int top_, mid_, bot_;
  const std::vector<bool>& vocab_;
  std::valarray<float> probs_;
  bool track_expected_;
};

#endif
