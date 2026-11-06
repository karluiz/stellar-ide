#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace raven {

struct EditorRecovery {
    std::string kind;
    std::filesystem::path path;
    std::string content;
    std::string saved_content;
};

class RecoveryStore {
public:
    explicit RecoveryStore(std::filesystem::path workspace);

    void load();
    void save() const;
    void set_draft(const std::string& text);
    void clear_draft();
    const std::string& draft() const;
    void set_editor(const std::string& kind, const std::filesystem::path& path,
                    const std::string& content, const std::string& saved_content);
    void clear_editor(const std::string& kind, const std::filesystem::path& path);
    const std::vector<EditorRecovery>& editors() const;

private:
    std::filesystem::path path_;
    std::string draft_;
    std::vector<EditorRecovery> editors_;
};

} // namespace raven
