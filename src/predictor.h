#ifndef PREDICTOR_H
#define PREDICTOR_H

#ifndef SEED
#define SEED 923
#endif
#ifndef UPDATE_LIMIT
#define UPDATE_LIMIT 3000
#endif
#ifndef FX2_CONTROL_PROGRAM
#define FX2_CONTROL_PROGRAM 0
#endif

#include "mixer/sigmoid.h"
#include "mixer/mixer-input.h"
#include "mixer/mixer.h"
#include "mixer/byte-mixer.h"
#include "mixer/sse.h"
#include "models/model.h"
#include "models/byte-model.h"
#include "context-manager.h"
#include "models/direct.h"
#include "models/direct-hash.h"
#include "models/indirect.h"
#include "models/conditional-indirect.h"
#include "models/gated-model.h"
#include "models/url-residual.h"
#include "models/match.h"
#include "models/ppmd.h"
#include "models/bracket.h"
#include "models/fxcmv1.h"
#include "mixer/lstm.h"
#include "contexts/context-hash.h"
#include "contexts/bracket-context.h"
#include "contexts/sparse.h"
#include "contexts/indirect-hash.h"
#include "contexts/interval.h"
#include "contexts/interval-hash.h"
#include "contexts/bit-context.h"
#include "contexts/combined-context.h"
#include "control/control-program.h"
#include "contexts/url-context.h"

#include "ds/SmallVector.h"
#include "ds/emhash_set.hpp"

#include <vector>
#include <set>
#include <memory>
#include <optional>
#include <array>

#ifndef NUMERIC_BOUNDARY_MODEL
#define NUMERIC_BOUNDARY_MODEL 0
#endif
#ifndef NUMERIC_LINKED_MODEL
#define NUMERIC_LINKED_MODEL 0
#endif
#ifndef NUMERIC_START_MODEL
#define NUMERIC_START_MODEL 0
#endif
#ifndef NUMERIC_FIELD_MODEL
#define NUMERIC_FIELD_MODEL 0
#endif
#ifndef NUMERIC_BOUNDARY_MIXER
#define NUMERIC_BOUNDARY_MIXER 0
#endif
#ifndef URL_MODEL
#define URL_MODEL 1
#endif
#ifndef URL_INTEGRATED
#define URL_INTEGRATED 0
#endif
#ifndef URL_SIDECAR
#define URL_SIDECAR (URL_MODEL && !URL_INTEGRATED)
#endif
#ifndef URL_SYNTAX_HEAD
#define URL_SYNTAX_HEAD URL_MODEL
#endif
#ifndef URL_COMPONENT_HEAD
#define URL_COMPONENT_HEAD URL_MODEL
#endif
#ifndef URL_RELATION_HEAD
#define URL_RELATION_HEAD URL_MODEL
#endif
#ifndef URL_TEMPLATE_HEAD
#define URL_TEMPLATE_HEAD URL_MODEL
#endif
#ifndef URL_MATCH_HEAD
#define URL_MATCH_HEAD URL_INTEGRATED
#endif
#ifndef URL_ROLE_MIXER
#define URL_ROLE_MIXER URL_INTEGRATED
#endif

class Predictor {
 public:
  Predictor(const std::vector<bool>& vocab);
  float Predict();
  void Perceive(int bit);
  void Pretrain(int bit);
  const std::array<float, 4>& NumericModelProbabilities() const {
    return numeric_model_probabilities_;
  }
  uint8_t NumericStartWordBucket() const {
    return manager_.numeric_sequence_.StartWordBucket();
  }
  uint8_t NumericStartWrtBucket() const {
    return manager_.numeric_sequence_.StartWrtBucket();
  }
  const UrlState& CurrentUrlState() const {
    return manager_.url_context_.State();
  }
#if FX2_CONTROL_PROGRAM
  const ControlFeatures& CurrentControlFeatures() const {
    return control_features_;
  }
  float BaselineProbability() const { return baseline_probability_; }
  float ControlCandidateProbability(uint8_t scale_class) const {
    return ApplyControlScale(baseline_probability_, scale_class, sigmoid_);
  }
#endif

 private:
  unsigned long long GetNumModels();
  void AddMixer(int layer, const unsigned long long& context,
      float learning_rate, const bool* active = nullptr);
  void AddAuxiliary();
  void AddPPMD();
  void AddBracket();
  void AddWord();
  void AddDirect();
  void AddMatch();
  void AddDoubleIndirect();
  void AddNumericBoundary();
  void AddNumericLinked();
  void AddNumericStart();
  void AddUrl();
  void AddMixers();

  llvm::SmallVector<Indirect<Nonstationary>, 30-7> indirect_ns_models_; // non-stationary
  llvm::SmallVector<Indirect<RunMap>, 1> indirect_r_models_; // run map
  llvm::SmallVector<ConditionalIndirect<Nonstationary>, 4>
      conditional_numeric_models_;
  llvm::SmallVector<GatedModel<Indirect<Nonstationary>>, 4>
      gated_url_models_;
  llvm::SmallVector<GatedModel<Match>, 1> gated_url_match_models_;
  llvm::SmallVector<Direct, 1> direct_models_;
  llvm::SmallVector<Match, 10> match_models_;
  
  std::optional<Bracket> bracket_model_;
  size_t auxiliary_size_ = 2; // 0 -> fxcm, 1 -> byte_mixer
  SSE sse_;
  llvm::SmallVector<MixerInput,2> layers_;
  llvm::SmallVector<Mixer, 23> mixer_0_;
  llvm::SmallVector<Mixer, 1> mixer_1_;
  std::vector<unsigned int> auxiliary_;
  ContextManager manager_;
  Sigmoid sigmoid_;
  std::optional<PPMD::PPMD> byte_model_;
  std::optional<ByteMixer> byte_mixer_;
  std::vector<bool> vocab_;
  std::array<float, 4> numeric_model_probabilities_ = {
      0.5f, 0.5f, 0.5f, 0.5f};
  std::array<float, 5> url_model_probabilities_ = {
      0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
  size_t url_model_probability_count_ = 0;
  UrlResidual url_residual_;
  bool url_residual_used_ = false;
#if FX2_CONTROL_PROGRAM
  ControlFeatures control_features_;
  float baseline_probability_ = 0.5f;
#endif
   FXCM fxcm_model_;
};

#endif
