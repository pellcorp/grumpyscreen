#pragma once
#include "hv/json.hpp"
#include <string>
#include <vector>
#include <fstream>
#include <cctype>
#include <algorithm>
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

    bool load_override(const std::string& ini_path) {
        json override_json = json::object();
        if (!load_ini(ini_path, override_json)) return false;
        merge_override(j_, override_json);
        return true;
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

    static void merge_override(json& base, const json& override_json) {
        if (!base.is_object() || !override_json.is_object()) return;

        for (auto& [key, val] : override_json.items()) {
            auto base_it = base.find(key);
            if (base_it == base.end()) {
                if (val.is_array() && !val.empty() && val[0].is_object() && val[0].contains("id")) {
                    base[key] = val;
                }
                continue;
            }

            if (base_it->is_object() && val.is_object()) {
                merge_override(*base_it, val);
                continue;
            }

            if (base_it->is_array() && val.is_array()) {
                merge_group_override(*base_it, val);
                continue;
            }

            *base_it = val;
        }
    }

    static void merge_group_override(json& base_group, const json& override_group) {
        for (const auto& override_item : override_group) {
            if (!override_item.is_object()) continue;

            const auto id_it = override_item.find("id");
            if (id_it == override_item.end() || !id_it->is_string()) continue;
            const auto& override_id = id_it->get<std::string>();

            auto found = std::find_if(base_group.begin(), base_group.end(), [&](const json& item) {
                if (!item.is_object()) return false;
                auto it = item.find("id");
                return it != item.end() && it->is_string() && it->get<std::string>() == override_id;
            });
            if (found != base_group.end()) merge_override(*found, override_item);
            else base_group.push_back(override_item);
        }
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
