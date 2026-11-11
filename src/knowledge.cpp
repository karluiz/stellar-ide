#include "knowledge.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace raven {
namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::vector<std::string> tokens(const std::string& value) {
    std::vector<std::string> result;
    std::string current;
    for (const unsigned char c : value) {
        if (std::isalnum(c)) current.push_back(static_cast<char>(std::tolower(c)));
        else if (!current.empty()) {
            if (current.size() >= 2) result.push_back(current);
            current.clear();
        }
    }
    if (current.size() >= 2) result.push_back(current);
    return result;
}

std::string slug(const std::string& value) {
    std::string result;
    for (const unsigned char c : value) {
        if (std::isalnum(c) || c == '.' || c == '_' || c == ':' || c == '-') result.push_back(static_cast<char>(c));
        else result.push_back('-');
    }
    return result;
}

std::string read_text(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_text(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    const auto temporary = path.parent_path() / ("." + path.filename().string() + ".tmp");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Cannot write " + path.string());
        output << content;
        output.close();
        if (!output) throw std::runtime_error("Cannot write " + path.string());
    }
    std::error_code error;
    fs::rename(temporary, path, error);
    if (error) {
        fs::remove(temporary);
        throw std::runtime_error("Cannot replace " + path.string() + ": " + error.message());
    }
}

std::string record_text(const GraphOperation& record) {
    std::string text = record.kind == GraphOpKind::UpsertEntity
        ? "ENTITY " + record.id + " | " + record.entity_type + " | " + record.name
        : "RELATION " + record.source_id + " -" + record.entity_type + "-> " + record.target_id;
    for (const auto& [key, value] : record.properties) {
        auto output = value;
        if (record.entity_type == "episode" && key == "transcript" && value.size() > 3'000) {
            output = value.substr(0, 1'200) + "\n…\n" + value.substr(value.size() - 1'800);
        }
        text += " | " + key + "=" + output;
    }
    text += " | source=" + record.source;
    std::ostringstream confidence;
    confidence << std::fixed << std::setprecision(2) << record.confidence;
    return text + " | confidence=" + confidence.str();
}

std::string search_text(const GraphOperation& record) {
    auto text = lower(record.id + " " + record.entity_type + " " + record.name + " " + record.source_id + " " + record.target_id);
    for (const auto& [key, value] : record.properties) text += " " + lower(key + " " + value);
    return text;
}

int score(const GraphOperation& record, const std::vector<std::string>& query) {
    const auto text = search_text(record);
    int result = 0;
    for (const auto& token : query) {
        const auto position = text.find(token);
        if (position == std::string::npos) continue;
        result += position == 0 ? 5 : 2;
        if (lower(record.id) == token || lower(record.name) == token) result += 8;
    }
    return result;
}

std::uint32_t feature_hash(const std::string& value) {
    std::uint32_t hash = 2166136261u;
    for (const unsigned char c : value) {
        hash ^= c;
        hash *= 16777619u;
    }
    return hash;
}

std::array<double, 256> local_vector(const std::string& text) {
    std::array<double, 256> vector{};
    for (const auto& token : tokens(text)) {
        vector[feature_hash("token:" + token) % vector.size()] += 1.0;
        if (token.size() < 3) continue;
        for (size_t i = 0; i + 3 <= token.size(); ++i) {
            vector[feature_hash("trigram:" + token.substr(i, 3)) % vector.size()] += 0.3;
        }
    }
    return vector;
}

double cosine_similarity(const std::array<double, 256>& left, const std::array<double, 256>& right) {
    double dot = 0.0;
    double left_norm = 0.0;
    double right_norm = 0.0;
    for (size_t i = 0; i < left.size(); ++i) {
        dot += left[i] * right[i];
        left_norm += left[i] * left[i];
        right_norm += right[i] * right[i];
    }
    return left_norm > 0.0 && right_norm > 0.0 ? dot / std::sqrt(left_norm * right_norm) : 0.0;
}

} // namespace

KnowledgeGraph::KnowledgeGraph(std::filesystem::path wiki_root)
    : graph_path_(wiki_root / "graph.jsonl"), staging_path_(wiki_root / "graph-proposals.jsonl") {
    load();
}

void KnowledgeGraph::load() {
    entities_.clear();
    relations_.clear();
    if (!fs::exists(graph_path_)) return;
    std::ifstream input(graph_path_);
    std::string line;
    size_t number = 0;
    while (std::getline(input, line)) {
        ++number;
        if (line.empty()) continue;
        try {
            auto operation = from_json(line);
            validate(operation);
            (operation.kind == GraphOpKind::UpsertEntity ? entities_ : relations_).push_back(std::move(operation));
        } catch (const std::exception& error) {
            throw std::runtime_error("Invalid " + graph_path_.string() + " line " + std::to_string(number) + ": " + error.what());
        }
    }
}

GraphOperation KnowledgeGraph::from_json(const std::string& text) {
    const auto data = json::parse(text);
    GraphOperation operation;
    const auto kind = data.value("op", "upsert_entity");
    if (kind == "relate") operation.kind = GraphOpKind::Relate;
    else if (kind != "upsert_entity") throw std::runtime_error("Unknown graph operation: " + kind);
    operation.entity_type = data.value("type", "");
    operation.name = data.value("name", "");
    operation.source_id = data.value("source_id", "");
    operation.target_id = data.value("target_id", "");
    operation.source = data.value("source", "");
    operation.confidence = data.value("confidence", 1.0);
    operation.id = data.value("id", "");
    if (operation.id.empty() && operation.kind == GraphOpKind::Relate) {
        operation.id = operation.source_id + "|" + operation.entity_type + "|" + operation.target_id;
    }
    if (data.contains("properties")) {
        if (!data["properties"].is_object()) throw std::runtime_error("Graph properties must be an object");
        for (const auto& [key, value] : data["properties"].items()) {
            if (!value.is_string()) throw std::runtime_error("Graph property values must be strings");
            operation.properties[key] = value.get<std::string>();
        }
    }
    validate(operation);
    return operation;
}

std::string KnowledgeGraph::to_json(const GraphOperation& operation, int indentation) {
    json data;
    data["op"] = operation.kind == GraphOpKind::Relate ? "relate" : "upsert_entity";
    data["id"] = operation.id;
    data["type"] = operation.entity_type;
    if (!operation.name.empty()) data["name"] = operation.name;
    if (!operation.source_id.empty()) data["source_id"] = operation.source_id;
    if (!operation.target_id.empty()) data["target_id"] = operation.target_id;
    data["source"] = operation.source;
    data["confidence"] = operation.confidence;
    data["properties"] = operation.properties;
    return data.dump(indentation);
}

void KnowledgeGraph::validate(const GraphOperation& operation) {
    const auto valid_id = [](const std::string& value) {
        if (value.empty() || !std::isalnum(static_cast<unsigned char>(value.front()))) return false;
        return std::all_of(value.begin(), value.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '.' || c == '_' || c == ':' || c == '-' || c == '|';
        });
    };
    if (!valid_id(operation.id)) throw std::runtime_error("Invalid graph id: " + operation.id);
    if (!valid_id(operation.entity_type)) throw std::runtime_error("Invalid graph type: " + operation.entity_type);
    if (operation.kind == GraphOpKind::UpsertEntity && operation.name.empty()) throw std::runtime_error("Entity name is required");
    if (operation.kind == GraphOpKind::Relate && (!valid_id(operation.source_id) || !valid_id(operation.target_id))) {
        throw std::runtime_error("Relation endpoints are invalid");
    }
    if (operation.kind == GraphOpKind::Relate && operation.source_id == operation.target_id) {
        throw std::runtime_error("Self-relations are not allowed");
    }
    if (operation.source.empty()) throw std::runtime_error("Every graph record needs provenance");
    if (!std::isfinite(operation.confidence) || operation.confidence < 0.0 || operation.confidence > 1.0) {
        throw std::runtime_error("Graph confidence must be between 0 and 1");
    }
}

std::string KnowledgeGraph::revision() const {
    if (!fs::exists(graph_path_)) return "empty";
    return std::to_string(std::hash<std::string>{}(read_text(graph_path_)));
}

KnowledgeStats KnowledgeGraph::stats() const {
    const auto episodes = std::count_if(entities_.begin(), entities_.end(), [](const auto& entity) {
        return entity.entity_type == "episode";
    });
    return {entities_.size(), relations_.size(), staged().size(), static_cast<size_t>(episodes)};
}
std::vector<GraphOperation> KnowledgeGraph::entities() const { return entities_; }
std::vector<GraphOperation> KnowledgeGraph::relations() const { return relations_; }

std::vector<GraphOperation> KnowledgeGraph::staged() const {
    std::vector<GraphOperation> result;
    if (!fs::exists(staging_path_)) return result;
    std::ifstream input(staging_path_);
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        // Staging is append-only, so re-staging a record (an episode renamed after its run) adds a
        // second line with the same id. The latest version replaces the earlier one in place.
        auto operation = from_json(line);
        const auto existing = std::find_if(result.begin(), result.end(), [&](const auto& item) { return item.id == operation.id; });
        if (existing != result.end()) *existing = std::move(operation);
        else result.push_back(std::move(operation));
    }
    return result;
}

void KnowledgeGraph::stage(const GraphOperation& operation) const {
    validate(operation);
    fs::create_directories(staging_path_.parent_path());
    std::ofstream output(staging_path_, std::ios::app);
    if (!output) throw std::runtime_error("Cannot stage graph proposal");
    output << to_json(operation) << '\n';
}

void KnowledgeGraph::clear_staged() {
    std::error_code error;
    fs::remove(staging_path_, error);
}

void KnowledgeGraph::apply(const std::vector<GraphOperation>& operations, const std::string& expected_revision) {
    if (revision() != expected_revision) throw std::runtime_error("Knowledge graph changed after review");
    auto updated_entities = entities_;
    auto updated_relations = relations_;
    const auto has_entity = [&](const std::string& id) {
        return std::any_of(updated_entities.begin(), updated_entities.end(), [&](const auto& entity) { return entity.id == id; });
    };
    for (const auto& operation : operations) {
        validate(operation);
        if (operation.kind == GraphOpKind::UpsertEntity) {
            const auto found = std::find_if(updated_entities.begin(), updated_entities.end(), [&](const auto& entity) {
                return entity.id == operation.id;
            });
            if (found == updated_entities.end()) updated_entities.push_back(operation);
            else *found = operation;
        } else {
            if (!has_entity(operation.source_id) || !has_entity(operation.target_id)) {
                throw std::runtime_error("Relation references an unknown entity: " + operation.id);
            }
            const auto found = std::find_if(updated_relations.begin(), updated_relations.end(), [&](const auto& relation) {
                return relation.id == operation.id;
            });
            if (found == updated_relations.end()) updated_relations.push_back(operation);
            else *found = operation;
        }
    }
    entities_ = std::move(updated_entities);
    relations_ = std::move(updated_relations);
    try {
        save();
    } catch (...) {
        load();
        throw;
    }
    clear_staged();
}

bool KnowledgeGraph::forget(const std::string& id) {
    if (id.empty()) throw std::runtime_error("Memory id is required");
    auto updated_entities = entities_;
    auto updated_relations = relations_;
    const auto entity_count = updated_entities.size();
    const auto relation_count = updated_relations.size();
    updated_entities.erase(std::remove_if(updated_entities.begin(), updated_entities.end(), [&](const auto& entity) {
        return entity.id == id;
    }), updated_entities.end());
    updated_relations.erase(std::remove_if(updated_relations.begin(), updated_relations.end(), [&](const auto& relation) {
        return relation.id == id || relation.source_id == id || relation.target_id == id;
    }), updated_relations.end());
    if (updated_entities.size() == entity_count && updated_relations.size() == relation_count) return false;
    entities_ = std::move(updated_entities);
    relations_ = std::move(updated_relations);
    try {
        save();
    } catch (...) {
        load();
        throw;
    }
    return true;
}

void KnowledgeGraph::save() const {
    std::string content;
    for (const auto& entity : entities_) content += to_json(entity) + "\n";
    for (const auto& relation : relations_) content += to_json(relation) + "\n";
    write_text(graph_path_, content);
}

std::string KnowledgeGraph::context(const std::string& query, size_t limit, bool vector_recall) const {
    struct RankedRecord {
        double score = 0.0;
        double similarity = 0.0;
        GraphOperation record;
    };
    const auto query_tokens = tokens(query);
    const auto query_vector = vector_recall ? local_vector(query) : std::array<double, 256>{};
    std::vector<RankedRecord> ranked;
    const auto rank = [&](const GraphOperation& record) {
        const auto similarity = vector_recall ? cosine_similarity(query_vector, local_vector(search_text(record))) : 0.0;
        ranked.push_back({score(record, query_tokens) + similarity * 8.0, similarity, record});
    };
    for (const auto& entity : entities_) rank(entity);
    for (const auto& relation : relations_) rank(relation);
    if (query_tokens.empty()) {
        for (auto& ranked_record : ranked) ranked_record.score = 1.0;
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.score > b.score; });

    std::unordered_set<std::string> selected;
    std::vector<GraphOperation> records;
    for (const auto& ranked_record : ranked) {
        const auto& record = ranked_record.record;
        if ((ranked_record.score == 0.0 && !vector_recall) || (vector_recall && !query_tokens.empty() && ranked_record.score < 0.8) || records.size() >= limit) continue;
        records.push_back(record);
        if (record.kind == GraphOpKind::UpsertEntity) selected.insert(record.id);
        else {
            selected.insert(record.source_id);
            selected.insert(record.target_id);
        }
    }

    std::string output;
    for (const auto& record : records) output += record_text(record) + "\n";
    size_t related = 0;
    for (const auto& relation : relations_) {
        if (related >= 12) break;
        if (!selected.count(relation.source_id) && !selected.count(relation.target_id)) continue;
        if (std::find_if(records.begin(), records.end(), [&](const auto& record) { return record.id == relation.id; }) != records.end()) continue;
        output += "RELATED " + record_text(relation) + "\n";
        ++related;
    }

    size_t inferred = 0;
    for (const auto& start : selected) {
        std::unordered_set<std::string> path{start};
        const auto walk = [&](auto&& self, const std::string& current, const std::string& relation_id, int depth) -> void {
            if (depth > 3 || inferred >= 6) return;
            for (const auto& relation : relations_) {
                if (relation.entity_type != "depends_on" || relation.source_id != current) continue;
                if (path.count(relation.target_id)) continue;
                output += "INFER " + start + " -depends_on(depth=" + std::to_string(depth) + ")-> " + relation.target_id +
                    " | rule=transitive | via=" + relation_id + " | source=" + relation.source + "\n";
                ++inferred;
                path.insert(relation.target_id);
                self(self, relation.target_id, relation.id, depth + 1);
                path.erase(relation.target_id);
            }
        };
        walk(walk, start, "direct", 1);
    }
    return output;
}

} // namespace raven
