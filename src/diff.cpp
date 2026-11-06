#include "diff.hpp"

#include <set>

namespace raven {
namespace {

std::string patch_path(std::string value) {
    if (value == "/dev/null") return value;
    if (value.rfind("a/", 0) == 0 || value.rfind("b/", 0) == 0 || value.rfind("c/", 0) == 0 || value.rfind("i/", 0) == 0) return value.substr(2);
    return value;
}

} // namespace

std::vector<DiffFile> parse_unified_diff(const std::string& text) {
    std::vector<DiffFile> files;
    DiffFile* file = nullptr;
    DiffHunk* hunk = nullptr;
    size_t begin = 0;
    int index = 0;
    while (begin < text.size()) {
        const auto end = text.find('\n', begin);
        const auto line = text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        if (line.rfind("diff --git ", 0) == 0) {
            const auto first_space = line.find(" a/");
            const auto second_space = first_space == std::string::npos ? std::string::npos : line.find(" b/", first_space + 3);
            const auto path = second_space == std::string::npos ? std::string{} : line.substr(first_space + 3, second_space - first_space - 3);
            files.push_back({patch_path(path), line + "\n", {}});
            file = &files.back();
            hunk = nullptr;
            index = 0;
        } else if (line.rfind("@@ ", 0) == 0 && file) {
            file->hunks.push_back({file->path + "#" + std::to_string(index++), file->path, line + "\n", {}});
            hunk = &file->hunks.back();
        } else if (file && !hunk && line.rfind("--- ", 0) == 0) {
            file->header += line + "\n";
            if (file->path.empty() || file->path == "/dev/null") file->path = patch_path(line.substr(4));
        } else if (file && !hunk && line.rfind("+++ ", 0) == 0) {
            file->header += line + "\n";
            if (file->path.empty() || file->path == "/dev/null") file->path = patch_path(line.substr(4));
        } else if (file && !hunk) {
            file->header += line + "\n";
        } else if (hunk) {
            hunk->body += line + "\n";
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return files;
}

std::string build_patch(const std::vector<DiffFile>& files, const std::vector<std::string>& hunk_ids) {
    const std::set<std::string> selected(hunk_ids.begin(), hunk_ids.end());
    std::string output;
    for (const auto& file : files) {
        bool include_file = false;
        for (const auto& hunk : file.hunks) include_file |= selected.count(hunk.id) > 0;
        if (!include_file) continue;
        output += file.header;
        for (const auto& hunk : file.hunks) {
            if (selected.count(hunk.id)) output += hunk.header + hunk.body;
        }
    }
    return output;
}

std::string hunk_label(const DiffHunk& hunk) {
    auto label = hunk.path + " · " + hunk.header;
    if (label.size() > 100) label = label.substr(0, 99) + "…";
    return label;
}

} // namespace raven
