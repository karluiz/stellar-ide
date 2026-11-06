#pragma once

#include "diff.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace raven {

struct GitMission {
    std::string id;
    std::string title;
    std::filesystem::path repository;
    std::filesystem::path worktree;
    std::string branch;
    std::string base_head;
    std::string status;
    std::vector<std::string> applied_hunks;
    std::int64_t created_at = 0;
    std::int64_t applied_at = 0;
};

class GitMissionStore {
public:
    explicit GitMissionStore(std::filesystem::path workspace);

    const std::filesystem::path& workspace() const;
    void load();
    GitMission create(const std::filesystem::path& repository, const std::string& title);
    GitMission* find(const std::string& id);
    const GitMission* find(const std::string& id) const;
    const std::vector<GitMission>& missions() const;
    std::string diff(const std::string& id);
    std::vector<DiffFile> hunks(const std::string& id);
    void apply(const std::string& id);
    void apply_hunks(const std::string& id, const std::vector<std::string>& hunk_ids);
    void discard(const std::string& id);

private:
    std::filesystem::path root_;
    std::filesystem::path storage_;
    std::vector<GitMission> missions_;

    std::filesystem::path directory(const std::string& id) const;
    std::filesystem::path validate_repository(const std::filesystem::path& repository) const;
    std::string patch(const std::string& id) const;
    void save(const GitMission& mission) const;
};

} // namespace raven
