#pragma once
#include <string>
#include <vector>
void i18n_load(const std::string &path);
const char *tr(const char *en);
std::vector<std::string> i18n_list(const std::string &dir);
