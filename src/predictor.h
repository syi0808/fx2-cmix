#ifndef PREDICTOR_H
#define PREDICTOR_H

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

#include "ds/SmallVector.h"
#include "ds/emhash_set.hpp"

#include <vector>
#include <set>
#include <memory>
#include <optional>

#if LATENT_TOPIC_SHADOW_EVAL
struct TopicShadowBranch {
  TopicShadowBranch(const Sigmoid& sigmoid,
      const std::valarray<float>& model_inputs,
      const unsigned long long& zero_context, size_t base_mixer_count,
      unsigned int mask);
  void SetBaseOutput(size_t index, float prediction);
  void Predict(uint32_t article_index, float fxcm_input, float byte_mixer_input,
      float override_prediction);
  void Perceive(int bit);

  unsigned int mask_;
  size_t base_mixer_count_;
  unsigned long long coarse_topic_ = 0;
  unsigned long long mid_topic_ = 0;
  unsigned long long fine_topic_ = 0;
  MixerInput extra_layer_;
  MixerInput top_layer_;
  std::vector<std::unique_ptr<Mixer>> topic_mixers_;
  std::unique_ptr<Mixer> top_mixer_;
  double loss_bits_ = 0;
  unsigned long long bits_ = 0;
  float prediction_ = 0.5;
};
#endif

class Predictor {
 public:
  Predictor(const std::vector<bool>& vocab);
#if LATENT_TOPIC_SHADOW_EVAL
  ~Predictor();
#endif
  float Predict();
  void Perceive(int bit);
  void Pretrain(int bit);

 private:
  unsigned long long GetNumModels();
  void AddMixer(int layer, const unsigned long long& context,
      float learning_rate);
  void AddAuxiliary();
  void AddPPMD();
  void AddBracket();
  void AddWord();
  void AddDirect();
  void AddMatch();
  void AddDoubleIndirect();
  void AddMixers();
#if LATENT_TOPIC_CONTEXT
  void UpdateLatentTopics();
#endif
#if LATENT_TOPIC_CONTEXT || LATENT_TOPIC_SHADOW_EVAL
  void UpdateArticleIndex(unsigned char byte);
#endif
#if LATENT_TOPIC_SHADOW_EVAL
  void AddShadowBranches();
#endif

  llvm::SmallVector<Indirect<Nonstationary>, 30-7> indirect_ns_models_; // non-stationary
  llvm::SmallVector<Indirect<RunMap>, 1> indirect_r_models_; // run map
  llvm::SmallVector<Direct, 1> direct_models_;
  llvm::SmallVector<Match, 10> match_models_;
  
  std::optional<Bracket> bracket_model_;
  size_t auxiliary_size_ = 2; // 0 -> fxcm, 1 -> byte_mixer
  SSE sse_;
  llvm::SmallVector<MixerInput,2> layers_;
  llvm::SmallVector<Mixer, 23 + ((LATENT_TOPIC_CONTEXT & 1) != 0)
      + ((LATENT_TOPIC_CONTEXT & 2) != 0)
      + ((LATENT_TOPIC_CONTEXT & 4) != 0)> mixer_0_;
  llvm::SmallVector<Mixer, 1> mixer_1_;
  std::vector<unsigned int> auxiliary_;
  ContextManager manager_;
  Sigmoid sigmoid_;
  std::optional<PPMD::PPMD> byte_model_;
  std::optional<ByteMixer> byte_mixer_;
  std::vector<bool> vocab_;
  FXCM fxcm_model_;
#if LATENT_TOPIC_CONTEXT
  unsigned long long coarse_topic_ = 0;
  unsigned long long mid_topic_ = 0;
  unsigned long long fine_topic_ = 0;
#endif
#if LATENT_TOPIC_SHADOW_EVAL
  std::vector<std::unique_ptr<TopicShadowBranch>> shadow_branches_;
#endif
#if LATENT_TOPIC_CONTEXT || LATENT_TOPIC_SHADOW_EVAL
  uint32_t article_index_ = 0;
  unsigned long long article_tail_ = 0;
#endif
};

#endif
