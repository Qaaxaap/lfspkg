#pragma once

namespace lfspkg {

class PackageDB;

void usage ();
int cmd_install (PackageDB &db, int argc, char **argv);
int cmd_upgrade (PackageDB &db, int argc, char **argv);
int cmd_install_meta (PackageDB &db, int argc, char **argv);
int cmd_remove (PackageDB &db, int argc, char **argv);
int cmd_list (PackageDB &db);
int cmd_info (PackageDB &db, int argc, char **argv);
int cmd_tree (PackageDB &db, int argc, char **argv);
int cmd_owners (PackageDB &db, int argc, char **argv);
int cmd_verify (PackageDB &db, int argc, char **argv);
int cmd_suggest_deps (PackageDB &db, int argc, char **argv);

}
