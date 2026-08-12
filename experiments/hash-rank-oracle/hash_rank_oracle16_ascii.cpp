#include "../../src/predictor.h"
#include "../../src/preprocess/preprocessor.h"

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

constexpr uint64_t kPrintableCandidateSpace = 95ULL * 95ULL;

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

struct SequenceEval {
  double loss = 0.0;
  bool valid = true;
  int crash_signal = 0;
};

// Score one complete 16-bit continuation in a forked snapshot. Evaluating only
// complete candidates avoids the extra next-bit Predict() that made the older
// prefix-DFS oracle crash on otherwise irrelevant intermediate states.
SequenceEval EvaluateSequence(Predictor* baseline, uint16_t value) {
  int fds[2];
  if (pipe(fds) != 0) { std::perror("pipe"); std::exit(2); }
  const pid_t pid = fork();
  if (pid < 0) { std::perror("fork"); std::exit(2); }
  if (pid == 0) {
    close(fds[0]);
    SequenceEval out;
    for (int shift = 15; shift >= 0; --shift) {
      const int bit = (value >> shift) & 1;
      const unsigned int p = Discretize(baseline->Predict());
      out.loss += BitLoss(bit, p);
      // The final bit does not need to be committed because no later
      // probability contributes to this finite-sequence likelihood.
      if (shift != 0) baseline->Perceive(bit);
    }
    const bool ok = WriteAll(fds[1], &out, sizeof(out));
    close(fds[1]);
    _exit(ok ? 0 : 3);
  }

  close(fds[1]);
  SequenceEval out;
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
  std::fprintf(stderr, "candidate failed status=%d value=%u\n",
      status, static_cast<unsigned int>(value));
  std::exit(3);
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

unsigned int FloorLog2(uint64_t value) {
  unsigned int result = 0;
  while (value >>= 1) ++result;
  return result;
}

unsigned int EliasGammaBits(uint64_t positive_value) {
  return 2 * FloorLog2(positive_value) + 1;
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

  const size_t position = std::min(
      EnvSize("FX2_ORACLE16_POSITION", 260000), data.size() - 2);
  const uint64_t budget = EnvSize(
      "FX2_ORACLE16_NODE_BUDGET", kPrintableCandidateSpace);
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
  const SequenceEval actual_eval = EvaluateSequence(&predictor, actual);
  if (!actual_eval.valid) {
    std::cerr << "actual block itself crashes Predictor\n";
    return 7;
  }
  const double target_loss = actual_eval.loss;
  const uint64_t target_hash = Fingerprint(actual);

  uint64_t evaluated = 0;
  uint64_t stable_candidates = 0;
  uint64_t crash_candidates = 0;
  uint64_t earlier = 0;
  uint64_t collision = 0;
  for (unsigned int first = 32; first <= 126; ++first) {
    for (unsigned int second = 32; second <= 126; ++second) {
      if (++evaluated > budget) {
        std::cerr << "candidate budget exhausted at " << evaluated << '\n';
        return 5;
      }
      const uint16_t candidate = static_cast<uint16_t>((first << 8) | second);
      if (candidate == actual) {
        ++stable_candidates;
        continue;
      }
      const SequenceEval eval = EvaluateSequence(&predictor, candidate);
      if (!eval.valid) {
        ++crash_candidates;
        if (crash_candidates <= 8) {
          std::fprintf(stderr, "skipping crashing candidate signal=%d value=%u\n",
              eval.crash_signal, static_cast<unsigned int>(candidate));
        }
        continue;
      }
      ++stable_candidates;
      const bool is_earlier = eval.loss < target_loss - 1e-12 ||
          (std::fabs(eval.loss - target_loss) <= 1e-12 && candidate < actual);
      if (is_earlier) {
        ++earlier;
        collision |= CollisionMask(candidate, target_hash);
      }
    }
  }

  const uint64_t rank = earlier + 1;
  const double rank_bits = std::log2(static_cast<double>(rank));
  const unsigned int hash_bits = MinHashBits(collision);
  const unsigned int rank_gamma_bits = EliasGammaBits(rank);
  const unsigned int hash_gamma_bits =
      hash_bits + EliasGammaBits(static_cast<uint64_t>(hash_bits) + 1);
  const unsigned int hash_fixed6_bits = hash_bits + 6;

  std::cout << "position,actual0,actual1,candidate_space,stable_candidate_space,surprisal_bits,rank,log2_rank,rank_gamma_bits,min_first_match_hash_bits,hash_gamma_framed_bits,hash_fixed6_framed_bits,rank_oracle_gain_bits,hash_payload_gain_bits,rank_gamma_gain_bits,hash_gamma_gain_bits,hash_fixed6_gain_bits,evaluated_candidates,crash_candidates\n";
  std::cout << position << ',' << static_cast<unsigned int>(a) << ','
            << static_cast<unsigned int>(b) << ',' << kPrintableCandidateSpace
            << ',' << stable_candidates << ','
            << std::fixed << std::setprecision(6) << target_loss << ','
            << rank << ',' << rank_bits << ',' << rank_gamma_bits << ','
            << hash_bits << ',' << hash_gamma_bits << ',' << hash_fixed6_bits
            << ',' << (target_loss - rank_bits)
            << ',' << (target_loss - hash_bits)
            << ',' << (target_loss - rank_gamma_bits)
            << ',' << (target_loss - hash_gamma_bits)
            << ',' << (target_loss - hash_fixed6_bits)
            << ',' << evaluated << ',' << crash_candidates << '\n';
  std::cerr << "complete printable-ASCII oracle; earlier=" << earlier
            << " evaluated=" << evaluated
            << " stable=" << stable_candidates
            << " crashes=" << crash_candidates << '\n';
  return 0;
}
