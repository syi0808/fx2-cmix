#ifndef URL_CONTEXT_H
#define URL_CONTEXT_H

#include <cstdint>

enum class UrlSymbol : uint8_t {
  AlphaLower,
  AlphaUpper,
  Digit,
  HighByte,
  Colon,
  Slash,
  Dot,
  Question,
  Ampersand,
  Equals,
  Hash,
  Percent,
  At,
  Hyphen,
  Underscore,
  Tilde,
  Plus,
  Quote,
  OpenBracket,
  CloseBracket,
  Whitespace,
  Newline,
  Other,
};

enum class UrlConfidence : uint8_t {
  Outside,
  SchemeCandidate,
  UrlConfirmed,
};

enum class SchemeClass : uint8_t {
  Unknown,
  Http,
  Https,
  Ftp,
  Mailto,
  ProtocolRelative,
  Www,
};

enum class UrlRole : uint8_t {
  Outside,
  Scheme,
  Authority,
  UserInfo,
  Domain,
  Port,
  PathSegment,
  Extension,
  QueryKey,
  QueryValue,
  Fragment,
};

enum class ComponentClass : uint8_t {
  Empty,
  Alphabetic,
  Numeric,
  Hex,
  UuidLike,
  DateLike,
  Slug,
  PercentEncoded,
  Base64Like,
  FileLike,
  Mixed,
};

enum class ValueClass : uint8_t {
  Empty,
  Alphabetic,
  Numeric,
  Hex,
  Structured,
  PercentEncoded,
  Mixed,
};

struct UrlState {
  UrlConfidence confidence = UrlConfidence::Outside;
  UrlRole role = UrlRole::Outside;
  SchemeClass scheme = SchemeClass::Unknown;

  uint16_t url_length = 0;
  uint16_t component_length = 0;
  uint16_t segment_index = 0;
  uint16_t query_pair_index = 0;
  uint8_t domain_label_index = 0;

  uint64_t scheme_hash = 0;
  uint64_t domain_hash = 0;
  uint64_t domain_label_hash = 0;
  uint64_t current_segment_hash = 0;
  uint64_t previous_segment_hash = 0;
  uint64_t path_hash = 0;
  uint64_t path_template_hash = 0;
  uint64_t endpoint_hash = 0;
  uint64_t query_key_hash = 0;
  uint64_t previous_query_key_hash = 0;
  uint64_t query_value_hash = 0;
  uint64_t extension_hash = 0;

  ComponentClass component_class = ComponentClass::Empty;
  ValueClass value_class = ValueClass::Empty;
  uint8_t percent_position = 0;
  uint8_t position_bucket = 0;
  uint8_t previous_symbol_class = 0;
  uint8_t paren_depth = 0;

  bool in_ipv6 = false;
  bool extension_candidate = false;
  bool html_entity = false;
};

UrlSymbol NormalizeUrlSymbol(uint8_t byte);
const char* UrlRoleName(UrlRole role);

class UrlContext {
 public:
  UrlContext();

  void Update(uint8_t byte);

  const UrlState& State() const { return state_; }
  const uint64_t& SyntaxContext() const { return syntax_context_; }
  const uint64_t& ComponentContext() const { return component_context_; }
  const uint64_t& RelationContext() const { return relation_context_; }
  const uint64_t& TemplateContext() const { return template_context_; }
  const uint64_t& MatchContext() const { return match_context_; }
  const uint64_t& MixerContext() const { return mixer_context_; }
  const bool& SyntaxGate() const { return syntax_gate_; }
  const bool& ActiveGate() const { return active_gate_; }
  const bool& RelationGate() const { return relation_gate_; }
  const bool& TemplateGate() const { return template_gate_; }

 private:
  void Reset();
  void StartScheme(SchemeClass scheme, uint8_t length);
  void Confirm(SchemeClass scheme, UrlRole role);
  void UpdateCandidate(uint8_t byte, UrlSymbol symbol);
  void UpdateConfirmed(uint8_t byte, UrlSymbol symbol);
  void UpdateComponent(uint8_t byte, UrlSymbol symbol);
  void FinishDomainLabel();
  void FinishPathSegment();
  void FinishQueryKey();
  void FinishQueryValue();
  void ResetComponent();
  void Refresh();
  bool ShouldTerminate(uint8_t byte, UrlSymbol symbol) const;

  UrlState state_;
  uint64_t syntax_context_ = 0;
  uint64_t component_context_ = 0;
  uint64_t relation_context_ = 0;
  uint64_t template_context_ = 0;
  uint64_t match_context_ = 0;
  uint64_t mixer_context_ = 0;
  uint64_t recent_hash_ = 0;
  uint64_t component_hash_ = 0;
  uint64_t component_prefix_hash_ = 0;
  uint64_t completed_path_hash_ = 0;
  uint64_t completed_template_hash_ = 0;
  uint32_t component_flags_ = 0;
  uint8_t candidate_slashes_ = 0;
  uint8_t marker_allowance_ = 0;
  uint8_t html_attribute_depth_ = 0;
  uint8_t external_link_depth_ = 0;
  uint8_t previous_byte_ = 0;
  uint8_t byte_before_previous_ = 0;
  bool syntax_gate_ = false;
  bool active_gate_ = false;
  bool relation_gate_ = false;
  bool template_gate_ = false;
};

#endif
