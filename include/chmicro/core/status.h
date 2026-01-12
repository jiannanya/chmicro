#pragma once

#include <optional>
#include <string>
#include <utility>

namespace chmicro {

enum class StatusCode {
    ok = 0,
    invalid_argument,
    not_found,
    timeout,
    unavailable,
    cancelled,
    internal_error,
};

class Status {
public:
    Status() : code_(StatusCode::ok) {}
    Status(StatusCode code, std::string message) : code_(code), message_(std::move(message)) {}

    static Status Ok() { return Status(); }

    bool ok() const { return code_ == StatusCode::ok; }
    StatusCode code() const { return code_; }
    const std::string& message() const { return message_; }

private:
    StatusCode code_;
    std::string message_;
};

template <class T>
class Result {
public:
    Result(T value) : status_(Status::Ok()), value_(std::move(value)) {}
    Result(Status status) : status_(std::move(status)) {}

    bool ok() const { return status_.ok(); }
    const Status& status() const { return status_; }

    const T& value() const& { return *value_; }
    T& value() & { return *value_; }
    T&& value() && { return std::move(*value_); }

private:
    Status status_;
    std::optional<T> value_;
};

} // namespace chmicro
