// test_i18n.cpp
#include <cassert>
#include <cstring>
#include <experimental/filesystem>
#include <fstream>
#include <iostream>
#include "i18n.h"

namespace fs = std::experimental::filesystem;

static void write_file(const fs::path &path, const std::string &text) {
    std::ofstream out(path);
    out << text;
}

int main() {
    const fs::path dir = fs::temp_directory_path() / "grumpy_i18n_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    write_file(dir / "it.json", R"({"_language":"Italiano","Back":"Indietro","Stop":"Ferma"})");
    write_file(dir / "de.json", R"({"_language":"Deutsch","Back":"Zurück"})");
    write_file(dir / "bad.json", "{not json");

    // no file: English from the code, nothing held in memory
    assert(std::strcmp(I18n::tr("Back"), "Back") == 0);
    assert(I18n::tr(nullptr) == nullptr);
    assert(I18n::loaded_strings() == 0);

    // English is always first, broken files are not offered
    const auto languages = I18n::available(dir.string());
    assert(languages.size() == 3);
    assert(languages[0].code == "en" && languages[1].name == "Deutsch" && languages[2].name == "Italiano");
    assert(I18n::available((dir / "missing").string()).size() == 1);

    // a button map keeps its address and follows the language
    const char *english_buttons[] = {"Back", "Other", ""};
    const char **buttons = I18n::map(english_buttons);
    assert(buttons == I18n::map(english_buttons));
    assert(std::strcmp(buttons[0], "Back") == 0 && buttons[2][0] == '\0');

    assert(I18n::load(dir.string(), "it"));
    assert(I18n::current() == "it");
    assert(I18n::loaded_strings() == 2);
    assert(std::strcmp(I18n::tr("Back"), "Indietro") == 0);
    assert(std::strcmp(I18n::tr("Not in the file"), "Not in the file") == 0);
    assert(std::strcmp(buttons[0], "Indietro") == 0 && std::strcmp(buttons[1], "Other") == 0);

    // the next language replaces the last, it does not add to it
    assert(I18n::load(dir.string(), "de"));
    assert(I18n::loaded_strings() == 1);
    assert(std::strcmp(I18n::tr("Stop"), "Stop") == 0);
    assert(std::strcmp(buttons[0], "Zurück") == 0);

    // a file that will not load leaves the active language alone
    assert(!I18n::load(dir.string(), "bad"));
    assert(!I18n::load(dir.string(), "missing"));
    assert(I18n::current() == "de" && I18n::loaded_strings() == 1);

    // English needs no file and frees the table
    assert(I18n::load(dir.string(), "en"));
    assert(I18n::loaded_strings() == 0);
    assert(std::strcmp(buttons[0], "Back") == 0);

    fs::remove_all(dir);
    std::cout << "test_i18n passed\n";
    return 0;
}
