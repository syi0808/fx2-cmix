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
#include <vector>

namespace {

unsigned int Discretize(float p) { return 1 + 65534 * p; }

double BitLoss(int bit, unsigned int probability) {
  const double p = static_cast<double>(probability) / 65536.0;
  return -std::log2(bit ? p : 1.0 - p);
}

std::vector<uint8_t> ReadFile(const char* path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    std::cerr << "cannot open " << path << '\n';
    std::exit(2);
  }
  in.seekg(0, std::ios::end);
  const size_t size = static_cast<size_t>(in.tellg());
  in.seekg(0, std::ios::beg);
  std::vector<uint8_t> data(size);
  if (size) in.read(reinterpret_cast<char*>(data.data()), size);
  return data;
}

size_t EnvSize(const char* name, size_t fallback) {
  const char* value = std::getenv(name);
  if (!value || !value[0]) return fallback;
  return static_cast<size_t>(std::strtoull(value, nullptr, 10));
}

struct ForcedTrie {
  // -1 = both continuations possible, 0/1 = forced next bit, -2 = no value.
  std::array<int8_t, 256> forced{};

  explicit ForcedTrie(const std::vector<bool>& vocab) {
    forced.fill(-2);
    for (unsigned int node = 1; node < 256; ++node) {
      unsigned int depth = 0;
      for (unsigned int x = node; x > 1; x >>= 1) ++depth;
      bool zero = false;
      bool one = false;
      for (unsigned int value = 0; value < 256; ++value) {
        if (!vocab[value]) continue;
        const unsigned int full = 256 + value;
        if ((full >> (8 - depth)) != node) continue;
        const int bit = (value >> (7 - depth)) & 1;
        if (bit) one = true;
        else zero = true;
      }
      if (zero && one) forced[node] = -1;
      else if (zero) forced[node] = 0;
      else if (one) forced[node] = 1;
    }
  }
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    std::cerr << "usage: vocab-forced-measure <predictor-input> [dictionary]\n";
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
  for (bool used : vocab) vocab_size += used ? 1 : 0;
  const ForcedTrie trie(vocab);

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

  const size_t limit = std::min(data.size(), EnvSize("FX2_VOCAB_LIMIT", data.size()));
  uint64_t bits = 0;
  uint64_t forced_bits = 0;
  double baseline_loss = 0.0;
  double constrained_loss = 0.0;
  double forced_baseline_loss = 0.0;
  double forced_constrained_loss = 0.0;
  std::array<uint64_t, 8> forced_by_depth{};
  std::array<double, 8> gain_by_depth{};

  unsigned int node = 1;
  unsigned int depth = 0;
  for (size_t position = 0; position < limit; ++position) {
    const uint8_t byte = data[position];
    for (int shift = 7; shift >= 0; --shift) {
      const int bit = (byte >> shift) & 1;
      const float raw = predictor.Predict();
      const unsigned int raw_q = Discretize(raw);
      const double raw_loss = BitLoss(bit, raw_q);
      baseline_loss += raw_loss;

      double constrained_bit_loss = raw_loss;
      const int forced = trie.forced[node];
      if (forced >= 0) {
        if (forced != bit) {
          std::cerr << "vocab trie contradiction at byte=" << position
                    << " depth=" << depth << '\n';
          return 3;
        }
        const unsigned int forced_q = forced ? 65535 : 1;
        constrained_bit_loss = BitLoss(bit, forced_q);
        ++forced_bits;
        forced_baseline_loss += raw_loss;
        forced_constrained_loss += constrained_bit_loss;
        ++forced_by_depth[depth];
        gain_by_depth[depth] += raw_loss - constrained_bit_loss;
      }
      constrained_loss += constrained_bit_loss;
      ++bits;
      predictor.Perceive(bit);

      node = (node << 1) | static_cast<unsigned int>(bit);
      ++depth;
      if (depth == 8) {
        node = 1;
        depth = 0;
      }
    }
  }

  std::cout << std::fixed << std::setprecision(9);
  std::cout << "predictor_bytes=" << limit << '\n';
  std::cout << "vocab_size=" << vocab_size << '\n';
  std::cout << "total_bits=" << bits << '\n';
  std::cout << "forced_bits=" << forced_bits << '\n';
  std::cout << "forced_bits_per_byte="
            << static_cast<double>(forced_bits) / limit << '\n';
  std::cout << "baseline_ideal_bits=" << baseline_loss << '\n';
  std::cout << "constrained_ideal_bits=" << constrained_loss << '\n';
  std::cout << "gain_bits=" << baseline_loss - constrained_loss << '\n';
  std::cout << "gain_bytes=" << (baseline_loss - constrained_loss) / 8.0 << '\n';
  std::cout << "gain_bits_per_byte="
            << (baseline_loss - constrained_loss) / limit << '\n';
  std::cout << "forced_baseline_bits=" << forced_baseline_loss << '\n';
  std::cout << "forced_constrained_bits=" << forced_constrained_loss << '\n';
  for (size_t d = 0; d < 8; ++d) {
    std::cout << "depth" << d << "_forced_bits=" << forced_by_depth[d] << '\n';
    std::cout << "depth" << d << "_gain_bits=" << gain_by_depth[d] << '\n';
  }
  return 0;
}
