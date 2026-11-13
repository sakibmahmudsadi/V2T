#include "core/text.h"

#include <cctype>

namespace pt {
namespace {

bool is_word_char(char c) {
  unsigned char u = static_cast<unsigned char>(c);
  return std::isalnum(u) != 0 || c == '_';
}

char lower(char c) {
  unsigned char u = static_cast<unsigned char>(c);
  if (u < 128) return static_cast<char>(std::tolower(u));
  return c;
}

// Case-insensitive comparison of text[pos, pos+len) with an ASCII needle.
bool region_iequals(const std::string& text, size_t pos, const std::string& needle) {
  if (pos + needle.size() > text.size()) return false;
  for (size_t i = 0; i < needle.size(); ++i) {
    if (lower(text[pos + i]) != lower(needle[i])) return false;
  }
  return true;
}

}  // namespace

std::vector<std::string> default_filler_words() {
  return {
      "uh",  "uhm", "umm", "uhh", "uhhh", "ehh", "ehm",
      "ahm", "hmm", "hm",  "mmm", "um",  "ah",  "eh",
  };
}

std::string remove_filler_words(const std::string& text,
                                const std::vector<std::string>& fillers) {
  if (text.empty() || fillers.empty()) return text;

  std::string out;
  out.reserve(text.size());

  size_t last_copy = 0;
  size_t i = 0;
  while (i < text.size()) {
    bool matched = false;

    for (const std::string& filler : fillers) {
      if (filler.empty()) continue;
      if (i + filler.size() > text.size()) continue;
      if (lower(text[i]) != lower(filler[0])) continue;
      if (!region_iequals(text, i, filler)) continue;

      // Whole-word match only: the characters around the hit must not be
      // word characters.
      const bool left_ok = (i == 0) || !is_word_char(text[i - 1]);
      const size_t end = i + filler.size();
      const bool right_ok = (end >= text.size()) || !is_word_char(text[end]);
      if (!left_ok || !right_ok) continue;

      // The punctuation that belonged to the removed word goes with it,
      // exactly like the `\bword\b[,.]?` rule this replaces.
      size_t skip = end;
      if (skip < text.size() && (text[skip] == ',' || text[skip] == '.')) ++skip;

      out.append(text, last_copy, i - last_copy);
      last_copy = skip;
      i = skip;
      matched = true;
      break;
    }

    if (!matched) ++i;
  }

  out.append(text, last_copy, text.size() - last_copy);
  return out;
}

std::string tidy_whitespace(const std::string& text) {
  std::string out;
  out.reserve(text.size());

  bool pending_space = false;
  for (char c : text) {
    const bool space = (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v');
    if (space) {
      if (!out.empty()) pending_space = true;
      continue;
    }
    if (pending_space) {
      out.push_back(' ');
      pending_space = false;
    }
    out.push_back(c);
  }
  return out;
}

std::string append_trailing_space(const std::string& text) {
  if (text.empty()) return text;
  const char last = text.back();
  if (last == ' ' || last == '\t' || last == '\n' || last == '\r') return text;
  return text + " ";
}

}  // namespace pt
