#pragma once

#include <string>

namespace lfspkg
{

struct LfspkgConfig
{
  std::string db_root = "/var/lib/lfspkg";
  std::string target_root = "/";
};

LfspkgConfig load_lfspkg_config ();

} // namespace lfspkg
