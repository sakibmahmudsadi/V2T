#include "core/config.h"

#include <cstdio>
#include <fstream>
#include <sstream>

#include "core/json.h"
#include "core/text.h"

namespace pt {
namespace {

std::string read_file(const std::string& path, bool& ok) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    ok = false;
    return std::string();
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  ok = true;
  return buffer.str();
}

}  // namespace

bool load_config(const std::string& path, Config& config, std::string& err) {
  config = Config();
  config.filler_words = default_filler_words();
  config.source_path = path;

  bool ok = false;
  const std::string content = read_file(path, ok);
  if (!ok) {
    err = "no config file at " + path + " (using defaults)";
    return false;
  }

  std::string parse_error;
  const json::ValuePtr root = json::parse(content, parse_error);
  if (root == nullptr || !root->is_object()) {
    err = "config file " + path + " is not valid JSON: " + parse_error;
    return false;
  }

  if (const json::Value* value = root->find("model_path")) {
    config.model_path = value->as_string(config.model_path);
  }
  if (const json::Value* value = root->find("engine_dir")) {
    config.engine_dir = value->as_string(config.engine_dir);
  }
  if (const json::Value* value = root->find("backend")) {
    config.backend = value->as_string(config.backend);
  }
  if (const json::Value* value = root->find("hotkey")) {
    const std::vector<std::string> keys = value->as_string_list();
    if (!keys.empty()) config.hotkey = keys;
  }
  if (const json::Value* value = root->find("microphone")) {
    config.microphone = value->as_string(config.microphone);
  }
  if (const json::Value* value = root->find("cancel_key")) {
    config.cancel_key = value->as_string(config.cancel_key);
  }
  if (const json::Value* value = root->find("model_unload_seconds")) {
    config.model_unload_seconds = static_cast<int>(value->as_number(config.model_unload_seconds));
  }
  if (const json::Value* value = root->find("max_buffer_seconds")) {
    config.max_buffer_seconds = static_cast<int>(value->as_number(config.max_buffer_seconds));
  }
  if (const json::Value* value = root->find("filler_removal")) {
    config.filler_removal = value->as_bool(config.filler_removal);
  }
  if (const json::Value* value = root->find("filler_words")) {
    const std::vector<std::string> words = value->as_string_list();
    if (!words.empty()) config.filler_words = words;
  }
  if (const json::Value* value = root->find("append_trailing_space")) {
    config.append_trailing_space = value->as_bool(config.append_trailing_space);
  }
  if (const json::Value* value = root->find("auto_submit")) {
    config.auto_submit = value->as_bool(config.auto_submit);
  }
  if (const json::Value* value = root->find("mic_button_edge")) {
    config.mic_button_edge = value->as_string(config.mic_button_edge);
  }
  if (const json::Value* value = root->find("log_path")) {
    config.log_path = value->as_string(config.log_path);
  }
  if (const json::Value* value = root->find("log_level")) {
    config.log_level = value->as_string(config.log_level);
  }

  if (config.model_unload_seconds < 0) config.model_unload_seconds = 0;
  if (config.max_buffer_seconds < 5) config.max_buffer_seconds = 5;
  return true;
}

bool save_config(const std::string& path, const Config& config, std::string& err) {
  const json::ValuePtr root = json::make_object();
  json::set(root, "model_path", json::make_string(config.model_path));
  json::set(root, "engine_dir", json::make_string(config.engine_dir));
  json::set(root, "backend", json::make_string(config.backend));

  const json::ValuePtr hotkey = std::make_shared<json::Value>();
  hotkey->type = json::Value::Type::Array;
  for (const std::string& key : config.hotkey) {
    hotkey->items.push_back(json::make_string(key));
  }
  json::set(root, "hotkey", hotkey);
  json::set(root, "cancel_key", json::make_string(config.cancel_key));
  json::set(root, "model_unload_seconds", json::make_number(config.model_unload_seconds));
  json::set(root, "max_buffer_seconds", json::make_number(config.max_buffer_seconds));
  json::set(root, "filler_removal", json::make_bool(config.filler_removal));
  json::set(root, "append_trailing_space", json::make_bool(config.append_trailing_space));
  json::set(root, "auto_submit", json::make_bool(config.auto_submit));
  json::set(root, "mic_button_edge", json::make_string(config.mic_button_edge));
  json::set(root, "log_path", json::make_string(config.log_path));
  json::set(root, "log_level", json::make_string(config.log_level));

  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    err = "cannot write " + path;
    return false;
  }
  file << "{\n";
  file << "  \"model_path\": " << json::quote(config.model_path) << ",\n";
  file << "  \"engine_dir\": " << json::quote(config.engine_dir) << ",\n";
  file << "  \"backend\": " << json::quote(config.backend) << ",\n";
  file << "  \"hotkey\": [";
  for (size_t i = 0; i < config.hotkey.size(); ++i) {
    if (i != 0) file << ", ";
    file << json::quote(config.hotkey[i]);
  }
  file << "],\n";
  file << "  \"cancel_key\": " << json::quote(config.cancel_key) << ",\n";
  file << "  \"model_unload_seconds\": " << config.model_unload_seconds << ",\n";
  file << "  \"max_buffer_seconds\": " << config.max_buffer_seconds << ",\n";
  file << "  \"filler_removal\": " << (config.filler_removal ? "true" : "false") << ",\n";
  file << "  \"append_trailing_space\": " << (config.append_trailing_space ? "true" : "false")
       << ",\n";
  file << "  \"auto_submit\": " << (config.auto_submit ? "true" : "false") << ",\n";
  file << "  \"mic_button_edge\": " << json::quote(config.mic_button_edge) << ",\n";
  file << "  \"log_path\": " << json::quote(config.log_path) << ",\n";
  file << "  \"log_level\": " << json::quote(config.log_level) << "\n";
  file << "}\n";
  return true;
}

}  // namespace pt
