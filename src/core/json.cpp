#include "core/json.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace pt {
namespace json {
namespace {

class Parser {
 public:
  Parser(const std::string& text, std::string& err) : text_(text), err_(err) {}

  ValuePtr parse_value() {
    skip_space();
    if (pos_ >= text_.size()) {
      fail("unexpected end of input");
      return nullptr;
    }
    const char c = text_[pos_];
    switch (c) {
      case '{': return parse_object();
      case '[': return parse_array();
      case '"': return parse_string_value();
      case 't': case 'f': return parse_bool();
      case 'n': return parse_null();
      default:
        if (c == '-' || (c >= '0' && c <= '9')) return parse_number();
        fail(std::string("unexpected character '") + c + "'");
        return nullptr;
    }
  }

  bool at_end() {
    skip_space();
    return pos_ >= text_.size();
  }

 private:
  void fail(const std::string& message) {
    if (err_.empty()) {
      err_ = message + " at offset " + std::to_string(pos_);
    }
  }

  void skip_space() {
    while (pos_ < text_.size()) {
      const char c = text_[pos_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++pos_;
      } else if (c == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '/') {
        // Line comment — handy for a hand-edited config file.
        while (pos_ < text_.size() && text_[pos_] != '\n') ++pos_;
      } else {
        break;
      }
    }
  }

  ValuePtr parse_object() {
    auto value = std::make_shared<Value>();
    value->type = Value::Type::Object;
    ++pos_;  // '{'
    skip_space();
    if (pos_ < text_.size() && text_[pos_] == '}') {
      ++pos_;
      return value;
    }
    while (true) {
      skip_space();
      if (pos_ >= text_.size() || text_[pos_] != '"') {
        fail("expected a key string");
        return nullptr;
      }
      const ValuePtr key = parse_string_value();
      if (key == nullptr) return nullptr;
      skip_space();
      if (pos_ >= text_.size() || text_[pos_] != ':') {
        fail("expected ':'");
        return nullptr;
      }
      ++pos_;
      const ValuePtr item = parse_value();
      if (item == nullptr) return nullptr;
      value->members.emplace_back(key->text, item);
      skip_space();
      if (pos_ < text_.size() && text_[pos_] == ',') {
        ++pos_;
        continue;
      }
      if (pos_ < text_.size() && text_[pos_] == '}') {
        ++pos_;
        return value;
      }
      fail("expected ',' or '}'");
      return nullptr;
    }
  }

  ValuePtr parse_array() {
    auto value = std::make_shared<Value>();
    value->type = Value::Type::Array;
    ++pos_;  // '['
    skip_space();
    if (pos_ < text_.size() && text_[pos_] == ']') {
      ++pos_;
      return value;
    }
    while (true) {
      const ValuePtr item = parse_value();
      if (item == nullptr) return nullptr;
      value->items.push_back(item);
      skip_space();
      if (pos_ < text_.size() && text_[pos_] == ',') {
        ++pos_;
        continue;
      }
      if (pos_ < text_.size() && text_[pos_] == ']') {
        ++pos_;
        return value;
      }
      fail("expected ',' or ']'");
      return nullptr;
    }
  }

  ValuePtr parse_string_value() {
    auto value = std::make_shared<Value>();
    value->type = Value::Type::String;
    ++pos_;  // opening quote
    while (pos_ < text_.size()) {
      const char c = text_[pos_++];
      if (c == '"') return value;
      if (c != '\\') {
        value->text.push_back(c);
        continue;
      }
      if (pos_ >= text_.size()) break;
      const char escape = text_[pos_++];
      switch (escape) {
        case '"': value->text.push_back('"'); break;
        case '\\': value->text.push_back('\\'); break;
        case '/': value->text.push_back('/'); break;
        case 'b': value->text.push_back('\b'); break;
        case 'f': value->text.push_back('\f'); break;
        case 'n': value->text.push_back('\n'); break;
        case 'r': value->text.push_back('\r'); break;
        case 't': value->text.push_back('\t'); break;
        case 'u': {
          if (pos_ + 4 > text_.size()) {
            fail("truncated \\u escape");
            return nullptr;
          }
          const unsigned code = static_cast<unsigned>(
              std::strtoul(text_.substr(pos_, 4).c_str(), nullptr, 16));
          pos_ += 4;
          // UTF-8 encode (surrogate pairs are not needed here).
          if (code < 0x80) {
            value->text.push_back(static_cast<char>(code));
          } else if (code < 0x800) {
            value->text.push_back(static_cast<char>(0xC0 | (code >> 6)));
            value->text.push_back(static_cast<char>(0x80 | (code & 0x3F)));
          } else {
            value->text.push_back(static_cast<char>(0xE0 | (code >> 12)));
            value->text.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            value->text.push_back(static_cast<char>(0x80 | (code & 0x3F)));
          }
          break;
        }
        default:
          fail("unknown escape sequence");
          return nullptr;
      }
    }
    fail("unterminated string");
    return nullptr;
  }

  ValuePtr parse_bool() {
    auto value = std::make_shared<Value>();
    value->type = Value::Type::Bool;
    if (text_.compare(pos_, 4, "true") == 0) {
      value->boolean = true;
      pos_ += 4;
    } else if (text_.compare(pos_, 5, "false") == 0) {
      value->boolean = false;
      pos_ += 5;
    } else {
      fail("invalid literal");
      return nullptr;
    }
    return value;
  }

  ValuePtr parse_null() {
    if (text_.compare(pos_, 4, "null") != 0) {
      fail("invalid literal");
      return nullptr;
    }
    pos_ += 4;
    return std::make_shared<Value>();
  }

  ValuePtr parse_number() {
    const size_t start = pos_;
    if (pos_ < text_.size() && text_[pos_] == '-') ++pos_;
    while (pos_ < text_.size() &&
           (std::isdigit(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '.' ||
            text_[pos_] == 'e' || text_[pos_] == 'E' || text_[pos_] == '+' ||
            text_[pos_] == '-')) {
      ++pos_;
    }
    auto value = std::make_shared<Value>();
    value->type = Value::Type::Number;
    value->number = std::strtod(text_.substr(start, pos_ - start).c_str(), nullptr);
    return value;
  }

  const std::string& text_;
  std::string& err_;
  size_t pos_ = 0;
};

}  // namespace

const Value* Value::find(const std::string& key) const {
  for (const auto& member : members) {
    if (member.first == key) return member.second.get();
  }
  return nullptr;
}

std::string Value::as_string(const std::string& fallback) const {
  return type == Type::String ? text : fallback;
}

double Value::as_number(double fallback) const {
  return type == Type::Number ? number : fallback;
}

bool Value::as_bool(bool fallback) const {
  return type == Type::Bool ? boolean : fallback;
}

std::vector<std::string> Value::as_string_list() const {
  std::vector<std::string> result;
  if (type != Type::Array) return result;
  for (const auto& item : items) {
    if (item != nullptr && item->is_string()) result.push_back(item->text);
  }
  return result;
}

ValuePtr parse(const std::string& input, std::string& err) {
  err.clear();
  Parser parser(input, err);
  ValuePtr value = parser.parse_value();
  if (value == nullptr) return nullptr;
  if (!parser.at_end()) {
    err = "trailing characters after the JSON value";
    return nullptr;
  }
  return value;
}

std::string quote(const std::string& value) {
  std::string out = "\"";
  for (char c : value) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buffer[8];
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
          out += buffer;
        } else {
          out.push_back(c);
        }
    }
  }
  out += "\"";
  return out;
}

std::string stringify(const Value& value) {
  switch (value.type) {
    case Value::Type::Null: return "null";
    case Value::Type::Bool: return value.boolean ? "true" : "false";
    case Value::Type::Number: {
      if (value.number == static_cast<double>(static_cast<long long>(value.number))) {
        return std::to_string(static_cast<long long>(value.number));
      }
      char buffer[32];
      std::snprintf(buffer, sizeof(buffer), "%g", value.number);
      return buffer;
    }
    case Value::Type::String: return quote(value.text);
    case Value::Type::Array: {
      std::string out = "[";
      for (size_t i = 0; i < value.items.size(); ++i) {
        if (i != 0) out += ",";
        out += stringify(*value.items[i]);
      }
      return out + "]";
    }
    case Value::Type::Object: {
      std::string out = "{";
      for (size_t i = 0; i < value.members.size(); ++i) {
        if (i != 0) out += ",";
        out += quote(value.members[i].first) + ":" + stringify(*value.members[i].second);
      }
      return out + "}";
    }
  }
  return "null";
}

ValuePtr make_string(const std::string& value) {
  auto result = std::make_shared<Value>();
  result->type = Value::Type::String;
  result->text = value;
  return result;
}

ValuePtr make_number(double value) {
  auto result = std::make_shared<Value>();
  result->type = Value::Type::Number;
  result->number = value;
  return result;
}

ValuePtr make_bool(bool value) {
  auto result = std::make_shared<Value>();
  result->type = Value::Type::Bool;
  result->boolean = value;
  return result;
}

ValuePtr make_object() {
  auto result = std::make_shared<Value>();
  result->type = Value::Type::Object;
  return result;
}

void set(const ValuePtr& object, const std::string& key, const ValuePtr& value) {
  if (object == nullptr || value == nullptr) return;
  object->members.emplace_back(key, value);
}

}  // namespace json
}  // namespace pt
