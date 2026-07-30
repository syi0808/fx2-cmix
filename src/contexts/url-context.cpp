#include "url-context.h"

#include "../preprocess/symbols.h"

#include <algorithm>
#include <cctype>

namespace {

constexpr uint16_t kMaxUrlLength = 4096;
constexpr uint16_t kMaxComponentLength = 1024;
constexpr uint64_t kPrime = 0x100000001b3ULL;

enum ComponentFlag : uint32_t {
  kHasAlpha = 1 << 0,
  kHasDigit = 1 << 1,
  kHasNonHexAlpha = 1 << 2,
  kHasHyphen = 1 << 3,
  kHasUnderscore = 1 << 4,
  kHasPercent = 1 << 5,
  kHasOther = 1 << 6,
  kHasDot = 1 << 7,
};

uint64_t Mix(uint64_t value) {
  value ^= value >> 30;
  value *= 0xbf58476d1ce4e5b9ULL;
  value ^= value >> 27;
  value *= 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}

uint64_t AddField(uint64_t hash, uint64_t value) {
  return Mix(hash ^ (value + 0x9e3779b97f4a7c15ULL + (hash << 6) +
                     (hash >> 2)));
}

uint64_t Packed(const char* text) {
  uint64_t value = 0;
  while (*text) value = (value << 8) | static_cast<uint8_t>(*text++);
  return value;
}

uint64_t SuffixMask(size_t length) {
  return length == 8 ? ~0ULL : ((1ULL << (length * 8)) - 1);
}

bool HasSuffix(uint64_t packed, const char* text, size_t length) {
  return (packed & SuffixMask(length)) == Packed(text);
}

uint8_t PositionBucket(uint16_t length) {
  if (length <= 4) return static_cast<uint8_t>(length);
  if (length <= 8) return 5;
  if (length <= 16) return 6;
  return 7;
}

bool IsHexAlpha(uint8_t byte) {
  byte = static_cast<uint8_t>(std::tolower(byte));
  return byte >= 'a' && byte <= 'f';
}

uint8_t CanonicalByte(uint8_t byte, UrlSymbol symbol) {
  switch (symbol) {
    case UrlSymbol::Colon: return ':';
    case UrlSymbol::Question: return '?';
    case UrlSymbol::Equals: return '=';
    default: return static_cast<uint8_t>(std::tolower(byte));
  }
}

}

UrlSymbol NormalizeUrlSymbol(uint8_t byte) {
  if (byte >= 'a' && byte <= 'z') return UrlSymbol::AlphaLower;
  if (byte >= 'A' && byte <= 'Z' &&
      byte != preprocess::kWrtColon &&
      byte != preprocess::kWrtLessThan &&
      byte != preprocess::kWrtEquals &&
      byte != preprocess::kWrtGreaterThan &&
      byte != preprocess::kWrtQuestion) {
    return UrlSymbol::AlphaUpper;
  }
  if (byte >= '0' && byte <= '9') return UrlSymbol::Digit;
  if (byte >= 0x80) return UrlSymbol::HighByte;
  if (preprocess::IsSemanticColon(byte)) return UrlSymbol::Colon;
  if (byte == '/') return UrlSymbol::Slash;
  if (byte == '.') return UrlSymbol::Dot;
  if (preprocess::IsSemanticQuestion(byte)) return UrlSymbol::Question;
  if (byte == '&') return UrlSymbol::Ampersand;
  if (preprocess::IsSemanticEquals(byte)) return UrlSymbol::Equals;
  if (byte == '#') return UrlSymbol::Hash;
  if (byte == '%') return UrlSymbol::Percent;
  if (byte == '@') return UrlSymbol::At;
  if (byte == '-') return UrlSymbol::Hyphen;
  if (byte == '_') return UrlSymbol::Underscore;
  if (byte == '~') return UrlSymbol::Tilde;
  if (byte == '+') return UrlSymbol::Plus;
  if (byte == '\'' || byte == '"') return UrlSymbol::Quote;
  if (byte == '[' || byte == '(' || byte == '{') {
    return UrlSymbol::OpenBracket;
  }
  if (byte == ']' || byte == ')' || byte == '}') {
    return UrlSymbol::CloseBracket;
  }
  if (byte == '\n' || byte == '\r') return UrlSymbol::Newline;
  if (byte == ' ' || byte == '\t' || byte == '\f') {
    return UrlSymbol::Whitespace;
  }
  return UrlSymbol::Other;
}

const char* UrlRoleName(UrlRole role) {
  static const char* names[] = {
      "outside", "scheme", "authority", "userinfo", "domain", "port",
      "path_segment", "extension", "query_key", "query_value", "fragment"};
  return names[static_cast<uint8_t>(role)];
}

UrlContext::UrlContext() {
  Refresh();
}

void UrlContext::ResetComponent() {
  state_.component_length = 0;
  state_.position_bucket = 0;
  state_.component_class = ComponentClass::Empty;
  state_.value_class = ValueClass::Empty;
  state_.percent_position = 0;
  component_hash_ = 0;
  component_prefix_hash_ = 0;
  component_flags_ = 0;
}

void UrlContext::Reset() {
  state_ = UrlState();
  candidate_slashes_ = 0;
  completed_path_hash_ = 0;
  completed_template_hash_ = 0;
  ResetComponent();
}

void UrlContext::StartScheme(SchemeClass scheme, uint8_t length) {
  Reset();
  state_.confidence = UrlConfidence::SchemeCandidate;
  state_.role = UrlRole::Scheme;
  state_.scheme = scheme;
  state_.component_length = length;
  state_.url_length = length;
  state_.position_bucket = PositionBucket(length);
  state_.scheme_hash = static_cast<uint8_t>(scheme) + 1;
}

void UrlContext::Confirm(SchemeClass scheme, UrlRole role) {
  state_.confidence = UrlConfidence::UrlConfirmed;
  state_.scheme = scheme;
  state_.role = role;
  state_.url_length = std::max<uint16_t>(state_.url_length, 1);
  candidate_slashes_ = 0;
  ResetComponent();
}

void UrlContext::UpdateComponent(uint8_t byte, UrlSymbol symbol) {
  if (state_.component_length < kMaxComponentLength) {
    ++state_.component_length;
  }
  state_.position_bucket = PositionBucket(state_.component_length);
  const uint8_t canonical = CanonicalByte(byte, symbol);
  component_hash_ = component_hash_ * kPrime + canonical + 1;
  if (state_.component_length <= 4) {
    component_prefix_hash_ =
        component_prefix_hash_ * kPrime + canonical + 1;
  }

  if (symbol == UrlSymbol::AlphaLower || symbol == UrlSymbol::AlphaUpper) {
    component_flags_ |= kHasAlpha;
    if (!IsHexAlpha(byte)) component_flags_ |= kHasNonHexAlpha;
  } else if (symbol == UrlSymbol::Digit) {
    component_flags_ |= kHasDigit;
  } else if (symbol == UrlSymbol::Hyphen) {
    component_flags_ |= kHasHyphen;
  } else if (symbol == UrlSymbol::Underscore) {
    component_flags_ |= kHasUnderscore;
  } else if (symbol == UrlSymbol::Percent) {
    component_flags_ |= kHasPercent;
    state_.percent_position = 1;
  } else if (symbol == UrlSymbol::Dot) {
    component_flags_ |= kHasDot;
  } else if (symbol != UrlSymbol::Plus && symbol != UrlSymbol::Tilde &&
             symbol != UrlSymbol::HighByte) {
    component_flags_ |= kHasOther;
  }

  if (state_.percent_position && symbol != UrlSymbol::Percent) {
    if (state_.percent_position < 3) {
      ++state_.percent_position;
    } else {
      state_.percent_position = 0;
    }
  }

  const bool alpha = component_flags_ & kHasAlpha;
  const bool digit = component_flags_ & kHasDigit;
  const bool non_hex = component_flags_ & kHasNonHexAlpha;
  const bool hyphen = component_flags_ & kHasHyphen;
  const bool other = component_flags_ & (kHasOther | kHasUnderscore);
  if (component_flags_ & kHasPercent) {
    state_.component_class = ComponentClass::PercentEncoded;
  } else if (digit && !alpha && hyphen && !other) {
    state_.component_class = ComponentClass::DateLike;
  } else if (digit && alpha && !non_hex && hyphen && !other &&
             state_.component_length >= 9) {
    state_.component_class = ComponentClass::UuidLike;
  } else if (digit && !alpha && !hyphen && !other) {
    state_.component_class = ComponentClass::Numeric;
  } else if (alpha && !digit && !hyphen && !other) {
    state_.component_class = ComponentClass::Alphabetic;
  } else if (digit && alpha && !non_hex && !hyphen && !other) {
    state_.component_class = ComponentClass::Hex;
  } else if (hyphen && !other) {
    state_.component_class = ComponentClass::Slug;
  } else if (alpha && digit && !other && state_.component_length >= 12) {
    state_.component_class = ComponentClass::Base64Like;
  } else {
    state_.component_class = ComponentClass::Mixed;
  }

  if (state_.role == UrlRole::Extension) {
    state_.component_class = ComponentClass::FileLike;
    state_.extension_hash = component_hash_;
  }
  switch (state_.component_class) {
    case ComponentClass::Empty:
      state_.value_class = ValueClass::Empty;
      break;
    case ComponentClass::Alphabetic:
      state_.value_class = ValueClass::Alphabetic;
      break;
    case ComponentClass::Numeric:
      state_.value_class = ValueClass::Numeric;
      break;
    case ComponentClass::Hex:
      state_.value_class = ValueClass::Hex;
      break;
    case ComponentClass::PercentEncoded:
      state_.value_class = ValueClass::PercentEncoded;
      break;
    case ComponentClass::UuidLike:
    case ComponentClass::DateLike:
    case ComponentClass::Slug:
      state_.value_class = ValueClass::Structured;
      break;
    default:
      state_.value_class = ValueClass::Mixed;
      break;
  }
  if (state_.role == UrlRole::PathSegment ||
      state_.role == UrlRole::Extension) {
    state_.path_hash =
        completed_path_hash_ * kPrime + Mix(component_hash_);
    state_.path_template_hash = completed_template_hash_ * kPrime +
        Mix(static_cast<uint8_t>(state_.component_class) + 1);
    state_.endpoint_hash =
        Mix(state_.domain_hash ^ (state_.path_hash * kPrime));
  }
}

void UrlContext::FinishDomainLabel() {
  if (!state_.component_length) return;
  state_.domain_label_hash = component_hash_;
  state_.domain_hash = state_.domain_hash * kPrime +
      Mix(component_hash_ ^ state_.domain_label_index);
  if (state_.domain_label_index < 255) ++state_.domain_label_index;
  ResetComponent();
}

void UrlContext::FinishPathSegment() {
  if (!state_.component_length) return;
  state_.previous_segment_hash = state_.current_segment_hash;
  state_.current_segment_hash = component_hash_;
  completed_path_hash_ =
      completed_path_hash_ * kPrime + Mix(component_hash_);
  const uint64_t normalized =
      static_cast<uint8_t>(state_.component_class) + 1;
  completed_template_hash_ =
      completed_template_hash_ * kPrime + Mix(normalized);
  state_.path_hash = completed_path_hash_;
  state_.path_template_hash = completed_template_hash_;
  state_.endpoint_hash =
      Mix(state_.domain_hash ^ (state_.path_hash * kPrime));
  ResetComponent();
}

void UrlContext::FinishQueryKey() {
  if (!state_.component_length) return;
  state_.previous_query_key_hash = state_.query_key_hash;
  state_.query_key_hash = component_hash_;
  ResetComponent();
}

void UrlContext::FinishQueryValue() {
  state_.query_value_hash = component_hash_;
  ResetComponent();
}

bool UrlContext::ShouldTerminate(uint8_t byte, UrlSymbol symbol) const {
  if (state_.url_length >= kMaxUrlLength ||
      state_.component_length >= kMaxComponentLength) {
    return true;
  }
  if (symbol == UrlSymbol::Whitespace || symbol == UrlSymbol::Newline ||
      symbol == UrlSymbol::Quote) {
    return true;
  }
  if (preprocess::IsSemanticLessThan(byte) ||
      preprocess::IsSemanticGreaterThan(byte) ||
      preprocess::IsSemanticVerticalBar(byte) || byte < 0x20) {
    return true;
  }
  if (symbol == UrlSymbol::CloseBracket && !state_.in_ipv6 &&
      byte == ')' && state_.paren_depth == 0) {
    return true;
  }
  if (symbol == UrlSymbol::CloseBracket && !state_.in_ipv6 &&
      byte == ']' && external_link_depth_) {
    return true;
  }
  return false;
}

void UrlContext::UpdateCandidate(uint8_t byte, UrlSymbol symbol) {
  ++state_.url_length;
  state_.previous_symbol_class = static_cast<uint8_t>(symbol);
  if (state_.scheme == SchemeClass::Http &&
      (byte == 's' || byte == 'S')) {
    state_.scheme = SchemeClass::Https;
    state_.scheme_hash = static_cast<uint8_t>(SchemeClass::Https) + 1;
    ++state_.component_length;
    state_.position_bucket = PositionBucket(state_.component_length);
    return;
  }
  if (state_.scheme == SchemeClass::Www && symbol == UrlSymbol::Dot) {
    Confirm(SchemeClass::Www, UrlRole::Domain);
    state_.domain_hash = Mix(Packed("www"));
    state_.domain_label_hash = Packed("www");
    state_.domain_label_index = 1;
    state_.url_length = 4;
    return;
  }
  if (state_.scheme == SchemeClass::Unknown &&
      state_.role == UrlRole::Domain) {
    if (symbol == UrlSymbol::Dot && state_.component_length) {
      Confirm(SchemeClass::Unknown, UrlRole::Domain);
      state_.domain_hash = Mix(component_hash_);
      state_.domain_label_hash = component_hash_;
      state_.domain_label_index = 1;
      return;
    }
    if (symbol == UrlSymbol::AlphaLower ||
        symbol == UrlSymbol::AlphaUpper || symbol == UrlSymbol::Digit ||
        symbol == UrlSymbol::Hyphen) {
      UpdateComponent(byte, symbol);
      return;
    }
    Reset();
    return;
  }

  if (symbol == UrlSymbol::Colon && candidate_slashes_ == 0) {
    state_.scheme_hash = state_.scheme_hash * kPrime + ':';
    if (state_.scheme == SchemeClass::Mailto) {
      Confirm(state_.scheme, UrlRole::UserInfo);
    } else {
      candidate_slashes_ = 1;
    }
    return;
  }
  if (symbol == UrlSymbol::Slash && candidate_slashes_ == 1) {
    candidate_slashes_ = 2;
    return;
  }
  if (symbol == UrlSymbol::Slash && candidate_slashes_ == 2) {
    Confirm(state_.scheme, UrlRole::Domain);
    return;
  }
  Reset();
}

void UrlContext::UpdateConfirmed(uint8_t byte, UrlSymbol symbol) {
  if (ShouldTerminate(byte, symbol)) {
    Reset();
    return;
  }
  ++state_.url_length;
  state_.previous_symbol_class = static_cast<uint8_t>(symbol);

  if (symbol == UrlSymbol::OpenBracket && byte == '(') {
    if (state_.paren_depth < 255) ++state_.paren_depth;
  } else if (symbol == UrlSymbol::CloseBracket && byte == ')' &&
             state_.paren_depth) {
    --state_.paren_depth;
  }

  if (state_.role != UrlRole::Fragment && symbol == UrlSymbol::Hash) {
    if (state_.role == UrlRole::Domain) FinishDomainLabel();
    else if (state_.role == UrlRole::PathSegment ||
             state_.role == UrlRole::Extension) FinishPathSegment();
    else if (state_.role == UrlRole::QueryKey) FinishQueryKey();
    else if (state_.role == UrlRole::QueryValue) FinishQueryValue();
    state_.role = UrlRole::Fragment;
    ResetComponent();
    return;
  }

  if ((state_.role == UrlRole::Domain ||
       state_.role == UrlRole::Authority ||
       state_.role == UrlRole::UserInfo) &&
      symbol == UrlSymbol::OpenBracket && byte == '[' &&
      !state_.component_length) {
    state_.in_ipv6 = true;
    UpdateComponent(byte, symbol);
    return;
  }
  if (state_.in_ipv6) {
    UpdateComponent(byte, symbol);
    if (symbol == UrlSymbol::CloseBracket && byte == ']') {
      state_.in_ipv6 = false;
    }
    return;
  }

  if (state_.role == UrlRole::Domain ||
      state_.role == UrlRole::Authority ||
      state_.role == UrlRole::UserInfo) {
    if (symbol == UrlSymbol::At) {
      state_.role = UrlRole::Domain;
      state_.domain_hash = 0;
      state_.domain_label_hash = 0;
      state_.domain_label_index = 0;
      ResetComponent();
    } else if (symbol == UrlSymbol::Dot) {
      FinishDomainLabel();
      state_.role = UrlRole::Domain;
    } else if (symbol == UrlSymbol::Colon) {
      FinishDomainLabel();
      state_.role = UrlRole::Port;
    } else if (symbol == UrlSymbol::Slash) {
      FinishDomainLabel();
      state_.role = UrlRole::PathSegment;
      state_.segment_index = 0;
      ResetComponent();
    } else if (symbol == UrlSymbol::Question) {
      FinishDomainLabel();
      state_.endpoint_hash = Mix(state_.domain_hash);
      state_.role = UrlRole::QueryKey;
      ResetComponent();
    } else {
      UpdateComponent(byte, symbol);
    }
    return;
  }

  if (state_.role == UrlRole::Port) {
    if (symbol == UrlSymbol::Slash) {
      state_.role = UrlRole::PathSegment;
      state_.segment_index = 0;
      ResetComponent();
    } else if (symbol == UrlSymbol::Question) {
      state_.endpoint_hash = Mix(state_.domain_hash);
      state_.role = UrlRole::QueryKey;
      ResetComponent();
    } else {
      UpdateComponent(byte, symbol);
    }
    return;
  }

  if (state_.role == UrlRole::PathSegment ||
      state_.role == UrlRole::Extension) {
    if (symbol == UrlSymbol::Slash) {
      FinishPathSegment();
      if (state_.segment_index < 65535) ++state_.segment_index;
      state_.role = UrlRole::PathSegment;
    } else if (symbol == UrlSymbol::Question) {
      FinishPathSegment();
      state_.role = UrlRole::QueryKey;
    } else if (symbol == UrlSymbol::Dot &&
               state_.role == UrlRole::PathSegment &&
               state_.component_length) {
      state_.current_segment_hash = component_hash_;
      state_.role = UrlRole::Extension;
      state_.extension_candidate = true;
      UpdateComponent(byte, symbol);
    } else {
      UpdateComponent(byte, symbol);
    }
    return;
  }

  if (state_.role == UrlRole::QueryKey) {
    if (symbol == UrlSymbol::Equals) {
      FinishQueryKey();
      state_.role = UrlRole::QueryValue;
    } else if (symbol == UrlSymbol::Ampersand) {
      FinishQueryKey();
      if (state_.query_pair_index < 65535) ++state_.query_pair_index;
    } else {
      UpdateComponent(byte, symbol);
    }
    return;
  }

  if (state_.role == UrlRole::QueryValue) {
    if (symbol == UrlSymbol::Ampersand) {
      FinishQueryValue();
      if (state_.query_pair_index < 65535) ++state_.query_pair_index;
      state_.role = UrlRole::QueryKey;
    } else {
      UpdateComponent(byte, symbol);
      state_.query_value_hash = component_hash_;
    }
    return;
  }

  UpdateComponent(byte, symbol);
}

void UrlContext::Refresh() {
  syntax_gate_ = state_.confidence != UrlConfidence::Outside;
  active_gate_ = state_.confidence == UrlConfidence::UrlConfirmed;
  relation_gate_ = active_gate_ &&
      (state_.domain_hash || state_.endpoint_hash ||
       state_.previous_segment_hash || state_.query_key_hash);
  template_gate_ = active_gate_ &&
      (state_.path_template_hash || state_.component_length);

  uint64_t context = static_cast<uint8_t>(state_.confidence);
  context = AddField(context, static_cast<uint8_t>(state_.role));
  context = AddField(context, static_cast<uint8_t>(state_.scheme));
  context = AddField(context, state_.position_bucket);
  context = AddField(context, state_.previous_symbol_class);
  context = AddField(context, state_.percent_position);
  syntax_context_ = context;

  context = static_cast<uint8_t>(state_.role);
  context = AddField(context, static_cast<uint8_t>(state_.component_class));
  const uint64_t lexical_hash = state_.role == UrlRole::QueryValue
      ? component_prefix_hash_
      : component_hash_;
  context = AddField(context, lexical_hash & 0xffff);
  context = AddField(context, state_.position_bucket);
  context = AddField(context, state_.previous_symbol_class);
  component_context_ = context;

  context = state_.domain_hash;
  if (state_.role == UrlRole::Domain) {
    context = AddField(state_.scheme_hash, state_.domain_label_hash);
    context = AddField(context, state_.domain_label_index);
    context = AddField(context, component_prefix_hash_ & 0xffff);
    context = AddField(context, state_.position_bucket);
  } else if (state_.role == UrlRole::PathSegment ||
             state_.role == UrlRole::Extension) {
    context = AddField(context, state_.segment_index);
    context = AddField(context, state_.previous_segment_hash);
    context = AddField(context,
        static_cast<uint8_t>(state_.component_class));
    context = AddField(context, component_prefix_hash_ & 0xffff);
    context = AddField(context, state_.position_bucket);
  } else if (state_.role == UrlRole::QueryKey) {
    context = AddField(state_.endpoint_hash, state_.query_pair_index);
    context = AddField(context, state_.previous_query_key_hash);
    context = AddField(context, component_prefix_hash_ & 0xffff);
    context = AddField(context, state_.position_bucket);
  } else if (state_.role == UrlRole::QueryValue) {
    context = AddField(state_.endpoint_hash, state_.query_key_hash);
    context = AddField(context, static_cast<uint8_t>(state_.value_class));
    context = AddField(context, state_.position_bucket);
    context = AddField(context, state_.previous_symbol_class);
  }
  relation_context_ = context;

  context = AddField(state_.domain_hash, state_.path_template_hash);
  context = AddField(context, static_cast<uint8_t>(state_.role));
  context = AddField(context, static_cast<uint8_t>(state_.component_class));
  context = AddField(context, state_.query_key_hash);
  context = AddField(context, state_.position_bucket);
  template_context_ = context;

  context = AddField(state_.domain_hash, state_.endpoint_hash);
  context = AddField(context, static_cast<uint8_t>(state_.role));
  context = AddField(context, state_.query_key_hash);
  match_context_ = context;

  mixer_context_ = static_cast<uint8_t>(state_.confidence);
  mixer_context_ = mixer_context_ * 12 + static_cast<uint8_t>(state_.role);
  mixer_context_ = mixer_context_ * 8 + state_.position_bucket;
  mixer_context_ = mixer_context_ * 12 +
      static_cast<uint8_t>(state_.component_class);
  mixer_context_ = mixer_context_ * 8 + static_cast<uint8_t>(state_.scheme);
  mixer_context_ = (mixer_context_ * 2 + (state_.percent_position != 0)) & 1023;
}

void UrlContext::Update(uint8_t byte) {
  const UrlSymbol symbol = NormalizeUrlSymbol(byte);
  const uint8_t canonical = CanonicalByte(byte, symbol);
  recent_hash_ = (recent_hash_ << 8) | canonical;

  if (state_.confidence == UrlConfidence::SchemeCandidate) {
    UpdateCandidate(byte, symbol);
  } else if (state_.confidence == UrlConfidence::UrlConfirmed) {
    UpdateConfirmed(byte, symbol);
  } else {
    if (HasSuffix(recent_hash_, "https", 5)) {
      StartScheme(SchemeClass::Https, 5);
    } else if (HasSuffix(recent_hash_, "http", 4)) {
      StartScheme(SchemeClass::Http, 4);
    } else if (HasSuffix(recent_hash_, "ftp", 3)) {
      StartScheme(SchemeClass::Ftp, 3);
    } else if (HasSuffix(recent_hash_, "mailto", 6)) {
      StartScheme(SchemeClass::Mailto, 6);
    } else if (HasSuffix(recent_hash_, "www", 3)) {
      StartScheme(SchemeClass::Www, 3);
    } else if (byte == '/' && previous_byte_ == '/' &&
               (byte_before_previous_ == 0 ||
                NormalizeUrlSymbol(byte_before_previous_) ==
                    UrlSymbol::Whitespace ||
                NormalizeUrlSymbol(byte_before_previous_) ==
                    UrlSymbol::Equals ||
                NormalizeUrlSymbol(byte_before_previous_) ==
                    UrlSymbol::OpenBracket)) {
      Confirm(SchemeClass::ProtocolRelative, UrlRole::Domain);
      state_.url_length = 2;
    } else {
      if (HasSuffix(recent_hash_, "url=", 4) ||
          HasSuffix(recent_hash_, "website=", 8) ||
          HasSuffix(recent_hash_, "homepage=", 8)) {
        marker_allowance_ = 64;
      } else if (marker_allowance_) {
        --marker_allowance_;
      }
      const bool bare_allowed =
          marker_allowance_ || html_attribute_depth_ || external_link_depth_;
      if (bare_allowed && symbol == UrlSymbol::Dot &&
          (NormalizeUrlSymbol(previous_byte_) == UrlSymbol::AlphaLower ||
           NormalizeUrlSymbol(previous_byte_) == UrlSymbol::AlphaUpper ||
           NormalizeUrlSymbol(previous_byte_) == UrlSymbol::Digit)) {
        Confirm(SchemeClass::Unknown, UrlRole::Domain);
        state_.domain_hash = Mix(recent_hash_ >> 8);
        state_.domain_label_hash = recent_hash_ >> 8;
        state_.domain_label_index = 1;
        state_.url_length = 2;
      }
    }
  }

  byte_before_previous_ = previous_byte_;
  previous_byte_ = byte;
  if (preprocess::IsSemanticLessThan(byte)) html_attribute_depth_ = 1;
  if (preprocess::IsSemanticGreaterThan(byte)) html_attribute_depth_ = 0;
  if (byte == '[') external_link_depth_ = 1;
  if (byte == ']') external_link_depth_ = 0;
  Refresh();
}
