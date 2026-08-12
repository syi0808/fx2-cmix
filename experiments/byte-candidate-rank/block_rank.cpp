#include "../../src/models/ppmd.h"

#include <algorithm>
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

constexpr double kTieEpsilon = 1e-12;

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

uint64_t Mix64(uint64_t value) {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}

uint64_t Fingerprint(uint32_t value) {
  return Mix64(static_cast<uint64_t>(value) ^ 0x50504d443332ULL);
}

uint64_t CollisionMask(uint32_t candidate, uint64_t target_hash) {
  const uint64_t hash = Fingerprint(candidate);
  uint64_t collision = 0;
  for (unsigned int bits = 0; bits <= 32; ++bits) {
    const uint64_t mask = bits == 0 ? 0 : ((1ULL << bits) - 1);
    if ((hash & mask) == (target_hash & mask)) collision |= 1ULL << bits;
  }
  return collision;
}

unsigned int MinimumHashBits(uint64_t collision) {
  for (unsigned int bits = 0; bits <= 32; ++bits) {
    if ((collision & (1ULL << bits)) == 0) return bits;
  }
  return 33;
}

unsigned int FloorLog2(uint64_t value) {
  unsigned int result = 0;
  while (value >>= 1) ++result;
  return result;
}

unsigned int EliasGammaBits(uint64_t positive_value) {
  return 2 * FloorLog2(positive_value) + 1;
}

struct SearchResult {
  uint64_t earlier = 0;
  uint64_t nodes = 0;
  uint64_t forks = 0;
  uint64_t collision = 0;
  int overflow = 0;
  int signal = 0;
};

SearchResult Explore(PPMD::PPMD* model, unsigned int* byte_context,
    const std::vector<bool>& vocab, unsigned int depth,
    unsigned int block_length, uint32_t prefix, double prefix_loss,
    double target_loss, uint32_t actual, uint64_t target_hash,
    uint64_t budget) {
  SearchResult result;
  const std::valarray<float>& distribution = model->BytePredict();

  for (unsigned int value = 0; value < 256; ++value) {
    if (!vocab[value]) continue;
    const double probability = std::max(
        1e-30, static_cast<double>(distribution[value]));
    const double child_loss = prefix_loss - std::log2(probability);
    const uint32_t child_prefix = (prefix << 8) | value;

    if (depth + 1 == block_length) {
      ++result.nodes;
      if (result.nodes > budget) {
        result.overflow = 1;
        return result;
      }
      const bool earlier = child_loss < target_loss - kTieEpsilon ||
          (std::fabs(child_loss - target_loss) <= kTieEpsilon &&
           child_prefix < actual);
      if (earlier) {
        ++result.earlier;
        result.collision |= CollisionMask(child_prefix, target_hash);
      }
      continue;
    }

    // Every future -log2(probability) is non-negative, so this prefix can
    // never outrank the target once it is already strictly worse.
    if (child_loss > target_loss + kTieEpsilon) continue;
    if (result.nodes >= budget) {
      result.overflow = 1;
      return result;
    }

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
      *byte_context = value;
      model->ByteUpdate();
      SearchResult child = Explore(model, byte_context, vocab,
          depth + 1, block_length, child_prefix, child_loss, target_loss,
          actual, target_hash, budget - result.nodes);
      const bool ok = WriteAll(pipefd[1], &child, sizeof(child));
      close(pipefd[1]);
      _exit(ok ? 0 : 3);
    }

    close(pipefd[1]);
    SearchResult child;
    const bool read_ok = ReadAll(pipefd[0], &child, sizeof(child));
    close(pipefd[0]);
    int status = 0;
    if (waitpid(pid, &status, 0) != pid) {
      std::perror("waitpid");
      std::exit(2);
    }
    ++result.forks;
    if (!read_ok || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      result.signal = WIFSIGNALED(status) ? WTERMSIG(status) : -1;
      return result;
    }

    result.earlier += child.earlier;
    result.nodes += child.nodes;
    result.forks += child.forks;
    result.collision |= child.collision;
    if (child.overflow || result.nodes > budget) {
      result.overflow = 1;
      return result;
    }
  }
  return result;
}

struct ActualLossResult {
  int valid = 0;
  int signal = 0;
  double loss = 0.0;
};

ActualLossResult ActualBlockLoss(PPMD::PPMD* model,
    unsigned int* byte_context, const std::vector<uint8_t>& data,
    size_t position, unsigned int block_length) {
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
    ActualLossResult result;
    for (unsigned int index = 0; index < block_length; ++index) {
      const uint8_t value = data[position + index];
      const std::valarray<float>& distribution = model->BytePredict();
      const double probability = std::max(
          1e-30, static_cast<double>(distribution[value]));
      result.loss -= std::log2(probability);
      if (index + 1 < block_length) {
        *byte_context = value;
        model->ByteUpdate();
      }
    }
    result.valid = 1;
    const bool ok = WriteAll(pipefd[1], &result, sizeof(result));
    close(pipefd[1]);
    _exit(ok ? 0 : 3);
  }

  close(pipefd[1]);
  ActualLossResult result;
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
  result.signal = WIFSIGNALED(status) ? WTERMSIG(status) : -1;
  return result;
}

uint32_t ReadBlockValue(const std::vector<uint8_t>& data, size_t position,
    unsigned int block_length) {
  uint32_t value = 0;
  for (unsigned int index = 0; index < block_length; ++index) {
    value = (value << 8) | data[position + index];
  }
  return value;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: ppmd-block-rank <predictor-input>\n";
    return 2;
  }

  const std::vector<uint8_t> data = ReadFile(argv[1]);
  const unsigned int block_length = static_cast<unsigned int>(
      EnvSize("FX2_BLOCK_RANK_LENGTH", 4));
  if (block_length == 0 || block_length > 4 || data.size() <= block_length) {
    std::cerr << "unsupported block length\n";
    return 2;
  }

  std::vector<bool> vocab(256, false);
  if (data.size() < 10000) {
    std::fill(vocab.begin(), vocab.end(), true);
  } else {
    for (uint8_t value : data) vocab[value] = true;
  }
  const size_t vocab_size = static_cast<size_t>(
      std::count(vocab.begin(), vocab.end(), true));

  const size_t warmup = EnvSize("FX2_BLOCK_RANK_WARMUP", 1024);
  const size_t stride = std::max<size_t>(
      1, EnvSize("FX2_BLOCK_RANK_STRIDE", 457));
  const size_t requested = EnvSize("FX2_BLOCK_RANK_SAMPLES", 64);
  const uint64_t budget = EnvSize("FX2_BLOCK_RANK_NODE_BUDGET", 250000);
  std::vector<size_t> targets;
  for (size_t sample = 0; sample < requested; ++sample) {
    const size_t position = warmup + sample * stride;
    if (position + block_length > data.size()) break;
    targets.push_back(position);
  }
  if (targets.empty()) return 2;

  unsigned int byte_context = 0;
  PPMD::PPMD model(25, 14000, byte_context, vocab);

  std::vector<uint64_t> ranks;
  ranks.reserve(targets.size());
  uint64_t top1 = 0, top2 = 0, top4 = 0, top8 = 0, top16 = 0;
  double total_nll = 0.0;
  double total_log2_rank = 0.0;
  uint64_t total_hash_bits = 0;
  uint64_t total_gamma_bits = 0;
  uint64_t total_nodes = 0;
  uint64_t total_forks = 0;
  uint64_t total_elapsed_ms = 0;

  std::cout << "position,actual,ppmd_block_nll_bits,rank,log2_rank,log2_rank_per_byte,rank_gamma_bits,min_first_match_hash_bits,search_nodes,search_forks,elapsed_ms\n";

  size_t target_index = 0;
  for (size_t position = 0;
      position + block_length <= data.size() && target_index < targets.size();
      ++position) {
    if (position == targets[target_index]) {
      const auto started = std::chrono::steady_clock::now();
      const ActualLossResult actual_loss = ActualBlockLoss(
          &model, &byte_context, data, position, block_length);
      if (!actual_loss.valid) {
        std::cerr << "actual block replay failed signal="
                  << actual_loss.signal << " position=" << position << '\n';
        return 3;
      }
      const uint32_t actual = ReadBlockValue(data, position, block_length);
      SearchResult search = Explore(&model, &byte_context, vocab, 0,
          block_length, 0, 0.0, actual_loss.loss, actual,
          Fingerprint(actual), budget);
      if (search.signal) {
        std::cerr << "search child failed signal=" << search.signal
                  << " position=" << position << '\n';
        return 4;
      }
      if (search.overflow) {
        std::cerr << "node budget exhausted position=" << position
                  << " nodes=" << search.nodes << '\n';
        return 5;
      }

      const uint64_t rank = search.earlier + 1;
      const double log_rank = std::log2(static_cast<double>(rank));
      const unsigned int hash_bits = MinimumHashBits(search.collision);
      const long long elapsed_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - started).count();

      ranks.push_back(rank);
      top1 += rank <= 1;
      top2 += rank <= 2;
      top4 += rank <= 4;
      top8 += rank <= 8;
      top16 += rank <= 16;
      total_nll += actual_loss.loss;
      total_log2_rank += log_rank;
      total_hash_bits += hash_bits;
      total_gamma_bits += EliasGammaBits(rank);
      total_nodes += search.nodes;
      total_forks += search.forks;
      total_elapsed_ms += elapsed_ms;

      std::cout << position << ',' << actual << ','
                << std::fixed << std::setprecision(9) << actual_loss.loss << ','
                << rank << ',' << log_rank << ',' << log_rank / block_length
                << ',' << EliasGammaBits(rank) << ',' << hash_bits << ','
                << search.nodes << ',' << search.forks << ',' << elapsed_ms
                << '\n';
      ++target_index;
    }

    byte_context = data[position];
    model.ByteUpdate();
  }

  const uint64_t max_rank = *std::max_element(ranks.begin(), ranks.end());
  std::vector<uint64_t> histogram(max_rank + 1, 0);
  for (uint64_t rank : ranks) ++histogram[rank];
  const double count = static_cast<double>(ranks.size());
  double rank_entropy = 0.0;
  for (size_t rank = 1; rank < histogram.size(); ++rank) {
    if (!histogram[rank]) continue;
    const double p = static_cast<double>(histogram[rank]) / count;
    rank_entropy -= p * std::log2(p);
  }

  std::cout << "SUMMARY"
            << ",samples=" << ranks.size()
            << ",block_length=" << block_length
            << ",vocab_size=" << vocab_size
            << ",mean_ppmd_block_nll_bits=" << total_nll / count
            << ",mean_ppmd_nll_per_byte=" << total_nll / count / block_length
            << ",mean_log2_rank_bits=" << total_log2_rank / count
            << ",mean_log2_rank_per_byte=" << total_log2_rank / count / block_length
            << ",rank_zero_order_entropy_bits=" << rank_entropy
            << ",rank_zero_order_entropy_per_byte=" << rank_entropy / block_length
            << ",mean_gamma_rank_bits=" << static_cast<double>(total_gamma_bits) / count
            << ",mean_hash_prefix_bits=" << static_cast<double>(total_hash_bits) / count
            << ",top1_rate=" << static_cast<double>(top1) / count
            << ",top2_rate=" << static_cast<double>(top2) / count
            << ",top4_rate=" << static_cast<double>(top4) / count
            << ",top8_rate=" << static_cast<double>(top8) / count
            << ",top16_rate=" << static_cast<double>(top16) / count
            << ",mean_search_nodes=" << static_cast<double>(total_nodes) / count
            << ",mean_search_forks=" << static_cast<double>(total_forks) / count
            << ",mean_elapsed_ms=" << static_cast<double>(total_elapsed_ms) / count
            << '\n';
  return 0;
}
