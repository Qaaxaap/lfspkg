#include "lfspkg/package.hpp"
#include "lfspkg/util.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

namespace lfspkg {

namespace fs = std::filesystem;

std::vector<ManifestEntry>
collect_manifest_from_stage (const fs::path &stage)
{
  if (!fs::exists (stage) || !fs::is_directory (stage))
    throw std::runtime_error ("stage directory does not exist: "
                              + stage.string ());

  std::vector<ManifestEntry> out;
  for (fs::recursive_directory_iterator
           it (stage, fs::directory_options::follow_directory_symlink),
       end;
       it != end; ++it)
    {
      fs::path rel = it->path ().lexically_relative (stage);
      std::string installPath = normalize_install_path (rel);
      fs::file_status st = it->symlink_status ();
      if (fs::is_directory (st))
        out.push_back ({ 'D', installPath });
      else if (fs::is_symlink (st))
        out.push_back ({ 'L', installPath });
      else if (fs::is_regular_file (st))
        out.push_back ({ 'F', installPath });
    }

  std::sort (out.begin (), out.end (),
             [] (const ManifestEntry &a, const ManifestEntry &b) {
               if (a.installPath == b.installPath)
                 return a.type < b.type;
               return a.installPath < b.installPath;
             });
  return out;
}

std::set<std::string>
manifest_leaf_set (const std::vector<ManifestEntry> &entries)
{
  std::set<std::string> s;
  for (const auto &e : entries)
    if (e.type != 'D')
      s.insert (e.installPath);
  return s;
}

std::set<std::string>
manifest_dir_set (const std::vector<ManifestEntry> &entries)
{
  std::set<std::string> s;
  for (const auto &e : entries)
    if (e.type == 'D')
      s.insert (e.installPath);
  return s;
}

static bool
path_is_owned_by_other (const std::map<std::string, std::string> &owners,
                        const std::string &path, const std::string &pkg)
{
  auto it = owners.find (path);
  return it != owners.end () && it->second != pkg;
}

static void
backup_existing_if_needed (const PackageDB &db, const fs::path &targetRoot,
                           const std::string &installPath, RollbackState &rb)
{
  if (rb.backedUpLeafs.count (installPath))
    return;
  fs::path real = targetRoot / fs::path (installPath).relative_path ();
  if (!(fs::exists (real) || fs::is_symlink (real)))
    return;
  fs::path backup
      = db.txn_root ("_backup") / fs::path (installPath).relative_path ();
  copy_path_exact (real, backup);
  rb.backedUpLeafs.insert (installPath);
  rb.backupRoot = db.txn_root ("_backup");
}

static void
rollback_copy (const fs::path &targetRoot, const RollbackState &rb)
{
  for (auto it = rb.createdLeafs.rbegin (); it != rb.createdLeafs.rend (); ++it)
    {
      std::error_code ec;
      fs::remove (targetRoot / fs::path (*it).relative_path (), ec);
    }

  for (const auto &installPath : rb.backedUpLeafs)
    {
      fs::path dst = targetRoot / fs::path (installPath).relative_path ();
      fs::path src = rb.backupRoot / fs::path (installPath).relative_path ();
      if (fs::exists (dst) || fs::is_symlink (dst))
        {
          std::error_code ec;
          fs::remove (dst, ec);
        }
      if (fs::exists (src) || fs::is_symlink (src))
        copy_path_exact (src, dst);
    }

  auto dirs = rb.createdDirs;
  std::sort (dirs.begin (), dirs.end (),
             [] (const std::string &a, const std::string &b) {
               if (depth_of (a) != depth_of (b))
                 return depth_of (a) > depth_of (b);
               return a > b;
             });
  for (const auto &path : dirs)
    {
      std::error_code ec;
      fs::remove (targetRoot / fs::path (path).relative_path (), ec);
    }
}

static void
cleanup_txn (const PackageDB &db)
{
  std::error_code ec;
  fs::remove_all (db.txn_root ("_backup"), ec);
}

static void
copy_manifest_to_root (const PackageDB &db, const fs::path &stage,
                       const fs::path &targetRoot, const std::string &pkg,
                       std::map<std::string, std::string> &owners,
                       const std::vector<ManifestEntry> &manifest,
                       bool allowOverwriteOwnedBySame, RollbackState &rb)
{
  (void)allowOverwriteOwnedBySame;
  for (const auto &e : manifest)
    {
      fs::path dst = targetRoot / fs::path (e.installPath).relative_path ();
      fs::path src = stage / fs::path (e.installPath).relative_path ();

      if (e.type == 'D')
        {
          if (!fs::exists (dst))
            rb.createdDirs.push_back (e.installPath);
          fs::create_directories (dst);
          continue;
        }

      if (path_is_owned_by_other (owners, e.installPath, pkg))
        throw std::runtime_error ("conflict: " + e.installPath
                                  + " already owned by "
                                  + owners[e.installPath]);

      if ((fs::exists (dst) || fs::is_symlink (dst))
          && !owners.count (e.installPath))
        throw std::runtime_error (
            "refusing to overwrite unowned existing path: " + e.installPath);

      if (owners.count (e.installPath) && owners[e.installPath] == pkg)
        backup_existing_if_needed (db, targetRoot, e.installPath, rb);
      else if (!(fs::exists (dst) || fs::is_symlink (dst)))
        rb.createdLeafs.push_back (e.installPath);

      copy_path_exact (src, dst);
      owners[e.installPath] = pkg;
    }
}

static void
remove_obsolete_entries (const std::vector<ManifestEntry> &oldManifest,
                         const std::vector<ManifestEntry> &newManifest,
                         const fs::path &targetRoot, const std::string &pkg,
                         std::map<std::string, std::string> &owners)
{
  auto newLeafs = manifest_leaf_set (newManifest);
  auto newDirs = manifest_dir_set (newManifest);

  std::vector<std::string> oldLeafs;
  std::vector<std::string> oldDirs;
  for (const auto &e : oldManifest)
    {
      if (e.type == 'D')
        oldDirs.push_back (e.installPath);
      else
        oldLeafs.push_back (e.installPath);
    }

  std::sort (oldLeafs.begin (), oldLeafs.end (),
             [] (const std::string &a, const std::string &b) {
               if (depth_of (a) != depth_of (b))
                 return depth_of (a) > depth_of (b);
               return a > b;
             });
  std::sort (oldDirs.begin (), oldDirs.end (),
             [] (const std::string &a, const std::string &b) {
               if (depth_of (a) != depth_of (b))
                 return depth_of (a) > depth_of (b);
               return a > b;
             });

  for (const auto &path : oldLeafs)
    {
      if (newLeafs.count (path))
        continue;
      auto it = owners.find (path);
      if (it != owners.end () && it->second == pkg)
        {
          std::error_code ec;
          fs::remove (targetRoot / fs::path (path).relative_path (), ec);
          owners.erase (it);
        }
    }

  for (const auto &path : oldDirs)
    {
      if (newDirs.count (path))
        continue;
      std::error_code ec;
      fs::remove (targetRoot / fs::path (path).relative_path (), ec);
    }
}

void
apply_install_or_upgrade (PackageDB &db, const PackageSpec &spec,
                          bool forceUpgrade)
{
  fs::path targetRoot = default_target_root ();
  auto owners = db.load_owners ();
  auto newManifest = collect_manifest_from_stage (spec.stage_dir);

  for (const auto &dep : spec.deps)
    {
      if (!db.installed (dep))
        throw std::runtime_error ("dependency not installed: " + dep);
    }

  bool alreadyInstalled = db.installed (spec.name);
  if (alreadyInstalled && !forceUpgrade)
    throw std::runtime_error ("package already installed: " + spec.name
                              + " (use upgrade or install-meta)");

  std::vector<ManifestEntry> oldManifest;
  if (alreadyInstalled)
    oldManifest = db.read_manifest (spec.name);

  RollbackState rb;
  cleanup_txn (db);

  try
    {
      copy_manifest_to_root (db, spec.stage_dir, targetRoot, spec.name, owners,
                             newManifest, alreadyInstalled, rb);

      if (alreadyInstalled)
        remove_obsolete_entries (oldManifest, newManifest, targetRoot,
                                 spec.name, owners);

      std::map<std::string, std::string> built_deps;
      for (const auto &dep : spec.deps)
        {
          if (db.installed (dep))
            built_deps[dep] = db.read_meta (dep).version;
        }

      PackageMeta meta{ spec.name, spec.version, now_iso8601_local (),
                        spec.stage_dir.string (), spec.deps, built_deps };
      db.write_meta_atomic (meta);
      db.write_manifest_atomic (spec.name, newManifest);
      db.save_owners_atomic (owners);
      cleanup_txn (db);

      std::cout << (alreadyInstalled ? "upgraded " : "installed ") << spec.name
                << "-" << spec.version << "\n";
    }
  catch (...)
    {
      rollback_copy (targetRoot, rb);
      cleanup_txn (db);
      throw;
    }
}

void
remove_package_files (PackageDB &db, const std::string &pkg,
                      const fs::path &targetRoot,
                      std::map<std::string, std::string> &owners)
{
  auto manifest = db.read_manifest (pkg);
  std::vector<std::string> leafPaths;
  std::vector<std::string> dirs;
  for (const auto &e : manifest)
    {
      if (e.type == 'D')
        dirs.push_back (e.installPath);
      else
        leafPaths.push_back (e.installPath);
    }

  std::sort (leafPaths.begin (), leafPaths.end (),
             [] (const std::string &a, const std::string &b) {
               if (depth_of (a) != depth_of (b))
                 return depth_of (a) > depth_of (b);
               return a > b;
             });
  std::sort (dirs.begin (), dirs.end (),
             [] (const std::string &a, const std::string &b) {
               if (depth_of (a) != depth_of (b))
                 return depth_of (a) > depth_of (b);
               return a > b;
             });

  for (const auto &path : leafPaths)
    {
      auto it = owners.find (path);
      if (it != owners.end () && it->second == pkg)
        {
          std::error_code ec;
          fs::remove (targetRoot / fs::path (path).relative_path (), ec);
          owners.erase (it);
        }
    }

  for (const auto &path : dirs)
    {
      std::error_code ec;
      fs::remove (targetRoot / fs::path (path).relative_path (), ec);
    }

  std::error_code ec1, ec2;
  fs::remove (db.meta_path (pkg), ec1);
  fs::remove (db.manifest_path (pkg), ec2);
}

std::vector<std::string>
reverse_dependencies (const PackageDB &db, const std::string &pkg)
{
  std::vector<std::string> out;
  for (const auto &p : db.list_packages ())
    {
      if (p == pkg)
        continue;
      PackageMeta m = db.read_meta (p);
      if (std::find (m.deps.begin (), m.deps.end (), pkg) != m.deps.end ())
        out.push_back (p);
    }
  std::sort (out.begin (), out.end ());
  return out;
}

void
print_tree_rec (const PackageDB &db, const std::string &pkg,
                std::set<std::string> &seen, const std::string &prefix,
                bool last)
{
  std::cout << prefix;
  if (!prefix.empty ())
    std::cout << (last ? "└── " : "├── ");
  std::cout << pkg << "\n";
  if (seen.count (pkg))
    return;
  seen.insert (pkg);

  PackageMeta m = db.read_meta (pkg);
  for (size_t i = 0; i < m.deps.size (); ++i)
    {
      bool childLast = (i + 1 == m.deps.size ());
      print_tree_rec (db, m.deps[i], seen,
                      prefix
                          + (prefix.empty () ? "" : (last ? "    " : "│   ")),
                      childLast);
    }
}

PackageSpec
parse_meta_file (const fs::path &metaFile)
{
  std::ifstream in (metaFile);
  if (!in)
    throw std::runtime_error ("cannot open meta file: " + metaFile.string ());
  PackageSpec spec;
  std::string line;
  while (std::getline (in, line))
    {
      line = trim (line);
      if (line.empty () || line[0] == '#')
        continue;
      auto pos = line.find ('=');
      if (pos == std::string::npos)
        continue;
      std::string k = trim (line.substr (0, pos));
      std::string v = trim (line.substr (pos + 1));
      if (k == "name")
        spec.name = v;
      else if (k == "version")
        spec.version = v;
      else if (k == "stage_dir")
        spec.stage_dir = v;
      else if (k == "deps")
        spec.deps = split (v, ',');
    }
  if (spec.name.empty ())
    throw std::runtime_error ("meta file missing name=");
  if (spec.version.empty ())
    throw std::runtime_error ("meta file missing version=");
  if (spec.stage_dir.empty ())
    throw std::runtime_error ("meta file missing stage_dir=");
  return spec;
}

void
verify_package (const PackageDB &db, const std::string &name,
                const fs::path &targetRoot, bool &ok)
{
  auto mf = db.read_manifest (name);
  for (const auto &e : mf)
    {
      fs::path real = targetRoot / fs::path (e.installPath).relative_path ();
      bool exists = fs::exists (real) || fs::is_symlink (real);
      if (!exists)
        {
          ok = false;
          std::cout << "missing: " << e.installPath << "\n";
        }
    }
}

std::vector<std::string>
find_stale_packages (const PackageDB &db)
{
  std::vector<std::string> stale;
  for (const auto &pkg : db.list_packages ())
    {
      PackageMeta m = db.read_meta (pkg);
      if (m.built_deps.empty ())
        continue;
      for (const auto &[dep, built_ver] : m.built_deps)
        {
          if (!db.installed (dep))
            continue;
          if (db.read_meta (dep).version != built_ver)
            {
              stale.push_back (pkg);
              break;
            }
        }
    }
  return stale;
}

}
