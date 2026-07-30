#include "../src/models/url-residual.h"

#include <array>
#include <cassert>
#include <iostream>

int main() {
  Sigmoid sigmoid(10001);
  UrlResidual residual;
  std::array<float, 5> models = {0.8f, 0.7f, 0.6f, 0.5f, 0.5f};
  UrlState state;

  const float baseline = 0.3f;
  assert(residual.Predict(baseline, models, models.size(), state, sigmoid) ==
      baseline);

  state.confidence = UrlConfidence::UrlConfirmed;
  state.role = UrlRole::Domain;
  assert(residual.Predict(baseline, models, models.size(), state, sigmoid) ==
      baseline);

  state.role = UrlRole::PathSegment;
  const float initial =
      residual.Predict(baseline, models, models.size(), state, sigmoid);
  residual.Perceive(1);
  const float learned =
      residual.Predict(baseline, models, models.size(), state, sigmoid);
  assert(learned > initial);

  std::cout << "url-residual tests passed\n";
}
