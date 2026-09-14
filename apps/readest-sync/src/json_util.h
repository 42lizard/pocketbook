#pragma once
#include <json-c/json.h>
#include <cctype>
#include <memory>
#include <stdexcept>
#include <string>

namespace readest {
long long record_timestamp(json_object* row, const char* key);
using Json = std::unique_ptr<json_object, decltype(&json_object_put)>;
inline Json parse_json(const std::string& input) {
    if (input.empty() || input.size() > 4 * 1024 * 1024)
        throw std::runtime_error("Invalid JSON response size");
    json_tokener* tok = json_tokener_new();
    if (!tok) throw std::runtime_error("Cannot allocate JSON parser");
    Json value(json_tokener_parse_ex(tok, input.c_str(), static_cast<int>(input.size())), json_object_put);
    bool parsed = json_tokener_get_error(tok) == json_tokener_success;
    size_t consumed = static_cast<size_t>(tok->char_offset);
    json_tokener_free(tok);
    while (consumed < input.size() && std::isspace(static_cast<unsigned char>(input[consumed]))) ++consumed;
    if (!parsed || !value || consumed != input.size()) throw std::runtime_error("Invalid JSON response");
    return value;
}
inline json_object* member(json_object* object, const char* key) {
    json_object* value = nullptr;
    if (!object || json_object_get_type(object) != json_type_object ||
        !json_object_object_get_ex(object, key, &value)) return nullptr;
    return value;
}
inline std::string string_member(json_object* object, const char* key) {
    auto* value = member(object, key);
    if (!value || json_object_get_type(value) != json_type_string)
        throw std::runtime_error("Missing or invalid JSON string field");
    return std::string(json_object_get_string(value), json_object_get_string_len(value));
}
inline long long integer_member(json_object* object, const char* key) {
    auto* value = member(object, key);
    if (!value || json_object_get_type(value) != json_type_int)
        throw std::runtime_error("Missing or invalid JSON integer field");
    return json_object_get_int64(value);
}
inline std::string json_text(json_object* object) {
    return json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN);
}
} // namespace readest
