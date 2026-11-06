#pragma once

#include "knowledge.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace raven {

struct AgentDefinition {
    std::string name;
    std::string description;
    std::filesystem::path path;
    std::string content;
};

struct WikiNote {
    std::string title;
    std::filesystem::path path;
    std::string content;
    std::vector<std::string> links;
    std::vector<std::string> backlinks;
};

struct WikiEdit {
    std::string path;
    std::string before;
    std::string after;
};

struct WikiProposal {
    std::string summary;
    std::vector<WikiEdit> edits;
    std::vector<GraphOperation> graph;
    std::string graph_revision;
};

class Workspace {
public:
    explicit Workspace(std::filesystem::path root);

    const std::filesystem::path& root() const;
    std::filesystem::path wiki_root() const;
    std::filesystem::path agents_root() const;
    std::string name() const;

    void initialize();
    std::vector<std::filesystem::path> repositories() const;
    std::vector<AgentDefinition> agents() const;
    std::vector<WikiNote> wiki_notes() const;

    AgentDefinition create_agent(const std::string& name) const;
    WikiNote create_note(const std::string& title) const;
    void save_agent(const AgentDefinition& agent, const std::string& content) const;
    void delete_agent(const std::filesystem::path& path) const;
    void save_note(const WikiNote& note, const std::string& content) const;
    void stage_run_episode(const std::string& session_id, const std::string& title, const std::string& harness,
                           const std::string& transcript, const std::string& status) const;

    WikiProposal validate_proposal(const std::string& summary,
                                   const std::vector<std::pair<std::string, std::string>>& edits,
                                   const std::vector<GraphOperation>& graph = {}) const;
    void apply_proposal(const WikiProposal& proposal);
    KnowledgeGraph& knowledge();
    const KnowledgeGraph& knowledge() const;
    std::string knowledge_context(const std::string& query, bool vector_recall = true) const;

    static std::vector<std::string> extract_links(const std::string& markdown);
    static std::string expand_wiki_links(const std::string& markdown);
    static std::string diff(const std::string& before, const std::string& after);
    static std::string slugify(const std::string& value);

private:
    std::filesystem::path root_;
    KnowledgeGraph knowledge_;
    std::filesystem::path safe_wiki_path(const std::string& relative) const;
};

std::string read_file(const std::filesystem::path& path);
void write_file(const std::filesystem::path& path, const std::string& content);

} // namespace raven
