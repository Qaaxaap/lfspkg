#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include <filesystem>

namespace lfspkg {

struct PackageMeta
{
  std::string name;
  std::string version;
  std::string install_time;
  std::string stage_dir;
  std::vector<std::string> deps;
  std::map<std::string, std::string> built_deps;
};

struct ManifestEntry
{
  char type;
  std::string installPath;
};

struct PackageSpec
{
  std::string name;
  std::string version;
  std::filesystem::path stage_dir;
  std::vector<std::string> deps;
};

struct RollbackState
{
  std::vector<std::string> createdLeafs;
  std::vector<std::string> createdDirs;
  std::set<std::string> backedUpLeafs;
  std::filesystem::path backupRoot;
};

class PackageDB
{
public:
  explicit PackageDB (std::filesystem::path root);

  void ensure () const;

  const std::filesystem::path &
  root () const
  {
    return root_;
  }

  std::filesystem::path meta_path (const std::string &name) const;
  std::filesystem::path manifest_path (const std::string &name) const;
  std::filesystem::path owners_path () const;
  std::filesystem::path txn_root (const std::string &name) const;

  bool installed (const std::string &name) const;

  void write_meta_atomic (const PackageMeta &meta) const;
  PackageMeta read_meta (const std::string &name) const;

  std::vector<std::string> list_packages () const;

  void write_manifest_atomic (const std::string &name,
                              const std::vector<ManifestEntry> &entries) const;
  std::vector<ManifestEntry>
  read_manifest (const std::string &name) const;

  std::map<std::string, std::string> load_owners () const;
  void save_owners_atomic (const std::map<std::string, std::string> &owners) const;

private:
  std::filesystem::path root_;
};

std::filesystem::path default_db_root ();
std::filesystem::path default_target_root ();

}
