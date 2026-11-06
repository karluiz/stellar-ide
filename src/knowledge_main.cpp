#include "workspace.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

void usage() {
    std::cout << "raven-ide-knowledge [--workspace PATH] COMMAND [ARGUMENTS]\n\n"
                 "Read commands:\n"
                 "  stats                 Show entity, relation, and staged-write counts\n"
                 "  context [QUERY]     Print relevant graph context and symbolic inferences\n"
                 "  entities            Print entities as JSONL\n"
                 "  episodes            Print run-episode memories as JSONL\n"
                 "  relations           Print relations as JSONL\n"
                 "  staged              Print writes waiting for human review\n\n"
                 "Reviewed write commands:\n"
                 "  propose JSON        Stage an upsert_entity or relate operation\n"
                 "  clear-staged        Discard all staged graph writes\n"
                 "  forget ID           Immediately remove an accepted record and touching relations\n\n"
                 "New graph records only enter through acceptance in Stellar; forget is an immediate user action.\n";
}

void print_records(const std::vector<raven::GraphOperation>& records) {
    for (const auto& record : records) std::cout << raven::KnowledgeGraph::to_json(record) << '\n';
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::filesystem::path workspace = std::filesystem::current_path();
        std::vector<std::string> arguments;
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "--workspace" && i + 1 < argc) workspace = argv[++i];
            else if (argument == "--help" || argument == "-h") {
                usage();
                return 0;
            } else arguments.push_back(argument);
        }
        if (arguments.empty()) {
            usage();
            return 0;
        }

        raven::Workspace workspace_model(workspace);
        workspace_model.initialize();
        auto& graph = workspace_model.knowledge();
        const auto& command = arguments[0];
        if (command == "stats") {
            const auto stats = graph.stats();
            std::cout << "entities: " << stats.entities << "\nrelations: " << stats.relations << "\nepisodes: " << stats.episodes << "\nstaged: " << stats.staged << '\n';
        } else if (command == "context" || command == "query") {
            std::string query;
            for (size_t i = 1; i < arguments.size(); ++i) {
                if (!query.empty()) query += " ";
                query += arguments[i];
            }
            const auto context = graph.context(query);
            std::cout << (context.empty() ? "No matching knowledge.\n" : context);
        } else if (command == "entities") {
            print_records(graph.entities());
        } else if (command == "episodes") {
            for (const auto& entity : graph.entities()) {
                if (entity.entity_type == "episode") std::cout << raven::KnowledgeGraph::to_json(entity) << '\n';
            }
        } else if (command == "relations") {
            print_records(graph.relations());
        } else if (command == "staged") {
            print_records(graph.staged());
        } else if (command == "propose") {
            if (arguments.size() != 2) throw std::runtime_error("propose requires one JSON operation");
            const auto operation = raven::KnowledgeGraph::from_json(arguments[1]);
            graph.stage(operation);
            std::cout << "STAGED " << operation.id << "\nReview and accept it in Stellar before it enters graph.jsonl.\n";
        } else if (command == "clear-staged") {
            graph.clear_staged();
            std::cout << "Staged graph writes discarded.\n";
        } else if (command == "forget") {
            if (arguments.size() != 2) throw std::runtime_error("forget requires one record ID");
            if (!graph.forget(arguments[1])) throw std::runtime_error("Accepted record not found: " + arguments[1]);
            std::cout << "FORGOT " << arguments[1] << "\n";
        } else {
            throw std::runtime_error("Unknown command: " + command);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "raven-ide-knowledge: " << error.what() << '\n';
        return 1;
    }
}
