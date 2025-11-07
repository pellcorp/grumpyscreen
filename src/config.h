// config.h
#pragma once
#include "hv/json.hpp"
#include <string>
#include <vector>
#include <variant>
#include <fstream>
#include <iomanip>
#include <type_traits>

class Config {
public:
    using json = nlohmann::json;

    static Config* get_instance() {
        static Config inst; return &inst;
    }

    bool load(const std::string& file) {
        path_ = file;
        std::ifstream in(file);
        if (!in.is_open()) { j_ = json::object(); return false; }
        try { in >> j_; } catch (...) { j_ = json::object(); return false; }
        return true;
    }

    bool save() const {
        if (path_.empty()) return false;
        std::ofstream out(path_);
        if (!out.is_open()) return false;
        out << std::setw(2) << j_ << '\n';
        return true;
    }

    template <typename T>
    T get(const std::string& json_ptr, T def = T{}) const {
        const auto jp = json::json_pointer(json_ptr);
        if (!j_.contains(jp)) return def;
        const auto& v = j_.at(jp);
        if constexpr (std::is_same_v<T, std::string>) {
            return v.is_string() ? v.get<std::string>() : def;
        } else if constexpr (std::is_same_v<T, bool>) {
            return v.is_boolean() ? v.get<bool>() : def;
        } else if constexpr (std::is_integral_v<T>) {
            return v.is_number_integer() ? static_cast<T>(v.get<long long>()) : def;
        } else {
            static_assert(!sizeof(T*), "unsupported T"); // hard fail at compile-time
        }
    }

    template <typename T>
    void set(const std::string& json_ptr, const T& value) {
        auto jp = json::json_pointer(json_ptr);
        j_[jp] = value;
    }

    // returns vector<json> (each element is an object), never null
    std::vector<json> get_objects(const std::string& json_ptr) const {
        std::vector<json> out;
        const auto jp = json::json_pointer(json_ptr);
        if (!j_.contains(jp)) return out;
        const auto& arr = j_.at(jp);
        if (!arr.is_array()) return out;
        out.reserve(arr.size());
        for (const auto& el : arr) {
            if (el.is_object()) out.push_back(el);
        }
        return out;
    }

    const json& raw() const { return j_; }
    json& raw_mut() { return j_; }
    const std::string& get_path() const { return path_; }

private:
    Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    std::string path_;
    json j_;
};
