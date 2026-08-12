#include "../../src/predictor.h"
#include "../../src/preprocess/preprocessor.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
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

uint64_t Mix64(uint64_t value) {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}

uint64_t Fingerprint16(uint16_t value) {
  return Mix64(static_cast<uint64_t>(value) ^ 0x4855545445523136ULL);
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

bool PrefixAllowed(const std::vector<bool>& vocab, int depth,
    unsigned int prefix) {
  if (depth == 0) return true;
  const int shift = 8 - depth;
  for (int value = 0; value < 256; ++value) {
    if (!vocab[value]) continue;
    if ((static_cast<unsigned int>(value) >> shift) == prefix) return true;
  }
  return false;
}

double ScoreActual16Forked(Predictor* predictor, uint16_t value) {
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
    double loss = 0.0;
    for (int shift = 15; shift >= 0; --shift) {
      const int bit = (value >> shift) & 1;
      const unsigned int probability = Discretize(predictor->Predict());
      loss += BitLoss(bit, probability);
      if (shift != 0) predictor->Perceive(bit);
    }
    const bool ok = WriteAll(pipefd[1], &loss, sizeof(loss));
    close(pipefd[1]);
    _exit(ok ? 0 : 3);
  }

  close(pipefd[1]);
  double loss = 0.0;
  const bool read_ok = ReadAll(pipefd[0], &loss, sizeof(loss));
  close(pipefd[0]);
  int status = 0;
  waitpid(pid, &status, 0);
  if (!read_ok || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    std::fprintf(stderr, "actual 16-bit scorer failed status=%d\n", status);
    std::exit(3);
  }
  return loss;
}

struct SearchResult {
  uint64_t earlier = 0;
  uint64_t nodes = 0;
  uint64_t collision_mask = 0;
  uint8_t overflow = 0;
};

void Merge(SearchResult* dst, const SearchResult& src) {
  dst->earlier += src.earlier;
  dst->nodes += src.nodes;
  dst->collision_mask |= src.collision_mask;
  dst->overflow = dst->overflow || src.overflow;
}

uint64_t CollisionMask(uint16_t candidate, uint64_t actual_hash) {
  const uint64_t candidate_hash = Fingerprint16(candidate);
  uint64_t collisions = 0;
  for (unsigned int bits = 0; bits <= 32; ++bits) {
    const uint64_t mask = bits == 0 ? 0 : ((1ULL << bits) - 1);
    if ((candidate_hash & mask) == (actual_hash & mask)) {
      collisions |= (1ULL << bits);
    }
  }
  return collisions;
}

SearchResult ExploreEarlier(
    Predictor* predictor,
    const std::vector<bool>& vocab,
    int depth,
    int byte_depth,
    unsigned int byte_prefix,
    uint16_t value_prefix,
    double loss,
    double target_loss,
    uint16_t actual,
    uint64_t actual_hash,
    uint64_t node_budget);

SearchResult ExploreChildForked(
    Predictor* predictor,
    const std::vector<bool>& vocab,
    int bit,
    int depth,
    int next_byte_depth,
    unsigned int next_byte_prefix,
    uint16_t next_value,
    double next_loss,
    double target_loss,
    uint16_t actual,
    uint64_t actual_hash,
    uint64_t node_budget) {
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
    predictor->Perceive(bit);
    SearchResult result = ExploreEarlier(
        predictor, vocab, depth + 1,
        next_byte_depth == 8 ? 0 : next_byte_depth,
        next_byte_depth == 8 ? 0 : next_byte_prefix,
        next_value, next_loss, target_loss, actual, actual_hash, node_budget);
    const bool ok = WriteAll(pipefd[1], &result, sizeof(result));
    close(pipefd[1]);
    _exit(ok ? 0 : 3);
  }

  close(pipefd[1]);
  SearchResult result;
  const bool read_ok = ReadAll(pipefd[0], &result, sizeof(result));
  close(pipefd[0]);
  int status = 0;
  waitpid(pid, &status, 0);
  if (!read_ok || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    if (WIFSIGNALED(status)) {
      std::fprintf(stderr, "16-bit subtree failed signal=%d depth=%d\n",
          WTERMSIG(status), depth);
    } else {
      std::fprintf(stderr, "16-bit subtree failed status=%d depth=%d\n",
          status, depth);
    }
    std::exit(3);
  }
  return result;
}

SearchResult ExploreEarlier(
    Predictor* predictor,
    const std::vector<bool>& vocab,
    int depth,
    int byte_depth,
    unsigned int byte_prefix,
    uint16_t value_prefix,
    double loss,
    double target_loss,
    uint16_t actual,
    uint64_t actual_hash,
    uint64_t node_budget) {
  SearchResult result;
  result.nodes = 1;
  if (node_budget <= 1) {
    result.overflow = 1;
    return result;
  }

  const unsigned int probability = Discretize(predictor->Predict());

  struct Child {
    int bit;
    int byte_depth;
    unsigned int byte_prefix;
    uint16_t value;
    double loss;
  };
  Child children[2];
  int child_count = 0;

  for (int bit = 0; bit <= 1; ++bit) {
    const int next_byte_depth = byte_depth + 1;
    const unsigned int next_byte_prefix = (byte_prefix << 1) | bit;
    if (!PrefixAllowed(vocab, next_byte_depth, next_byte_prefix)) continue;

    const double next_loss = loss + BitLoss(bit, probability);
    // Every later bit has strictly positive quantized loss, so a nonterminal
    // prefix that already reaches target_loss cannot produce an earlier leaf.
    if (depth < 15 && next_loss >= target_loss) continue;
    if (depth == 15 && next_loss > target_loss + 1e-12) continue;

    children[child_count++] = Child{
        bit,
        next_byte_depth,
        next_byte_prefix,
        static_cast<uint16_t>((value_prefix << 1) | bit),
        next_loss};
  }

  if (depth == 15) {
    for (int i = 0; i < child_count; ++i) {
      ++result.nodes;
      const Child& child = children[i];
      const bool lower_loss = child.loss < target_loss - 1e-12;
      const bool tied_before =
          std::fabs(child.loss - target_loss) <= 1e-12 &&
          child.value < actual;
      if (lower_loss || tied_before) {
        ++result.earlier;
        result.collision_mask |= CollisionMask(child.value, actual_hash);
      }
      if (result.nodes >= node_budget && i + 1 < child_count) {
        result.overflow = 1;
        return result;
      }
    }
    return result;
  }

  if (child_count == 0) return result;

  // Explore the first viable branch in a child process. The current process
  // stays at the exact post-Predict state and can then consume the second bit,
  // giving us a rollback-free depth-first traversal with at most one active
  // sibling per level.
  if (child_count == 2) {
    const uint64_t child_budget = node_budget - result.nodes;
    SearchResult first = ExploreChildForked(
        predictor, vocab, children[0].bit, depth,
        children[0].byte_depth, children[0].byte_prefix,
        children[0].value, children[0].loss, target_loss,
        actual, actual_hash, child_budget);
    Merge(&result, first);
    if (result.overflow || result.nodes >= node_budget) {
      result.overflow = 1;
      return result;
    }

    const Child& second = children[1];
    predictor->Perceive(second.bit);
    SearchResult second_result = ExploreEarlier(
        predictor, vocab, depth + 1,
        second.byte_depth == 8 ? 0 : second.byte_depth,
        second.byte_depth == 8 ? 0 : second.byte_prefix,
        second.value, second.loss, target_loss, actual, actual_hash,
        node_budget - result.nodes);
    Merge(&result, second_result);
    return result;
  }

  const Child& only = children[0];
  predictor->Perceive(only.bit);
  SearchResult child_result = ExploreEarlier(
      predictor, vocab, depth + 1,
      only.byte_depth == 8 ? 0 : only.byte_depth,
      only.byte_depth == 8 ? 0 : only.byte_prefix,
      only.value, only.loss, target_loss, actual, actual_hash,
      node_budget - result.nodes);
  Merge(&result, child_result);
  return result;
}

unsigned int MinimumFingerprintBits(uint64_t collision_mask) {
  for (unsigned int bits = 0; bits <= 32; ++bits) {
    if ((collision_mask & (1ULL << bits)) == 0) return bits;
  }
  return 33;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    std::fprintf(stderr,
        "usage: hash-rank-oracle16 <predictor-input> [dictionary]\n");
    return 2;
  }

  std::srand(SEED);
  const std::vector<uint8_t> data = ReadFile(argv[1]);
  if (data.size() < 2) return 2;

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

  const size_t position = std::min(
      EnvSize("FX2_ORACLE16_POSITION", 32768), data.size() - 2);
  const uint64_t node_budget =
      EnvSize("FX2_ORACLE16_NODE_BUDGET", 50000);

  for (size_t i = 0; i < position; ++i) {
    ScoreByte(&predictor, data[i]);
  }

  const uint16_t actual =
      (static_cast<uint16_t>(data[position]) << 8) |
      static_cast<uint16_t>(data[position + 1]);
  const double actual_loss = ScoreActual16Forked(&predictor, actual);
  const uint64_t actual_hash = Fingerprint16(actual);

  std::cerr << "position=" << position
            << " actual0=" << static_cast<unsigned int>(data[position])
            << " actual1=" << static_cast<unsigned int>(data[position + 1])
            << " actual_surprisal_bits=" << std::setprecision(9)
            << actual_loss
            << " node_budget=" << node_budget << "\n";

  SearchResult search = ExploreEarlier(
      &predictor, vocab, 0, 0, 0, 0, 0.0, actual_loss,
      actual, actual_hash, node_budget);

  const uint64_t rank = search.earlier + 1;
  const double rank_bits = std::log2(static_cast<double>(rank));
  const unsigned int hash_bits =
      MinimumFingerprintBits(search.collision_mask);

  std::cout << "position,actual0,actual1,surprisal_bits,rank,log2_rank,min_first_match_hash_bits,rank_oracle_gain_bits,hash_payload_gain_bits,search_nodes,overflow\n";
  std::cout << position << ','
            << static_cast<unsigned int>(data[position]) << ','
            << static_cast<unsigned int>(data[position + 1]) << ','
            << std::fixed << std::setprecision(6) << actual_loss << ','
            << rank << ',' << rank_bits << ',' << hash_bits << ','
            << (actual_loss - rank_bits) << ','
            << (actual_loss - hash_bits) << ','
            << search.nodes << ',' << static_cast<unsigned int>(search.overflow)
            << '\n';

  if (search.overflow) {
    std::cerr << "node budget exhausted; rank/hash columns are lower-bound/partial\n";
    return 5;
  }
  return 0;
}
