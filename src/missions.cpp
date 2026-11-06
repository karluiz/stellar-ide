#include "missions.hpp"

#include "workspace.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <stdexcept>
#include <sys/wait.h>

namespace raven {
namespace {

namespace fs = std::filesystem;

struct CommandResult {
    int status = -1;
    std::string output;
};

std::string shell_quote(const std::string& value) {
    std::string result = "'";
    for (const char c : value) {
        if (c == '\'') result += "'\\''";
        else result.push_back(c);
    }
    return result + "'";
}

std::string trimmed(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

CommandResult run(const std::string& command) {
    CommandResult result;
    FILE* pipe = popen((command + " 2>&1").c_str(), "r");
    if (!pipe) throw std::runtime_error("Could not start Git");
    std::array<char, 4096> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) result.output += buffer.data();
    const int status = pclose(pipe);
    result.status = status == -1 ? -1 : WIFEXITED(status) ? WEXITSTATUS(status) : 128;
    return result;
}

void require_success(const CommandResult& result, const std::string& action) {
    if (result.status != 0) throw std::runtime_error(action + ": " + trimmed(result.output));
}

bool contained(const fs::path& root, const fs::path& candidate) {
    return candidate == root || std::mismatch(root.begin(), root.end(), candidate.begin()).first == root.end();
}

std::string valid_id(const std::string& id) {
    if (id.empty() || id == "." || id == ".." || id.find_first_of("/\\") != std::string::npos) {
        throw std::runtime_error("Invalid mission identifier");
    }
    return id;
}

void copy_shared(const fs::path& source, const fs::path& destination) {
    if (!fs::exists(source)) return;
    fs::create_directories(destination.parent_path());
    fs::copy(source, destination, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
}

bool binary_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    std::array<char, 4096> buffer{};
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    return std::find(buffer.begin(), buffer.begin() + input.gcount(), '\0') != buffer.begin() + input.gcount();
}

} // namespace

GitMissionStore::GitMissionStore(fs::path workspace)
    : root_(fs::absolute(std::move(workspace)).lexically_normal()), storage_(root_ / ".stellar" / "missions") {}

const fs::path& GitMissionStore::workspace() const { return root_; }

fs::path GitMissionStore::directory(const std::string& id) const {
    return storage_ / valid_id(id);
}

fs::path GitMissionStore::validate_repository(const fs::path& repository) const {
    const auto root = fs::weakly_canonical(root_);
    const auto candidate = fs::weakly_canonical(repository);
    if (!contained(root, candidate)) throw std::runtime_error("Mission repository is outside the workspace");
    const auto top = run("git -C " + shell_quote(candidate.string()) + " rev-parse --show-toplevel");
    require_success(top, "Not a Git worktree");
    if (fs::weakly_canonical(trimmed(top.output)) != candidate) {
        throw std::runtime_error("Mission repository must be the root of its Git worktree");
    }
    return candidate;
}

void GitMissionStore::load() {
    missions_.clear();
    std::error_code error;
    if (!fs::is_directory(storage_, error)) return;
    for (const auto& entry : fs::directory_iterator(storage_, error)) {
        if (!entry.is_directory()) continue;
        const auto manifest = entry.path() / "mission.json";
        if (!fs::is_regular_file(manifest, error)) continue;
        const auto data = nlohmann::json::parse(read_file(manifest));
        GitMission mission;
        mission.id = valid_id(data.at("id").get<std::string>());
        if (directory(mission.id) != entry.path()) throw std::runtime_error("Mission identifier does not match its directory");
        mission.title = data.at("title").get<std::string>();
        mission.repository = fs::path(data.at("repository").get<std::string>());
        mission.worktree = fs::path(data.at("worktree").get<std::string>());
        mission.branch = data.at("branch").get<std::string>();
        mission.base_head = data.at("base_head").get<std::string>();
        mission.status = data.value("status", "ready");
        mission.created_at = data.value("created_at", std::int64_t{0});
        mission.applied_at = data.value("applied_at", std::int64_t{0});
        if (data.contains("applied_hunks")) mission.applied_hunks = data.at("applied_hunks").get<std::vector<std::string>>();
        const auto canonical_root = fs::weakly_canonical(root_);
        if (!contained(canonical_root, fs::weakly_canonical(mission.repository)) ||
            !contained(fs::weakly_canonical(storage_), fs::weakly_canonical(mission.worktree))) {
            throw std::runtime_error("Mission manifest contains an unsafe path");
        }
        missions_.push_back(std::move(mission));
    }
    std::sort(missions_.begin(), missions_.end(), [](const auto& a, const auto& b) {
        return a.created_at > b.created_at;
    });
}

GitMission* GitMissionStore::find(const std::string& id) {
    const auto found = std::find_if(missions_.begin(), missions_.end(), [&](const auto& mission) {
        return mission.id == id;
    });
    return found == missions_.end() ? nullptr : &*found;
}

const GitMission* GitMissionStore::find(const std::string& id) const {
    const auto found = std::find_if(missions_.begin(), missions_.end(), [&](const auto& mission) {
        return mission.id == id;
    });
    return found == missions_.end() ? nullptr : &*found;
}

const std::vector<GitMission>& GitMissionStore::missions() const { return missions_; }

void GitMissionStore::save(const GitMission& mission) const {
    nlohmann::json data = {
        {"version", 1}, {"id", mission.id}, {"title", mission.title},
        {"repository", mission.repository.string()}, {"worktree", mission.worktree.string()},
        {"branch", mission.branch}, {"base_head", mission.base_head}, {"status", mission.status},
        {"created_at", mission.created_at}, {"applied_at", mission.applied_at},
        {"applied_hunks", mission.applied_hunks}
    };
    write_file(directory(mission.id) / "mission.json", data.dump(2) + "\n");
}

GitMission GitMissionStore::create(const fs::path& repository, const std::string& title) {
    load();
    const auto repo = validate_repository(repository);
    if (trimmed(title).empty()) throw std::runtime_error("Describe the mission first");
    fs::create_directories(storage_);
    if (fs::is_directory(root_ / ".git")) {
        const auto exclude = root_ / ".git" / "info" / "exclude";
        const auto current = fs::exists(exclude) ? read_file(exclude) : "";
        if (current.find(".stellar/") == std::string::npos) {
            write_file(exclude, current + (current.empty() || current.back() == '\n' ? "" : "\n") + ".stellar/\n");
        }
    }

    GitMission mission;
    mission.created_at = std::time(nullptr);
    int sequence = static_cast<int>(missions_.size()) + 1;
    do {
        mission.id = "mission-" + std::to_string(mission.created_at) + "-" + std::to_string(sequence++);
    } while (fs::exists(directory(mission.id)));
    mission.title = trimmed(title);
    mission.repository = repo;
    mission.worktree = directory(mission.id) / "worktree";
    mission.branch = "stellar/" + mission.id;
    mission.status = "ready";
    mission.base_head = trimmed(run("git -C " + shell_quote(repo.string()) + " rev-parse HEAD").output);
    if (mission.base_head.empty()) throw std::runtime_error("Mission repository has no HEAD commit");

    const auto added = run("git -C " + shell_quote(repo.string()) + " worktree add -b " +
        shell_quote(mission.branch) + " " + shell_quote(mission.worktree.string()) + " HEAD");
    require_success(added, "Could not create mission worktree");
    try {
        copy_shared(root_ / ".agents", mission.worktree / ".agents");
        copy_shared(root_ / "wiki", mission.worktree / "wiki");
        save(mission);
    } catch (...) {
        run("git -C " + shell_quote(repo.string()) + " worktree remove --force " + shell_quote(mission.worktree.string()));
        run("git -C " + shell_quote(repo.string()) + " branch -D " + shell_quote(mission.branch));
        fs::remove_all(directory(mission.id));
        throw;
    }
    missions_.insert(missions_.begin(), mission);
    return mission;
}

std::string GitMissionStore::patch(const std::string& id) const {
    const auto* mission = find(id);
    if (!mission) throw std::runtime_error("Mission not found");
    if (!fs::is_directory(mission->worktree)) throw std::runtime_error("Mission worktree is missing");
    require_success(run("git -C " + shell_quote(mission->worktree.string()) + " add -A"), "Could not stage mission diff");
    const auto added = run("git -C " + shell_quote(mission->worktree.string()) + " diff --cached --name-only --diff-filter=A HEAD");
    require_success(added, "Could not inspect mission artifacts");
    std::string excluded;
    size_t begin = 0;
    while (begin < added.output.size()) {
        const auto end = added.output.find('\n', begin);
        const auto relative = added.output.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        if (!relative.empty() && binary_file(mission->worktree / relative)) {
            require_success(run("git -C " + shell_quote(mission->worktree.string()) + " reset -q -- " + shell_quote(relative)),
                "Could not exclude mission binary artifact");
            excluded += " " + shell_quote(":(exclude)" + relative);
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    require_success(run("git -C " + shell_quote(mission->worktree.string()) + " add -A -- ." + excluded), "Could not stage mission diff");
    const auto difference = run("git -C " + shell_quote(mission->worktree.string()) +
        " diff --cached --no-ext-diff --binary HEAD -- . ':(exclude).agents' ':(exclude)wiki'");
    require_success(difference, "Could not read mission diff");
    return difference.output;
}

std::string GitMissionStore::diff(const std::string& id) {
    const auto files = hunks(id);
    std::vector<std::string> remaining;
    for (const auto& file : files) for (const auto& hunk : file.hunks) remaining.push_back(hunk.id);
    auto output = build_patch(files, remaining);
    if (output.empty()) return "No unapplied changes in this mission.";
    if (output.size() > 2 * 1024 * 1024) {
        output.resize(2 * 1024 * 1024);
        output += "\n… Diff truncated for display; apply still uses the complete patch.\n";
    }
    return output;
}

std::vector<DiffFile> GitMissionStore::hunks(const std::string& id) {
    const auto* mission = find(id);
    if (!mission) throw std::runtime_error("Mission not found");
    auto files = parse_unified_diff(patch(id));
    for (auto& file : files) {
        file.hunks.erase(std::remove_if(file.hunks.begin(), file.hunks.end(), [&](const auto& hunk) {
            return std::find(mission->applied_hunks.begin(), mission->applied_hunks.end(), hunk.id) != mission->applied_hunks.end();
        }), file.hunks.end());
    }
    return files;
}

void GitMissionStore::apply_hunks(const std::string& id, const std::vector<std::string>& hunk_ids) {
    auto* mission = find(id);
    if (!mission) throw std::runtime_error("Mission not found");
    if (mission->status == "applied") throw std::runtime_error("Mission has already been applied");
    const auto repo = validate_repository(mission->repository);
    const auto selected_patch = build_patch(hunks(id), hunk_ids);
    if (selected_patch.empty()) throw std::runtime_error("Select at least one hunk to apply");
    const auto patch_path = directory(id) / "selected-hunks.patch";
    write_file(patch_path, selected_patch);
    const auto quoted_patch = shell_quote(patch_path.string());
    require_success(run("git -C " + shell_quote(repo.string()) + " apply --check --whitespace=nowarn " + quoted_patch),
        "Selected mission hunks conflict with the current workspace");
    require_success(run("git -C " + shell_quote(repo.string()) + " apply --whitespace=nowarn " + quoted_patch),
        "Could not apply selected mission hunks");
    for (const auto& hunk_id : hunk_ids) {
        if (std::find(mission->applied_hunks.begin(), mission->applied_hunks.end(), hunk_id) == mission->applied_hunks.end()) {
            mission->applied_hunks.push_back(hunk_id);
        }
    }
    mission->status = "partially-applied";
    mission->applied_at = std::time(nullptr);
    save(*mission);
}

void GitMissionStore::apply(const std::string& id) {
    auto* mission = find(id);
    if (!mission) throw std::runtime_error("Mission not found");
    if (mission->status == "applied") throw std::runtime_error("Mission has already been applied");
    const auto repo = validate_repository(mission->repository);
    const auto files = hunks(id);
    std::vector<std::string> remaining;
    for (const auto& file : files) for (const auto& hunk : file.hunks) remaining.push_back(hunk.id);
    const auto difference = build_patch(files, remaining);
    if (difference.empty()) {
        mission->status = "applied";
        mission->applied_at = std::time(nullptr);
        save(*mission);
        return;
    }
    const auto patch_path = directory(id) / "mission.patch";
    write_file(patch_path, difference);
    const auto quoted_patch = shell_quote(patch_path.string());
    require_success(run("git -C " + shell_quote(repo.string()) + " apply --check --whitespace=nowarn " + quoted_patch),
        "Mission conflicts with the current workspace");
    require_success(run("git -C " + shell_quote(repo.string()) + " apply --whitespace=nowarn " + quoted_patch),
        "Could not apply mission");
    mission->status = "applied";
    mission->applied_at = std::time(nullptr);
    save(*mission);
}

void GitMissionStore::discard(const std::string& id) {
    const auto* mission = find(id);
    if (!mission) throw std::runtime_error("Mission not found");
    const auto repo = validate_repository(mission->repository);
    if (fs::exists(mission->worktree)) {
        require_success(run("git -C " + shell_quote(repo.string()) + " worktree remove --force " + shell_quote(mission->worktree.string())),
            "Could not remove mission worktree");
    }
    require_success(run("git -C " + shell_quote(repo.string()) + " branch -D " + shell_quote(mission->branch)),
        "Could not remove mission branch");
    fs::remove_all(directory(id));
    missions_.erase(std::remove_if(missions_.begin(), missions_.end(), [&](const auto& item) {
        return item.id == id;
    }), missions_.end());
}

} // namespace raven
