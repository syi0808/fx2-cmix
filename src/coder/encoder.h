#ifndef ENCODER_H
#define ENCODER_H

#include <fstream>
#include <vector>

#include "../predictor.h"

#ifndef FX2_EXPERIMENT_TRACE
#define FX2_EXPERIMENT_TRACE 0
#endif

struct NumericTrace;
struct UrlTrace;
#if FX2_EXPERIMENT_TRACE
struct ExperimentTrace;
#endif

class Encoder {
 public:
  Encoder(std::ofstream* os, Predictor* p);
  ~Encoder();
  void Encode(int bit);
  void Flush();
  size_t OutputSize() { return out_.size();}
 private:
  void WriteByte(unsigned int byte);
  unsigned int Discretize(float p);

  std::vector<char> out_;
  std::ofstream* os_;
  unsigned int x1_, x2_;
  Predictor* p_;
  NumericTrace* numeric_trace_;
  UrlTrace* url_trace_;
#if FX2_EXPERIMENT_TRACE
  ExperimentTrace* experiment_trace_;
#endif
};
#endif
