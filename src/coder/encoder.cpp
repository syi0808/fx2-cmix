#include "encoder.h"

#include "../contexts/structural-class.h"
#include "../contexts/numeric-sequence-context.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <iterator>
#include <string>

#ifndef URL_TRACE
#define URL_TRACE 1
#endif

namespace {

const char* const kModelNames[] = {
    "boundary", "linked", "start_coarse", "start_field"};

struct TraceBucket {
  uint64_t events = 0;
  double bits = 0;
  std::array<double, 4> model_bits = {};

  void Add(double loss, const std::array<double, 4>& model_loss) {
    ++events;
    bits += loss;
    for (size_t index = 0; index < model_bits.size(); ++index) {
      model_bits[index] += model_loss[index];
    }
  }
};

double BitLoss(int bit, double probability) {
  probability = std::max(1.0 / 65536.0,
      std::min(65535.0 / 65536.0, probability));
  return -std::log2(bit ? probability : 1.0 - probability);
}

const char* StructuralName(StructuralClass structural_class) {
  static const char* names[] = {
      "other", "whitespace", "letter", "word_token", "open_markup",
      "close_markup", "equals", "pipe", "colon", "semicolon", "dash",
      "plus", "slash", "dot", "comma", "quote", "newline"};
  return names[static_cast<uint8_t>(structural_class)];
}

const char* SeparatorName(uint8_t byte) {
  if (byte == '-') return "dash";
  if (byte == ':' || byte == 'J') return "colon";
  if (byte == '.') return "dot";
  if (byte == ',') return "comma";
  if (byte == '/') return "slash";
  if (byte == '+') return "plus";
  if (byte == 'T') return "bridge";
  return "other";
}

const char* TerminatorName(uint8_t byte) {
  const StructuralClass structural_class = ClassifyStructural(byte);
  if (structural_class == StructuralClass::Whitespace ||
      structural_class == StructuralClass::Newline) return "whitespace";
  if (structural_class == StructuralClass::OpenMarkup ||
      structural_class == StructuralClass::CloseMarkup) return "markup";
  if (structural_class == StructuralClass::Dash) return "dash";
  if (structural_class == StructuralClass::Colon) return "colon";
  if (structural_class == StructuralClass::Dot) return "dot";
  if (structural_class == StructuralClass::Comma) return "comma";
  if (structural_class == StructuralClass::Slash) return "slash";
  return "other";
}

void PrintBucket(FILE* output, const TraceBucket& bucket, int indent) {
  const double rate = bucket.events ? bucket.bits / bucket.events : 0;
  std::fprintf(output,
      "{\"events\": %llu, \"bits\": %.9f, \"bits_per_event\": %.9f, "
      "\"standalone_bits\": {",
      static_cast<unsigned long long>(bucket.events), bucket.bits, rate);
  for (size_t index = 0; index < bucket.model_bits.size(); ++index) {
    std::fprintf(output, "\"%s\": %.9f%s", kModelNames[index],
        bucket.model_bits[index],
        index + 1 == bucket.model_bits.size() ? "" : ", ");
  }
  std::fprintf(output, "}}");
  (void)indent;
}

void PrintMap(FILE* output, const char* name,
    const std::map<std::string, TraceBucket>& buckets, bool comma) {
  std::fprintf(output, "  \"%s\": {\n", name);
  size_t index = 0;
  for (const auto& item : buckets) {
    std::fprintf(output, "    \"%s\": ", item.first.c_str());
    PrintBucket(output, item.second, 4);
    std::fprintf(output, "%s\n", ++index == buckets.size() ? "" : ",");
  }
  std::fprintf(output, "  }%s\n", comma ? "," : "");
}

}

struct NumericTrace {
  NumericTrace(const char* path, const char* probability_path)
      : path_(path ? path : ""), probability_output_(nullptr) {
    if (probability_path && probability_path[0]) {
      probability_output_ = std::fopen(probability_path, "wb");
      if (probability_output_) {
        const char header[8] = {'F', 'X', '2', 'N', 'B', 'P', '1', 0};
        std::fwrite(header, 1, sizeof(header), probability_output_);
      } else {
        std::fprintf(stderr, "\ncan't open numeric probability trace: %s\n",
            probability_path);
      }
    }
  }

  ~NumericTrace() {
    if (probability_output_) std::fclose(probability_output_);
  }

  void AddBit(int bit, unsigned int final_probability,
      const std::array<float, 4>& model_probabilities,
      uint8_t start_word_bucket, uint8_t start_wrt_bucket) {
    byte_loss_ += BitLoss(
        bit, static_cast<double>(final_probability) / 65536.0);
    for (size_t index = 0; index < model_probabilities.size(); ++index) {
      byte_model_loss_[index] += BitLoss(bit, model_probabilities[index]);
    }
    const size_t bit_index = bit_count_;
    byte_final_probabilities_[bit_index] =
        static_cast<uint16_t>(final_probability);
    const float boundary_probability = std::max(
        1.0f / 65536.0f,
        std::min(65535.0f / 65536.0f, model_probabilities[0]));
    byte_boundary_probabilities_[bit_index] =
        static_cast<uint16_t>(boundary_probability * 65536.0f);
    byte_bits_[bit_index] = static_cast<uint8_t>(bit);
    byte_ = static_cast<uint8_t>((byte_ << 1) | bit);
    byte_start_word_bucket_ = start_word_bucket;
    byte_start_wrt_bucket_ = start_wrt_bucket;
    if (++bit_count_ == 8) {
      AddByte();
      bit_count_ = 0;
      byte_ = 0;
      byte_loss_ = 0;
      byte_model_loss_.fill(0);
    }
  }

  void Add(const char* name) {
    events_[name].Add(byte_loss_, byte_model_loss_);
  }

  void AddDimension(const char* dimension, const std::string& value) {
    dimensions_[dimension][value].Add(byte_loss_, byte_model_loss_);
  }

  void AddByte() {
    Add("all");
    const bool digit = byte_ >= '0' && byte_ <= '9';
    const bool previous_digit =
        previous_byte_ >= '0' && previous_byte_ <= '9';
    Add(previous_digit ? "boundary_active" : "boundary_inactive");
    if (previous_digit) WriteProbabilityByte(digit);
    if (digit) {
      Add("digit");
      if (previous_digit) {
        Add("continue_digit");
        Add("run_continued");
        ++run_length_;
      } else if (linked_pending_) {
        Add("linked_run_start");
        AddDimension("separator_type", linked_separator_);
        ++segment_index_;
        run_length_ = 1;
        linked_pending_ = false;
      } else {
        Add("start_digit");
        start_structural_class_ = previous_structural_class_;
        AddDimension("start_structural_class",
            StructuralName(start_structural_class_));
        AddDimension("start_word_bucket",
            std::to_string(byte_start_word_bucket_));
        AddDimension("start_wrt_bucket",
            std::to_string(byte_start_wrt_bucket_));
        segment_index_ = 0;
        shape_label_.clear();
        run_length_ = 1;
      }
      AddDimension("segment_index",
          std::to_string(std::min<uint8_t>(segment_index_, 5)));
    } else {
      if (previous_digit) {
        Add("run_ended");
        AddDimension("terminator", TerminatorName(byte_));
        AddDimension("previous_run_length",
            std::to_string(NumericSequenceContext::RunLengthBucket(
                static_cast<uint8_t>(std::min<uint64_t>(run_length_, 255)))));
        const char* separator = SeparatorName(byte_);
        const bool numeric_separator =
            std::string(separator) != "other" &&
            (byte_ != 'T' || segment_index_ >= 2);
        if (numeric_separator) {
          Add("separator");
          linked_separator_ = separator;
          linked_pending_ = true;
          const uint8_t length_bucket =
              NumericSequenceContext::RunLengthBucket(
                  static_cast<uint8_t>(std::min<uint64_t>(run_length_, 255)));
          if (!shape_label_.empty()) shape_label_ += "/";
          shape_label_ += std::to_string(length_bucket);
          shape_label_ += "-";
          shape_label_ += separator;
          AddDimension("shape_state", shape_label_);
        } else {
          Add("sequence_end");
          linked_pending_ = false;
          segment_index_ = 0;
        }
      } else if (linked_pending_) {
        Add("sequence_end");
        linked_pending_ = false;
        segment_index_ = 0;
      } else {
        Add("non_numeric");
      }
      run_length_ = 0;
      previous_structural_class_ = ClassifyStructural(byte_);
    }
    previous_byte_ = byte_;
    ++byte_position_;
  }

  void WriteProbabilityByte(bool continued) {
    if (!probability_output_) return;
    uint8_t terminator = 0;
    if (!continued) {
      const std::string name = TerminatorName(byte_);
      if (name == "whitespace") terminator = 1;
      else if (name == "markup") terminator = 2;
      else if (name == "dash") terminator = 3;
      else if (name == "colon") terminator = 4;
      else if (name == "dot") terminator = 5;
      else if (name == "comma") terminator = 6;
      else if (name == "slash") terminator = 7;
      else terminator = 8;
    }
    for (uint8_t bit_index = 0; bit_index < 8; ++bit_index) {
      std::array<uint8_t, 16> record = {};
      for (int byte_index = 0; byte_index < 8; ++byte_index) {
        record[byte_index] =
            static_cast<uint8_t>(byte_position_ >> (8 * byte_index));
      }
      record[8] = static_cast<uint8_t>(
          byte_final_probabilities_[bit_index]);
      record[9] = static_cast<uint8_t>(
          byte_final_probabilities_[bit_index] >> 8);
      record[10] = static_cast<uint8_t>(
          byte_boundary_probabilities_[bit_index]);
      record[11] = static_cast<uint8_t>(
          byte_boundary_probabilities_[bit_index] >> 8);
      record[12] = bit_index;
      record[13] = byte_bits_[bit_index];
      record[14] = continued ? 1 : 2;
      record[15] = terminator;
      std::fwrite(record.data(), 1, record.size(), probability_output_);
    }
  }

  void Write() const {
    if (path_.empty()) return;
    FILE* output = std::fopen(path_.c_str(), "w");
    if (!output) {
      std::fprintf(stderr, "\ncan't open numeric trace: %s\n", path_.c_str());
      return;
    }
    std::fprintf(output, "{\n");
    PrintMap(output, "events", events_, true);
    std::fprintf(output, "  \"after_digit\": {\n");
    std::fprintf(output, "    \"continued\": ");
    PrintBucket(output, events_.at("run_continued"), 4);
    std::fprintf(output, ",\n    \"ended\": ");
    PrintBucket(output, events_.at("run_ended"), 4);
    std::fprintf(output, "\n  },\n");
    std::fprintf(output, "  \"linked_run\": {\n");
    std::fprintf(output, "    \"first_digit\": ");
    PrintBucket(output, events_.at("linked_run_start"), 4);
    std::fprintf(output, ",\n    \"separator\": ");
    PrintBucket(output, events_.at("separator"), 4);
    std::fprintf(output, "\n  },\n");
    for (auto iterator = dimensions_.begin();
         iterator != dimensions_.end(); ++iterator) {
      PrintMap(output, iterator->first.c_str(), iterator->second,
          std::next(iterator) != dimensions_.end());
    }
    std::fprintf(output, "}\n");
    std::fclose(output);
  }

  std::string path_;
  std::map<std::string, TraceBucket> events_ = {
      {"all", {}}, {"boundary_active", {}}, {"boundary_inactive", {}},
      {"continue_digit", {}}, {"digit", {}},
      {"linked_run_start", {}}, {"non_numeric", {}},
      {"run_continued", {}}, {"run_ended", {}}, {"separator", {}},
      {"sequence_end", {}}, {"start_digit", {}}};
  std::map<std::string, std::map<std::string, TraceBucket>> dimensions_ = {
      {"previous_run_length", {}}, {"segment_index", {}},
      {"separator_type", {}}, {"shape_state", {}},
      {"start_structural_class", {}}, {"start_word_bucket", {}},
      {"start_wrt_bucket", {}}, {"terminator", {}}};
  uint64_t run_length_ = 0;
  uint8_t previous_byte_ = 0;
  uint8_t byte_ = 0;
  uint8_t bit_count_ = 0;
  uint8_t segment_index_ = 0;
  uint8_t byte_start_word_bucket_ = 0;
  uint8_t byte_start_wrt_bucket_ = 0;
  bool linked_pending_ = false;
  StructuralClass previous_structural_class_ = StructuralClass::Other;
  StructuralClass start_structural_class_ = StructuralClass::Other;
  std::string linked_separator_;
  std::string shape_label_;
  FILE* probability_output_;
  uint64_t byte_position_ = 0;
  double byte_loss_ = 0;
  std::array<double, 4> byte_model_loss_ = {};
  std::array<uint16_t, 8> byte_final_probabilities_ = {};
  std::array<uint16_t, 8> byte_boundary_probabilities_ = {};
  std::array<uint8_t, 8> byte_bits_ = {};
};

struct UrlTrace {
  struct Bucket {
    uint64_t urls = 0;
    uint64_t bytes = 0;
    double bits = 0;
  };

  explicit UrlTrace(const char* path) : path_(path ? path : "") {}

  void AddBit(int bit, unsigned int probability, const UrlState& state) {
    if (bit_count_ == 0) {
      role_ = state.role;
      confidence_ = state.confidence;
      active_ = state.confidence == UrlConfidence::UrlConfirmed;
      domain_hash_ = state.domain_hash;
      template_hash_ = state.path_template_hash;
      if (active_ && !previous_active_) {
        ++current_url_id_;
      }
    }
    byte_loss_ += BitLoss(
        bit, static_cast<double>(probability) / 65536.0);
    if (++bit_count_ == 8) {
      AddByte();
      bit_count_ = 0;
      byte_loss_ = 0;
    }
  }

  void AddByte() {
    Bucket& total_bucket = totals_["all"];
    ++total_bucket.bytes;
    total_bucket.bits += byte_loss_;
    const char* region = "outside";
    if (confidence_ == UrlConfidence::SchemeCandidate) region = "candidate";
    if (confidence_ == UrlConfidence::UrlConfirmed) region = "url";
    Bucket& region_bucket = regions_[region];
    ++region_bucket.bytes;
    region_bucket.bits += byte_loss_;
    if (active_ && region_last_url_[region] != current_url_id_) {
      ++region_bucket.urls;
      region_last_url_[region] = current_url_id_;
    }

    const std::string role = UrlRoleName(role_);
    Bucket& role_bucket = roles_[role];
    ++role_bucket.bytes;
    role_bucket.bits += byte_loss_;
    if (role_last_url_[role] != current_url_id_ && active_) {
      ++role_bucket.urls;
      role_last_url_[role] = current_url_id_;
    }
    if (active_ && domain_hash_) {
      const std::string domain = Hex(domain_hash_);
      Bucket& bucket = domains_[domain];
      ++bucket.bytes;
      bucket.bits += byte_loss_;
      if (domain_last_url_[domain] != current_url_id_) {
        ++bucket.urls;
        domain_last_url_[domain] = current_url_id_;
      }
      if (template_hash_) {
        const std::string endpoint_key =
            domain + ":" + Hex(template_hash_);
        Bucket& endpoint = endpoints_[endpoint_key];
        ++endpoint.bytes;
        endpoint.bits += byte_loss_;
        if (endpoint_last_url_[endpoint_key] != current_url_id_) {
          ++endpoint.urls;
          endpoint_last_url_[endpoint_key] = current_url_id_;
        }
      }
    }
    previous_active_ = active_;
  }

  static std::string Hex(uint64_t value) {
    char buffer[17] = {};
    std::snprintf(buffer, sizeof(buffer), "%016llx",
        static_cast<unsigned long long>(value));
    return buffer;
  }

  void WriteMap(FILE* output, const char* dimension,
      const std::map<std::string, Bucket>& buckets) const {
    for (const auto& item : buckets) {
      const double bpb =
          item.second.bytes ? item.second.bits / item.second.bytes : 0;
      std::fprintf(output, "%s,%s,%llu,%llu,%.9f,%.9f\n", dimension,
          item.first.c_str(),
          static_cast<unsigned long long>(item.second.urls),
          static_cast<unsigned long long>(item.second.bytes),
          item.second.bits, bpb);
    }
  }

  void Write() const {
    if (path_.empty()) return;
    FILE* output = std::fopen(path_.c_str(), "w");
    if (!output) {
      std::fprintf(stderr, "\ncan't open URL trace: %s\n", path_.c_str());
      return;
    }
    std::fprintf(output, "dimension,key,url_count,bytes,bits,bpb\n");
    WriteMap(output, "total", totals_);
    WriteMap(output, "region", regions_);
    WriteMap(output, "role", roles_);
    WriteMap(output, "domain", domains_);
    WriteMap(output, "endpoint", endpoints_);
    std::fclose(output);
  }

  std::string path_;
  std::map<std::string, Bucket> totals_;
  std::map<std::string, Bucket> regions_;
  std::map<std::string, Bucket> roles_;
  std::map<std::string, Bucket> domains_;
  std::map<std::string, Bucket> endpoints_;
  std::map<std::string, uint64_t> role_last_url_;
  std::map<std::string, uint64_t> region_last_url_;
  std::map<std::string, uint64_t> domain_last_url_;
  std::map<std::string, uint64_t> endpoint_last_url_;
  UrlRole role_ = UrlRole::Outside;
  UrlConfidence confidence_ = UrlConfidence::Outside;
  uint64_t domain_hash_ = 0;
  uint64_t template_hash_ = 0;
  uint8_t bit_count_ = 0;
  double byte_loss_ = 0;
  bool active_ = false;
  bool previous_active_ = false;
  uint64_t current_url_id_ = 0;
};

Encoder::Encoder(std::ofstream* os, Predictor* p) : os_(os), x1_(0),
    x2_(0xffffffff), p_(p), numeric_trace_(nullptr), url_trace_(nullptr) {
  const char* trace_path = std::getenv("FX2_NUMERIC_TRACE");
  const char* probability_path = std::getenv("FX2_NUMERIC_PROB_TRACE");
  if ((trace_path && trace_path[0]) ||
      (probability_path && probability_path[0])) {
    numeric_trace_ = new NumericTrace(trace_path, probability_path);
  }
#if URL_TRACE
  const char* url_trace_path = std::getenv("FX2_URL_TRACE");
  if (url_trace_path && url_trace_path[0]) {
    url_trace_ = new UrlTrace(url_trace_path);
  }
#endif
}

Encoder::~Encoder() {
  delete numeric_trace_;
  delete url_trace_;
}

void Encoder::WriteByte(unsigned int byte) {
  out_.push_back(byte);
}

unsigned int Encoder::Discretize(float p) {
  return 1 + 65534 * p;
}

void Encoder::Encode(int bit) {
  const unsigned int p = Discretize(p_->Predict());
  if (numeric_trace_) {
    numeric_trace_->AddBit(bit, p, p_->NumericModelProbabilities(),
        p_->NumericStartWordBucket(), p_->NumericStartWrtBucket());
  }
  if (url_trace_) {
    url_trace_->AddBit(bit, p, p_->CurrentUrlState());
  }
  const unsigned int xmid = x1_ + ((x2_ - x1_) >> 16) * p +
      (((x2_ - x1_) & 0xffff) * p >> 16);
  if (bit) {
    x2_ = xmid;
  } else {
    x1_ = xmid + 1;
  }
  p_->Perceive(bit);

  while (((x1_^x2_) & 0xff000000) == 0) {
    WriteByte(x2_ >> 24);
    x1_ <<= 8;
    x2_ = (x2_ << 8) + 255;
  }
}

void Encoder::Flush() {
  while (((x1_^x2_) & 0xff000000) == 0) {
    WriteByte(x2_ >> 24);
    x1_ <<= 8;
    x2_ = (x2_ << 8) + 255;
  }
  WriteByte(x2_ >> 24);

  auto* data = reinterpret_cast<const char*>(out_.data());
  os_->write(data, out_.size());
  if (numeric_trace_) numeric_trace_->Write();
  if (url_trace_) url_trace_->Write();
}
