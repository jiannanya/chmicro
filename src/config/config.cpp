#include <chmicro/config/config.h>

#include <fstream>
#include <sstream>

namespace chmicro::config {

namespace {

const char* ErrorCodeToString(chjson::error_code code) {
    switch (code) {
        case chjson::error_code::ok: return "ok";
        case chjson::error_code::unexpected_eof: return "unexpected_eof";
        case chjson::error_code::invalid_value: return "invalid_value";
        case chjson::error_code::invalid_number: return "invalid_number";
        case chjson::error_code::invalid_string: return "invalid_string";
        case chjson::error_code::invalid_escape: return "invalid_escape";
        case chjson::error_code::invalid_unicode_escape: return "invalid_unicode_escape";
        case chjson::error_code::invalid_utf16_surrogate: return "invalid_utf16_surrogate";
        case chjson::error_code::expected_colon: return "expected_colon";
        case chjson::error_code::expected_comma_or_end: return "expected_comma_or_end";
        case chjson::error_code::expected_key_string: return "expected_key_string";
        case chjson::error_code::trailing_characters: return "trailing_characters";
        case chjson::error_code::nesting_too_deep: return "nesting_too_deep";
        case chjson::error_code::out_of_memory: return "out_of_memory";
    }
    return "unknown";
}

std::string FormatParseError(const chjson::error& e) {
    std::ostringstream oss;
    oss << "invalid json: " << ErrorCodeToString(e.code)
        << " at line " << e.line << ", col " << e.column;
    return oss.str();
}

} // namespace

chmicro::Result<Config> Config::LoadFile(std::string path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        return chmicro::Status(chmicro::StatusCode::not_found, "config file not found");
    }

    std::ostringstream ss;
    ss << ifs.rdbuf();
    auto text = ss.str();

    auto r = chjson::parse(text);
    if (r.err) {
        return chmicro::Status(chmicro::StatusCode::invalid_argument, FormatParseError(r.err));
    }

    if (!r.doc.root().is_object()) {
        return chmicro::Status(chmicro::StatusCode::invalid_argument, "config root must be a JSON object");
    }

    Config c;
    c.doc_ = std::move(r.doc);
    return c;
}

bool Config::Has(std::string_view key) const {
    return doc_.root().find(key) != nullptr;
}

chmicro::Result<std::string> Config::GetString(std::string_view key) const {
    const auto* v = doc_.root().find(key);
    if (v == nullptr) {
        return chmicro::Status(chmicro::StatusCode::not_found, "missing key");
    }
    if (!v->is_string()) {
        return chmicro::Status(chmicro::StatusCode::invalid_argument, "not a string");
    }
    return std::string(v->as_string_view());
}

chmicro::Result<int> Config::GetInt(std::string_view key) const {
    const auto* v = doc_.root().find(key);
    if (v == nullptr) {
        return chmicro::Status(chmicro::StatusCode::not_found, "missing key");
    }
    if (!v->is_number() || !v->is_int()) {
        return chmicro::Status(chmicro::StatusCode::invalid_argument, "not an int");
    }
    return static_cast<int>(v->as_int());
}

} // namespace chmicro::config
