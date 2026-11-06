#pragma once

#include <string>
#include <vector>

namespace raven {

struct DiffHunk {
    std::string id;
    std::string path;
    std::string header;
    std::string body;
};

struct DiffFile {
    std::string path;
    std::string header;
    std::vector<DiffHunk> hunks;
};

std::vector<DiffFile> parse_unified_diff(const std::string& text);
std::string build_patch(const std::vector<DiffFile>& files, const std::vector<std::string>& hunk_ids);
std::string hunk_label(const DiffHunk& hunk);

} // namespace raven
