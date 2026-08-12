#include "../../src/predictor.h"
#include "../../src/preprocess/preprocessor.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

unsigned int Discretize(float probability) {
  return 1 + 65534 * probability;
}

double BitLoss(int bit, unsigned int probability) {
  const double p = static_cast<double>(probability) / 65536.0;
  return -std::log2(bit ? p : 1.0 - p);
}

double ScoreByte(Predictor* predictor, uint8_t value) {
  double loss = 0.0;
  for (int shift = 7; shift >= 0; --shift) {
    const int bit = (value >> shift) & 1;
    const unsigned int probability = Discretize(predictor->Predict());
    loss += BitLoss(bit, probability);
    predictor->Perceive(bit);
  }
  return loss;
}

bool WriteAll(int fd, const void* data, size_t size) {
  const char* cursor = static_cast<const char*>(data);
  while (size) {
    const ssize_t written = write(fd, cursor, size);
    if (written < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    cursor += written;
    size -= static_cast<size_t>(written);
  }
  return true;
}

bool ReadAll(int fd, void* data, size_t size) {
  char* cursor = static_cast<char*>(data);
  while (size) {
    const ssize_t received = read(fd, cursor, size);
    if (received == 0) return false;
    if (received < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    cursor += received;
    size -= static_cast<size_t>(received);
  }
  return true;
}

double ScoreByteForked(Predictor* predictor, uint8_t value) {
  int pipefd[2];
  if (pipe(pipefd) != 0) {
    std::perror("pipe");
    std::exit(2);
  }

  const pid_t pid = fork();
  if (pid < 0) {
    std::perror("fork");
    std::exit(2);
  }

  if (pid == 0) {
    close(pipefd[0]);
    const double loss = ScoreByte(predictor, value);
    const bool ok = WriteAll(pipefd[1], &loss, sizeof(loss));
    close(pipefd[1]);
    _exit(ok ? 0 : 3);
  }

  close(pipefd[1]);
  double loss = 0.0;
  const bool read_ok = ReadAll(pipefd[0], &loss, sizeof(loss));
  close(pipefd[0]);

  int status = 0;
  if (waitpid(pid, &status, 0) != pid || !read_ok ||
      !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    std::fprintf(stderr, "candidate child failed for value=%u\n", value);
    std::exit(3);
  }
  return loss;
}

uint64_t Mix64(uint64_t value) {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}

uint64_t Fingerprint(uint8_t value) {
  return Mix64(static_cast<uint64_t>(value) ^ 0x485554544552ULL);
}

unsigned int MinimumFirstMatchFingerprintBits(
    const std::vector<int>& order, size_t actual_index, uint8_t actual) {
  const uint64_t actual_hash = Fingerprint(actual);
  for (unsigned int bits = 0; bits <= 32; ++bits) {
    const uint64_t mask = bits == 0 ? 0 : ((1ULL << bits) - 1);
    bool collision = false;
    for (size_t i = 0; i < actual_index; ++i) {
      if ((Fingerprint(static_cast<uint8_t>(order[i])) & mask) ==
          (actual_hash & mask)) {
        collision = true;
        break;
      }
    }
    if (!collision) return bits;
  }
  return 33;
}

std::vector<uint8_t> ReadFile(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    std::cerr << "cannot open input: " << path << "\n";
    std::exit(2);
  }
  input.seekg(0, std::ios::end);
  const std::streamoff size = input.tellg();
  input.seekg(0, std::ios::beg);
  std::vector<uint8_t> data(static_cast<size_t>(size));
  if (!data.empty()) {
    input.read(reinterpret_cast<char*>(data.data()), data.size());
  }
  return data;
}

size_t EnvSize(const char* name, size_t fallback) {
  const char* value = std::getenv(name);
  if (!value || !value[0]) return fallback;
  char* end = nullptr;
  const unsigned long long parsed = std::strtoull(value, &end, 10);
  if (!end || *end != '\0') {
    std::cerr << "invalid " << name << "=" << value << "\n";
    std::exit(2);
  }
  return static_cast<size_t>(parsed);
}

struct Sample {
  size_t position = 0;
  uint8_t actual = 0;
  double surprisal = 0.0;
  size_t rank = 0;
  double rank_bits = 0.0;
  unsigned int fingerprint_bits = 0;
};

Sample Evaluate(Predictor* predictor, size_t position, uint8_t actual) {
  std::vector<double> losses(256);
  for (int candidate = 0; candidate < 256; ++candidate) {
    losses[candidate] = ScoreByteForked(
        predictor, static_cast<uint8_t>(candidate));
  }

  std::vector<int> order(256);
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(), [&](int lhs, int rhs) {
    if (losses[lhs] != losses[rhs]) return losses[lhs] < losses[rhs];
    return lhs < rhs;
  });

  const auto found = std::find(order.begin(), order.end(), actual);
  const size_t index = static_cast<size_t>(found - order.begin());
  Sample sample;
  sample.position = position;
  sample.actual = actual;
  sample.surprisal = losses[actual];
  sample.rank = index + 1;
  sample.rank_bits = std::log2(static_cast<double>(sample.rank));
  sample.fingerprint_bits =
      MinimumFirstMatchFingerprintBits(order, index, actual);
  return sample;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    std::fprintf(stderr,
        "usage: hash-rank-oracle <predictor-input> [dictionary]\n");
    return 2;
  }

  std::srand(SEED);
  const std::string input_path = argv[1];
  const std::vector<uint8_t> data = ReadFile(input_path);
  if (data.empty()) {
    std::cerr << "empty predictor input\n";
    return 2;
  }

  std::vector<bool> vocab(256, false);
  if (data.size() < 10000) {
    std::fill(vocab.begin(), vocab.end(), true);
  } else {
    for (uint8_t value : data) vocab[value] = true;
  }

  Predictor predictor(vocab);
  FILE* dictionary = nullptr;
  if (argc == 3) {
    dictionary = std::fopen(argv[2], "rb");
    if (!dictionary) {
      std::perror("dictionary");
      return 2;
    }
    preprocessor::Pretrain(&predictor, dictionary);
    std::fclose(dictionary);
  }

  const size_t warmup = std::min(
      EnvSize("FX2_ORACLE_WARMUP", 32768), data.size() - 1);
  const size_t samples_requested = EnvSize("FX2_ORACLE_SAMPLES", 8);
  const size_t stride = std::max<size_t>(
      1, EnvSize("FX2_ORACLE_STRIDE", 65536));

  std::vector<size_t> targets;
  for (size_t i = 0; i < samples_requested; ++i) {
    const size_t position = warmup + i * stride;
    if (position >= data.size()) break;
    targets.push_back(position);
  }
  if (targets.empty()) targets.push_back(warmup);

  std::cerr << "predictor_input_bytes=" << data.size()
            << " warmup=" << warmup
            << " samples=" << targets.size()
            << " stride=" << stride << "\n";

  std::vector<Sample> samples;
  size_t target_index = 0;
  for (size_t position = 0; position < data.size(); ++position) {
    if (target_index < targets.size() && position == targets[target_index]) {
      std::cerr << "evaluating position " << position << " ("
                << (target_index + 1) << "/" << targets.size() << ")\n";
      samples.push_back(Evaluate(&predictor, position, data[position]));
      ++target_index;
    }

    ScoreByte(&predictor, data[position]);
    if (target_index == targets.size() && position >= targets.back()) break;
  }

  std::cout << "position,actual,surprisal_bits,rank,log2_rank,min_first_match_hash_bits,rank_oracle_gain_bits,hash_payload_gain_bits\n";
  double total_surprisal = 0.0;
  double total_rank_bits = 0.0;
  double total_hash_bits = 0.0;
  for (const Sample& sample : samples) {
    total_surprisal += sample.surprisal;
    total_rank_bits += sample.rank_bits;
    total_hash_bits += sample.fingerprint_bits;
    std::cout << sample.position << ','
              << static_cast<unsigned int>(sample.actual) << ','
              << std::fixed << std::setprecision(6) << sample.surprisal << ','
              << sample.rank << ',' << sample.rank_bits << ','
              << sample.fingerprint_bits << ','
              << (sample.surprisal - sample.rank_bits) << ','
              << (sample.surprisal - sample.fingerprint_bits) << '\n';
  }

  const double count = static_cast<double>(samples.size());
  std::cout << "SUMMARY"
            << ",samples=" << samples.size()
            << ",mean_surprisal_bits=" << (total_surprisal / count)
            << ",mean_log2_rank=" << (total_rank_bits / count)
            << ",mean_min_hash_bits=" << (total_hash_bits / count)
            << ",mean_rank_oracle_gain_bits="
            << ((total_surprisal - total_rank_bits) / count)
            << ",mean_hash_payload_gain_bits="
            << ((total_surprisal - total_hash_bits) / count)
            << '\n';
  return 0;
}
