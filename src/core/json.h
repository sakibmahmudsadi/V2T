// A small JSON reader/writer.
//
// Enough for two jobs: the config file and the control messages between the
// controller and the worker. Deliberately not a general-purpose library — no
// dependencies, no exceptions, and it fails with a message instead of throwing.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace pt {
namespace json {

class Value;
using ValuePtr = std::shared_ptr<Value>;

class Value {
 public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  Type type = Type::Null;
  bool boolean = false;
  double number = 0.0;
  std::string text;
  std::vector<ValuePtr> items;
  std::vector<std::pair<std::string, ValuePtr>> members;

  bool is_null() const { return type == Type::Null; }
  bool is_object() const { return type == Type::Object; }
  bool is_array() const { return type == Type::Array; }
  bool is_string() const { return type == Type::String; }
  bool is_number() const { return type == Type::Number; }
  bool is_bool() const { return type == Type::Bool; }

  // Object lookup; returns nullptr when absent.
  const Value* find(const std::string& key) const;

  // Typed getters with defaults.
  std::string as_string(const std::string& fallback = std::string()) const;
  double as_number(double fallback = 0.0) const;
  bool as_bool(bool fallback = false) const;

  // Array of strings; ignores non-string entries.
  std::vector<std::string> as_string_list() const;
};

// Parses `input`. On failure returns nullptr and fills `err`.
ValuePtr parse(const std::string& input, std::string& err);

// --- building ---------------------------------------------------------------

// Escapes a string for use inside JSON.
std::string quote(const std::string& value);

// Serialises a value (objects keep insertion order).
std::string stringify(const Value& value);

// Convenience builders used by the control protocol.
ValuePtr make_string(const std::string& value);
ValuePtr make_number(double value);
ValuePtr make_bool(bool value);
ValuePtr make_object();
void set(const ValuePtr& object, const std::string& key, const ValuePtr& value);

}  // namespace json
}  // namespace pt
