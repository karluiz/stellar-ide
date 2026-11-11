#include "checkpoints.hpp"

#include "workspace.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>

namespace fs = std::filesystem;

namespace raven {
namespace {

constexpr std::uintmax_t max_file_size = 2 * 1024 * 1024;
constexpr std::uintmax_t max_snapshot_size = 128 * 1024 * 1024;
constexpr size_t max_snapshot_files = 12'000;

struct WorkspaceScan {
    std::map<std::string, fs::path> files;
    bool partial = false;
};

bool ignored_directory(const fs::path& path) {
    const auto name = path.filename().string();
    return name == ".git" || name == ".stellar" || name == "build" || name == "node_modules" ||
        name == "dist" || name == "out" || name == "target" || name == ".next" || name == ".cache" ||
        name == ".venv" || name == "venv" || name == "__pycache__";
}

// Executables, object files and archives produced by builds. They are rebuilt rather than reviewed,
// and a compiled binary otherwise appears in Review as an unreadable "changed file".
bool compiled_artifact(const fs::path& path) {
    const auto extension = path.extension().string();
    static const char* extensions[] = {".o", ".obj", ".a", ".so", ".dylib", ".dll", ".exe", ".lib", ".pyc", ".class", ".wasm"};
    for (const auto* candidate : extensions) {
        if (extension == candidate) return true;
    }
    std::ifstream input(path, std::ios::binary);
    unsigned char magic[4] = {};
    if (!input.read(reinterpret_cast<char*>(magic), sizeof(magic))) return false;
    const auto is = [&](unsigned char a, unsigned char b, unsigned char c, unsigned char d) {
        return magic[0] == a && magic[1] == b && magic[2] == c && magic[3] == d;
    };
    return is(0x7F, 'E', 'L', 'F') ||                                  // ELF
        is(0xCF, 0xFA, 0xED, 0xFE) || is(0xCE, 0xFA, 0xED, 0xFE) ||    // Mach-O, little-endian
        is(0xFE, 0xED, 0xFA, 0xCF) || is(0xFE, 0xED, 0xFA, 0xCE) ||    // Mach-O, big-endian
        is(0xCA, 0xFE, 0xBA, 0xBE);                                     // universal Mach-O
}

WorkspaceScan scan_workspace(const fs::path& root) {
    WorkspaceScan result;
    std::error_code error;
    fs::recursive_directory_iterator iterator(root, fs::directory_options::skip_permission_denied, error);
    const fs::recursive_directory_iterator end;
    std::uintmax_t total_size = 0;
    while (iterator != end) {
        if (error) {
            result.partial = true;
            error.clear();
            iterator.increment(error);
            continue;
        }
        const auto& entry = *iterator;
        if (entry.is_directory()) {
            if (ignored_directory(entry.path())) iterator.disable_recursion_pending();
        } else if (entry.is_regular_file() && !entry.is_symlink() && !compiled_artifact(entry.path())) {
            const auto size = entry.file_size(error);
            if (error) {
                result.partial = true;
                error.clear();
            } else if (size > max_file_size || total_size + size > max_snapshot_size ||
                       result.files.size() >= max_snapshot_files) {
                result.partial = true;
            } else {
                auto relative = fs::relative(entry.path(), root, error).generic_string();
                if (error) {
                    result.partial = true;
                    error.clear();
                } else {
                    total_size += size;
                    result.files.emplace(std::move(relative), entry.path());
                }
            }
        }
        iterator.increment(error);
    }
    return result;
}

std::string valid_relative(const std::string& value) {
    const fs::path path(value);
    if (path.empty() || path.is_absolute()) throw std::runtime_error("Checkpoint contains an invalid file path");
    for (const auto& part : path) {
        if (part == "..") throw std::runtime_error("Checkpoint file path escapes the workspace");
    }
    return path.lexically_normal().generic_string();
}

bool contained(const fs::path& root, const fs::path& candidate) {
    return candidate == root || std::mismatch(root.begin(), root.end(), candidate.begin()).first == root.end();
}

fs::path workspace_file(const fs::path& root, const std::string& relative) {
    const auto canonical_root = fs::weakly_canonical(root);
    const auto candidate = fs::weakly_canonical(root / fs::path(valid_relative(relative)));
    if (!contained(canonical_root, candidate)) throw std::runtime_error("Checkpoint file path escapes the workspace");
    return candidate;
}

std::vector<CheckpointChange> read_changes(const nlohmann::json& data) {
    std::vector<CheckpointChange> changes;
    for (const auto& item : data.at("changes")) {
        const auto kind = item.at("kind").get<std::string>();
        changes.push_back({valid_relative(item.at("path").get<std::string>()),
            kind == "added" ? CheckpointChangeKind::Added
                : kind == "deleted" ? CheckpointChangeKind::Deleted : CheckpointChangeKind::Modified});
    }
    return changes;
}

std::vector<VerificationCheck> read_checks(const nlohmann::json& data) {
    std::vector<VerificationCheck> checks;
    if (!data.contains("checks")) return checks;
    for (const auto& item : data.at("checks")) {
        const auto status = item.value("status", "failed");
        checks.push_back({item.value("id", ""), item.at("command").get<std::string>(), item.value("kind", "test"),
            status == "passed" ? VerificationStatus::Passed
                : status == "running" ? VerificationStatus::Running : VerificationStatus::Failed,
            item.value("output", "")});
    }
    return checks;
}

const char* change_kind(CheckpointChangeKind kind) {
    switch (kind) {
    case CheckpointChangeKind::Added: return "added";
    case CheckpointChangeKind::Modified: return "modified";
    case CheckpointChangeKind::Deleted: return "deleted";
    }
    return "modified";
}

bool binary(const std::string& content) {
    return content.find('\0') != std::string::npos;
}

std::string shell_quote(const std::string& value) {
    std::string result = "'";
    for (const char c : value) {
        if (c == '\'') result += "'\\''";
        else result.push_back(c);
    }
    return result + "'";
}

void apply_patch(const fs::path& root, const fs::path& patch, bool reverse) {
    const auto command = "git -C " + shell_quote(root.string()) + " apply " + (reverse ? "-R " : "") +
        "--whitespace=nowarn " + shell_quote(patch.string());
    if (std::system((command + " --check >/dev/null 2>&1").c_str()) != 0) {
        throw std::runtime_error("Selected hunks conflict with the current files");
    }
    if (std::system((command + " >/dev/null 2>&1").c_str()) != 0) {
        throw std::runtime_error("Selected hunks could not be applied");
    }
}

} // namespace

CheckpointStore::CheckpointStore(fs::path workspace)
    : root_(fs::absolute(std::move(workspace)).lexically_normal()), storage_(root_ / ".stellar" / "checkpoints") {}

fs::path CheckpointStore::directory(const std::string& id) const {
    if (id.empty() || id.find_first_of("/\\") != std::string::npos || id == "." || id == "..") {
        throw std::runtime_error("Invalid checkpoint identifier");
    }
    return storage_ / id;
}

void CheckpointStore::load() {
    checkpoints_.clear();
    std::error_code error;
    if (!fs::is_directory(storage_, error)) return;
    for (const auto& entry : fs::directory_iterator(storage_, error)) {
        if (!entry.is_directory()) continue;
        const auto manifest = entry.path() / "checkpoint.json";
        if (!fs::is_regular_file(manifest, error)) continue;
        const auto data = nlohmann::json::parse(read_file(manifest));
        TaskCheckpoint checkpoint;
        checkpoint.id = data.at("id").get<std::string>();
        if (directory(checkpoint.id) != entry.path()) throw std::runtime_error("Checkpoint identifier does not match its directory");
        checkpoint.title = data.at("title").get<std::string>();
        checkpoint.status = data.value("status", "complete");
        checkpoint.created_at = data.value("created_at", std::int64_t{0});
        checkpoint.finished_at = data.value("finished_at", std::int64_t{0});
        checkpoint.partial = data.value("partial", false);
        if (data.contains("applied_hunks")) checkpoint.applied_hunks = data.at("applied_hunks").get<std::vector<std::string>>();
        checkpoint.changes = read_changes(data);
        checkpoint.checks = read_checks(data);
        if (checkpoint.status != "recording" && (!checkpoint.changes.empty() || !checkpoint.checks.empty())) {
            checkpoints_.push_back(std::move(checkpoint));
        }
    }
    std::sort(checkpoints_.begin(), checkpoints_.end(), [](const auto& a, const auto& b) {
        return a.created_at > b.created_at;
    });
}

void CheckpointStore::save(const TaskCheckpoint& checkpoint) const {
    nlohmann::json changes = nlohmann::json::array();
    for (const auto& change : checkpoint.changes) {
        changes.push_back({{"path", change.path}, {"kind", change_kind(change.kind)}});
    }
    nlohmann::json checks = nlohmann::json::array();
    for (const auto& check : checkpoint.checks) {
        checks.push_back({{"id", check.id}, {"command", check.command}, {"kind", check.kind},
            {"status", verification_status_name(check.status)}, {"output", check.output}});
    }
    const nlohmann::json data = {
        {"version", 1}, {"id", checkpoint.id}, {"title", checkpoint.title}, {"status", checkpoint.status},
        {"created_at", checkpoint.created_at}, {"finished_at", checkpoint.finished_at},
        {"partial", checkpoint.partial}, {"applied_hunks", checkpoint.applied_hunks}, {"changes", std::move(changes)}, {"checks", std::move(checks)}
    };
    write_file(directory(checkpoint.id) / "checkpoint.json", data.dump(2) + "\n");
}

std::string CheckpointStore::begin(const std::string& title) {
    static int sequence = 0;
    fs::create_directories(storage_);
    std::string id;
    do {
        id = "run-" + std::to_string(std::time(nullptr)) + "-" + std::to_string(++sequence);
    } while (fs::exists(directory(id)));

    TaskCheckpoint checkpoint;
    checkpoint.id = id;
    checkpoint.title = title.empty() ? "Agent task" : title;
    checkpoint.status = "recording";
    checkpoint.created_at = std::time(nullptr);
    const auto dir = directory(id);
    try {
        const auto scan = scan_workspace(root_);
        checkpoint.partial = scan.partial;
        for (const auto& [relative, source] : scan.files) {
            const auto destination = dir / "before" / fs::path(relative);
            fs::create_directories(destination.parent_path());
            fs::copy_file(source, destination, fs::copy_options::none);
        }
        save(checkpoint);
    } catch (...) {
        std::error_code error;
        fs::remove_all(dir, error);
        throw;
    }
    return id;
}

TaskCheckpoint CheckpointStore::finish(const std::string& id, const std::string& status,
                                       const std::vector<VerificationCheck>& checks) {
    const auto dir = directory(id);
    const auto manifest = dir / "checkpoint.json";
    if (!fs::is_regular_file(manifest)) throw std::runtime_error("Checkpoint is no longer available");
    const auto data = nlohmann::json::parse(read_file(manifest));
    TaskCheckpoint checkpoint;
    checkpoint.id = id;
    checkpoint.title = data.at("title").get<std::string>();
    checkpoint.status = status;
    checkpoint.created_at = data.value("created_at", std::int64_t{0});
    checkpoint.finished_at = std::time(nullptr);
    checkpoint.partial = data.value("partial", false);
    checkpoint.checks = checks;

    WorkspaceScan before_scan;
    if (fs::is_directory(dir / "before")) {
        for (const auto& entry : fs::recursive_directory_iterator(dir / "before")) {
            if (entry.is_regular_file()) {
                before_scan.files[fs::relative(entry.path(), dir / "before").generic_string()] = entry.path();
            }
        }
    }
    const auto after_scan = scan_workspace(root_);
    checkpoint.partial = checkpoint.partial || after_scan.partial;
    std::set<std::string> changed;
    for (const auto& [relative, current] : after_scan.files) {
        const auto before = before_scan.files.find(relative);
        if (before == before_scan.files.end() || read_file(before->second) != read_file(current)) {
            changed.insert(relative);
            checkpoint.changes.push_back({relative, before == before_scan.files.end() ? CheckpointChangeKind::Added : CheckpointChangeKind::Modified});
        }
    }
    for (const auto& [relative, before] : before_scan.files) {
        if (!after_scan.files.count(relative)) {
            changed.insert(relative);
            checkpoint.changes.push_back({relative, CheckpointChangeKind::Deleted});
        }
    }
    std::sort(checkpoint.changes.begin(), checkpoint.changes.end(), [](const auto& a, const auto& b) {
        return a.path < b.path;
    });

    if (checkpoint.changes.empty() && checkpoint.checks.empty()) {
        fs::remove_all(dir);
        checkpoints_.erase(std::remove_if(checkpoints_.begin(), checkpoints_.end(), [&](const auto& item) {
            return item.id == id;
        }), checkpoints_.end());
        return checkpoint;
    }

    for (const auto& change : checkpoint.changes) {
        if (change.kind == CheckpointChangeKind::Deleted) continue;
        const auto source = workspace_file(root_, change.path);
        const auto destination = dir / "after" / fs::path(change.path);
        fs::create_directories(destination.parent_path());
        fs::copy_file(source, destination, fs::copy_options::overwrite_existing);
    }
    for (const auto& [relative, before] : before_scan.files) {
        if (!changed.count(relative)) fs::remove(before);
    }
    save(checkpoint);
    const auto existing = std::find_if(checkpoints_.begin(), checkpoints_.end(), [&](const auto& item) {
        return item.id == id;
    });
    if (existing == checkpoints_.end()) checkpoints_.push_back(checkpoint);
    else *existing = checkpoint;
    std::sort(checkpoints_.begin(), checkpoints_.end(), [](const auto& a, const auto& b) {
        return a.created_at > b.created_at;
    });
    return checkpoint;
}

const TaskCheckpoint* CheckpointStore::find(const std::string& id) const {
    const auto found = std::find_if(checkpoints_.begin(), checkpoints_.end(), [&](const auto& checkpoint) {
        return checkpoint.id == id;
    });
    return found == checkpoints_.end() ? nullptr : &*found;
}

const std::vector<TaskCheckpoint>& CheckpointStore::checkpoints() const { return checkpoints_; }

std::string CheckpointStore::diff(const std::string& id, const std::string& relative) const {
    const auto* checkpoint = find(id);
    if (!checkpoint) throw std::runtime_error("Checkpoint not found");
    const auto normalized = valid_relative(relative);
    const auto change = std::find_if(checkpoint->changes.begin(), checkpoint->changes.end(), [&](const auto& item) {
        return item.path == normalized;
    });
    if (change == checkpoint->changes.end()) throw std::runtime_error("File is not part of this checkpoint");
    const auto dir = directory(id);
    const auto before_path = dir / "before" / fs::path(normalized);
    const auto after_path = dir / "after" / fs::path(normalized);
    const auto before = fs::is_regular_file(before_path) ? read_file(before_path) : "";
    const auto after = fs::is_regular_file(after_path) ? read_file(after_path) : "";
    if (binary(before) || binary(after)) return "Binary file changed; no text diff is available.\n";
    return Workspace::diff(before, after);
}

void CheckpointStore::update_check(const std::string& id, const VerificationCheck& check) {
    const auto checkpoint = std::find_if(checkpoints_.begin(), checkpoints_.end(), [&](const auto& item) {
        return item.id == id;
    });
    if (checkpoint == checkpoints_.end()) throw std::runtime_error("Checkpoint not found");
    const auto existing = std::find_if(checkpoint->checks.begin(), checkpoint->checks.end(), [&](const auto& item) {
        return item.id == check.id;
    });
    if (existing == checkpoint->checks.end()) throw std::runtime_error("Verification check not found");
    *existing = check;
    save(*checkpoint);
}

std::vector<DiffFile> CheckpointStore::hunks(const std::string& id) const {
    const auto* checkpoint = find(id);
    if (!checkpoint) throw std::runtime_error("Checkpoint not found");
    std::vector<DiffFile> result;
    for (const auto& change : checkpoint->changes) {
        const auto normalized = valid_relative(change.path);
        auto difference = diff(id, normalized);
        if (difference.find("Binary file changed") == 0) continue;
        if (const auto position = difference.find("--- before\n"); position != std::string::npos) {
            difference.replace(position, 11, "--- a/" + normalized + "\n");
        }
        if (const auto position = difference.find("+++ proposed\n"); position != std::string::npos) {
            difference.replace(position, 13, "+++ b/" + normalized + "\n");
        }
        const auto dir = directory(id);
        const auto before_path = dir / "before" / fs::path(normalized);
        const auto after_path = dir / "after" / fs::path(normalized);
        const auto before = fs::is_regular_file(before_path) ? read_file(before_path) : "";
        const auto after = fs::is_regular_file(after_path) ? read_file(after_path) : "";
        const auto line_count = [](const std::string& content) {
            size_t count = 0;
            for (const char c : content) if (c == '\n') ++count;
            if (!content.empty() && content.back() != '\n') ++count;
            return count;
        };
        const auto hunk_header = "@@ -" + (before.empty() ? std::string("0,0") : "1," + std::to_string(line_count(before))) +
            " +" + (after.empty() ? std::string("0,0") : "1," + std::to_string(line_count(after))) + " @@\n";
        const auto marker = "+++ b/" + normalized + "\n";
        if (const auto position = difference.find(marker); position != std::string::npos) {
            difference.insert(position + marker.size(), hunk_header);
        }
        const auto normalize = [&](const std::string& from, const std::string& to) {
            size_t position = 0;
            while ((position = difference.find(from, position)) != std::string::npos) difference.replace(position, from.size(), to);
        };
        normalize("\n  ", "\n ");
        normalize("\n+ ", "\n+");
        normalize("\n- ", "\n-");
        if (difference.size() >= 2 && (difference.ends_with(" \n") || difference.ends_with("+\n") || difference.ends_with("-\n"))) {
            difference.resize(difference.size() - 2);
        }
        std::string metadata;
        if (change.kind == CheckpointChangeKind::Added) {
            const auto marker = "--- a/" + normalized + "\n";
            if (const auto position = difference.find(marker); position != std::string::npos) {
                difference.replace(position, marker.size(), "--- /dev/null\n");
            }
            metadata = "new file mode 100644\n";
        } else if (change.kind == CheckpointChangeKind::Deleted) {
            const auto marker = "+++ b/" + normalized + "\n";
            if (const auto position = difference.find(marker); position != std::string::npos) {
                difference.replace(position, marker.size(), "+++ /dev/null\n");
            }
            metadata = "deleted file mode 100644\n";
        }
        const auto text = "diff --git a/" + normalized + " b/" + normalized + "\n" + metadata + difference;
        auto parsed = parse_unified_diff(text);
        for (auto& file : parsed) {
            file.hunks.erase(std::remove_if(file.hunks.begin(), file.hunks.end(), [&](const auto& hunk) {
                return std::find(checkpoint->applied_hunks.begin(), checkpoint->applied_hunks.end(), hunk.id) != checkpoint->applied_hunks.end();
            }), file.hunks.end());
        }
        result.insert(result.end(), parsed.begin(), parsed.end());
    }
    return result;
}

void CheckpointStore::restore_hunks(const std::string& id, const std::vector<std::string>& hunk_ids) {
    const auto* checkpoint = find(id);
    if (!checkpoint) throw std::runtime_error("Checkpoint not found");
    const auto files = hunks(id);
    const auto selected_patch = build_patch(files, hunk_ids);
    if (selected_patch.empty()) throw std::runtime_error("Select at least one text hunk to restore");
    const auto dir = directory(id);
    std::set<std::string> paths;
    for (const auto& file : files) {
        for (const auto& hunk : file.hunks) {
            if (std::find(hunk_ids.begin(), hunk_ids.end(), hunk.id) != hunk_ids.end()) paths.insert(file.path);
        }
    }
    for (const auto& path : paths) {
        const auto normalized = valid_relative(path);
        const auto current_path = workspace_file(root_, normalized);
        const auto after_path = dir / "after" / fs::path(normalized);
        if (!fs::is_regular_file(after_path)) {
            if (fs::exists(current_path)) throw std::runtime_error("Cannot restore " + normalized + ": the file was recreated after this run");
        } else if (!fs::is_regular_file(current_path) || read_file(after_path) != read_file(current_path)) {
            throw std::runtime_error("Cannot restore " + normalized + ": the file changed after this run");
        }
    }
    const auto patch_path = dir / "restore-hunks.patch";
    write_file(patch_path, selected_patch);
    apply_patch(root_, patch_path, true);
    auto restored = *checkpoint;
    for (const auto& hunk_id : hunk_ids) {
        if (std::find(restored.applied_hunks.begin(), restored.applied_hunks.end(), hunk_id) == restored.applied_hunks.end()) {
            restored.applied_hunks.push_back(hunk_id);
        }
    }
    restored.status = "partially-restored";
    restored.finished_at = std::time(nullptr);
    save(restored);
    const auto existing = std::find_if(checkpoints_.begin(), checkpoints_.end(), [&](const auto& item) {
        return item.id == id;
    });
    *existing = std::move(restored);
}

void CheckpointStore::restore(const std::string& id) {
    const auto* checkpoint = find(id);
    if (!checkpoint) throw std::runtime_error("Checkpoint not found");
    if (checkpoint->status == "restored") throw std::runtime_error("This task has already been restored");
    const auto dir = directory(id);
    if (!checkpoint->applied_hunks.empty()) {
        const auto files = hunks(id);
        std::vector<std::string> remaining;
        for (const auto& file : files) for (const auto& hunk : file.hunks) remaining.push_back(hunk.id);
        if (!remaining.empty()) restore_hunks(id, remaining);
        auto restored = *find(id);
        restored.status = "restored";
        restored.finished_at = std::time(nullptr);
        save(restored);
        const auto existing = std::find_if(checkpoints_.begin(), checkpoints_.end(), [&](const auto& item) {
            return item.id == id;
        });
        *existing = std::move(restored);
        return;
    }

    for (const auto& change : checkpoint->changes) {
        const auto current_path = workspace_file(root_, change.path);
        const auto after_path = dir / "after" / fs::path(change.path);
        if (change.kind == CheckpointChangeKind::Deleted) {
            if (fs::exists(current_path)) throw std::runtime_error("Cannot restore " + change.path + ": the file was recreated after this run");
        } else {
            if (!fs::is_regular_file(after_path) || !fs::is_regular_file(current_path) ||
                read_file(after_path) != read_file(current_path)) {
                throw std::runtime_error("Cannot restore " + change.path + ": the file changed after this run");
            }
        }
    }

    std::vector<CheckpointChange> applied;
    try {
        for (const auto& change : checkpoint->changes) {
            const auto current_path = workspace_file(root_, change.path);
            if (change.kind == CheckpointChangeKind::Added) {
                fs::remove(current_path);
            } else {
                const auto before_path = dir / "before" / fs::path(change.path);
                if (!fs::is_regular_file(before_path)) throw std::runtime_error("Checkpoint content is missing for " + change.path);
                write_file(current_path, read_file(before_path));
            }
            applied.push_back(change);
        }
    } catch (...) {
        for (const auto& change : applied) {
            const auto current_path = workspace_file(root_, change.path);
            std::error_code error;
            if (change.kind == CheckpointChangeKind::Deleted) {
                fs::remove(current_path, error);
            } else {
                const auto after_path = dir / "after" / fs::path(change.path);
                if (fs::is_regular_file(after_path)) write_file(current_path, read_file(after_path));
            }
        }
        throw;
    }

    auto restored = *checkpoint;
    restored.status = "restored";
    restored.finished_at = std::time(nullptr);
    save(restored);
    const auto existing = std::find_if(checkpoints_.begin(), checkpoints_.end(), [&](const auto& item) {
        return item.id == id;
    });
    *existing = std::move(restored);
}

} // namespace raven
