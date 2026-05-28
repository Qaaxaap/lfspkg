#include "cli.hpp"

#include "lfspkg/db.hpp"
#include "lfspkg/elf.hpp"
#include "lfspkg/package.hpp"
#include "lfspkg/util.hpp"

#include <cstdio>
#include <iostream>
#include <stdexcept>

namespace lfspkg {

void
usage ()
{
  std::printf ("%s\n\n", _ ("lfspkg — minimalist package tracker for LFS"));
  std::printf ("%s\n", _ ("Environment:"));
  std::printf ("  LFSPKG_DB    %s\n",
               _ ("package database directory (default: /var/lib/lfspkg)"));
  std::printf ("  LFSPKG_ROOT  %s\n\n",
               _ ("installation target root (default: /)"));
  std::printf ("%s\n", _ ("Commands:"));
  std::printf ("  install      <name> <version> <stage_dir> [dep1,dep2,...]\n");
  std::printf ("  upgrade      <name> <version> <stage_dir> [dep1,dep2,...]\n");
  std::printf ("  install-meta <meta_file>\n");
  std::printf ("  remove       <name> [--force]\n");
  std::printf ("  list\n");
  std::printf ("  info         <name>\n");
  std::printf ("  tree         <name>\n");
  std::printf ("  owners       </absolute/path>\n");
  std::printf ("  verify       <name>\n");
  std::printf ("  suggest-deps <stage_dir>\n\n");
  std::printf ("  --help       %s\n", _ ("Show this help"));
  std::printf ("  --version    %s\n", _ ("Show version information"));
}

int
cmd_install (PackageDB &db, int argc, char **argv)
{
  if (argc < 5)
    throw std::runtime_error ("usage: lfspkg install <name> <version> "
                              "<stage_dir> [dep1,dep2,...]");
  PackageSpec spec;
  spec.name = argv[2];
  spec.version = argv[3];
  spec.stage_dir = argv[4];
  spec.deps = (argc >= 6) ? split (argv[5], ',') : std::vector<std::string>{};
  apply_install_or_upgrade (db, spec, false);
  return 0;
}

int
cmd_upgrade (PackageDB &db, int argc, char **argv)
{
  if (argc < 5)
    throw std::runtime_error ("usage: lfspkg upgrade <name> <version> "
                              "<stage_dir> [dep1,dep2,...]");
  PackageSpec spec;
  spec.name = argv[2];
  spec.version = argv[3];
  spec.stage_dir = argv[4];
  spec.deps = (argc >= 6) ? split (argv[5], ',') : std::vector<std::string>{};
  if (!db.installed (spec.name))
    throw std::runtime_error ("package not installed: " + spec.name);
  apply_install_or_upgrade (db, spec, true);
  return 0;
}

int
cmd_install_meta (PackageDB &db, int argc, char **argv)
{
  if (argc < 3)
    throw std::runtime_error ("usage: lfspkg install-meta <meta_file>");
  PackageSpec spec = parse_meta_file (argv[2]);
  bool alreadyInstalled = db.installed (spec.name);
  apply_install_or_upgrade (db, spec, alreadyInstalled);
  return 0;
}

int
cmd_remove (PackageDB &db, int argc, char **argv)
{
  if (argc < 3)
    throw std::runtime_error ("usage: lfspkg remove <name> [--force]");
  const std::string name = argv[2];
  bool force = false;
  for (int i = 3; i < argc; ++i)
    if (std::string (argv[i]) == "--force")
      force = true;
  if (!db.installed (name))
    throw std::runtime_error ("package not installed: " + name);

  auto rdeps = reverse_dependencies (db, name);
  if (!force && !rdeps.empty ())
    {
      std::ostringstream os;
      os << "refusing to remove " << name
         << "; required by: " << join (rdeps, ',');
      throw std::runtime_error (os.str ());
    }

  auto owners = db.load_owners ();
  remove_package_files (db, name, default_target_root (), owners);
  db.save_owners_atomic (owners);
  std::cout << "removed " << name << "\n";
  return 0;
}

int
cmd_list (PackageDB &db)
{
  for (const auto &pkg : db.list_packages ())
    {
      auto meta = db.read_meta (pkg);
      std::cout << meta.name << ' ' << meta.version << "\n";
    }
  return 0;
}

int
cmd_info (PackageDB &db, int argc, char **argv)
{
  if (argc < 3)
    throw std::runtime_error ("usage: lfspkg info <name>");
  auto m = db.read_meta (argv[2]);
  auto mf = db.read_manifest (argv[2]);
  auto rdeps = reverse_dependencies (db, argv[2]);
  std::cout << "name: " << m.name << "\n";
  std::cout << "version: " << m.version << "\n";
  std::cout << "installed_at: " << m.install_time << "\n";
  std::cout << "stage_dir: "
            << (m.stage_dir.empty () ? "(unknown)" : m.stage_dir) << "\n";
  std::cout << "deps: " << (m.deps.empty () ? "(none)" : join (m.deps, ','))
            << "\n";
  std::cout << "required_by: "
            << (rdeps.empty () ? "(none)" : join (rdeps, ',')) << "\n";
  std::cout << "entries: " << mf.size () << "\n";
  return 0;
}

int
cmd_tree (PackageDB &db, int argc, char **argv)
{
  if (argc < 3)
    throw std::runtime_error ("usage: lfspkg tree <name>");
  std::set<std::string> seen;
  print_tree_rec (db, argv[2], seen, "", true);
  return 0;
}

int
cmd_owners (PackageDB &db, int argc, char **argv)
{
  if (argc < 3)
    throw std::runtime_error ("usage: lfspkg owners </path/to/file>");
  auto owners = db.load_owners ();
  std::string path = argv[2];
  if (path.empty () || path[0] != '/')
    path = "/" + path;
  auto it = owners.find (path);
  if (it == owners.end ())
    std::cout << "unowned or unknown: " << path << "\n";
  else
    std::cout << path << " -> " << it->second << "\n";
  return 0;
}

int
cmd_verify (PackageDB &db, int argc, char **argv)
{
  if (argc < 3)
    throw std::runtime_error ("usage: lfspkg verify <name>");
  bool ok = true;
  verify_package (db, argv[2], default_target_root (), ok);
  if (ok)
    std::cout << "ok\n";
  return ok ? 0 : 1;
}

int
cmd_suggest_deps (PackageDB &db, int argc, char **argv)
{
  if (argc < 3)
    throw std::runtime_error ("usage: lfspkg suggest-deps <stage_dir>");
  std::set<std::string> pkgs;
  std::vector<std::string> unresolved;
  suggest_deps (db, argv[2], pkgs, unresolved);

  std::cout << "suggested_packages="
            << (pkgs.empty () ? ""
                              : join (std::vector<std::string> (pkgs.begin (),
                                                                pkgs.end ()),
                                      ','))
            << "\n";
  if (!unresolved.empty ())
    std::cout << "unresolved_libs=" << join (unresolved, ',') << "\n";
  return 0;
}

}
