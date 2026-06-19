#pragma once

#include "db.hpp"

#include <filesystem>
#include <set>
#include <vector>

namespace lfspkg
{

std::vector<ManifestEntry>
collect_manifest_from_stage (const std::filesystem::path &stage);

std::set<std::string>
manifest_leaf_set (const std::vector<ManifestEntry> &entries);

std::set<std::string>
manifest_dir_set (const std::vector<ManifestEntry> &entries);

void apply_install_or_upgrade (PackageDB &db, const PackageSpec &spec,
                               bool forceUpgrade);

void remove_package_files (PackageDB &db, const std::string &pkg,
                           const std::filesystem::path &targetRoot,
                           std::map<std::string, std::string> &owners);

std::vector<std::string> reverse_dependencies (const PackageDB &db,
                                               const std::string &pkg);

std::vector<std::string> find_stale_packages (const PackageDB &db);

void print_tree_rec (const PackageDB &db, const std::string &pkg,
                     std::set<std::string> &seen, const std::string &prefix,
                     bool last);

PackageSpec parse_meta_file (const std::filesystem::path &metaFile);

void verify_package (const PackageDB &db, const std::string &name,
                     const std::filesystem::path &targetRoot, bool &ok);

} // namespace lfspkg
