#include "../../src/predictor.h"
#include "../../src/preprocess/preprocessor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
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

uint32_t ReadBlockValue(const std::vector<uint8_t>& data, size_t position,
    unsigned int length) {
  uint32_t value = 0;
  for (unsigned int index = 0; index < length; ++index) {
    value = (value << 8) | data[position + index];
  }
  return value;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    std::cerr << "usage: full-block-baseline <predictor-input> [dictionary]\n";
    return 2;
  }

  std::srand(SEED);
  const std::vector<uint8_t> data = ReadFile(argv[1]);
  const unsigned int block_length = static_cast<unsigned int>(
      EnvSize("FX2_BLOCK_RANK_LENGTH", 4));
  if (block_length == 0 || block_length > 4 || data.size() <= block_length) {
    return 2;
  }

  std::vector<bool> vocab(256, false);
  if (data.size() < 10000) {
    std::fill(vocab.begin(), vocab.end(), true);
  } else {
    for (uint8_t value : data) vocab[value] = true;
  }

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

  const size_t warmup = EnvSize("FX2_BLOCK_RANK_WARMUP", 1024);
  const size_t stride = std::max<size_t>(
      1, EnvSize("FX2_BLOCK_RANK_STRIDE", 457));
  const size_t requested = EnvSize("FX2_BLOCK_RANK_SAMPLES", 64);
  std::vector<size_t> targets;
  for (size_t sample = 0; sample < requested; ++sample) {
    const size_t position = warmup + sample * stride;
    if (position + block_length > data.size()) break;
    targets.push_back(position);
  }
  if (targets.empty()) return 2;

  const size_t end = targets.back() + block_length;
  std::vector<double> byte_losses(end, 0.0);
  for (size_t position = 0; position < end; ++position) {
    byte_losses[position] = AdvanceByte(&predictor, data[position]);
  }

  double total = 0.0;
  std::cout << "position,actual,full_fx2_block_surprisal_bits\n";
  for (size_t position : targets) {
    double loss = 0.0;
    for (unsigned int index = 0; index < block_length; ++index) {
      loss += byte_losses[position + index];
    }
    total += loss;
    std::cout << position << ',' << ReadBlockValue(data, position, block_length)
              << ',' << std::fixed << std::setprecision(9) << loss << '\n';
  }

  const double count = static_cast<double>(targets.size());
  std::cout << "SUMMARY"
            << ",samples=" << targets.size()
            << ",block_length=" << block_length
            << ",mean_full_fx2_block_surprisal_bits=" << total / count
            << ",mean_full_fx2_surprisal_per_byte="
            << total / count / block_length << '\n';
  return 0;
}
