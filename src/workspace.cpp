#include "workspace.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace raven {
namespace {

constexpr const char* orchestrator_agent = R"AGENT(---
name: orchestrator
description: Lead agent for workspace tasks and coordinated implementation
---
You are the lead agent for this multi-repository workspace. Understand the requested outcome before acting. Delegate focused investigation or independent work to subagents when it improves the result, and integrate their findings yourself. Keep changes inside the workspace. Do not edit wiki/ as part of the implementation; a separate wiki curator will propose knowledge updates for human review. Use raven-ide-knowledge for typed graph context and staged writes; never edit wiki/graph.jsonl directly.
)AGENT";

constexpr const char* reviewer_agent = R"AGENT(---
name: code-reviewer
description: Independent read-only implementation and verification review
tools: Read, Grep, Glob, Bash
---
Inspect the relevant implementation and evidence without editing, creating, deleting, or staging files. Report only actionable findings ordered by severity, with workspace paths and line references and a concrete failure scenario for each. Check whether the claimed build and test evidence covers the change. If there are no material findings, say so plainly and identify residual test gaps. Never modify wiki/graph.jsonl directly.
)AGENT";

constexpr const char* curator_agent = R"AGENT(---
name: wiki-curator
description: Proposes reviewed Markdown knowledge updates after agent runs
tools: Read, Grep, Glob
---
You maintain the workspace wiki as a durable, linked knowledge base. Read existing notes before proposing changes. Prefer updating existing pages over creating near-duplicates. Record settled architecture, decisions, commands, and hard-won findings; do not record transient task progress, secrets, or speculation. Return only the requested structured proposal. Every edit must contain the complete replacement Markdown file, use workspace-wiki-relative paths ending in .md, and use [[Wiki Links]] where they help navigation.
)AGENT";

constexpr const char* home_note = R"NOTE(# Workspace knowledge

This wiki belongs to the agents and people working in this workspace.

## Start here

- [[Architecture]] — how the repositories fit together
- [[Decisions]] — decisions and their rationale
- [[Runbooks]] — verified commands and recovery steps

The wiki curator proposes updates after completed runs. Nothing changes until you review and accept the proposal.
)NOTE";

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::vector<std::string> lines(const std::string& value) {
    std::vector<std::string> result;
    std::istringstream input(value);
    std::string line;
    while (std::getline(input, line)) result.push_back(line);
    if (value.empty()) return {};
    if (value.back() == '\n') result.emplace_back();
    return result;
}

std::string frontmatter_value(const std::string& content, const std::string& key) {
    std::istringstream input(content);
    std::string line;
    if (!std::getline(input, line) || trim(line) != "---") return {};
    while (std::getline(input, line)) {
        if (trim(line) == "---") break;
        const auto prefix = key + ":";
        if (line.rfind(prefix, 0) == 0) return trim(line.substr(prefix.size()));
    }
    return {};
}

} // namespace

std::string read_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_file(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    const auto temporary = path.parent_path() / ("." + path.filename().string() + ".raven-tmp");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Cannot write " + path.string());
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
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

Workspace::Workspace(fs::path root)
    : root_(fs::absolute(std::move(root)).lexically_normal()), knowledge_(wiki_root()) {}

const fs::path& Workspace::root() const { return root_; }
fs::path Workspace::wiki_root() const { return root_ / "wiki"; }
fs::path Workspace::agents_root() const { return root_ / ".agents" / "agents"; }
std::string Workspace::name() const { return root_.filename().empty() ? root_.string() : root_.filename().string(); }

void Workspace::initialize() {
    if (!fs::exists(root_)) throw std::runtime_error("Workspace does not exist: " + root_.string());
    if (!fs::is_directory(root_)) throw std::runtime_error("Workspace is not a folder: " + root_.string());
    fs::create_directories(agents_root());
    const auto legacy_agents = root_ / ".claude" / "agents";
    std::vector<fs::path> migrations;
    if (fs::exists(legacy_agents)) {
        for (const auto& entry : fs::directory_iterator(legacy_agents)) {
            if (entry.is_regular_file() && entry.path().extension() == ".md") migrations.push_back(entry.path());
        }
    }
    for (const auto& source : migrations) {
        const auto destination = agents_root() / source.filename();
        if (!fs::exists(destination)) fs::rename(source, destination);
    }
    std::error_code remove_error;
    fs::remove(legacy_agents, remove_error);
    fs::create_directories(wiki_root());
    if (!fs::exists(agents_root() / "orchestrator.md")) write_file(agents_root() / "orchestrator.md", orchestrator_agent);
    if (!fs::exists(agents_root() / "code-reviewer.md")) write_file(agents_root() / "code-reviewer.md", reviewer_agent);
    if (!fs::exists(agents_root() / "wiki-curator.md")) write_file(agents_root() / "wiki-curator.md", curator_agent);
    if (!fs::exists(wiki_root() / "Home.md")) write_file(wiki_root() / "Home.md", home_note);
}

std::vector<fs::path> Workspace::repositories() const {
    std::vector<fs::path> result;
    std::error_code error;
    if (fs::exists(root_ / ".git")) result.push_back(root_);
    for (const auto& entry : fs::directory_iterator(root_, error)) {
        if (entry.is_directory() && fs::exists(entry.path() / ".git")) result.push_back(entry.path());
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<AgentDefinition> Workspace::agents() const {
    std::vector<AgentDefinition> result;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(agents_root(), error)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".md") continue;
        AgentDefinition agent;
        agent.path = entry.path();
        agent.content = read_file(agent.path);
        agent.name = frontmatter_value(agent.content, "name");
        if (agent.name.empty()) agent.name = agent.path.stem().string();
        agent.description = frontmatter_value(agent.content, "description");
        result.push_back(std::move(agent));
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    return result;
}

std::vector<WikiNote> Workspace::wiki_notes() const {
    std::vector<WikiNote> result;
    std::error_code error;
    for (const auto& entry : fs::recursive_directory_iterator(wiki_root(), error)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".md") continue;
        WikiNote note;
        note.path = entry.path();
        note.title = note.path.stem().string();
        note.content = read_file(note.path);
        note.links = extract_links(note.content);
        result.push_back(std::move(note));
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.title < b.title; });
    for (auto& note : result) {
        for (const auto& other : result) {
            if (other.path == note.path) continue;
            const auto found = std::find_if(other.links.begin(), other.links.end(), [&](const std::string& link) {
                return lower(link) == lower(note.title);
            });
            if (found != other.links.end()) note.backlinks.push_back(other.title);
        }
    }
    return result;
}

std::string Workspace::slugify(const std::string& value) {
    std::string result;
    bool dash = false;
    for (const unsigned char c : value) {
        if (std::isalnum(c)) {
            if (dash && !result.empty()) result.push_back('-');
            dash = false;
            result.push_back(static_cast<char>(std::tolower(c)));
        } else {
            dash = true;
        }
    }
    return result.empty() ? "untitled" : result;
}

AgentDefinition Workspace::create_agent(const std::string& name) const {
    AgentDefinition agent;
    agent.name = slugify(name);
    agent.path = agents_root() / (agent.name + ".md");
    if (fs::exists(agent.path)) throw std::runtime_error("An agent with that name already exists");
    agent.description = "Workspace agent";
    agent.content = "---\nname: " + agent.name + "\ndescription: " + agent.description +
        "\n---\nDescribe this agent's role, boundaries, and expected output.\n";
    write_file(agent.path, agent.content);
    return agent;
}

WikiNote Workspace::create_note(const std::string& title) const {
    WikiNote note;
    note.title = trim(title).empty() ? "Untitled" : trim(title);
    note.path = wiki_root() / (note.title + ".md");
    if (fs::exists(note.path)) throw std::runtime_error("A note with that title already exists");
    note.content = "# " + note.title + "\n\n";
    write_file(note.path, note.content);
    return note;
}

void Workspace::save_agent(const AgentDefinition& agent, const std::string& content) const {
    if (agent.path.parent_path() != agents_root()) throw std::runtime_error("Agent path is outside the workspace");
    write_file(agent.path, content);
}

void Workspace::delete_agent(const fs::path& path) const {
    const auto candidate = path.lexically_normal();
    if (candidate.parent_path() != agents_root() || candidate.extension() != ".md") {
        throw std::runtime_error("Agent path is outside the shared agent directory");
    }
    const auto filename = candidate.filename();
    if (filename == "orchestrator.md" || filename == "code-reviewer.md" || filename == "wiki-curator.md") {
        throw std::runtime_error("Built-in agents cannot be deleted");
    }
    std::error_code error;
    const auto status = fs::symlink_status(candidate, error);
    if (error || !fs::is_regular_file(status)) throw std::runtime_error("Agent file does not exist");
    if (!fs::remove(candidate, error) || error) throw std::runtime_error("Agent file could not be deleted");
}

void Workspace::save_note(const WikiNote& note, const std::string& content) const {
    safe_wiki_path(fs::relative(note.path, wiki_root()).string());
    write_file(note.path, content);
}

void Workspace::stage_run_episode(const std::string& session_id, const std::string& title, const std::string& harness,
                                  const std::string& transcript, const std::string& status) const {
    const auto session = trim(session_id).empty() ? "current" : trim(session_id);
    const auto task = trim(title).empty() ? "Untitled run" : trim(title);
    constexpr size_t max_transcript = 16'000;
    auto memory = transcript;
    if (memory.size() > max_transcript) {
        constexpr size_t head = 4'000;
        constexpr size_t marker = 5;
        memory = memory.substr(0, head) + "\n…\n" + memory.substr(memory.size() - (max_transcript - head - marker));
    }
    GraphOperation episode{
        GraphOpKind::UpsertEntity,
        "run:" + slugify(session),
        "episode",
        task,
        "",
        "",
        "run:" + session,
        1.0,
        {{"memory", "episodic"}, {"session_id", session}, {"harness", harness}, {"status", status},
         {"task", task}, {"transcript", memory}}
    };
    knowledge_.stage(episode);
}

fs::path Workspace::safe_wiki_path(const std::string& relative) const {
    const fs::path input(relative);
    if (input.empty() || input.is_absolute() || input.extension() != ".md") {
        throw std::runtime_error("Wiki edits must be relative Markdown paths");
    }
    for (const auto& part : input) {
        if (part == "..") throw std::runtime_error("Wiki edit path escapes the wiki");
    }
    const auto path = (wiki_root() / input).lexically_normal();
    const auto canonical_root = fs::weakly_canonical(wiki_root());
    const auto canonical_path = fs::weakly_canonical(path);
    auto root_it = canonical_root.begin();
    auto path_it = canonical_path.begin();
    for (; root_it != canonical_root.end(); ++root_it, ++path_it) {
        if (path_it == canonical_path.end() || *root_it != *path_it) {
            throw std::runtime_error("Wiki edit path escapes the wiki");
        }
    }
    return path;
}

WikiProposal Workspace::validate_proposal(
    const std::string& summary, const std::vector<std::pair<std::string, std::string>>& edits,
    const std::vector<GraphOperation>& graph) const {
    if (edits.empty() && graph.empty()) throw std::runtime_error("The wiki curator returned no edits");
    WikiProposal proposal;
    proposal.summary = trim(summary).empty() ? "Suggested workspace knowledge updates" : summary;
    proposal.graph = graph;
    proposal.graph_revision = knowledge_.revision();
    std::set<std::string> paths;
    for (const auto& [relative, after] : edits) {
        const auto path = safe_wiki_path(relative);
        const auto normalized = fs::relative(path, wiki_root()).generic_string();
        if (!paths.insert(normalized).second) throw std::runtime_error("Duplicate wiki edit: " + normalized);
        if (after.size() > 2 * 1024 * 1024) throw std::runtime_error("Wiki edit is too large: " + normalized);
        proposal.edits.push_back({normalized, fs::exists(path) ? read_file(path) : "", after});
    }
    for (const auto& operation : graph) KnowledgeGraph::validate(operation);
    return proposal;
}

void Workspace::apply_proposal(const WikiProposal& proposal) {
    for (const auto& edit : proposal.edits) {
        const auto path = safe_wiki_path(edit.path);
        const auto current = fs::exists(path) ? read_file(path) : "";
        if (current != edit.before) throw std::runtime_error("Wiki file changed after review: " + edit.path);
    }
    if (!proposal.graph.empty()) knowledge_.apply(proposal.graph, proposal.graph_revision);
    for (const auto& edit : proposal.edits) write_file(safe_wiki_path(edit.path), edit.after);
}

KnowledgeGraph& Workspace::knowledge() { return knowledge_; }
const KnowledgeGraph& Workspace::knowledge() const { return knowledge_; }
std::string Workspace::knowledge_context(const std::string& query, bool vector_recall) const {
    return knowledge_.context(query, 8, vector_recall);
}

std::vector<std::string> Workspace::extract_links(const std::string& markdown) {
    std::vector<std::string> result;
    std::set<std::string> seen;
    size_t position = 0;
    while ((position = markdown.find("[[", position)) != std::string::npos) {
        const auto end = markdown.find("]]", position + 2);
        if (end == std::string::npos) break;
        auto target = trim(markdown.substr(position + 2, end - position - 2));
        if (const auto pipe = target.find('|'); pipe != std::string::npos) target = trim(target.substr(0, pipe));
        if (!target.empty() && seen.insert(lower(target)).second) result.push_back(target);
        position = end + 2;
    }
    return result;
}

std::string Workspace::expand_wiki_links(const std::string& markdown) {
    std::string result;
    size_t position = 0;
    while (position < markdown.size()) {
        const auto start = markdown.find("[[", position);
        if (start == std::string::npos) {
            result.append(markdown, position, std::string::npos);
            break;
        }
        const auto end = markdown.find("]]", start + 2);
        if (end == std::string::npos) {
            result.append(markdown, position, std::string::npos);
            break;
        }
        result.append(markdown, position, start - position);
        auto inner = trim(markdown.substr(start + 2, end - start - 2));
        const auto pipe = inner.find('|');
        const auto target = trim(inner.substr(0, pipe));
        const auto label = pipe == std::string::npos ? target : trim(inner.substr(pipe + 1));
        std::string encoded;
        const char* hex = "0123456789ABCDEF";
        for (const unsigned char c : target) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') encoded.push_back(static_cast<char>(c));
            else {
                encoded.push_back('%');
                encoded.push_back(hex[c >> 4]);
                encoded.push_back(hex[c & 15]);
            }
        }
        result += "[" + label + "](wiki:" + encoded + ")";
        position = end + 2;
    }
    return result;
}

std::string Workspace::diff(const std::string& before, const std::string& after) {
    if (before == after) return "No content changes\n";
    const auto a = lines(before);
    const auto b = lines(after);
    std::string output = "--- before\n+++ proposed\n";
    if (a.size() * b.size() > 4'000'000) {
        for (const auto& line : a) output += "- " + line + "\n";
        for (const auto& line : b) output += "+ " + line + "\n";
        return output;
    }
    std::vector<int> table((a.size() + 1) * (b.size() + 1), 0);
    const auto at = [&](size_t i, size_t j) -> int& { return table[i * (b.size() + 1) + j]; };
    for (size_t i = a.size(); i-- > 0;) {
        for (size_t j = b.size(); j-- > 0;) {
            at(i, j) = a[i] == b[j] ? at(i + 1, j + 1) + 1 : std::max(at(i + 1, j), at(i, j + 1));
        }
    }
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] == b[j]) output += "  " + a[i] + "\n", ++i, ++j;
        else if (at(i + 1, j) >= at(i, j + 1)) output += "- " + a[i] + "\n", ++i;
        else output += "+ " + b[j] + "\n", ++j;
    }
    while (i < a.size()) output += "- " + a[i++] + "\n";
    while (j < b.size()) output += "+ " + b[j++] + "\n";
    return output;
}

bool is_valid_utf8(std::string_view text) {
    size_t i = 0;
    while (i < text.size()) {
        const auto first = static_cast<unsigned char>(text[i]);
        if (first <= 0x7F) {
            ++i;
            continue;
        }
        size_t length = 0;
        std::uint32_t codepoint = 0;
        std::uint32_t minimum = 0;
        if (first >= 0xC2 && first <= 0xDF) {
            length = 2;
            codepoint = first & 0x1F;
            minimum = 0x80;
        } else if (first >= 0xE0 && first <= 0xEF) {
            length = 3;
            codepoint = first & 0x0F;
            minimum = 0x800;
        } else if (first >= 0xF0 && first <= 0xF4) {
            length = 4;
            codepoint = first & 0x07;
            minimum = 0x10000;
        } else {
            return false;
        }
        if (i + length > text.size()) return false;
        for (size_t j = 1; j < length; ++j) {
            const auto next = static_cast<unsigned char>(text[i + j]);
            if ((next & 0xC0) != 0x80) return false;
            codepoint = (codepoint << 6) | (next & 0x3F);
        }
        if (codepoint < minimum || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) return false;
        i += length;
    }
    return true;
}

} // namespace raven
