#include "lfspkg/db.hpp"
#include "lfspkg/util.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace lfspkg
{

namespace fs = std::filesystem;

PackageDB::PackageDB (fs::path root) : root_ (std::move (root)) {}

void
PackageDB::ensure () const
{
  fs::create_directories (root_ / "packages");
  fs::create_directories (root_ / "manifests");
  fs::create_directories (root_ / "txn");
}

fs::path
PackageDB::meta_path (const std::string &name) const
{
  return root_ / "packages" / (name + ".meta");
}

fs::path
PackageDB::manifest_path (const std::string &name) const
{
  return root_ / "manifests" / (name + ".list");
}

fs::path
PackageDB::owners_path () const
{
  return root_ / "owners.tsv";
}

fs::path
PackageDB::txn_root (const std::string &name) const
{
  return root_ / "txn" / name;
}

bool
PackageDB::installed (const std::string &name) const
{
  return fs::exists (meta_path (name));
}

void
PackageDB::write_meta_atomic (const PackageMeta &meta) const
{
  ensure ();
  fs::path tmp = meta_path (meta.name);
  tmp += ".tmp";
  std::ofstream out (tmp);
  if (!out)
    throw std::runtime_error ("cannot write meta");
  out << "name=" << meta.name << "\n";
  out << "version=" << meta.version << "\n";
  out << "install_time=" << meta.install_time << "\n";
  out << "stage_dir=" << meta.stage_dir << "\n";
  out << "deps=" << join (meta.deps, ',') << "\n";
  std::string bd;
  for (const auto &[dep, ver] : meta.built_deps)
    {
      if (!bd.empty ())
        bd += ',';
      bd += dep + ':' + ver;
    }
  out << "built_deps=" << bd << "\n";
  out.close ();
  fs::rename (tmp, meta_path (meta.name));
}

PackageMeta
PackageDB::read_meta (const std::string &name) const
{
  std::ifstream in (meta_path (name));
  if (!in)
    throw std::runtime_error ("package not installed: " + name);
  PackageMeta m;
  std::string line;
  while (std::getline (in, line))
    {
      auto pos = line.find ('=');
      if (pos == std::string::npos)
        continue;
      std::string k = line.substr (0, pos);
      std::string v = line.substr (pos + 1);
      if (k == "name")
        m.name = v;
      else if (k == "version")
        m.version = v;
      else if (k == "install_time")
        m.install_time = v;
      else if (k == "stage_dir")
        m.stage_dir = v;
      else if (k == "deps")
        m.deps = split (v, ',');
      else if (k == "built_deps" && !v.empty ())
        {
          for (const auto &pair : split (v, ','))
            {
              auto colon = pair.find (':');
              if (colon != std::string::npos)
                m.built_deps[pair.substr (0, colon)] = pair.substr (colon + 1);
            }
        }
    }
  return m;
}

std::vector<std::string>
PackageDB::list_packages () const
{
  std::vector<std::string> pkgs;
  fs::path dir = root_ / "packages";
  if (!fs::exists (dir))
    return pkgs;
  for (const auto &e : fs::directory_iterator (dir))
    {
      if (e.is_regular_file () && e.path ().extension () == ".meta")
        pkgs.push_back (e.path ().stem ().string ());
    }
  std::sort (pkgs.begin (), pkgs.end ());
  return pkgs;
}

void
PackageDB::write_manifest_atomic (
    const std::string &name, const std::vector<ManifestEntry> &entries) const
{
  ensure ();
  fs::path tmp = manifest_path (name);
  tmp += ".tmp";
  std::ofstream out (tmp);
  if (!out)
    throw std::runtime_error ("cannot write manifest");
  for (const auto &e : entries)
    out << e.type << '\t' << e.installPath << "\n";
  out.close ();
  fs::rename (tmp, manifest_path (name));
}

std::vector<ManifestEntry>
PackageDB::read_manifest (const std::string &name) const
{
  std::ifstream in (manifest_path (name));
  if (!in)
    throw std::runtime_error ("manifest not found for: " + name);
  std::vector<ManifestEntry> entries;
  std::string line;
  while (std::getline (in, line))
    {
      if (line.size () < 3 || line[1] != '\t')
        continue;
      entries.push_back ({ line[0], line.substr (2) });
    }
  return entries;
}

std::map<std::string, std::string>
PackageDB::load_owners () const
{
  std::map<std::string, std::string> owners;
  std::ifstream in (owners_path ());
  if (!in)
    return owners;
  std::string line;
  while (std::getline (in, line))
    {
      auto pos = line.find ('\t');
      if (pos == std::string::npos)
        continue;
      owners[line.substr (0, pos)] = line.substr (pos + 1);
    }
  return owners;
}

void
PackageDB::save_owners_atomic (
    const std::map<std::string, std::string> &owners) const
{
  ensure ();
  fs::path tmp = owners_path ();
  tmp += ".tmp";
  std::ofstream out (tmp);
  if (!out)
    throw std::runtime_error ("cannot write owners index");
  for (const auto &[path, pkg] : owners)
    out << path << '\t' << pkg << "\n";
  out.close ();
  fs::rename (tmp, owners_path ());
}

fs::path
default_db_root ()
{
  if (const char *env = std::getenv ("LFSPKG_DB"))
    return fs::path (env);
  return "/var/lib/lfspkg";
}

fs::path
default_target_root ()
{
  if (const char *env = std::getenv ("LFSPKG_ROOT"))
    return fs::path (env);
  return "/";
}

} // namespace lfspkg
