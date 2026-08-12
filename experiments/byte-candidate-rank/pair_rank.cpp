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

struct PairResult {
  size_t position = 0;
  uint8_t actual_first = 0;
  uint8_t actual_second = 0;
  size_t candidate_space = 0;
  double ppmd_nll = 0.0;
  uint64_t rank = 0;
  double log2_rank = 0.0;
  unsigned int gamma_bits = 0;
  unsigned int hash_bits = 0;
  unsigned int hash_gamma_bits = 0;
  uint64_t forks = 0;
  long long elapsed_ms = 0;
};

PairResult EvaluatePair(PPMD::PPMD* model, unsigned int* byte_context,
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

  const size_t vocab_size = static_cast<size_t>(
      std::count(vocab.begin(), vocab.end(), true));
  std::vector<Candidate> candidates;
  candidates.reserve(vocab_size * vocab_size);
  uint64_t forks = 0;

  for (unsigned int first = 0; first < 256; ++first) {
    if (!vocab[first]) continue;
    ++forks;
    const ConditionalDistribution conditional = ConditionalAfterFirstByte(
        model, byte_context, static_cast<uint8_t>(first));
    if (!conditional.valid) {
      std::fprintf(stderr,
          "conditional PPMD child failed first=%u signal=%d position=%zu\n",
          first, conditional.signal, position);
      std::exit(3);
    }
    const double p1 = std::max(1e-30, first_probability[first]);
    for (unsigned int second = 0; second < 256; ++second) {
      if (!vocab[second]) continue;
      const double p2 = std::max(
          1e-30, static_cast<double>(conditional.probabilities[second]));
      candidates.push_back({
          static_cast<uint16_t>((first << 8) | second),
          -std::log2(p1) - std::log2(p2)});
    }
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

  PairResult result;
  result.position = position;
  result.actual_first = actual_first;
  result.actual_second = actual_second;
  result.candidate_space = candidates.size();
  result.ppmd_nll = found->loss;
  result.rank = rank;
  result.log2_rank = rank_bits;
  result.gamma_bits = EliasGammaBits(rank);
  result.hash_bits = hash_bits;
  result.hash_gamma_bits =
      hash_bits + EliasGammaBits(static_cast<uint64_t>(hash_bits) + 1);
  result.forks = forks;
  result.elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started).count();
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: ppmd-pair-rank <predictor-input>\n";
    return 2;
  }

  const std::vector<uint8_t> data = ReadFile(argv[1]);
  if (data.size() < 4) return 2;

  std::vector<bool> vocab(256, false);
  if (data.size() < 10000) {
    std::fill(vocab.begin(), vocab.end(), true);
  } else {
    for (uint8_t value : data) vocab[value] = true;
  }
  const size_t vocab_size = static_cast<size_t>(
      std::count(vocab.begin(), vocab.end(), true));

  const size_t warmup = EnvSize("FX2_PAIR_RANK_WARMUP", 1024);
  const size_t stride = std::max<size_t>(1, EnvSize("FX2_PAIR_RANK_STRIDE", 113));
  const size_t requested = EnvSize("FX2_PAIR_RANK_SAMPLES", 256);
  std::vector<size_t> targets;
  targets.reserve(requested);
  for (size_t sample = 0; sample < requested; ++sample) {
    const size_t position = warmup + sample * stride;
    if (position + 1 >= data.size()) break;
    targets.push_back(position);
  }
  if (targets.empty()) return 2;

  unsigned int byte_context = 0;
  PPMD::PPMD model(25, 14000, byte_context, vocab);

  std::cout << "position,actual0,actual1,candidate_space,ppmd_pair_nll_bits,rank,log2_rank,log2_rank_per_byte,rank_gamma_bits,min_first_match_hash_bits,hash_gamma_framed_bits,conditional_forks,elapsed_ms\n";

  std::vector<uint64_t> rank_hist(vocab_size * vocab_size + 1, 0);
  uint64_t sample_count = 0;
  uint64_t top1 = 0, top2 = 0, top4 = 0, top8 = 0, top16 = 0;
  double total_nll = 0.0;
  double total_log2_rank = 0.0;
  uint64_t total_gamma = 0;
  uint64_t total_hash = 0;
  uint64_t total_hash_gamma = 0;
  uint64_t total_forks = 0;
  uint64_t total_elapsed_ms = 0;

  size_t target_index = 0;
  for (size_t position = 0;
      position + 1 < data.size() && target_index < targets.size(); ++position) {
    if (position == targets[target_index]) {
      const PairResult result = EvaluatePair(
          &model, &byte_context, vocab, data, position);
      ++sample_count;
      ++rank_hist[result.rank];
      top1 += result.rank <= 1;
      top2 += result.rank <= 2;
      top4 += result.rank <= 4;
      top8 += result.rank <= 8;
      top16 += result.rank <= 16;
      total_nll += result.ppmd_nll;
      total_log2_rank += result.log2_rank;
      total_gamma += result.gamma_bits;
      total_hash += result.hash_bits;
      total_hash_gamma += result.hash_gamma_bits;
      total_forks += result.forks;
      total_elapsed_ms += result.elapsed_ms;

      std::cout << result.position << ','
                << static_cast<unsigned int>(result.actual_first) << ','
                << static_cast<unsigned int>(result.actual_second) << ','
                << result.candidate_space << ','
                << std::fixed << std::setprecision(9) << result.ppmd_nll << ','
                << result.rank << ',' << result.log2_rank << ','
                << result.log2_rank / 2.0 << ',' << result.gamma_bits << ','
                << result.hash_bits << ',' << result.hash_gamma_bits << ','
                << result.forks << ',' << result.elapsed_ms << '\n';
      ++target_index;
    }
    byte_context = data[position];
    model.ByteUpdate();
  }

  double rank_entropy = 0.0;
  for (size_t rank = 1; rank < rank_hist.size(); ++rank) {
    if (!rank_hist[rank]) continue;
    const double probability = static_cast<double>(rank_hist[rank]) / sample_count;
    rank_entropy -= probability * std::log2(probability);
  }

  const double count = static_cast<double>(sample_count);
  std::cout << "SUMMARY"
            << ",samples=" << sample_count
            << ",vocab_size=" << vocab_size
            << ",candidate_space=" << vocab_size * vocab_size
            << ",mean_ppmd_pair_nll_bits=" << total_nll / count
            << ",mean_log2_rank_bits=" << total_log2_rank / count
            << ",mean_log2_rank_per_byte=" << total_log2_rank / count / 2.0
            << ",rank_zero_order_entropy_bits=" << rank_entropy
            << ",rank_zero_order_entropy_per_byte=" << rank_entropy / 2.0
            << ",mean_gamma_rank_bits=" << static_cast<double>(total_gamma) / count
            << ",mean_hash_prefix_bits=" << static_cast<double>(total_hash) / count
            << ",mean_hash_gamma_bits=" << static_cast<double>(total_hash_gamma) / count
            << ",top1_rate=" << static_cast<double>(top1) / count
            << ",top2_rate=" << static_cast<double>(top2) / count
            << ",top4_rate=" << static_cast<double>(top4) / count
            << ",top8_rate=" << static_cast<double>(top8) / count
            << ",top16_rate=" << static_cast<double>(top16) / count
            << ",mean_conditional_forks=" << static_cast<double>(total_forks) / count
            << ",mean_elapsed_ms=" << static_cast<double>(total_elapsed_ms) / count
            << '\n';
  return 0;
}
