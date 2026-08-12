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
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

unsigned int Discretize(float p) { return 1 + 65534 * p; }

double BitLoss(int bit, unsigned int probability) {
  const double p = static_cast<double>(probability) / 65536.0;
  return -std::log2(bit ? p : 1.0 - p);
}

double AdvanceByte(Predictor* predictor, uint8_t value) {
  double loss = 0.0;
  for (int shift = 7; shift >= 0; --shift) {
    const int bit = (value >> shift) & 1;
    const unsigned int p = Discretize(predictor->Predict());
    loss += BitLoss(bit, p);
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
  bool valid = true;
  int crash_signal = 0;
};

// Always replay from an untouched baseline Predictor. No DFS state is reused.
// Some arbitrary counterfactual continuations violate assumptions inside the
// production Predictor and crash even though the actual input path is valid.
// Those paths are undefined for this oracle; report them to the caller so the
// entire experiment can prune the corresponding subtree instead of aborting.
PrefixEval EvaluatePrefix(Predictor* baseline, uint16_t prefix, int depth) {
  int fds[2];
  if (pipe(fds) != 0) { std::perror("pipe"); std::exit(2); }
  const pid_t pid = fork();
  if (pid < 0) { std::perror("fork"); std::exit(2); }
  if (pid == 0) {
    close(fds[0]);
    PrefixEval out;
    for (int shift = depth - 1; shift >= 0; --shift) {
      const int bit = (prefix >> shift) & 1;
      const unsigned int p = Discretize(baseline->Predict());
      out.loss += BitLoss(bit, p);
      baseline->Perceive(bit);
    }
    if (depth < 16) out.next_probability = Discretize(baseline->Predict());
    const bool ok = WriteAll(fds[1], &out, sizeof(out));
    close(fds[1]);
    _exit(ok ? 0 : 3);
  }

  close(fds[1]);
  PrefixEval out;
  const bool ok = ReadAll(fds[0], &out, sizeof(out));
  close(fds[0]);
  int status = 0;
  if (waitpid(pid, &status, 0) != pid) {
    std::perror("waitpid");
    std::exit(2);
  }
  if (ok && WIFEXITED(status) && WEXITSTATUS(status) == 0) return out;
  if (WIFSIGNALED(status)) {
    out.valid = false;
    out.crash_signal = WTERMSIG(status);
    return out;
  }

  std::fprintf(stderr, "prefix failed depth=%d status=%d prefix=%u\n",
      depth, status, static_cast<unsigned int>(prefix));
  std::exit(3);
}

bool AsciiBytePrefixAllowed(int depth, unsigned int prefix) {
  if (depth == 0) return true;
  const int shift = 8 - depth;
  for (unsigned int value = 32; value <= 126; ++value) {
    if ((value >> shift) == prefix) return true;
  }
  return false;
}

bool Ascii16PrefixAllowed(uint16_t prefix, int depth) {
  if (depth <= 8) return AsciiBytePrefixAllowed(depth, prefix);
  const int second_depth = depth - 8;
  const unsigned int first = prefix >> second_depth;
  if (first < 32 || first > 126) return false;
  const unsigned int mask = (1u << second_depth) - 1;
  return AsciiBytePrefixAllowed(second_depth, prefix & mask);
}

uint64_t PrintableLeavesUnderPrefix(uint16_t prefix, int depth) {
  uint64_t count = 0;
  const int shift = 16 - depth;
  for (unsigned int first = 32; first <= 126; ++first) {
    for (unsigned int second = 32; second <= 126; ++second) {
      const uint32_t value = (first << 8) | second;
      if (depth == 0 || (value >> shift) == prefix) ++count;
    }
  }
  return count;
}

uint64_t Mix64(uint64_t x) {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}

uint64_t Fingerprint(uint16_t value) {
  return Mix64(static_cast<uint64_t>(value) ^ 0x4855545445523136ULL);
}

uint64_t CollisionMask(uint16_t candidate, uint64_t target_hash) {
  const uint64_t h = Fingerprint(candidate);
  uint64_t collision = 0;
  for (unsigned int bits = 0; bits <= 32; ++bits) {
    const uint64_t mask = bits == 0 ? 0 : ((1ULL << bits) - 1);
    if ((h & mask) == (target_hash & mask)) collision |= 1ULL << bits;
  }
  return collision;
}

unsigned int MinHashBits(uint64_t collision) {
  for (unsigned int bits = 0; bits <= 32; ++bits) {
    if ((collision & (1ULL << bits)) == 0) return bits;
  }
  return 33;
}

struct Result {
  uint64_t earlier = 0;
  uint64_t nodes = 0;
  uint64_t collision = 0;
  uint64_t crash_prefixes = 0;
  uint64_t crash_candidates_pruned = 0;
  int min_crash_depth = 17;
  bool actual_seen = false;
  bool overflow = false;
};

void Explore(Predictor* baseline, uint16_t prefix, int depth,
    double target_loss, uint16_t actual, uint64_t target_hash,
    uint64_t budget, Result* result) {
  if (result->overflow) return;
  if (++result->nodes > budget) { result->overflow = true; return; }
  if (!Ascii16PrefixAllowed(prefix, depth)) return;

  const PrefixEval eval = EvaluatePrefix(baseline, prefix, depth);
  if (!eval.valid) {
    ++result->crash_prefixes;
    result->crash_candidates_pruned += PrintableLeavesUnderPrefix(prefix, depth);
    result->min_crash_depth = std::min(result->min_crash_depth, depth);
    if (result->crash_prefixes <= 8) {
      std::fprintf(stderr,
          "pruning crashing prefix depth=%d signal=%d prefix=%u leaves=%llu\n",
          depth, eval.crash_signal, static_cast<unsigned int>(prefix),
          static_cast<unsigned long long>(PrintableLeavesUnderPrefix(prefix, depth)));
    }
    return;
  }

  if (depth == 16) {
    if (prefix == actual) { result->actual_seen = true; return; }
    const bool earlier = eval.loss < target_loss - 1e-12 ||
        (std::fabs(eval.loss - target_loss) <= 1e-12 && prefix < actual);
    if (earlier) {
      ++result->earlier;
      result->collision |= CollisionMask(prefix, target_hash);
    }
    return;
  }

  if (eval.loss >= target_loss) return;
  for (int bit = 0; bit <= 1; ++bit) {
    const uint16_t child = static_cast<uint16_t>((prefix << 1) | bit);
    if (!Ascii16PrefixAllowed(child, depth + 1)) continue;
    const double lower = eval.loss + BitLoss(bit, eval.next_probability);
    if (depth + 1 < 16 && lower >= target_loss) continue;
    if (depth + 1 == 16 && lower > target_loss + 1e-12) continue;
    Explore(baseline, child, depth + 1, target_loss, actual, target_hash,
        budget, result);
    if (result->overflow) return;
  }
}

std::vector<uint8_t> ReadFile(const char* path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) { std::cerr << "cannot open " << path << '\n'; std::exit(2); }
  in.seekg(0, std::ios::end);
  const size_t size = static_cast<size_t>(in.tellg());
  in.seekg(0, std::ios::beg);
  std::vector<uint8_t> data(size);
  if (size) in.read(reinterpret_cast<char*>(data.data()), size);
  return data;
}

size_t EnvSize(const char* name, size_t fallback) {
  const char* s = std::getenv(name);
  return s && *s ? static_cast<size_t>(std::strtoull(s, nullptr, 10)) : fallback;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    std::cerr << "usage: hash-rank-oracle16-ascii <predictor-input> [dictionary]\n";
    return 2;
  }
  std::srand(SEED);
  const std::vector<uint8_t> data = ReadFile(argv[1]);
  std::vector<bool> global_vocab(256, false);
  for (uint8_t value : data) global_vocab[value] = true;
  if (data.size() < 10000) std::fill(global_vocab.begin(), global_vocab.end(), true);

  Predictor predictor(global_vocab);
  if (argc == 3) {
    FILE* dictionary = std::fopen(argv[2], "rb");
    if (!dictionary) { std::perror("dictionary"); return 2; }
    preprocessor::Pretrain(&predictor, dictionary);
    std::fclose(dictionary);
  }

  const size_t position = std::min(EnvSize("FX2_ORACLE16_POSITION", 260000), data.size() - 2);
  const uint64_t budget = EnvSize("FX2_ORACLE16_NODE_BUDGET", 20000);
  for (size_t i = 0; i < position; ++i) AdvanceByte(&predictor, data[i]);

  const uint8_t a = data[position];
  const uint8_t b = data[position + 1];
  if (a < 32 || a > 126 || b < 32 || b > 126) {
    std::cerr << "actual block is not printable ASCII: "
              << static_cast<unsigned int>(a) << ','
              << static_cast<unsigned int>(b) << '\n';
    return 4;
  }
  const uint16_t actual = (static_cast<uint16_t>(a) << 8) | b;
  const PrefixEval actual_eval = EvaluatePrefix(&predictor, actual, 16);
  if (!actual_eval.valid) {
    std::cerr << "actual block itself crashes Predictor\n";
    return 7;
  }
  const double target_loss = actual_eval.loss;

  Result result;
  Explore(&predictor, 0, 0, target_loss, actual, Fingerprint(actual), budget, &result);
  if (result.overflow) { std::cerr << "node budget exhausted\n"; return 5; }
  if (!result.actual_seen) { std::cerr << "actual leaf not reached\n"; return 6; }

  const uint64_t rank = result.earlier + 1;
  const double rank_bits = std::log2(static_cast<double>(rank));
  const unsigned int hash_bits = MinHashBits(result.collision);
  const uint64_t stable_candidates = 9025 - result.crash_candidates_pruned;
  const int min_crash_depth = result.crash_prefixes ? result.min_crash_depth : -1;

  std::cout << "position,actual0,actual1,candidate_space,stable_candidate_space,surprisal_bits,rank,log2_rank,min_first_match_hash_bits,rank_oracle_gain_bits,hash_payload_gain_bits,search_nodes,crash_prefixes,crash_candidates_pruned,min_crash_depth\n";
  std::cout << position << ',' << static_cast<unsigned int>(a) << ','
            << static_cast<unsigned int>(b) << ",9025," << stable_candidates << ','
            << std::fixed << std::setprecision(6) << target_loss << ','
            << rank << ',' << rank_bits << ',' << hash_bits << ','
            << (target_loss - rank_bits) << ',' << (target_loss - hash_bits)
            << ',' << result.nodes << ',' << result.crash_prefixes << ','
            << result.crash_candidates_pruned << ',' << min_crash_depth << '\n';
  std::cerr << "stable printable-ASCII oracle complete; earlier=" << result.earlier
            << " nodes=" << result.nodes
            << " crash_prefixes=" << result.crash_prefixes
            << " pruned_candidates=" << result.crash_candidates_pruned << '\n';
  return 0;
}
