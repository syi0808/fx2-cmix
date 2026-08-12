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

uint32_t BlockValue(const std::vector<uint8_t>& data, size_t position) {
  uint32_t value = 0;
  for (size_t index = 0; index < 4; ++index) {
    value = (value << 8) | data[position + index];
  }
  return value;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    std::cerr << "usage: hard-scan <predictor-input> [dictionary]\n";
    return 2;
  }

  std::srand(SEED);
  const std::vector<uint8_t> data = ReadFile(argv[1]);
  if (data.size() < 4) return 2;

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

  const size_t start = std::min(EnvSize("FX2_HARD_SCAN_START", 1024), data.size() - 4);
  const size_t requested_end = EnvSize("FX2_HARD_SCAN_END", 30000);
  const size_t end = std::min(data.size(), requested_end);
  if (start + 4 > end) return 2;

  std::vector<double> fx2_loss(end, 0.0);
  std::vector<double> ppmd_loss(end, 0.0);
  for (size_t position = 0; position < end; ++position) {
    const uint8_t actual = data[position];
    const std::valarray<float>& distribution = predictor.PpmdByteProbabilities();
    ppmd_loss[position] = -std::log2(std::max(
        1e-30, static_cast<double>(distribution[actual])));
    fx2_loss[position] = AdvanceByte(&predictor, actual);
  }

  std::cout << "position,actual,full_fx2_4byte_bits,ppmd_4byte_nll_bits,ppmd_minus_fx2_bits\n";
  std::cout << std::fixed << std::setprecision(9);
  for (size_t position = start; position + 4 <= end; ++position) {
    double fx2 = 0.0;
    double ppmd = 0.0;
    for (size_t index = 0; index < 4; ++index) {
      fx2 += fx2_loss[position + index];
      ppmd += ppmd_loss[position + index];
    }
    std::cout << position << ',' << BlockValue(data, position) << ','
              << fx2 << ',' << ppmd << ',' << (ppmd - fx2) << '\n';
  }
  return 0;
}
