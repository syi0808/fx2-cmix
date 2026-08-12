from pathlib import Path

path = Path("experiments/byte-candidate-rank/block_rank.cpp")
text = path.read_text()

replacements = [
    (
        "#include <iostream>\n#include <vector>\n",
        "#include <iostream>\n#include <unordered_map>\n#include <vector>\n",
    ),
    (
        "    if (depth + 1 == block_length) {\n"
        "      ++result.nodes;\n"
        "      if (result.nodes > budget) {\n"
        "        result.overflow = 1;\n"
        "        return result;\n"
        "      }\n",
        "    if (depth + 1 == block_length) {\n"
        "      // Leaf comparisons are cheap. Budget only the expensive PPMD\n"
        "      // state forks needed to descend to another byte.\n"
        "      ++result.nodes;\n",
    ),
    (
        "    if (result.nodes >= budget) {\n"
        "      result.overflow = 1;\n"
        "      return result;\n"
        "    }\n",
        "    if (result.forks >= budget) {\n"
        "      result.overflow = 1;\n"
        "      return result;\n"
        "    }\n",
    ),
    (
        "          actual, target_hash, budget - result.nodes);\n",
        "          actual, target_hash, budget - result.forks - 1);\n",
    ),
    (
        "    if (child.overflow || result.nodes > budget) {\n",
        "    if (child.overflow || result.forks > budget) {\n",
    ),
    (
        "  const uint64_t budget = EnvSize(\"FX2_BLOCK_RANK_NODE_BUDGET\", 250000);\n",
        "  const char* fork_budget_value = std::getenv(\"FX2_BLOCK_RANK_FORK_BUDGET\");\n"
        "  const uint64_t budget = fork_budget_value && fork_budget_value[0]\n"
        "      ? std::strtoull(fork_budget_value, nullptr, 10)\n"
        "      : EnvSize(\"FX2_BLOCK_RANK_NODE_BUDGET\", 50000);\n",
    ),
    (
        "        std::cerr << \"node budget exhausted position=\" << position\n"
        "                  << \" nodes=\" << search.nodes << '\\n';\n",
        "        std::cerr << \"fork budget exhausted position=\" << position\n"
        "                  << \" forks=\" << search.forks\n"
        "                  << \" nodes=\" << search.nodes\n"
        "                  << \" ppmd_nll=\" << actual_loss.loss << '\\n';\n",
    ),
    (
        "  const uint64_t max_rank = *std::max_element(ranks.begin(), ranks.end());\n"
        "  std::vector<uint64_t> histogram(max_rank + 1, 0);\n"
        "  for (uint64_t rank : ranks) ++histogram[rank];\n"
        "  const double count = static_cast<double>(ranks.size());\n"
        "  double rank_entropy = 0.0;\n"
        "  for (size_t rank = 1; rank < histogram.size(); ++rank) {\n"
        "    if (!histogram[rank]) continue;\n"
        "    const double p = static_cast<double>(histogram[rank]) / count;\n"
        "    rank_entropy -= p * std::log2(p);\n"
        "  }\n",
        "  std::unordered_map<uint64_t, uint64_t> histogram;\n"
        "  for (uint64_t rank : ranks) ++histogram[rank];\n"
        "  const double count = static_cast<double>(ranks.size());\n"
        "  double rank_entropy = 0.0;\n"
        "  for (const auto& item : histogram) {\n"
        "    const double p = static_cast<double>(item.second) / count;\n"
        "    rank_entropy -= p * std::log2(p);\n"
        "  }\n",
    ),
]

for old, new in replacements:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"expected one match, found {count}: {old[:80]!r}")
    text = text.replace(old, new)

path.write_text(text)
print("patched block oracle: fork-only budget + sparse rank histogram")
