#pragma once

#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>
#include <sys/stat.h>

// Edits only the two UI settings which grumpyscreen exposes.  This deliberately
// works on the INI text instead of serialising Config, so comments, ordering,
// unknown settings and the user's choice of ':' or '=' are retained.
class ConfigOverride {
public:
    explicit ConfigOverride(std::string path) : path_(std::move(path)) {}

    bool set_prompt_emergency_stop(bool enabled) const {
        return patch_ui_value("prompt_emergency_stop", enabled ? "true" : "false");
    }

    bool set_display_sleep_sec(int32_t seconds) const {
        return patch_ui_value("display_sleep_sec", std::to_string(seconds));
    }

private:
    static std::string trimmed(const std::string& value) {
        const size_t first = value.find_first_not_of(" \t\r");
        if (first == std::string::npos) return {};
        const size_t last = value.find_last_not_of(" \t\r");
        return value.substr(first, last - first + 1);
    }

    static bool is_section(const std::string& line, const std::string& name) {
        std::string value = line;
        const size_t comment = value.find_first_of("#;");
        if (comment != std::string::npos) value.erase(comment);
        return trimmed(value) == "[" + name + "]";
    }

    static bool is_any_section(const std::string& line) {
        std::string value = line;
        const size_t comment = value.find_first_of("#;");
        if (comment != std::string::npos) value.erase(comment);
        value = trimmed(value);
        return value.size() >= 2 && value.front() == '[' && value.back() == ']';
    }

    static bool patch_assignment(std::string& line, const std::string& key,
                                 const std::string& value) {
        size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#' || line[first] == ';') return false;

        const size_t separator = line.find_first_of(":=", first);
        if (separator == std::string::npos || trimmed(line.substr(first, separator - first)) != key) {
            return false;
        }

        size_t value_start = separator + 1;
        while (value_start < line.size() && (line[value_start] == ' ' || line[value_start] == '\t')) {
            ++value_start;
        }
        size_t comment = line.find_first_of("#;", value_start);
        size_t value_end = comment == std::string::npos ? line.size() : comment;
        while (value_end > value_start &&
               (line[value_end - 1] == ' ' || line[value_end - 1] == '\t' || line[value_end - 1] == '\r')) {
            --value_end;
        }
        line.replace(value_start, value_end - value_start, value);
        return true;
    }

    bool patch_ui_value(const std::string& key, const std::string& value) const {
        if (path_.empty()) return false;

        std::ifstream input(path_, std::ios::binary);
        std::string content;
        if (input.is_open()) {
            content.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
            if (input.bad()) return false;
        }

        const bool trailing_newline = !content.empty() && content.back() == '\n';
        const std::string newline = content.find("\r\n") != std::string::npos ? "\r\n" : "\n";
        std::vector<std::string> lines;
        size_t start = 0;
        while (start < content.size()) {
            const size_t end = content.find('\n', start);
            if (end == std::string::npos) {
                lines.push_back(content.substr(start));
                break;
            }
            std::string line = content.substr(start, end - start);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(std::move(line));
            start = end + 1;
        }

        size_t last_ui_end = lines.size();
        size_t assignment = lines.size();
        bool in_ui = false;
        bool saw_ui = false;
        for (size_t i = 0; i < lines.size(); ++i) {
            if (is_section(lines[i], "ui")) {
                in_ui = true;
                saw_ui = true;
                last_ui_end = lines.size();
                continue;
            }
            if (is_any_section(lines[i])) {
                if (in_ui) last_ui_end = i;
                in_ui = false;
                continue;
            }
            if (in_ui) {
                std::string candidate = lines[i];
                if (patch_assignment(candidate, key, value)) assignment = i;
            }
        }

        if (assignment != lines.size()) {
            patch_assignment(lines[assignment], key, value);
        } else if (saw_ui) {
            lines.insert(lines.begin() + last_ui_end, key + ": " + value);
        } else {
            if (!lines.empty() && !lines.back().empty()) lines.push_back({});
            lines.push_back("[ui]");
            lines.push_back(key + ": " + value);
        }

        std::string updated;
        for (size_t i = 0; i < lines.size(); ++i) {
            if (i != 0) updated += newline;
            updated += lines[i];
        }
        if (trailing_newline || content.empty()) updated += newline;

        const std::string temporary = path_ + ".tmp";
        struct stat existing_stat {};
        const bool existed = stat(path_.c_str(), &existing_stat) == 0;
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output.is_open()) return false;
            output.write(updated.data(), static_cast<std::streamsize>(updated.size()));
            if (!output) {
                output.close();
                std::remove(temporary.c_str());
                return false;
            }
        }
        if (existed) chmod(temporary.c_str(), existing_stat.st_mode);
        if (std::rename(temporary.c_str(), path_.c_str()) != 0) {
            std::remove(temporary.c_str());
            return false;
        }
        return true;
    }

    std::string path_;
};
