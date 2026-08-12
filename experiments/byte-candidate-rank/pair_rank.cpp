#include "../../src/models/ppmd.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

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

unsigned int FloorLog2(uint64_t value) {
  unsigned int result = 0;
  while (value >>= 1) ++result;
  return result;
}

unsigned int EliasGammaBits(uint64_t positive_value) {
  return 2 * FloorLog2(positive_value) + 1;
}

uint64_t Mix64(uint64_t value) {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}

uint64_t Fingerprint(uint16_t value) {
  return Mix64(static_cast<uint64_t>(value) ^ 0x50504d443136ULL);
}

unsigned int MinimumFirstMatchFingerprintBits(
    const std::vector<uint16_t>& order, size_t actual_index, uint16_t actual) {
  const uint64_t target = Fingerprint(actual);
  for (unsigned int bits = 0; bits <= 32; ++bits) {
    const uint64_t mask = bits == 0 ? 0 : ((1ULL << bits) - 1);
    bool collision = false;
    for (size_t index = 0; index < actual_index; ++index) {
      if ((Fingerprint(order[index]) & mask) == (target & mask)) {
        collision = true;
        break;
      }
    }
    if (!collision) return bits;
  }
  return 33;
}

struct ConditionalDistribution {
  int valid = 0;
  int signal = 0;
  std::array<float, 256> probabilities = {};
};

ConditionalDistribution ConditionalAfterFirstByte(PPMD::PPMD* model,
    unsigned int* byte_context, uint8_t first) {
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
    ConditionalDistribution result;
    *byte_context = first;
    model->ByteUpdate();
    const std::valarray<float>& probabilities = model->BytePredict();
    for (size_t index = 0; index < result.probabilities.size(); ++index) {
      result.probabilities[index] = probabilities[index];
    }
    result.valid = 1;
    const bool ok = WriteAll(pipefd[1], &result, sizeof(result));
    close(pipefd[1]);
    // Do not run PPMD::~PPMD() in the child: the parent still owns ppm.temp.
    _exit(ok ? 0 : 3);
  }

  close(pipefd[1]);
  ConditionalDistribution result;
  const bool read_ok = ReadAll(pipefd[0], &result, sizeof(result));
  close(pipefd[0]);
  int status = 0;
  if (waitpid(pid, &status, 0) != pid) {
    std::perror("waitpid");
    std::exit(2);
  }
  if (read_ok && WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
      result.valid) {
    return result;
  }
  result.valid = 0;
  if (WIFSIGNALED(status)) result.signal = WTERMSIG(status);
  return result;
}

struct Candidate {
  uint16_t value = 0;
  double loss = 0.0;
};

void EvaluatePair(PPMD::PPMD* model, unsigned int* byte_context,
    const std::vector<bool>& vocab, const std::vector<uint8_t>& data,
    size_t position) {
  const auto started = std::chrono::steady_clock::now();
  const uint8_t actual_first = data[position];
  const uint8_t actual_second = data[position + 1];
  const uint16_t actual = static_cast<uint16_t>(actual_first) << 8 |
      static_cast<uint16_t>(actual_second);

  std::array<double, 256> first_probability = {};
  const std::valarray<float>& first_distribution = model->BytePredict();
  for (size_t value = 0; value < 256; ++value) {
    first_probability[value] = first_distribution[value];
  }

  std::vector<Candidate> candidates;
  const size_t vocab_size = static_cast<size_t>(
      std::count(vocab.begin(), vocab.end(), true));
  candidates.reserve(vocab_size * vocab_size);
  uint64_t forks = 0;
  uint64_t failed_first_bytes = 0;

  for (unsigned int first = 0; first < 256; ++first) {
    if (!vocab[first]) continue;
    ++forks;
    const ConditionalDistribution conditional = ConditionalAfterFirstByte(
        model, byte_context, static_cast<uint8_t>(first));
    if (!conditional.valid) {
      ++failed_first_bytes;
      std::fprintf(stderr, "conditional PPMD child failed first=%u signal=%d\n",
          first, conditional.signal);
      continue;
    }
    const double p1 = std::max(1e-30, first_probability[first]);
    for (unsigned int second = 0; second < 256; ++second) {
      if (!vocab[second]) continue;
      const double p2 = std::max(
          1e-30, static_cast<double>(conditional.probabilities[second]));
      Candidate candidate;
      candidate.value = static_cast<uint16_t>((first << 8) | second);
      candidate.loss = -std::log2(p1) - std::log2(p2);
      candidates.push_back(candidate);
    }
  }

  if (failed_first_bytes) {
    std::cerr << "pair oracle incomplete because conditional branches failed\n";
    std::exit(3);
  }

  std::stable_sort(candidates.begin(), candidates.end(),
      [](const Candidate& lhs, const Candidate& rhs) {
        if (lhs.loss != rhs.loss) return lhs.loss < rhs.loss;
        return lhs.value < rhs.value;
      });

  const auto found = std::find_if(candidates.begin(), candidates.end(),
      [actual](const Candidate& candidate) { return candidate.value == actual; });
  if (found == candidates.end()) {
    std::cerr << "actual pair absent from candidate space\n";
    std::exit(4);
  }
  const size_t actual_index = static_cast<size_t>(found - candidates.begin());
  const uint64_t rank = actual_index + 1;
  const double rank_bits = std::log2(static_cast<double>(rank));

  std::vector<uint16_t> order;
  order.reserve(candidates.size());
  for (const Candidate& candidate : candidates) order.push_back(candidate.value);
  const unsigned int hash_bits = MinimumFirstMatchFingerprintBits(
      order, actual_index, actual);
  const unsigned int rank_gamma_bits = EliasGammaBits(rank);
  const unsigned int hash_gamma_bits =
      hash_bits + EliasGammaBits(static_cast<uint64_t>(hash_bits) + 1);

  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started).count();

  std::cout << position << ','
            << static_cast<unsigned int>(actual_first) << ','
            << static_cast<unsigned int>(actual_second) << ','
            << candidates.size() << ','
            << std::fixed << std::setprecision(9) << found->loss << ','
            << rank << ',' << rank_bits << ',' << rank_bits / 2.0 << ','
            << rank_gamma_bits << ',' << hash_bits << ',' << hash_gamma_bits
            << ',' << forks << ',' << elapsed << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: ppmd-pair-rank <predictor-input>\n";
    return 2;
  }

  const std::vector<uint8_t> data = ReadFile(argv[1]);
  if (data.size() < 28674) {
    std::cerr << "predictor input too short\n";
    return 2;
  }

  std::vector<bool> vocab(256, false);
  if (data.size() < 10000) {
    std::fill(vocab.begin(), vocab.end(), true);
  } else {
    for (uint8_t value : data) vocab[value] = true;
  }

  unsigned int byte_context = 0;
  PPMD::PPMD model(25, 14000, byte_context, vocab);
  static constexpr std::array<size_t, 7> targets = {
      4096, 8192, 12288, 16384, 20480, 24576, 28672};

  std::cout << "position,actual0,actual1,candidate_space,ppmd_pair_nll_bits,rank,log2_rank,log2_rank_per_byte,rank_gamma_bits,min_first_match_hash_bits,hash_gamma_framed_bits,conditional_forks,elapsed_ms\n";

  size_t target_index = 0;
  for (size_t position = 0;
      position + 1 < data.size() && target_index < targets.size(); ++position) {
    if (position == targets[target_index]) {
      EvaluatePair(&model, &byte_context, vocab, data, position);
      ++target_index;
    }
    byte_context = data[position];
    model.ByteUpdate();
  }
  return 0;
}
