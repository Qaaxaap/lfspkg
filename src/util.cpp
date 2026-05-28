#include "lfspkg/util.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace lfspkg {

static const char*
find_locale_dir ()
{
  const char* dir = std::getenv ("TEXTDOMAINDIR");
  if (dir != nullptr)
    return dir;

  if (access (LOCALEDIR, F_OK) == 0)
    return LOCALEDIR;

  if (access ("po/locale", F_OK) == 0)
    return "po/locale";

  return LOCALEDIR;
}

void
i18n_init ()
{
  setlocale (LC_ALL, "");
  bindtextdomain ("lfspkg", find_locale_dir ());
  textdomain ("lfspkg");
}

std::string
trim (const std::string &s)
{
  size_t a = 0;
  while (a < s.size () && std::isspace (static_cast<unsigned char> (s[a])))
    ++a;
  size_t b = s.size ();
  while (b > a && std::isspace (static_cast<unsigned char> (s[b - 1])))
    --b;
  return s.substr (a, b - a);
}

std::vector<std::string>
split (const std::string &s, char delim)
{
  std::vector<std::string> out;
  std::stringstream ss (s);
  std::string item;
  while (std::getline (ss, item, delim))
    {
      item = trim (item);
      if (!item.empty ())
        out.push_back (item);
    }
  return out;
}

std::string
join (const std::vector<std::string> &items, char delim)
{
  std::ostringstream os;
  for (size_t i = 0; i < items.size (); ++i)
    {
      if (i)
        os << delim;
      os << items[i];
    }
  return os.str ();
}

std::string
now_iso8601_local ()
{
  auto now = std::chrono::system_clock::now ();
  std::time_t t = std::chrono::system_clock::to_time_t (now);
  std::tm tm{};
  localtime_r (&t, &tm);
  std::ostringstream os;
  os << std::put_time (&tm, "%Y-%m-%d %H:%M:%S");
  return os.str ();
}

std::string
shell_escape_single_quotes (const std::string &s)
{
  std::string out = "'";
  for (char c : s)
    {
      if (c == '\'')
        out += "'\\''";
      else
        out += c;
    }
  out += "'";
  return out;
}

void
ensure_parents (const std::filesystem::path &p)
{
  std::filesystem::create_directories (p.parent_path ());
}

std::string
normalize_install_path (const std::filesystem::path &rel)
{
  std::string s = "/" + rel.generic_string ();
  if (s.size () > 1 && s.back () == '/')
    s.pop_back ();
  return s;
}

int
depth_of (const std::string &s)
{
  return static_cast<int> (std::count (s.begin (), s.end (), '/'));
}

void
copy_path_exact (const std::filesystem::path &src,
                 const std::filesystem::path &dst)
{
  namespace fs = std::filesystem;
  fs::file_status st = fs::symlink_status (src);
  ensure_parents (dst);
  if (fs::is_symlink (st))
    {
      if (fs::exists (dst) || fs::is_symlink (dst))
        fs::remove (dst);
      fs::create_symlink (fs::read_symlink (src), dst);
    }
  else if (fs::is_regular_file (st))
    {
      if (fs::exists (dst) || fs::is_symlink (dst))
        fs::remove (dst);
      fs::copy_file (src, dst, fs::copy_options::overwrite_existing);
      fs::permissions (dst, fs::status (src).permissions (),
                       fs::perm_options::replace);
    }
  else if (fs::is_directory (st))
    {
      fs::create_directories (dst);
    }
  else
    {
      throw std::runtime_error ("unsupported file type for copy: "
                                + src.string ());
    }
}

}
