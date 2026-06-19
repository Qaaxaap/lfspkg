#pragma once

#include "db.hpp"

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace lfspkg
{

bool looks_like_elf_candidate (const std::filesystem::path &p);

std::set<std::string> readelf_needed (const std::filesystem::path &file);

std::map<std::string, std::set<std::string>>
build_basename_to_pkg (const std::map<std::string, std::string> &owners);

void suggest_deps (const PackageDB &db, const std::filesystem::path &stage,
                   std::set<std::string> &pkgs_out,
                   std::vector<std::string> &unresolved_out);

}
