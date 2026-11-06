#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace raven {

enum class GraphOpKind {
    UpsertEntity,
    Relate,
};

struct GraphOperation {
    GraphOpKind kind = GraphOpKind::UpsertEntity;
    std::string id;
    std::string entity_type;
    std::string name;
    std::string source_id;
    std::string target_id;
    std::string source;
    double confidence = 1.0;
    std::map<std::string, std::string> properties;
};

struct KnowledgeStats {
    size_t entities = 0;
    size_t relations = 0;
    size_t staged = 0;
    size_t episodes = 0;
};

class KnowledgeGraph {
public:
    explicit KnowledgeGraph(std::filesystem::path wiki_root);

    std::string revision() const;
    KnowledgeStats stats() const;
    std::vector<GraphOperation> entities() const;
    std::vector<GraphOperation> relations() const;
    std::vector<GraphOperation> staged() const;
    std::string context(const std::string& query, size_t limit = 8, bool vector_recall = true) const;

    void stage(const GraphOperation& operation) const;
    void clear_staged();
    void apply(const std::vector<GraphOperation>& operations, const std::string& expected_revision);
    bool forget(const std::string& id);

    static GraphOperation from_json(const std::string& text);
    static std::string to_json(const GraphOperation& operation, int indentation = -1);
    static void validate(const GraphOperation& operation);

private:
    std::filesystem::path graph_path_;
    std::filesystem::path staging_path_;
    std::vector<GraphOperation> entities_;
    std::vector<GraphOperation> relations_;

    void load();
    void save() const;
};

} // namespace raven
