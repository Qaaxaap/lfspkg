#include "lfspkg/elf.hpp"
#include "lfspkg/db.hpp"
#include "lfspkg/util.hpp"

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace lfspkg
{

namespace fs = std::filesystem;

bool
looks_like_elf_candidate (const fs::path &p)
{
  std::string name = p.filename ().string ();
  std::string s = p.generic_string ();
  if (name.find (".so") != std::string::npos)
    return true;
  if (s.find ("/bin/") != std::string::npos
      || s.find ("/sbin/") != std::string::npos
      || s.find ("/lib/") != std::string::npos
      || s.find ("/lib64/") != std::string::npos)
    return true;
  std::error_code ec;
  auto permBits = fs::status (p, ec).permissions ();
  if (!ec)
    {
      using fs::perms;
      if ((permBits & perms::owner_exec) != perms::none
          || (permBits & perms::group_exec) != perms::none
          || (permBits & perms::others_exec) != perms::none)
        return true;
    }
  return false;
}

std::set<std::string>
readelf_needed (const fs::path &file)
{
  std::set<std::string> needed;
  std::string cmd = "readelf -d " + shell_escape_single_quotes (file.string ())
                    + " 2>/dev/null";
  FILE *pipe = popen (cmd.c_str (), "r");
  if (!pipe)
    return needed;

  char buf[4096];
  while (fgets (buf, sizeof (buf), pipe))
    {
      std::string line (buf);
      auto pos = line.find ("Shared library: [");
      if (pos == std::string::npos)
        continue;
      pos += std::string ("Shared library: [").size ();
      auto end = line.find (']', pos);
      if (end == std::string::npos)
        continue;
      needed.insert (line.substr (pos, end - pos));
    }
  pclose (pipe);
  return needed;
}

std::map<std::string, std::set<std::string>>
build_basename_to_pkg (const std::map<std::string, std::string> &owners)
{
  std::map<std::string, std::set<std::string>> out;
  for (const auto &[path, pkg] : owners)
    out[fs::path (path).filename ().string ()].insert (pkg);
  return out;
}

void
suggest_deps (const PackageDB &db, const fs::path &stage,
              std::set<std::string> &pkgs_out,
              std::vector<std::string> &unresolved_out)
{
  auto owners = db.load_owners ();
  auto byBase = build_basename_to_pkg (owners);
  std::set<std::string> libs;

  for (fs::recursive_directory_iterator
           it (stage, fs::directory_options::follow_directory_symlink),
       end;
       it != end; ++it)
    {
      std::error_code ec;
      if (!it->is_regular_file (ec))
        continue;
      if (!looks_like_elf_candidate (it->path ()))
        continue;
      auto needed = readelf_needed (it->path ());
      libs.insert (needed.begin (), needed.end ());
    }

  for (const auto &lib : libs)
    {
      auto it = byBase.find (lib);
      if (it == byBase.end ())
        unresolved_out.push_back (lib);
      else
        pkgs_out.insert (it->second.begin (), it->second.end ());
    }
  std::sort (unresolved_out.begin (), unresolved_out.end ());
}

}
