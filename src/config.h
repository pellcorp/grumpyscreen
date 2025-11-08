#pragma once
#include "hv/json.hpp"
#include <string>
#include <vector>
#include <fstream>
#include <cctype>
#include <type_traits>

class Config {
public:
    using json = nlohmann::json;

    static Config* get_instance() { static Config inst; return &inst; }

    bool load(const std::string& ini_path) {
        path_ = ini_path;
        j_ = json::object();
        return load_ini(ini_path, j_);
    }

    template <typename T>
    T get(const std::string& json_ptr, T def = T{}) const {
        const auto jp = json::json_pointer(json_ptr);
        if (!j_.contains(jp)) return def;
        const json& v = j_.at(jp);
        if constexpr (std::is_same_v<T, std::string>) return v.is_string() ? v.get<std::string>() : def;
        else if constexpr (std::is_same_v<T, bool>)   return v.is_boolean() ? v.get<bool>() : def;
        else if constexpr (std::is_integral_v<T>)     return v.is_number_integer() ? static_cast<T>(v.get<long long>()) : def;
        else static_assert(!sizeof(T*), "unsupported T");
    }

    std::vector<json> get_objects(const std::string& json_ptr) const {
        std::vector<json> out;
        const auto jp = json::json_pointer(json_ptr);
        if (!j_.contains(jp)) return out;
        const json& a = j_.at(jp);
        if (!a.is_array()) return out;
        out.reserve(a.size());
        for (const auto& el : a) if (el.is_object()) out.push_back(el);
        return out;
    }

    const std::string& get_path() const { return path_; }

private:
    Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    static void trim(std::string& s) {
        size_t b = 0; while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
        size_t e = s.size(); while (e > b && std::isspace(static_cast<unsigned char>(s[e-1]))) --e;
        s.assign(s.data()+b, e-b);
    }
    static std::string unquote(std::string s) {
        if (s.size() >= 2 && ((s.front()=='"' && s.back()=='"') || (s.front()=='\'' && s.back()=='\'')))
            return s.substr(1, s.size()-2);
        return s;
    }
    static bool parse_bool(const std::string& s, bool& out) {
        std::string t; t.reserve(s.size());
        for (char c: s) t.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        if (t=="true"||t=="1"||t=="yes"||t=="on")  { out = true;  return true; }
        if (t=="false"||t=="0"||t=="no"||t=="off") { out = false; return true; }
        return false;
    }
    static bool parse_int(const std::string& s, long long& out) {
        if (s.empty()) return false;
        char* end = nullptr; errno = 0;
        long long v = std::strtoll(s.c_str(), &end, 10);
        if (errno || end==s.c_str() || *end!='\0') return false;
        out = v; return true;
    }
    static void set_typed(json& obj, const std::string& k, const std::string& val) {
        bool bv; long long iv;
        if (parse_bool(val, bv)) obj[k] = bv;
        else if (parse_int(val, iv)) obj[k] = iv;
        else obj[k] = val;
    }

    // INI: [section] or [group "id"]; key [:|=] value; comments start with # or ;
    static bool load_ini(const std::string& path, json& out) {
        std::ifstream in(path);
        if (!in.is_open()) return false;

        std::string line;
        std::string cur_section; // for [section]
        std::string cur_group;   // for [group "id"]
        std::string cur_id;
        json cur_obj = json::object();
        auto flush_group = [&](){
            if (!cur_group.empty() && !cur_id.empty()) {
                cur_obj["id"] = cur_id;
                out[cur_group].push_back(cur_obj);
            }
            cur_group.clear(); cur_id.clear(); cur_obj = json::object();
        };

        while (std::getline(in, line)) {
            if (!line.empty() && line.back()=='\r') line.pop_back();

            // strip line comments if the first non-space is # or ;
            std::string s = line;
            size_t i = 0; while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
            if (i < s.size() && (s[i]=='#' || s[i]==';')) continue;
            size_t cpos = s.find_first_of("#;");
            if (cpos != std::string::npos) s.erase(cpos);

            trim(s);
            if (s.empty()) continue;

            if (s.front()=='[' && s.back()==']') {
                flush_group();
                cur_section.clear();
                std::string inside = s.substr(1, s.size()-2);
                trim(inside);
                size_t sp = inside.find_first_of(" \t");
                if (sp == std::string::npos) {
                    cur_section = inside; // [section]
                } else {
                    cur_group = inside.substr(0, sp); // [group "id"]
                    std::string rest = inside.substr(sp+1);
                    trim(rest);
                    cur_id = unquote(rest);
                }
                continue;
            }

            size_t pos = s.find_first_of("=:");
            if (pos == std::string::npos) continue;
            std::string key = s.substr(0, pos);
            std::string val = s.substr(pos+1);
            trim(key); trim(val);
            val = unquote(val);

            if (!cur_group.empty() && !cur_id.empty()) {
                set_typed(cur_obj, key, val);
            } else if (!cur_section.empty()) {
                set_typed(out[cur_section], key, val);
            } else {
                // no section: ignore
            }
        }
        flush_group();
        return true;
    }

    std::string path_;
    json j_;
};
