#pragma once

#include <libintl.h>
#include <string>
#include <vector>

#include <filesystem>

#define _(String)  gettext (String)
#define N_(String) (String)

namespace lfspkg {

void i18n_init ();

std::string trim (const std::string &s);
std::vector<std::string> split (const std::string &s, char delim);
std::string join (const std::vector<std::string> &items, char delim);

std::string now_iso8601_local ();
std::string shell_escape_single_quotes (const std::string &s);

void ensure_parents (const std::filesystem::path &p);
std::string normalize_install_path (const std::filesystem::path &rel);
int depth_of (const std::string &s);

void copy_path_exact (const std::filesystem::path &src,
                      const std::filesystem::path &dst);

}
