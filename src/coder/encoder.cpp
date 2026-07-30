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
  explicit NumericTrace(const char* path) : path_(path) {}

  void AddBit(int bit, unsigned int final_probability,
      const std::array<float, 4>& model_probabilities,
      uint8_t start_word_bucket, uint8_t start_wrt_bucket) {
    byte_loss_ += BitLoss(
        bit, static_cast<double>(final_probability) / 65536.0);
    for (size_t index = 0; index < model_probabilities.size(); ++index) {
      byte_model_loss_[index] += BitLoss(bit, model_probabilities[index]);
    }
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
  }

  void Write() const {
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
      {"all", {}}, {"continue_digit", {}}, {"digit", {}},
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
  double byte_loss_ = 0;
  std::array<double, 4> byte_model_loss_ = {};
};

Encoder::Encoder(std::ofstream* os, Predictor* p) : os_(os), x1_(0),
    x2_(0xffffffff), p_(p), numeric_trace_(nullptr) {
  const char* trace_path = std::getenv("FX2_NUMERIC_TRACE");
  if (trace_path && trace_path[0]) {
    numeric_trace_ = new NumericTrace(trace_path);
  }
}

Encoder::~Encoder() {
  delete numeric_trace_;
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
}
