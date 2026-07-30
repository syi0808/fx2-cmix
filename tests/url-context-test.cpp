#include "../src/contexts/url-context.h"
#include "../src/preprocess/symbols.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

void Feed(UrlContext* context, const std::string& text) {
  for (uint8_t byte : text) context->Update(byte);
}

void ExpectActive(const std::string& text, UrlRole role) {
  UrlContext context;
  Feed(&context, text);
  assert(context.State().confidence == UrlConfidence::UrlConfirmed);
  assert(context.State().role == role);
  assert(context.ActiveGate());
}

void TestNormalization() {
  assert(NormalizeUrlSymbol(':') == UrlSymbol::Colon);
  assert(NormalizeUrlSymbol(preprocess::kWrtColon) == UrlSymbol::Colon);
  assert(NormalizeUrlSymbol('?') == UrlSymbol::Question);
  assert(NormalizeUrlSymbol(preprocess::kWrtQuestion) ==
      UrlSymbol::Question);
  assert(NormalizeUrlSymbol('=') == UrlSymbol::Equals);
  assert(NormalizeUrlSymbol(preprocess::kWrtEquals) == UrlSymbol::Equals);
}

void TestSyntax() {
  ExpectActive("http://example.com", UrlRole::Domain);
  ExpectActive("https://example.com/", UrlRole::PathSegment);
  ExpectActive("ftp://example.org/file.txt", UrlRole::Extension);
  ExpectActive("//cdn.example.org/image.png", UrlRole::Extension);
  ExpectActive("www.example.com/path", UrlRole::PathSegment);
  ExpectActive("url=example.org/path", UrlRole::PathSegment);
  ExpectActive("urlMhttpsJ//example.org/aOqMx", UrlRole::QueryValue);
}

void TestRelationsAndTemplates() {
  UrlContext first;
  UrlContext second;
  Feed(&first, "https://example.com/item/12345");
  Feed(&second, "https://example.com/item/92837");
  assert(first.State().domain_hash == second.State().domain_hash);
  assert(first.State().path_template_hash ==
      second.State().path_template_hash);
  assert(first.State().path_hash != second.State().path_hash);

  UrlContext query;
  Feed(&query, "https://example.com/index.php?title=Entropy&action=edit");
  assert(query.State().role == UrlRole::QueryValue);
  assert(query.State().endpoint_hash != 0);
  assert(query.State().query_key_hash != 0);
  assert(query.RelationGate());
}

void TestSpecialCharacters() {
  ExpectActive("https://example.com/a%20b", UrlRole::PathSegment);
  ExpectActive("https://example.com/Foo_(bar)", UrlRole::PathSegment);
  ExpectActive("https://user@example.com:8080/path",
      UrlRole::PathSegment);
  ExpectActive("https://[2001:db8::1]/index.html", UrlRole::Extension);
}

void TestTermination() {
  UrlContext sentence;
  Feed(&sentence, "See https://example.com/foo. ");
  assert(sentence.State().confidence == UrlConfidence::Outside);
  assert(!sentence.ActiveGate());

  UrlContext bracket;
  Feed(&bracket, "[https://example.com/foo label]");
  assert(bracket.State().confidence == UrlConfidence::Outside);

  UrlContext malformed;
  Feed(&malformed, "https://example.com/");
  Feed(&malformed, std::string(4097, 'a'));
  assert(malformed.State().confidence == UrlConfidence::Outside);
}

}

int main() {
  TestNormalization();
  TestSyntax();
  TestRelationsAndTemplates();
  TestSpecialCharacters();
  TestTermination();
  std::cout << "url-context tests passed\n";
}
