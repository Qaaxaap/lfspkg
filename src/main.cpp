#include "config.h"

#include "cli.hpp"
#include "lfspkg/db.hpp"
#include "lfspkg/util.hpp"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>

int
main (int argc, char **argv)
{
  lfspkg::i18n_init ();

  if (argc < 2)
    {
      lfspkg::usage ();
      return 1;
    }

  std::string cmd = argv[1];

  if (cmd == "--help")
    {
      lfspkg::usage ();
      return 0;
    }

  if (cmd == "--version")
    {
      std::printf ("lfspkg %s\n", PACKAGE_VERSION);
      std::printf ("%s\n",
                   _ ("Copyright (C) 2026 Qaaxaap.  License BSD 3-Clause."));
      return 0;
    }

  try
    {
      lfspkg::PackageDB db (lfspkg::default_db_root ());
      db.ensure ();

      if (cmd == "install")
        return lfspkg::cmd_install (db, argc, argv);
      else if (cmd == "upgrade")
        return lfspkg::cmd_upgrade (db, argc, argv);
      else if (cmd == "install-meta")
        return lfspkg::cmd_install_meta (db, argc, argv);
      else if (cmd == "remove")
        return lfspkg::cmd_remove (db, argc, argv);
      else if (cmd == "list")
        return lfspkg::cmd_list (db);
      else if (cmd == "info")
        return lfspkg::cmd_info (db, argc, argv);
      else if (cmd == "tree")
        return lfspkg::cmd_tree (db, argc, argv);
      else if (cmd == "owners")
        return lfspkg::cmd_owners (db, argc, argv);
      else if (cmd == "verify")
        return lfspkg::cmd_verify (db, argc, argv);
      else if (cmd == "suggest-deps")
        return lfspkg::cmd_suggest_deps (db, argc, argv);
      else
        {
          lfspkg::usage ();
          return 1;
        }
    }
  catch (const std::exception &e)
    {
      std::cerr << "error: " << e.what () << "\n";
      return 2;
    }
}
