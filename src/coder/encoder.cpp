#include "encoder.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

struct TraceBucket {
  uint64_t bytes = 0;
  double bits = 0;

  void Add(double loss) {
    ++bytes;
    bits += loss;
  }
};

uint8_t PositionBucket(uint64_t length) {
  if (length <= 5) return static_cast<uint8_t>(length);
  if (length <= 7) return 6;
  if (length <= 9) return 7;
  if (length <= 15) return 8;
  return 9;
}

uint8_t LeftClass(uint8_t byte) {
  if (byte == 0) return 0;
  if (byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n') return 1;
  if (byte >= 'a' && byte <= 'z') return 2;
  if (byte >= 'A' && byte <= 'Z') return 3;
  if (byte >= '0' && byte <= '9') return 4;
  if (byte == '=') return 5;
  if (byte == '<' || byte == '>') return 6;
  if (byte == '+' || byte == '-') return 7;
  if (byte == '/' || byte == '\\') return 8;
  if (byte == '.' || byte == ',') return 9;
  if (byte == ':' || byte == ';') return 10;
  if (byte == '(' || byte == '[' || byte == '{') return 11;
  if (byte == ')' || byte == ']' || byte == '}') return 12;
  if (byte == '\'' || byte == '"') return 13;
  if (byte < 0x20 || byte == 0x7f) return 14;
  return 15;
}

bool IsNumericSeparator(uint8_t byte) {
  return byte == '.' || byte == ',' || byte == '-' || byte == '+' ||
      byte == ':' || byte == '/';
}

const char* PositionName(size_t position) {
  static const char* names[] = {
      "0", "1", "2", "3", "4", "5", "6-7", "8-9", "10-15", "16+"};
  return names[position];
}

const char* LeftClassName(size_t left_class) {
  static const char* names[] = {
      "start", "space", "lower", "upper", "digit", "equals", "angle",
      "sign", "slash", "decimal", "colon", "open", "close", "quote",
      "control", "other"};
  return names[left_class];
}

void PrintBucket(FILE* output, const char* name, const TraceBucket& bucket,
    bool comma) {
  const double rate = bucket.bytes ? bucket.bits / bucket.bytes : 0;
  std::fprintf(output,
      "    \"%s\": {\"bytes\": %llu, \"bits\": %.9f, \"bits_per_byte\": %.9f}%s\n",
      name, static_cast<unsigned long long>(bucket.bytes), bucket.bits, rate,
      comma ? "," : "");
}

}

struct NumericTrace {
  explicit NumericTrace(const char* path) : path_(path) {}

  void AddBit(int bit, unsigned int probability) {
    const double probability_one = static_cast<double>(probability) / 65536.0;
    const double actual = bit ? probability_one : 1.0 - probability_one;
    byte_loss_ -= std::log2(actual);
    byte_ = static_cast<uint8_t>((byte_ << 1) | bit);
    if (++bit_count_ == 8) {
      AddByte();
      bit_count_ = 0;
      byte_ = 0;
      byte_loss_ = 0;
    }
  }

  void AddByte() {
    ++total_bytes_;
    total_.Add(byte_loss_);
    const bool digit = byte_ >= '0' && byte_ <= '9';
    const bool previous_was_digit =
        previous_byte_ >= '0' && previous_byte_ <= '9';

    if (digit) {
      digit_.Add(byte_loss_);
      by_digit_[byte_ - '0'].Add(byte_loss_);
      const uint8_t position = PositionBucket(run_length_);
      by_position_[position].Add(byte_loss_);
      by_left_class_[left_class_].Add(byte_loss_);
      if (previous_was_digit) {
        categories_[1].Add(byte_loss_);
        by_previous_digit_[previous_byte_ - '0'].Add(byte_loss_);
      } else {
        categories_[0].Add(byte_loss_);
      }
      ++run_length_;
    } else {
      if (previous_was_digit) {
        after_digit_.Add(byte_loss_);
        if (IsNumericSeparator(byte_)) {
          categories_[3].Add(byte_loss_);
        } else {
          categories_[2].Add(byte_loss_);
        }
      } else {
        categories_[4].Add(byte_loss_);
      }
      run_length_ = 0;
      left_class_ = LeftClass(byte_);
    }
    previous_byte_ = byte_;
  }

  void Write() const {
    FILE* output = std::fopen(path_.c_str(), "w");
    if (!output) {
      std::fprintf(stderr, "\ncan't open numeric trace: %s\n", path_.c_str());
      return;
    }
    const double bits_per_digit =
        digit_.bytes ? digit_.bits / digit_.bytes : 0;
    std::fprintf(output, "{\n");
    std::fprintf(output, "  \"predictor_input_bytes\": %llu,\n",
        static_cast<unsigned long long>(total_bytes_));
    std::fprintf(output, "  \"predictor_input_bits\": %.9f,\n", total_.bits);
    std::fprintf(output, "  \"bits_per_byte\": %.9f,\n",
        total_.bytes ? total_.bits / total_.bytes : 0);
    std::fprintf(output, "  \"digit_bytes\": %llu,\n",
        static_cast<unsigned long long>(digit_.bytes));
    std::fprintf(output, "  \"digit_bits\": %.9f,\n", digit_.bits);
    std::fprintf(output, "  \"bits_per_digit\": %.9f,\n", bits_per_digit);
    std::fprintf(output, "  \"digit_ratio\": %.9f,\n",
        total_bytes_ ? static_cast<double>(digit_.bytes) / total_bytes_ : 0);
    std::fprintf(output, "  \"first_digit_bits_per_digit\": %.9f,\n",
        categories_[0].bytes ? categories_[0].bits / categories_[0].bytes : 0);
    std::fprintf(output, "  \"continuation_bits_per_digit\": %.9f,\n",
        categories_[1].bytes ? categories_[1].bits / categories_[1].bytes : 0);
    std::fprintf(output, "  \"after_digit_bits_per_byte\": %.9f,\n",
        after_digit_.bytes ? after_digit_.bits / after_digit_.bytes : 0);

    static const char* category_names[] = {
        "DIGIT_START", "DIGIT_CONTINUE", "AFTER_DIGIT",
        "NUMERIC_SEPARATOR", "NON_NUMERIC"};
    std::fprintf(output, "  \"categories\": {\n");
    for (size_t i = 0; i < categories_.size(); ++i) {
      PrintBucket(output, category_names[i], categories_[i],
          i + 1 != categories_.size());
    }
    std::fprintf(output, "  },\n");

    std::fprintf(output, "  \"by_position\": {\n");
    for (size_t i = 0; i < by_position_.size(); ++i) {
      PrintBucket(output, PositionName(i), by_position_[i],
          i + 1 != by_position_.size());
    }
    std::fprintf(output, "  },\n");

    std::fprintf(output, "  \"by_digit\": {\n");
    for (size_t i = 0; i < by_digit_.size(); ++i) {
      const char name[] = {static_cast<char>('0' + i), 0};
      PrintBucket(output, name, by_digit_[i], i + 1 != by_digit_.size());
    }
    std::fprintf(output, "  },\n");

    std::fprintf(output, "  \"by_previous_digit\": {\n");
    for (size_t i = 0; i < by_previous_digit_.size(); ++i) {
      const char name[] = {static_cast<char>('0' + i), 0};
      PrintBucket(output, name, by_previous_digit_[i],
          i + 1 != by_previous_digit_.size());
    }
    std::fprintf(output, "  },\n");

    std::fprintf(output, "  \"by_left_context\": {\n");
    for (size_t i = 0; i < by_left_class_.size(); ++i) {
      PrintBucket(output, LeftClassName(i), by_left_class_[i],
          i + 1 != by_left_class_.size());
    }
    std::fprintf(output, "  }\n");
    std::fprintf(output, "}\n");
    std::fclose(output);
  }

  std::string path_;
  uint64_t total_bytes_ = 0;
  uint64_t run_length_ = 0;
  uint8_t previous_byte_ = 0;
  uint8_t left_class_ = 0;
  uint8_t byte_ = 0;
  uint8_t bit_count_ = 0;
  double byte_loss_ = 0;
  TraceBucket digit_;
  TraceBucket total_;
  TraceBucket after_digit_;
  std::array<TraceBucket, 5> categories_;
  std::array<TraceBucket, 10> by_position_;
  std::array<TraceBucket, 10> by_digit_;
  std::array<TraceBucket, 10> by_previous_digit_;
  std::array<TraceBucket, 16> by_left_class_;
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
  if (numeric_trace_) numeric_trace_->AddBit(bit, p);
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
