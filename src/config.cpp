#include "lfspkg/config.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace lfspkg
{

static std::string
trim (std::string s)
{
  size_t a = 0;
  while (a < s.size () && (s[a] == ' ' || s[a] == '\t'))
    ++a;
  size_t b = s.size ();
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t'))
    --b;
  return s.substr (a, b - a);
}

static std::string
unquote (const std::string &v)
{
  auto t = trim (v);
  if (t.size () >= 2 && t.front () == '"' && t.back () == '"')
    return t.substr (1, t.size () - 2);
  return t;
}

static void
apply (LfspkgConfig &cfg, const std::string &key, const std::string &value)
{
  if (key == "DB_ROOT")
    cfg.db_root = unquote (value);
  else if (key == "TARGET_ROOT")
    cfg.target_root = unquote (value);
}

static void
parse_file (const std::string &path, LfspkgConfig &cfg)
{
  std::ifstream in (path);
  if (!in)
    return;

  std::string line;
  while (std::getline (in, line))
    {
      line = trim (line);
      if (line.empty () || line[0] == '#')
        continue;

      size_t pos = line.find ('=');
      if (pos == std::string::npos)
        continue;

      std::string key = trim (line.substr (0, pos));
      std::string value = trim (line.substr (pos + 1));
      apply (cfg, key, value);
    }
}

LfspkgConfig
load_lfspkg_config ()
{
  LfspkgConfig cfg;

  parse_file ("/etc/lfspkg.conf", cfg);

  const char *home = std::getenv ("HOME");
  if (home != nullptr)
    {
      std::string user_conf = std::string (home) + "/.config/lfspkg/lfspkg.conf";
      parse_file (user_conf, cfg);
    }

  /* Environment variables take highest priority. */
  if (const char *env = std::getenv ("LFSPKG_DB"))
    cfg.db_root = env;
  if (const char *env = std::getenv ("LFSPKG_ROOT"))
    cfg.target_root = env;

  return cfg;
}

} // namespace lfspkg
