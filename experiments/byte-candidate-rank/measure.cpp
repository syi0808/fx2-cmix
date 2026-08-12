#include "../../src/predictor.h"
#include "../../src/preprocess/preprocessor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <vector>

namespace {

unsigned int Discretize(float probability) {
  return 1 + 65534 * probability;
}

double BitLoss(int bit, unsigned int probability) {
  const double p = static_cast<double>(probability) / 65536.0;
  return -std::log2(bit ? p : 1.0 - p);
}

double AdvanceByte(Predictor* predictor, uint8_t value) {
  double loss = 0.0;
  for (int shift = 7; shift >= 0; --shift) {
    const int bit = (value >> shift) & 1;
    const unsigned int probability = Discretize(predictor->Predict());
    loss += BitLoss(bit, probability);
    predictor->Perceive(bit);
  }
  return loss;
}

std::vector<uint8_t> ReadFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    std::cerr << "cannot open input: " << path << '\n';
    std::exit(2);
  }
  input.seekg(0, std::ios::end);
  const size_t size = static_cast<size_t>(input.tellg());
  input.seekg(0, std::ios::beg);
  std::vector<uint8_t> data(size);
  if (size) input.read(reinterpret_cast<char*>(data.data()), size);
  return data;
}

size_t EnvSize(const char* name, size_t fallback) {
  const char* value = std::getenv(name);
  if (!value || !value[0]) return fallback;
  return static_cast<size_t>(std::strtoull(value, nullptr, 10));
}

unsigned int FloorLog2(uint64_t value) {
  unsigned int result = 0;
  while (value >>= 1) ++result;
  return result;
}

unsigned int EliasGammaBits(uint64_t positive_value) {
  return 2 * FloorLog2(positive_value) + 1;
}

struct DistributionMetrics {
  uint64_t samples = 0;
  double nll_bits = 0.0;
  double log2_rank_bits = 0.0;
  uint64_t gamma_rank_bits = 0;
  double reciprocal_rank = 0.0;
  std::array<uint64_t, 8> topk = {};
  uint64_t rank_sum = 0;
  uint64_t max_rank = 0;

  void Add(const std::valarray<float>& probabilities,
      const std::vector<bool>& vocab, uint8_t actual) {
    double total = 0.0;
    for (unsigned int value = 0; value < 256; ++value) {
      if (vocab[value]) total += std::max(0.0f, probabilities[value]);
    }
    const double actual_probability = total > 0
        ? std::max(1e-30, static_cast<double>(probabilities[actual]) / total)
        : 1.0 / 256.0;
    nll_bits += -std::log2(actual_probability);

    uint64_t earlier = 0;
    const float target = probabilities[actual];
    for (unsigned int value = 0; value < 256; ++value) {
      if (!vocab[value] || value == actual) continue;
      if (probabilities[value] > target ||
          (probabilities[value] == target && value < actual)) {
        ++earlier;
      }
    }
    const uint64_t rank = earlier + 1;
    ++samples;
    rank_sum += rank;
    max_rank = std::max(max_rank, rank);
    log2_rank_bits += std::log2(static_cast<double>(rank));
    gamma_rank_bits += EliasGammaBits(rank);
    reciprocal_rank += 1.0 / static_cast<double>(rank);

    static constexpr std::array<unsigned int, 8> kValues = {
        1, 2, 4, 8, 16, 32, 64, 128};
    for (size_t index = 0; index < kValues.size(); ++index) {
      if (rank <= kValues[index]) ++topk[index];
    }
  }

  void Print(const char* prefix) const {
    const double count = static_cast<double>(samples);
    std::cout << prefix << "_samples=" << samples << '\n';
    std::cout << prefix << "_mean_nll_bits=" << nll_bits / count << '\n';
    std::cout << prefix << "_mean_log2_rank=" << log2_rank_bits / count << '\n';
    std::cout << prefix << "_mean_gamma_rank_bits="
              << static_cast<double>(gamma_rank_bits) / count << '\n';
    std::cout << prefix << "_mean_rank="
              << static_cast<double>(rank_sum) / count << '\n';
    std::cout << prefix << "_mean_reciprocal_rank="
              << reciprocal_rank / count << '\n';
    std::cout << prefix << "_max_rank=" << max_rank << '\n';
    static constexpr std::array<unsigned int, 8> kValues = {
        1, 2, 4, 8, 16, 32, 64, 128};
    for (size_t index = 0; index < kValues.size(); ++index) {
      std::cout << prefix << "_top" << kValues[index] << "_rate="
                << static_cast<double>(topk[index]) / count << '\n';
    }
  }
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    std::cerr << "usage: byte-candidate-rank <predictor-input> [dictionary]\n";
    return 2;
  }

  std::srand(SEED);
  const std::vector<uint8_t> data = ReadFile(argv[1]);
  if (data.empty()) return 2;

  std::vector<bool> vocab(256, false);
  if (data.size() < 10000) {
    std::fill(vocab.begin(), vocab.end(), true);
  } else {
    for (uint8_t value : data) vocab[value] = true;
  }
  size_t vocab_size = 0;
  for (bool enabled : vocab) if (enabled) ++vocab_size;

  Predictor predictor(vocab);
  if (argc == 3) {
    FILE* dictionary = std::fopen(argv[2], "rb");
    if (!dictionary) {
      std::perror("dictionary");
      return 2;
    }
    preprocessor::Pretrain(&predictor, dictionary);
    std::fclose(dictionary);
  }

  const size_t warmup = std::min(EnvSize("FX2_BYTE_RANK_WARMUP", 256),
      data.size() - 1);
  const size_t requested = EnvSize("FX2_BYTE_RANK_LIMIT", data.size());
  const size_t end = std::min(data.size(), warmup + requested);

  DistributionMetrics ppmd;
  DistributionMetrics byte_mixer;
  double full_predictor_bits = 0.0;
  for (size_t position = 0; position < end; ++position) {
    const uint8_t actual = data[position];
    if (position >= warmup) {
      ppmd.Add(predictor.PpmdByteProbabilities(), vocab, actual);
      byte_mixer.Add(predictor.ByteMixerProbabilities(), vocab, actual);
    }
    const double loss = AdvanceByte(&predictor, actual);
    if (position >= warmup) full_predictor_bits += loss;
  }

  const double samples = static_cast<double>(end - warmup);
  std::cout << std::fixed << std::setprecision(9);
  std::cout << "predictor_input_bytes=" << data.size() << '\n';
  std::cout << "vocab_size=" << vocab_size << '\n';
  std::cout << "warmup_bytes=" << warmup << '\n';
  std::cout << "measured_bytes=" << (end - warmup) << '\n';
  std::cout << "full_predictor_mean_surprisal_bits="
            << full_predictor_bits / samples << '\n';
  ppmd.Print("ppmd");
  byte_mixer.Print("byte_mixer");
  return 0;
}
