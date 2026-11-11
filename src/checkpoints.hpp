#pragma once

#include "diff.hpp"
#include "verification.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace raven {

// Directories every workspace scan skips: VCS metadata, Stellar state, build output, dependencies.
bool ignored_directory(const std::filesystem::path& path);
// Executables, object files and libraries produced by builds.
bool compiled_artifact(const std::filesystem::path& path);

enum class CheckpointChangeKind {
    Added,
    Modified,
    Deleted,
};

struct CheckpointChange {
    std::string path;
    CheckpointChangeKind kind = CheckpointChangeKind::Modified;
};

struct TaskCheckpoint {
    std::string id;
    std::string title;
    std::string status;
    std::int64_t created_at = 0;
    std::int64_t finished_at = 0;
    bool partial = false;
    std::vector<std::string> applied_hunks;
    std::vector<CheckpointChange> changes;
    std::vector<VerificationCheck> checks;
};

class CheckpointStore {
public:
    explicit CheckpointStore(std::filesystem::path workspace);

    void load();
    std::string begin(const std::string& title);
    TaskCheckpoint finish(const std::string& id, const std::string& status,
                          const std::vector<VerificationCheck>& checks = {});
    const TaskCheckpoint* find(const std::string& id) const;
    const std::vector<TaskCheckpoint>& checkpoints() const;
    std::string diff(const std::string& id, const std::string& relative) const;
    std::vector<DiffFile> hunks(const std::string& id) const;
    void update_check(const std::string& id, const VerificationCheck& check);
    void restore(const std::string& id);
    void restore_hunks(const std::string& id, const std::vector<std::string>& hunk_ids);

private:
    std::filesystem::path root_;
    std::filesystem::path storage_;
    std::vector<TaskCheckpoint> checkpoints_;

    std::filesystem::path directory(const std::string& id) const;
    void save(const TaskCheckpoint& checkpoint) const;
};

} // namespace raven
