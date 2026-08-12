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

bool WriteAll(int fd, const void* data, size_t size) {
  const char* p = static_cast<const char*>(data);
  while (size) {
    const ssize_t n = write(fd, p, size);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    p += n;
    size -= static_cast<size_t>(n);
  }
  return true;
}

bool ReadAll(int fd, void* data, size_t size) {
  char* p = static_cast<char*>(data);
  while (size) {
    const ssize_t n = read(fd, p, size);
    if (n == 0) return false;
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    p += n;
    size -= static_cast<size_t>(n);
  }
  return true;
}

struct PrefixEval {
  double loss = 0.0;
  unsigned int next_probability = 0;
  uint8_t has_next = 0;
};

// The parent Predictor is never mutated. Every prefix is replayed from the
// exact same baseline state in a fresh child process. This is deliberately
// expensive, but eliminates rollback / sibling-contamination ambiguity.
PrefixEval EvaluatePrefixForked(
    Predictor* baseline, uint16_t prefix, int depth) {
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
    PrefixEval result;
    for (int i = depth - 1; i >= 0; --i) {
      const int bit = (prefix >> i) & 1;
      const unsigned int probability = Discretize(baseline->Predict());
      result.loss += BitLoss(bit, probability);
      baseline->Perceive(bit);
    }
    if (depth < 16) {
      result.next_probability = Discretize(baseline->Predict());
      result.has_next = 1;
    }
    const bool ok = WriteAll(pipefd[1], &result, sizeof(result));
    close(pipefd[1]);
    _exit(ok ? 0 : 3);
  }

  close(pipefd[1]);
  PrefixEval result;
  const bool read_ok = ReadAll(pipefd[0], &result, sizeof(result));
  close(pipefd[0]);
  int status = 0;
  waitpid(pid, &status, 0);
  if (!read_ok || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    if (WIFSIGNALED(status)) {
      std::fprintf(stderr, "prefix child failed depth=%d signal=%d\n",
          depth, WTERMSIG(status));
    } else {
      std::fprintf(stderr, "prefix child failed depth=%d status=%d\n",
          depth, status);
    }
    std::exit(3);
  }
  return result;
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

uint64_t CollisionMask(uint16_t candidate, uint64_t actual_hash) {
  const uint64_t candidate_hash = Fingerprint16(candidate);
  uint64_t mask_set = 0;
  for (unsigned int bits = 0; bits <= 32; ++bits) {
    const uint64_t mask = bits == 0 ? 0 : ((1ULL << bits) - 1);
    if ((candidate_hash & mask) == (actual_hash & mask)) {
      mask_set |= (1ULL << bits);
    }
  }
  return mask_set;
}

unsigned int MinimumFingerprintBits(uint64_t collision_mask) {
  for (unsigned int bits = 0; bits <= 32; ++bits) {
    if ((collision_mask & (1ULL << bits)) == 0) return bits;
  }
  return 33;
}

bool BytePrefixAllowed(
    const std::vector<bool>& vocab, int depth, unsigned int prefix) {
  if (depth == 0) return true;
  const int shift = 8 - depth;
  for (int value = 0; value < 256; ++value) {
    if (!vocab[value]) continue;
    if ((static_cast<unsigned int>(value) >> shift) == prefix) return true;
  }
  return false;
}

bool PrefixAllowed16(
    const std::vector<bool>& vocab, uint16_t prefix, int depth) {
  if (depth <= 8) {
    return BytePrefixAllowed(vocab, depth, prefix);
  }
  const int second_depth = depth - 8;
  const unsigned int first_byte = prefix >> second_depth;
  if (first_byte >= 256 || !vocab[first_byte]) return false;
  const unsigned int second_mask = (1u << second_depth) - 1;
  const unsigned int second_prefix = prefix & second_mask;
  return BytePrefixAllowed(vocab, second_depth, second_prefix);
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
  if (!data.empty()) input.read(reinterpret_cast<char*>(data.data()), data.size());
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

struct SearchResult {
  uint64_t earlier = 0;
  uint64_t nodes = 0;
  uint64_t collision_mask = 0;
  uint8_t actual_seen = 0;
  uint8_t overflow = 0;
};

void Explore(
    Predictor* baseline,
    const std::vector<bool>& vocab,
    uint16_t prefix,
    int depth,
    double target_loss,
    uint16_t actual,
    uint64_t actual_hash,
    uint64_t node_budget,
    SearchResult* out) {
  if (out->overflow) return;
  if (++out->nodes > node_budget) {
    out->overflow = 1;
    return;
  }

  if (!PrefixAllowed16(vocab, prefix, depth)) return;
  const PrefixEval eval = EvaluatePrefixForked(baseline, prefix, depth);

  if (depth == 16) {
    if (prefix == actual) {
      out->actual_seen = 1;
      return;
    }
    const bool lower = eval.loss < target_loss - 1e-12;
    const bool tied_before =
        std::fabs(eval.loss - target_loss) <= 1e-12 && prefix < actual;
    if (lower || tied_before) {
      ++out->earlier;
      out->collision_mask |= CollisionMask(prefix, actual_hash);
    }
    return;
  }

  // All future quantized bit losses are strictly positive. Therefore once a
  // nonterminal prefix reaches the target loss, no descendant can outrank the
  // actual 16-bit block.
  if (eval.loss >= target_loss) return;

  for (int bit = 0; bit <= 1; ++bit) {
    const uint16_t child = static_cast<uint16_t>((prefix << 1) | bit);
    if (!PrefixAllowed16(vocab, child, depth + 1)) continue;
    const double child_lower_bound =
        eval.loss + BitLoss(bit, eval.next_probability);
    if (depth + 1 < 16 && child_lower_bound >= target_loss) continue;
    if (depth + 1 == 16 && child_lower_bound > target_loss + 1e-12) continue;
    Explore(baseline, vocab, child, depth + 1, target_loss,
        actual, actual_hash, node_budget, out);
    if (out->overflow) return;
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    std::fprintf(stderr,
        "usage: hash-rank-oracle16-replay <predictor-input> [dictionary]\n");
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
  const uint64_t node_budget = EnvSize("FX2_ORACLE16_NODE_BUDGET", 50000);

  for (size_t i = 0; i < position; ++i) AdvanceByte(&predictor, data[i]);

  const uint16_t actual =
      (static_cast<uint16_t>(data[position]) << 8) |
      static_cast<uint16_t>(data[position + 1]);
  const PrefixEval actual_eval = EvaluatePrefixForked(&predictor, actual, 16);
  const double actual_loss = actual_eval.loss;
  const uint64_t actual_hash = Fingerprint16(actual);

  SearchResult search;
  Explore(&predictor, vocab, 0, 0, actual_loss,
      actual, actual_hash, node_budget, &search);

  if (!search.actual_seen) {
    std::cerr << "actual leaf not reached; oracle invariant failed\n";
    return 6;
  }
  if (search.overflow) {
    std::cerr << "node budget exhausted\n";
    return 5;
  }

  const uint64_t rank = search.earlier + 1;
  const double rank_bits = std::log2(static_cast<double>(rank));
  const unsigned int hash_bits = MinimumFingerprintBits(search.collision_mask);

  std::cout << "position,actual0,actual1,surprisal_bits,rank,log2_rank,min_first_match_hash_bits,rank_oracle_gain_bits,hash_payload_gain_bits,search_nodes,overflow\n";
  std::cout << position << ','
            << static_cast<unsigned int>(data[position]) << ','
            << static_cast<unsigned int>(data[position + 1]) << ','
            << std::fixed << std::setprecision(6) << actual_loss << ','
            << rank << ',' << rank_bits << ',' << hash_bits << ','
            << (actual_loss - rank_bits) << ','
            << (actual_loss - hash_bits) << ','
            << search.nodes << ",0\n";

  std::cerr << "position=" << position
            << " actual=" << actual
            << " target_loss=" << std::setprecision(12) << actual_loss
            << " earlier=" << search.earlier
            << " nodes=" << search.nodes
            << " min_hash_bits=" << hash_bits << "\n";
  return 0;
}
