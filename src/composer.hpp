#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace raven {

struct CompletionItem {
    std::string text;
    std::string detail;
};

struct ContentMatch {
    std::string path;
    int line = 0;
    std::string text;
};

struct ContextAttachment {
    std::filesystem::path path;
    std::uintmax_t bytes = 0;
    std::uintmax_t tokens = 0;
};

class ComposerIndex {
public:
    void set_workspace(std::filesystem::path root);
    void refresh();

    std::vector<CompletionItem> skills(const std::string& prefix, bool namespaced) const;
    std::vector<CompletionItem> files(const std::string& directory, const std::string& prefix) const;
    std::vector<CompletionItem> search_files(const std::string& query, size_t limit = 80) const;
    std::vector<ContentMatch> search_content(const std::string& query, size_t limit = 80) const;
    std::vector<std::filesystem::path> mentions(const std::string& text) const;
    std::vector<ContextAttachment> mention_details(const std::string& text) const;
    std::string strip_mentions(const std::string& text) const;
    std::string strip_mention(const std::string& text, const std::string& relative) const;
    bool has_skill(const std::string& name) const;
    bool has_file(const std::string& relative) const;

private:
    std::filesystem::path root_;
    std::vector<CompletionItem> skills_;
    std::vector<std::string> files_;
    std::unordered_set<std::string> file_set_;
};

} // namespace raven
