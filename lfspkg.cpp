#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct PackageMeta {
    std::string name;
    std::string version;
    std::string install_time;
    std::string stage_dir;
    std::vector<std::string> deps;
};

struct ManifestEntry {
    char type; // 'F' file, 'L' symlink, 'D' dir
    std::string
        installPath; // absolute path under target root, e.g. /usr/bin/foo
};

struct PackageSpec {
    std::string name;
    std::string version;
    fs::path stage_dir;
    std::vector<std::string> deps;
};

struct RollbackState {
    std::vector<std::string> createdLeafs;
    std::vector<std::string> createdDirs;
    std::set<std::string> backedUpLeafs;
    fs::path backupRoot;
};

static std::string trim(const std::string &s) {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return s.substr(a, b - a);
}

static std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        item = trim(item);
        if (!item.empty())
            out.push_back(item);
    }
    return out;
}

static std::string join(const std::vector<std::string> &items, char delim) {
    std::ostringstream os;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i)
            os << delim;
        os << items[i];
    }
    return os.str();
}

static std::string now_iso8601_local() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return os.str();
}

static std::string shell_escape_single_quotes(const std::string &s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'')
            out += "'\\''";
        else
            out += c;
    }
    out += "'";
    return out;
}

static void ensure_parents(const fs::path &p) {
    fs::create_directories(p.parent_path());
}

static std::string normalize_install_path(const fs::path &rel) {
    std::string s = "/" + rel.generic_string();
    if (s.size() > 1 && s.back() == '/')
        s.pop_back();
    return s;
}

static int depth_of(const std::string &s) {
    return static_cast<int>(std::count(s.begin(), s.end(), '/'));
}

class PackageDB {
public:
    explicit PackageDB(fs::path root) : root_(std::move(root)) {}

    void ensure() const {
        fs::create_directories(root_ / "packages");
        fs::create_directories(root_ / "manifests");
        fs::create_directories(root_ / "txn");
    }

    const fs::path &root() const { return root_; }

    fs::path meta_path(const std::string &name) const {
        return root_ / "packages" / (name + ".meta");
    }

    fs::path manifest_path(const std::string &name) const {
        return root_ / "manifests" / (name + ".list");
    }

    fs::path owners_path() const { return root_ / "owners.tsv"; }

    fs::path txn_root(const std::string &name) const {
        return root_ / "txn" / name;
    }

    bool installed(const std::string &name) const {
        return fs::exists(meta_path(name));
    }

    void write_meta_atomic(const PackageMeta &meta) const {
        ensure();
        fs::path tmp = meta_path(meta.name);
        tmp += ".tmp";
        std::ofstream out(tmp);
        if (!out)
            throw std::runtime_error("cannot write meta");
        out << "name=" << meta.name << "\n";
        out << "version=" << meta.version << "\n";
        out << "install_time=" << meta.install_time << "\n";
        out << "stage_dir=" << meta.stage_dir << "\n";
        out << "deps=" << join(meta.deps, ',') << "\n";
        out.close();
        fs::rename(tmp, meta_path(meta.name));
    }

    PackageMeta read_meta(const std::string &name) const {
        std::ifstream in(meta_path(name));
        if (!in)
            throw std::runtime_error("package not installed: " + name);
        PackageMeta m;
        std::string line;
        while (std::getline(in, line)) {
            auto pos = line.find('=');
            if (pos == std::string::npos)
                continue;
            std::string k = line.substr(0, pos);
            std::string v = line.substr(pos + 1);
            if (k == "name")
                m.name = v;
            else if (k == "version")
                m.version = v;
            else if (k == "install_time")
                m.install_time = v;
            else if (k == "stage_dir")
                m.stage_dir = v;
            else if (k == "deps")
                m.deps = split(v, ',');
        }
        return m;
    }

    std::vector<std::string> list_packages() const {
        std::vector<std::string> pkgs;
        fs::path dir = root_ / "packages";
        if (!fs::exists(dir))
            return pkgs;
        for (const auto &e : fs::directory_iterator(dir)) {
            if (e.is_regular_file() && e.path().extension() == ".meta") {
                pkgs.push_back(e.path().stem().string());
            }
        }
        std::sort(pkgs.begin(), pkgs.end());
        return pkgs;
    }

    void
    write_manifest_atomic(const std::string &name,
                          const std::vector<ManifestEntry> &entries) const {
        ensure();
        fs::path tmp = manifest_path(name);
        tmp += ".tmp";
        std::ofstream out(tmp);
        if (!out)
            throw std::runtime_error("cannot write manifest");
        for (const auto &e : entries)
            out << e.type << '\t' << e.installPath << "\n";
        out.close();
        fs::rename(tmp, manifest_path(name));
    }

    std::vector<ManifestEntry> read_manifest(const std::string &name) const {
        std::ifstream in(manifest_path(name));
        if (!in)
            throw std::runtime_error("manifest not found for: " + name);
        std::vector<ManifestEntry> entries;
        std::string line;
        while (std::getline(in, line)) {
            if (line.size() < 3 || line[1] != '\t')
                continue;
            entries.push_back({line[0], line.substr(2)});
        }
        return entries;
    }

    std::map<std::string, std::string> load_owners() const {
        std::map<std::string, std::string> owners;
        std::ifstream in(owners_path());
        if (!in)
            return owners;
        std::string line;
        while (std::getline(in, line)) {
            auto pos = line.find('\t');
            if (pos == std::string::npos)
                continue;
            owners[line.substr(0, pos)] = line.substr(pos + 1);
        }
        return owners;
    }

    void
    save_owners_atomic(const std::map<std::string, std::string> &owners) const {
        ensure();
        fs::path tmp = owners_path();
        tmp += ".tmp";
        std::ofstream out(tmp);
        if (!out)
            throw std::runtime_error("cannot write owners index");
        for (const auto &[path, pkg] : owners)
            out << path << '\t' << pkg << "\n";
        out.close();
        fs::rename(tmp, owners_path());
    }

private:
    fs::path root_;
};

static fs::path default_db_root() {
    if (const char *env = std::getenv("LFSPKG_DB"))
        return fs::path(env);
    return "/var/lib/lfspkg";
}

static fs::path default_target_root() {
    if (const char *env = std::getenv("LFSPKG_ROOT"))
        return fs::path(env);
    return "/";
}

static std::vector<ManifestEntry>
collect_manifest_from_stage(const fs::path &stage) {
    if (!fs::exists(stage) || !fs::is_directory(stage)) {
        throw std::runtime_error("stage directory does not exist: " +
                                 stage.string());
    }

    std::vector<ManifestEntry> out;
    for (fs::recursive_directory_iterator
             it(stage, fs::directory_options::follow_directory_symlink),
         end;
         it != end; ++it) {
        fs::path rel = it->path().lexically_relative(stage);
        std::string installPath = normalize_install_path(rel);
        fs::file_status st = it->symlink_status();
        if (fs::is_directory(st))
            out.push_back({'D', installPath});
        else if (fs::is_symlink(st))
            out.push_back({'L', installPath});
        else if (fs::is_regular_file(st))
            out.push_back({'F', installPath});
    }

    std::sort(out.begin(), out.end(),
              [](const ManifestEntry &a, const ManifestEntry &b) {
                  if (a.installPath == b.installPath)
                      return a.type < b.type;
                  return a.installPath < b.installPath;
              });
    return out;
}

static std::vector<std::string> reverse_dependencies(const PackageDB &db,
                                                     const std::string &pkg) {
    std::vector<std::string> out;
    for (const auto &p : db.list_packages()) {
        if (p == pkg)
            continue;
        PackageMeta m = db.read_meta(p);
        if (std::find(m.deps.begin(), m.deps.end(), pkg) != m.deps.end())
            out.push_back(p);
    }
    std::sort(out.begin(), out.end());
    return out;
}

static bool
path_is_owned_by_other(const std::map<std::string, std::string> &owners,
                       const std::string &path, const std::string &pkg) {
    auto it = owners.find(path);
    return it != owners.end() && it->second != pkg;
}

static void copy_path_exact(const fs::path &src, const fs::path &dst) {
    fs::file_status st = fs::symlink_status(src);
    ensure_parents(dst);
    if (fs::is_symlink(st)) {
        if (fs::exists(dst) || fs::is_symlink(dst))
            fs::remove(dst);
        fs::create_symlink(fs::read_symlink(src), dst);
    } else if (fs::is_regular_file(st)) {
        if (fs::exists(dst) || fs::is_symlink(dst))
            fs::remove(dst);
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
        fs::permissions(dst, fs::status(src).permissions(),
                        fs::perm_options::replace);
    } else if (fs::is_directory(st)) {
        fs::create_directories(dst);
    } else {
        throw std::runtime_error("unsupported file type for copy: " +
                                 src.string());
    }
}

static void backup_existing_if_needed(const PackageDB &db,
                                      const fs::path &targetRoot,
                                      const std::string &installPath,
                                      RollbackState &rb) {
    if (rb.backedUpLeafs.count(installPath))
        return;
    fs::path real = targetRoot / fs::path(installPath).relative_path();
    if (!(fs::exists(real) || fs::is_symlink(real)))
        return;
    fs::path backup =
        db.txn_root("_backup") / fs::path(installPath).relative_path();
    copy_path_exact(real, backup);
    rb.backedUpLeafs.insert(installPath);
    rb.backupRoot = db.txn_root("_backup");
}

static void rollback_copy(const fs::path &targetRoot, const RollbackState &rb) {
    for (auto it = rb.createdLeafs.rbegin(); it != rb.createdLeafs.rend();
         ++it) {
        std::error_code ec;
        fs::remove(targetRoot / fs::path(*it).relative_path(), ec);
    }

    for (const auto &installPath : rb.backedUpLeafs) {
        fs::path dst = targetRoot / fs::path(installPath).relative_path();
        fs::path src = rb.backupRoot / fs::path(installPath).relative_path();
        if (fs::exists(dst) || fs::is_symlink(dst)) {
            std::error_code ec;
            fs::remove(dst, ec);
        }
        if (fs::exists(src) || fs::is_symlink(src))
            copy_path_exact(src, dst);
    }

    auto dirs = rb.createdDirs;
    std::sort(dirs.begin(), dirs.end(),
              [](const std::string &a, const std::string &b) {
                  if (depth_of(a) != depth_of(b))
                      return depth_of(a) > depth_of(b);
                  return a > b;
              });
    for (const auto &path : dirs) {
        std::error_code ec;
        fs::remove(targetRoot / fs::path(path).relative_path(), ec);
    }
}

static void cleanup_txn(const PackageDB &db) {
    std::error_code ec;
    fs::remove_all(db.txn_root("_backup"), ec);
}

static void copy_manifest_to_root(const PackageDB &db, const fs::path &stage,
                                  const fs::path &targetRoot,
                                  const std::string &pkg,
                                  std::map<std::string, std::string> &owners,
                                  const std::vector<ManifestEntry> &manifest,
                                  bool allowOverwriteOwnedBySame,
                                  RollbackState &rb) {
    (void)allowOverwriteOwnedBySame;
    for (const auto &e : manifest) {
        fs::path dst = targetRoot / fs::path(e.installPath).relative_path();
        fs::path src = stage / fs::path(e.installPath).relative_path();

        if (e.type == 'D') {
            if (!fs::exists(dst))
                rb.createdDirs.push_back(e.installPath);
            fs::create_directories(dst);
            continue;
        }

        if (path_is_owned_by_other(owners, e.installPath, pkg)) {
            throw std::runtime_error("conflict: " + e.installPath +
                                     " already owned by " +
                                     owners[e.installPath]);
        }

        if ((fs::exists(dst) || fs::is_symlink(dst)) &&
            !owners.count(e.installPath)) {
            throw std::runtime_error(
                "refusing to overwrite unowned existing path: " +
                e.installPath);
        }

        if (owners.count(e.installPath) && owners[e.installPath] == pkg) {
            backup_existing_if_needed(db, targetRoot, e.installPath, rb);
        } else if (!(fs::exists(dst) || fs::is_symlink(dst))) {
            rb.createdLeafs.push_back(e.installPath);
        }

        copy_path_exact(src, dst);
        owners[e.installPath] = pkg;
    }
}

static std::set<std::string>
manifest_leaf_set(const std::vector<ManifestEntry> &entries) {
    std::set<std::string> s;
    for (const auto &e : entries)
        if (e.type != 'D')
            s.insert(e.installPath);
    return s;
}

static std::set<std::string>
manifest_dir_set(const std::vector<ManifestEntry> &entries) {
    std::set<std::string> s;
    for (const auto &e : entries)
        if (e.type == 'D')
            s.insert(e.installPath);
    return s;
}

static void
remove_obsolete_entries(const std::vector<ManifestEntry> &oldManifest,
                        const std::vector<ManifestEntry> &newManifest,
                        const fs::path &targetRoot, const std::string &pkg,
                        std::map<std::string, std::string> &owners) {
    auto newLeafs = manifest_leaf_set(newManifest);
    auto newDirs = manifest_dir_set(newManifest);

    std::vector<std::string> oldLeafs;
    std::vector<std::string> oldDirs;
    for (const auto &e : oldManifest) {
        if (e.type == 'D')
            oldDirs.push_back(e.installPath);
        else
            oldLeafs.push_back(e.installPath);
    }

    std::sort(oldLeafs.begin(), oldLeafs.end(),
              [](const std::string &a, const std::string &b) {
                  if (depth_of(a) != depth_of(b))
                      return depth_of(a) > depth_of(b);
                  return a > b;
              });
    std::sort(oldDirs.begin(), oldDirs.end(),
              [](const std::string &a, const std::string &b) {
                  if (depth_of(a) != depth_of(b))
                      return depth_of(a) > depth_of(b);
                  return a > b;
              });

    for (const auto &path : oldLeafs) {
        if (newLeafs.count(path))
            continue;
        auto it = owners.find(path);
        if (it != owners.end() && it->second == pkg) {
            std::error_code ec;
            fs::remove(targetRoot / fs::path(path).relative_path(), ec);
            owners.erase(it);
        }
    }

    for (const auto &path : oldDirs) {
        if (newDirs.count(path))
            continue;
        std::error_code ec;
        fs::remove(targetRoot / fs::path(path).relative_path(), ec);
    }
}

static void remove_package_files(const PackageDB &db, const std::string &pkg,
                                 const fs::path &targetRoot,
                                 std::map<std::string, std::string> &owners) {
    auto manifest = db.read_manifest(pkg);
    std::vector<std::string> leafPaths;
    std::vector<std::string> dirs;
    for (const auto &e : manifest) {
        if (e.type == 'D')
            dirs.push_back(e.installPath);
        else
            leafPaths.push_back(e.installPath);
    }

    std::sort(leafPaths.begin(), leafPaths.end(),
              [](const std::string &a, const std::string &b) {
                  if (depth_of(a) != depth_of(b))
                      return depth_of(a) > depth_of(b);
                  return a > b;
              });
    std::sort(dirs.begin(), dirs.end(),
              [](const std::string &a, const std::string &b) {
                  if (depth_of(a) != depth_of(b))
                      return depth_of(a) > depth_of(b);
                  return a > b;
              });

    for (const auto &path : leafPaths) {
        auto it = owners.find(path);
        if (it != owners.end() && it->second == pkg) {
            std::error_code ec;
            fs::remove(targetRoot / fs::path(path).relative_path(), ec);
            owners.erase(it);
        }
    }

    for (const auto &path : dirs) {
        std::error_code ec;
        fs::remove(targetRoot / fs::path(path).relative_path(), ec);
    }

    std::error_code ec1, ec2;
    fs::remove(db.meta_path(pkg), ec1);
    fs::remove(db.manifest_path(pkg), ec2);
}

static void print_tree_rec(const PackageDB &db, const std::string &pkg,
                           std::set<std::string> &seen,
                           const std::string &prefix, bool last) {
    std::cout << prefix;
    if (!prefix.empty())
        std::cout << (last ? "└── " : "├── ");
    std::cout << pkg << "\n";
    if (seen.count(pkg))
        return;
    seen.insert(pkg);

    PackageMeta m = db.read_meta(pkg);
    for (size_t i = 0; i < m.deps.size(); ++i) {
        bool childLast = (i + 1 == m.deps.size());
        print_tree_rec(db, m.deps[i], seen,
                       prefix +
                           (prefix.empty() ? "" : (last ? "    " : "│   ")),
                       childLast);
    }
}

static bool looks_like_elf_candidate(const fs::path &p) {
    std::string name = p.filename().string();
    std::string s = p.generic_string();
    if (name.find(".so") != std::string::npos)
        return true;
    if (s.find("/bin/") != std::string::npos ||
        s.find("/sbin/") != std::string::npos ||
        s.find("/lib/") != std::string::npos ||
        s.find("/lib64/") != std::string::npos)
        return true;
    std::error_code ec;
    auto permBits = fs::status(p, ec).permissions();
    if (!ec) {
        using fs::perms;
        if ((permBits & perms::owner_exec) != perms::none ||
            (permBits & perms::group_exec) != perms::none ||
            (permBits & perms::others_exec) != perms::none)
            return true;
    }
    return false;
}

static std::set<std::string> readelf_needed(const fs::path &file) {
    std::set<std::string> needed;
    std::string cmd = "readelf -d " +
                      shell_escape_single_quotes(file.string()) +
                      " 2>/dev/null";
    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe)
        return needed;

    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) {
        std::string line(buf);
        auto pos = line.find("Shared library: [");
        if (pos == std::string::npos)
            continue;
        pos += std::string("Shared library: [").size();
        auto end = line.find(']', pos);
        if (end == std::string::npos)
            continue;
        needed.insert(line.substr(pos, end - pos));
    }
    pclose(pipe);
    return needed;
}

static std::map<std::string, std::set<std::string>>
build_basename_to_pkg(const std::map<std::string, std::string> &owners) {
    std::map<std::string, std::set<std::string>> out;
    for (const auto &[path, pkg] : owners)
        out[fs::path(path).filename().string()].insert(pkg);
    return out;
}

static PackageSpec parse_meta_file(const fs::path &metaFile) {
    std::ifstream in(metaFile);
    if (!in)
        throw std::runtime_error("cannot open meta file: " + metaFile.string());
    PackageSpec spec;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;
        auto pos = line.find('=');
        if (pos == std::string::npos)
            continue;
        std::string k = trim(line.substr(0, pos));
        std::string v = trim(line.substr(pos + 1));
        if (k == "name")
            spec.name = v;
        else if (k == "version")
            spec.version = v;
        else if (k == "stage_dir")
            spec.stage_dir = v;
        else if (k == "deps")
            spec.deps = split(v, ',');
    }
    if (spec.name.empty())
        throw std::runtime_error("meta file missing name=");
    if (spec.version.empty())
        throw std::runtime_error("meta file missing version=");
    if (spec.stage_dir.empty())
        throw std::runtime_error("meta file missing stage_dir=");
    return spec;
}

static void apply_install_or_upgrade(PackageDB &db, const PackageSpec &spec,
                                     bool forceUpgrade) {
    fs::path targetRoot = default_target_root();
    auto owners = db.load_owners();
    auto newManifest = collect_manifest_from_stage(spec.stage_dir);

    for (const auto &dep : spec.deps) {
        if (!db.installed(dep))
            throw std::runtime_error("dependency not installed: " + dep);
    }

    bool alreadyInstalled = db.installed(spec.name);
    if (alreadyInstalled && !forceUpgrade) {
        throw std::runtime_error("package already installed: " + spec.name +
                                 " (use upgrade or install-meta)");
    }

    std::vector<ManifestEntry> oldManifest;
    if (alreadyInstalled)
        oldManifest = db.read_manifest(spec.name);

    RollbackState rb;
    cleanup_txn(db);

    try {
        copy_manifest_to_root(db, spec.stage_dir, targetRoot, spec.name, owners,
                              newManifest, alreadyInstalled, rb);

        if (alreadyInstalled)
            remove_obsolete_entries(oldManifest, newManifest, targetRoot,
                                    spec.name, owners);

        PackageMeta meta{spec.name, spec.version, now_iso8601_local(),
                         spec.stage_dir.string(), spec.deps};
        db.write_meta_atomic(meta);
        db.write_manifest_atomic(spec.name, newManifest);
        db.save_owners_atomic(owners);
        cleanup_txn(db);

        std::cout << (alreadyInstalled ? "upgraded " : "installed ")
                  << spec.name << "-" << spec.version << "\n";
    } catch (...) {
        rollback_copy(targetRoot, rb);
        cleanup_txn(db);
        throw;
    }
}

static void cmd_install(PackageDB &db, int argc, char **argv) {
    if (argc < 5)
        throw std::runtime_error("usage: lfspkg install <name> <version> "
                                 "<stage_dir> [dep1,dep2,...]");
    PackageSpec spec;
    spec.name = argv[2];
    spec.version = argv[3];
    spec.stage_dir = argv[4];
    spec.deps = (argc >= 6) ? split(argv[5], ',') : std::vector<std::string>{};
    apply_install_or_upgrade(db, spec, false);
}

static void cmd_upgrade(PackageDB &db, int argc, char **argv) {
    if (argc < 5)
        throw std::runtime_error("usage: lfspkg upgrade <name> <version> "
                                 "<stage_dir> [dep1,dep2,...]");
    PackageSpec spec;
    spec.name = argv[2];
    spec.version = argv[3];
    spec.stage_dir = argv[4];
    spec.deps = (argc >= 6) ? split(argv[5], ',') : std::vector<std::string>{};
    if (!db.installed(spec.name))
        throw std::runtime_error("package not installed: " + spec.name);
    apply_install_or_upgrade(db, spec, true);
}

static void cmd_install_meta(PackageDB &db, int argc, char **argv) {
    if (argc < 3)
        throw std::runtime_error("usage: lfspkg install-meta <meta_file>");
    PackageSpec spec = parse_meta_file(argv[2]);
    bool alreadyInstalled = db.installed(spec.name);
    apply_install_or_upgrade(db, spec, alreadyInstalled);
}

static void cmd_remove(PackageDB &db, int argc, char **argv) {
    if (argc < 3)
        throw std::runtime_error("usage: lfspkg remove <name> [--force]");
    const std::string name = argv[2];
    bool force = false;
    for (int i = 3; i < argc; ++i)
        if (std::string(argv[i]) == "--force")
            force = true;
    if (!db.installed(name))
        throw std::runtime_error("package not installed: " + name);

    auto rdeps = reverse_dependencies(db, name);
    if (!force && !rdeps.empty()) {
        std::ostringstream os;
        os << "refusing to remove " << name
           << "; required by: " << join(rdeps, ',');
        throw std::runtime_error(os.str());
    }

    auto owners = db.load_owners();
    remove_package_files(db, name, default_target_root(), owners);
    db.save_owners_atomic(owners);
    std::cout << "removed " << name << "\n";
}

static void cmd_list(PackageDB &db) {
    for (const auto &pkg : db.list_packages()) {
        auto meta = db.read_meta(pkg);
        std::cout << meta.name << ' ' << meta.version << "\n";
    }
}

static void cmd_info(PackageDB &db, int argc, char **argv) {
    if (argc < 3)
        throw std::runtime_error("usage: lfspkg info <name>");
    auto m = db.read_meta(argv[2]);
    auto mf = db.read_manifest(argv[2]);
    auto rdeps = reverse_dependencies(db, argv[2]);
    std::cout << "name: " << m.name << "\n";
    std::cout << "version: " << m.version << "\n";
    std::cout << "installed_at: " << m.install_time << "\n";
    std::cout << "stage_dir: "
              << (m.stage_dir.empty() ? "(unknown)" : m.stage_dir) << "\n";
    std::cout << "deps: " << (m.deps.empty() ? "(none)" : join(m.deps, ','))
              << "\n";
    std::cout << "required_by: "
              << (rdeps.empty() ? "(none)" : join(rdeps, ',')) << "\n";
    std::cout << "entries: " << mf.size() << "\n";
}

static void cmd_tree(PackageDB &db, int argc, char **argv) {
    if (argc < 3)
        throw std::runtime_error("usage: lfspkg tree <name>");
    std::set<std::string> seen;
    print_tree_rec(db, argv[2], seen, "", true);
}

static void cmd_owners(PackageDB &db, int argc, char **argv) {
    if (argc < 3)
        throw std::runtime_error("usage: lfspkg owners </path/to/file>");
    auto owners = db.load_owners();
    std::string path = argv[2];
    if (path.empty() || path[0] != '/')
        path = "/" + path;
    auto it = owners.find(path);
    if (it == owners.end())
        std::cout << "unowned or unknown: " << path << "\n";
    else
        std::cout << path << " -> " << it->second << "\n";
}

static void cmd_verify(PackageDB &db, int argc, char **argv) {
    if (argc < 3)
        throw std::runtime_error("usage: lfspkg verify <name>");
    auto mf = db.read_manifest(argv[2]);
    fs::path root = default_target_root();
    bool ok = true;
    for (const auto &e : mf) {
        fs::path real = root / fs::path(e.installPath).relative_path();
        bool exists = fs::exists(real) || fs::is_symlink(real);
        if (!exists) {
            ok = false;
            std::cout << "missing: " << e.installPath << "\n";
        }
    }
    if (ok)
        std::cout << "ok\n";
}

static void cmd_suggest_deps(PackageDB &db, int argc, char **argv) {
    if (argc < 3)
        throw std::runtime_error("usage: lfspkg suggest-deps <stage_dir>");
    fs::path stage = argv[2];
    auto owners = db.load_owners();
    auto byBase = build_basename_to_pkg(owners);
    std::set<std::string> libs;

    for (fs::recursive_directory_iterator
             it(stage, fs::directory_options::follow_directory_symlink),
         end;
         it != end; ++it) {
        std::error_code ec;
        if (!it->is_regular_file(ec))
            continue;
        if (!looks_like_elf_candidate(it->path()))
            continue;
        auto needed = readelf_needed(it->path());
        libs.insert(needed.begin(), needed.end());
    }

    std::set<std::string> pkgs;
    std::vector<std::string> unresolved;
    for (const auto &lib : libs) {
        auto it = byBase.find(lib);
        if (it == byBase.end())
            unresolved.push_back(lib);
        else
            pkgs.insert(it->second.begin(), it->second.end());
    }

    std::cout << "suggested_packages="
              << (pkgs.empty()
                      ? ""
                      : join(std::vector<std::string>(pkgs.begin(), pkgs.end()),
                             ','))
              << "\n";
    if (!unresolved.empty()) {
        std::sort(unresolved.begin(), unresolved.end());
        std::cout << "unresolved_libs=" << join(unresolved, ',') << "\n";
    }
}

static void usage() {
    std::cout << "lfspkg - 适合 LFS 的极简包跟踪器\n\n"
              << "Environment:\n"
              << "  LFSPKG_DB   包数据库目录 (default: /var/lib/lfspkg)\n"
              << "  LFSPKG_ROOT 安装目标根目录 (default: /)\n\n"
              << "Commands:\n"
              << "  install <name> <version> <stage_dir> [dep1,dep2,...]\n"
              << "  upgrade <name> <version> <stage_dir> [dep1,dep2,...]\n"
              << "  install-meta <meta_file>\n"
              << "  remove <name> [--force]\n"
              << "  list\n"
              << "  info <name>\n"
              << "  tree <name>\n"
              << "  owners </absolute/path>\n"
              << "  verify <name>\n"
              << "  suggest-deps <stage_dir>\n";
}

int main(int argc, char **argv) {
    try {
        if (argc < 2) {
            usage();
            return 1;
        }

        PackageDB db(default_db_root());
        db.ensure();

        std::string cmd = argv[1];
        if (cmd == "install")
            cmd_install(db, argc, argv);
        else if (cmd == "upgrade")
            cmd_upgrade(db, argc, argv);
        else if (cmd == "install-meta")
            cmd_install_meta(db, argc, argv);
        else if (cmd == "remove")
            cmd_remove(db, argc, argv);
        else if (cmd == "list")
            cmd_list(db);
        else if (cmd == "info")
            cmd_info(db, argc, argv);
        else if (cmd == "tree")
            cmd_tree(db, argc, argv);
        else if (cmd == "owners")
            cmd_owners(db, argc, argv);
        else if (cmd == "verify")
            cmd_verify(db, argc, argv);
        else if (cmd == "suggest-deps")
            cmd_suggest_deps(db, argc, argv);
        else {
            usage();
            return 1;
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }
}
