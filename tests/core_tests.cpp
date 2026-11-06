#include "checkpoints.hpp"
#include "claude.hpp"
#include "composer.hpp"
#include "diff.hpp"
#include "memory.hpp"
#include "missions.hpp"
#include "notifications.hpp"
#include "opening_audio.hpp"
#include "profile.hpp"
#include "recall.hpp"
#include "recovery.hpp"
#include "splash.hpp"
#include "theme_gallery.hpp"
#include "threads.hpp"
#include "verification.hpp"
#include "workspace.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void workspace_tests() {
    const auto root = std::filesystem::temp_directory_path() / "raven-ide-core-tests";
    std::filesystem::remove_all(root);
    raven::Workspace workspace(root);
    std::filesystem::create_directories(root);
    workspace.initialize();

    expect(workspace.agents_root() == root / ".agents" / "agents", "shared agent directory was not selected");
    expect(workspace.agents().size() == 3, "default agents were not created");
    const auto agent = workspace.create_agent("Security Reviewer");
    expect(agent.name == "security-reviewer", "agent name was not normalized");
    const auto note = workspace.create_note("Architecture");
    expect(std::filesystem::exists(note.path), "wiki note was not created");
    raven::write_file(note.path, "# Architecture\n\nSee [[Home]] and [[Decisions|ADR index]].\n");
    raven::write_file(root / "wiki" / "Home.md", "# Home\n\n[[Architecture]]\n");

    const auto notes = workspace.wiki_notes();
    const auto architecture = std::find_if(notes.begin(), notes.end(), [](const auto& item) {
        return item.title == "Architecture";
    });
    expect(architecture != notes.end(), "wiki note was not indexed");
    expect(architecture->links.size() == 2, "wiki links were not extracted");
    expect(architecture->backlinks.size() == 1 && architecture->backlinks[0] == "Home", "backlinks were not indexed");
    expect(raven::Workspace::expand_wiki_links("See [[Home|start]].") == "See [start](wiki:Home).", "wiki link expansion failed");
    expect(raven::Workspace::expand_wiki_links("[[Session Expiry]]") == "[Session Expiry](wiki:Session%20Expiry)",
           "wiki link targets were not URL encoded");

    const auto proposal = workspace.validate_proposal("Test", {{"Architecture.md", "# New\n"}});
    expect(proposal.edits.size() == 1, "wiki proposal was not validated");
    expect(raven::Workspace::diff(proposal.edits[0].before, proposal.edits[0].after).find("+ # New") != std::string::npos,
           "wiki diff did not contain the addition");
    workspace.apply_proposal(proposal);
    expect(raven::read_file(note.path) == "# New\n", "wiki proposal was not applied");

    bool rejected = false;
    try {
        workspace.validate_proposal("Unsafe", {{"../outside.md", "no"}});
    } catch (const std::exception&) {
        rejected = true;
    }
    expect(rejected, "wiki path traversal was accepted");

    raven::write_file(note.path, "changed after review");
    rejected = false;
    try {
        workspace.apply_proposal(proposal);
    } catch (const std::exception&) {
        rejected = true;
    }
    expect(rejected, "stale wiki proposal overwrote a note");
    std::filesystem::remove_all(root);
}

void agent_definition_tests() {
    const auto root = std::filesystem::temp_directory_path() / "raven-ide-agent-tests";
    std::filesystem::remove_all(root);
    raven::Workspace workspace(root);
    raven::write_file(root / ".claude" / "agents" / "orchestrator.md",
        "---\nname: orchestrator\ndescription: Custom lead\n---\nCustom orchestrator\n");
    workspace.initialize();

    const auto shared_orchestrator = workspace.agents_root() / "orchestrator.md";
    expect(std::filesystem::exists(shared_orchestrator), "legacy orchestrator was not migrated");
    expect(!std::filesystem::exists(root / ".claude" / "agents" / "orchestrator.md"), "migrated agent still has a legacy source");
    expect(raven::read_file(shared_orchestrator).find("Custom orchestrator") != std::string::npos,
           "migration replaced the existing agent prompt");

    raven::write_file(workspace.agents_root() / "reviewer.md",
        "---\nname: reviewer\ndescription: Reviews code\ntools:\n  - Read\n  - Grep\ndisallowedTools: [Write, Edit]\n---\nReport findings.\n");
    const auto definitions = nlohmann::json::parse(raven::claude_agents_json(workspace.agents()));
    expect(definitions["orchestrator"]["description"] == "Custom lead", "Claude description conversion failed");
    expect(definitions["orchestrator"]["prompt"] == "Custom orchestrator\n", "Claude prompt conversion failed");
    expect(definitions["wiki-curator"]["tools"].size() == 3 && definitions["wiki-curator"]["tools"][0] == "Read",
           "comma-separated tools were not converted");
    expect(definitions["reviewer"]["tools"].size() == 2 && definitions["reviewer"]["tools"][1] == "Grep",
           "YAML tool list was not converted");
    expect(definitions["reviewer"]["disallowedTools"].size() == 2 && definitions["reviewer"]["disallowedTools"][1] == "Edit",
           "inline tool list was not converted");

    workspace.delete_agent(workspace.agents_root() / "reviewer.md");
    expect(!std::filesystem::exists(workspace.agents_root() / "reviewer.md"), "custom agent was not deleted");

    raven::write_file(root / ".agents" / "outside.md", "do not delete");
    bool rejected = false;
    try {
        workspace.delete_agent(workspace.agents_root() / "../outside.md");
    } catch (const std::exception&) {
        rejected = true;
    }
    expect(rejected && std::filesystem::exists(root / ".agents" / "outside.md"), "agent path traversal was accepted");

    for (const auto* name : {"orchestrator.md", "code-reviewer.md", "wiki-curator.md"}) {
        rejected = false;
        try {
            workspace.delete_agent(workspace.agents_root() / name);
        } catch (const std::exception&) {
            rejected = true;
        }
        expect(rejected && std::filesystem::exists(workspace.agents_root() / name), "built-in agent was not protected");
    }

    raven::write_file(root / ".claude" / "agents" / "orchestrator.md", "conflicting legacy copy");
    workspace.initialize();
    expect(raven::read_file(shared_orchestrator).find("Custom orchestrator") != std::string::npos,
           "conflicting legacy agent overwrote the shared source");
    std::filesystem::remove_all(root);
}

void knowledge_tests() {
    const auto root = std::filesystem::temp_directory_path() / "raven-ide-knowledge-tests";
    std::filesystem::remove_all(root);
    raven::KnowledgeGraph graph(root / "wiki");
    const raven::GraphOperation alpha{raven::GraphOpKind::UpsertEntity, "repo-alpha", "repository", "repo-alpha", "", "", "wiki/Architecture.md", 1.0, {{"role", "ingestion"}}};
    const raven::GraphOperation beta{raven::GraphOpKind::Relate, "repo-alpha|depends_on|repo-beta", "depends_on", "", "repo-alpha", "repo-beta", "wiki/Architecture.md", 0.9, {}};
    graph.stage(alpha);
    graph.stage({raven::GraphOpKind::UpsertEntity, "repo-beta", "repository", "repo-beta", "", "", "wiki/Architecture.md", 1.0, {{"role", "queries"}}});
    graph.apply(graph.staged(), graph.revision());
    graph.stage(beta);
    graph.apply(graph.staged(), graph.revision());

    const auto stats = graph.stats();
    expect(stats.entities == 2 && stats.relations == 1 && stats.staged == 0, "graph apply/staging counts are wrong");
    const auto context = graph.context("repo-alpha");
    expect(context.find("role=ingestion") != std::string::npos, "graph context missed entity properties");
    expect(context.find("INFER repo-alpha -depends_on(depth=1)-> repo-beta") != std::string::npos,
           "transitive inference missing from graph context");
    expect(context.find("source=wiki/Architecture.md") != std::string::npos, "provenance missing from graph context");

    bool rejected = false;
    try {
        graph.apply({alpha}, "stale-revision");
    } catch (const std::exception&) {
        rejected = true;
    }
    expect(rejected, "stale graph revision was accepted");
    rejected = false;
    try {
        graph.apply(std::vector<raven::GraphOperation>{{raven::GraphOpKind::Relate, "bad|depends_on|missing", "depends_on", "", "bad", "missing", "test", 1.0, {}}}, graph.revision());
    } catch (const std::exception&) {
        rejected = true;
    }
    expect(rejected, "relation with missing endpoint was accepted");
    expect(graph.stats().relations == 1, "failed relation changed graph state");
    expect(graph.forget("repo-alpha"), "accepted entity could not be forgotten");
    expect(graph.stats().entities == 1 && graph.stats().relations == 0, "forget did not clean touching relations");
    expect(!graph.forget("missing"), "forgetting an unknown record reported success");
    raven::KnowledgeGraph reloaded(root / "wiki");
    expect(reloaded.stats().entities == 1 && reloaded.stats().relations == 0, "forget did not persist atomically");

    raven::KnowledgeGraph vectors(root / "vectors");
    vectors.stage({raven::GraphOpKind::UpsertEntity, "run:renewal", "episode", "Token renewal", "", "", "run:renewal", 1.0,
        {{"transcript", "Renewal extends authentication credentials."}}});
    vectors.stage({raven::GraphOpKind::UpsertEntity, "run:deploy", "episode", "Deployment", "", "", "run:deploy", 1.0,
        {{"transcript", "Packaging a release for production."}}});
    vectors.apply(vectors.staged(), vectors.revision());
    expect(vectors.context("renewing login").find("run:renewal") != std::string::npos,
           "deterministic local vectors did not recall a morphological paraphrase");
    expect(vectors.context("renewing login", 8, false).empty(), "disabling vector recall did not restore lexical-only matching");
    std::filesystem::remove_all(root);
}

void memory_tests() {
    const auto root = std::filesystem::temp_directory_path() / "raven-ide-memory-tests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    raven::Workspace workspace(root);
    workspace.initialize();

    workspace.stage_run_episode("session-1", "Fix authentication timeout", "Kimi Code",
        "USER:\nFix authentication timeout\n\nASSISTANT:\nUpdated token refresh logic.", "completed");
    const auto staged = workspace.knowledge().staged();
    expect(staged.size() == 1 && staged[0].entity_type == "episode", "completed run did not stage an episode");
    expect(staged[0].properties.at("memory") == "episodic" && staged[0].properties.at("session_id") == "session-1",
           "episode metadata is incomplete");
    workspace.knowledge().apply(staged, workspace.knowledge().revision());

    auto stats = workspace.knowledge().stats();
    expect(stats.entities == 1 && stats.episodes == 1 && stats.staged == 0, "accepted episode was not persisted");
    const auto recalled = workspace.knowledge_context("authentication timeout");
    expect(recalled.find("Updated token refresh logic") != std::string::npos && recalled.find("episode") != std::string::npos,
           "relevant episode was not recalled");

    workspace.stage_run_episode("session-1", "Fix authentication timeout", "Kimi Code",
        "USER:\nFix authentication timeout\n\nASSISTANT:\nAdjusted refresh expiry.", "completed");
    workspace.knowledge().apply(workspace.knowledge().staged(), workspace.knowledge().revision());
    stats = workspace.knowledge().stats();
    expect(stats.entities == 1 && stats.episodes == 1, "resumed session duplicated its episode");
    expect(workspace.knowledge_context("refresh expiry").find("Adjusted refresh expiry") != std::string::npos,
           "resumed session did not update remembered content");

    raven::KnowledgeGraph reloaded(root / "wiki");
    expect(reloaded.stats().episodes == 1, "episode did not survive graph reload");
    expect(reloaded.context("authentication").find("run:session-1") != std::string::npos,
           "reloaded episode could not be retrieved");

    workspace.stage_run_episode("session-2", "Large memory", "Claude Code", std::string(20'000, 'x'), "completed");
    const auto bounded = workspace.knowledge().staged();
    expect(bounded.size() == 1 && bounded[0].properties.at("transcript").size() <= 16'000,
           "episode transcript was not bounded");
    std::filesystem::remove_all(root);
}

void memory_universe_tests() {
    const raven::GraphOperation episode{raven::GraphOpKind::UpsertEntity, "run:one", "episode", "First run", "", "", "run:one", 1.0,
        {{"transcript", "remembered"}}};
    const raven::GraphOperation entity{raven::GraphOpKind::UpsertEntity, "repo:stellar", "repository", "Stellar", "", "", "wiki/Home.md", 1.0, {}};
    const raven::GraphOperation relation{raven::GraphOpKind::Relate, "run:one|mentions|repo:stellar", "mentions", "", "run:one", "repo:stellar", "wiki/Home.md", 1.0, {}};
    raven::MemoryUniverse universe;
    universe.set_data({episode, entity}, {relation});
    const auto stats = universe.stats();
    expect(stats.entities == 2 && stats.episodes == 1 && stats.relations == 1, "Memory Universe counts are incorrect");
    expect(universe.selected_markdown().empty(), "Memory Universe invented an initial selection");
    universe.fit();
    universe.set_data({}, {});
    const auto empty = universe.stats();
    expect(empty.entities == 0 && empty.episodes == 0 && empty.relations == 0, "Memory Universe retained cleared data");
    expect(!universe.advance(0.1, true), "Memory Universe animated with reduced motion enabled");
    expect(universe.advance(0.04, false), "Memory Universe did not reach its animation frame");
    expect(!universe.advance(0.01, false), "Memory Universe exceeded its animation frame rate");
    raven::ThemeGallery gallery;
    expect(!gallery.advance(0.1, true), "Theme gallery animated with reduced motion enabled");
    expect(gallery.advance(0.04, false), "Theme gallery did not reach its animation frame");
    expect(!gallery.advance(0.01, false), "Theme gallery exceeded its animation frame rate");
    REvent wheel{};
    wheel.type = R_EVENT_MOUSE_WHEEL;
    wheel.wheel_y = 1.0f;
    expect(gallery.event(R_INVALID_ID, &wheel) == -1, "Theme gallery did not navigate a complete wheel step");
    expect(gallery.event(R_INVALID_ID, &wheel) == 0, "Theme gallery navigated twice during one wheel gesture");
    wheel.wheel_y = -1.0f;
    expect(gallery.event(R_INVALID_ID, &wheel) == 0, "Theme gallery reversed during the wheel cooldown");
    REvent mouse{};
    mouse.type = R_EVENT_MOUSE_MOVE;
    expect(gallery.event(R_INVALID_ID, &mouse) == 0, "Theme gallery treated pointer movement as navigation");
    mouse.type = R_EVENT_MOUSE_DOWN;
    mouse.button = R_MOUSE_LEFT;
    expect(gallery.event(R_INVALID_ID, &mouse) == 0, "Theme gallery treated pointer press as navigation");
    mouse.type = R_EVENT_MOUSE_UP;
    expect(gallery.event(R_INVALID_ID, &mouse) == 0, "Theme gallery navigated without a side-preview hit");
}

void composer_tests() {
    const auto root = std::filesystem::temp_directory_path() / "raven-ide-composer-tests";
    std::filesystem::remove_all(root);
    raven::write_file(root / ".agents" / "skills" / "test-skill" / "SKILL.md",
        "---\nname: test-skill\ndescription: Test workflow\n---\nFollow the test workflow.\n");
    raven::write_file(root / "src" / "main.cpp", "int main() {}\n");
    raven::write_file(root / "docs" / "with space.md", "# Test\n");
    raven::write_file(root / "build" / "ignored.cpp", "ignored\n");
    raven::write_file(root / ".stellar" / "threads.json", "sentinel\n");

    raven::ComposerIndex index;
    index.set_workspace(root);
    index.refresh();
    const auto skills = index.skills("test", false);
    expect(skills.size() == 1 && skills[0].text == "test-skill" && skills[0].detail == "Test workflow",
           "project skill completion failed");
    const auto namespaced = index.skills("test", true);
    expect(namespaced.size() == 1 && namespaced[0].text == "test-skill", "namespaced skill completion failed");
    const auto files = index.files("src/", "ma");
    expect(files.size() == 1 && files[0].text == "main.cpp" && files[0].detail == "src",
           "nested workspace file completion failed");
    expect(!index.has_file("build/ignored.cpp"), "build output leaked into file completion");
    const auto mentions = index.mentions("Read @src/main.cpp and @\"docs/with space.md\"");
    expect(mentions.size() == 2 && mentions[0].filename() == "main.cpp" && mentions[1].filename() == "with space.md",
           "workspace resource mentions were not extracted");
    const auto details = index.mention_details("Read @src/main.cpp and @\"docs/with space.md\"");
    expect(details.size() == 2 && details[0].bytes > 0 && details[0].tokens >= 1 && details[1].tokens >= 1,
           "attachment token estimates are incorrect");
    expect(index.strip_mention("Read @src/main.cpp and @\"docs/with space.md\"", "src/main.cpp") == "Read  and @\"docs/with space.md\"",
           "single attachment removal damaged the remaining prompt");
    expect(index.mentions("Read @../outside.cpp").empty(), "outside file mention was accepted");
    expect(index.strip_mentions("Read @src/main.cpp and @\"docs/with space.md\" then @../outside.cpp") ==
           "Read  and  then @../outside.cpp", "valid context mentions were not removed safely");
    expect(index.search_files("MAIN").size() == 1, "case-insensitive file search failed");
    const auto matches = index.search_content("INT MAIN");
    expect(matches.size() == 1 && matches[0].path == "src/main.cpp" && matches[0].line == 1,
           "global content search returned the wrong location");
    expect(index.search_content("sentinel").empty(), "Stellar state leaked into global search");
    std::filesystem::remove_all(root);
}

void verification_tests() {
    expect(raven::verification_command_kind("ctest --output-on-failure") == "test", "ctest was not recognized");
    expect(raven::verification_command_kind("cmake --build build") == "build", "CMake build was not recognized");
    expect(raven::verification_command_kind("cmake --build build && ctest") == "test", "combined build and test was not recognized");
    expect(raven::verification_command_kind("npm run test") == "test", "npm test was not recognized");
    expect(raven::verification_command_kind("cat docs/testing.md").empty(), "unrelated command became a verification check");

    const auto diagnostics = raven::parse_diagnostics(
        "src/main.cpp:4:9: error: expected semicolon\n"
        "src/main.cpp:8: warning: unused value\n"
        "  File \"tests/test_app.py\", line 17, in test_answer\n"
        "  --> src/lib.rs:12:5\n");
    expect(diagnostics.size() == 4, "diagnostics were not parsed");
    expect(diagnostics[0].path == "src/main.cpp" && diagnostics[0].line == 4 && diagnostics[0].column == 9 &&
           diagnostics[0].severity == "error", "native diagnostic fields are incorrect");
    expect(diagnostics[1].severity == "warning", "warning severity was not preserved");
    expect(diagnostics[2].path == "tests/test_app.py" && diagnostics[2].line == 17,
           "Python diagnostic location is incorrect");
    expect(diagnostics[3].path == "src/lib.rs" && diagnostics[3].column == 5,
           "Rust diagnostic location is incorrect");

    const auto root = std::filesystem::temp_directory_path() / "raven-ide-verification-tests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    raven::CheckpointStore store(root);
    const auto id = store.begin("Run regression checks");
    const std::vector<raven::VerificationCheck> checks = {
        {"tool-1", "ctest --output-on-failure", "test", raven::VerificationStatus::Failed,
         "src/main.cpp:4:9: error: expected semicolon\n"}
    };
    const auto checkpoint = store.finish(id, "complete", checks);
    expect(checkpoint.changes.empty() && checkpoint.checks.size() == 1,
           "verification-only checkpoint has incorrect contents");
    expect(std::filesystem::exists(root / ".stellar" / "checkpoints" / id / "checkpoint.json"),
           "verification-only checkpoint was not retained");
    raven::CheckpointStore loaded(root);
    loaded.load();
    expect(loaded.checkpoints().size() == 1 && loaded.find(id)->checks[0].status == raven::VerificationStatus::Failed &&
           loaded.find(id)->checks[0].output == checks[0].output,
           "verification results did not survive reload");
    const auto passed = raven::run_verification_command("tool-1", "printf 'RERUN_OK\\n'", root);
    expect(passed.status == raven::VerificationStatus::Passed && passed.output.find("RERUN_OK") != std::string::npos,
           "passing rerun returned the wrong result");
    const auto failed = raven::run_verification_command("tool-1", "exit 3", root);
    expect(failed.status == raven::VerificationStatus::Failed, "failing rerun returned the wrong result");
    store.update_check(id, passed);
    raven::CheckpointStore updated(root);
    updated.load();
    expect(updated.find(id)->checks[0].status == raven::VerificationStatus::Passed &&
           updated.find(id)->checks[0].output == passed.output, "rerun result did not persist");
    std::filesystem::remove_all(root);
}

void checkpoint_tests() {
    const auto root = std::filesystem::temp_directory_path() / "raven-ide-checkpoint-tests";
    std::filesystem::remove_all(root);
    raven::write_file(root / "src" / "main.cpp", "int main() { return 0; }\n");
    raven::write_file(root / "src" / "obsolete.txt", "remove me\n");
    raven::CheckpointStore store(root);
    const auto id = store.begin("Update source files");
    raven::write_file(root / "src" / "main.cpp", "int main() { return 1; }\n");
    raven::write_file(root / "src" / "created.txt", "new file\n");
    raven::write_file(root / "build" / "ignored.txt", "build output\n");
    std::filesystem::remove(root / "src" / "obsolete.txt");

    const auto checkpoint = store.finish(id, "complete");
    expect(checkpoint.changes.size() == 3, "checkpoint did not detect added, modified, and deleted files");
    expect(store.diff(id, "src/main.cpp").find("+ int main() { return 1; }") != std::string::npos,
           "checkpoint source diff is missing the modification");
    expect(store.diff(id, "src/created.txt").find("+ new file") != std::string::npos,
           "checkpoint source diff is missing the addition");
    expect(std::none_of(checkpoint.changes.begin(), checkpoint.changes.end(), [](const auto& change) {
        return change.path.find("build/") == 0;
    }), "build output leaked into a checkpoint");

    raven::CheckpointStore loaded(root);
    loaded.load();
    expect(loaded.checkpoints().size() == 1 && loaded.find(id) != nullptr,
           "checkpoint did not survive reload");
    raven::write_file(root / "src" / "main.cpp", "user work after the run\n");
    bool rejected = false;
    try {
        loaded.restore(id);
    } catch (const std::exception&) {
        rejected = true;
    }
    expect(rejected && raven::read_file(root / "src" / "main.cpp") == "user work after the run\n" &&
           std::filesystem::exists(root / "src" / "created.txt"),
           "stale checkpoint restore overwrote later work or partially applied");

    raven::write_file(root / "src" / "main.cpp", "int main() { return 1; }\n");
    loaded.restore(id);
    expect(raven::read_file(root / "src" / "main.cpp") == "int main() { return 0; }\n" &&
           !std::filesystem::exists(root / "src" / "created.txt") &&
           raven::read_file(root / "src" / "obsolete.txt") == "remove me\n",
           "checkpoint did not restore all change types");
    rejected = false;
    try {
        loaded.restore(id);
    } catch (const std::exception&) {
        rejected = true;
    }
    expect(rejected, "a restored checkpoint could be applied twice");

    const auto empty_id = loaded.begin("No source changes");
    const auto empty = loaded.finish(empty_id, "complete");
    expect(empty.changes.empty() && !std::filesystem::exists(root / ".stellar" / "checkpoints" / empty_id),
           "empty checkpoint was retained");
    std::filesystem::remove_all(root);
}

void hunk_review_tests() {
    const std::string patch =
        "diff --git a/src/a.cpp b/src/a.cpp\n"
        "--- a/src/a.cpp\n"
        "+++ b/src/a.cpp\n"
        "@@ -1 +1 @@\n"
        "-old\n"
        "+new\n"
        "diff --git a/src/b.cpp b/src/b.cpp\n"
        "--- a/src/b.cpp\n"
        "+++ b/src/b.cpp\n"
        "@@ -1 +1 @@\n"
        "-before\n"
        "+after\n";
    const auto files = raven::parse_unified_diff(patch);
    expect(files.size() == 2 && files[0].hunks.size() == 1 && files[1].hunks.size() == 1,
           "unified diff files or hunks were not parsed");
    const auto selected = raven::build_patch(files, {"src/a.cpp#0"});
    expect(selected.find("+new") != std::string::npos && selected.find("+after") == std::string::npos,
           "hunk selection included the wrong patch");

    const auto root = std::filesystem::temp_directory_path() / "raven-ide-hunk-tests";
    std::filesystem::remove_all(root);
    raven::write_file(root / "src" / "main.cpp", "one\ntwo\n");
    raven::CheckpointStore store(root);
    const auto id = store.begin("Partial restore");
    raven::write_file(root / "src" / "main.cpp", "one\nchanged\n");
    raven::write_file(root / "created.txt", "new file\n");
    store.finish(id, "complete");
    store.restore_hunks(id, {"src/main.cpp#0"});
    expect(raven::read_file(root / "src" / "main.cpp") == "one\ntwo\n" && std::filesystem::exists(root / "created.txt"),
           "partial restore changed the wrong files");
    expect(store.find(id)->applied_hunks.size() == 1, "restored hunk was not persisted");
    store.restore(id);
    expect(!std::filesystem::exists(root / "created.txt") && store.find(id)->status == "restored",
           "full restore did not complete the remaining hunks");
    std::filesystem::remove_all(root);
}

void mission_tests() {
    const auto root = std::filesystem::temp_directory_path() / "raven-ide-mission-tests";
    std::filesystem::remove_all(root);
    raven::write_file(root / "src" / "main.cpp", "int main() { return 0; }\n");
    const auto git = [&](const std::string& arguments) {
        const auto command = "git -C \"" + root.string() + "\" " + arguments + " >/dev/null 2>&1";
        expect(std::system(command.c_str()) == 0, "Git test setup failed");
    };
    git("init");
    git("config user.email stellar@example.test");
    git("config user.name Stellar");
    git("add src/main.cpp");
    git("commit -m initial");
    raven::write_file(root / ".agents" / "agents" / "orchestrator.md", "shared agent\n");
    raven::write_file(root / "wiki" / "Home.md", "shared knowledge\n");

    raven::GitMissionStore store(root);
    const auto mission = store.create(root, "Verify isolated change");
    expect(std::filesystem::is_directory(mission.worktree) && !mission.base_head.empty(), "mission worktree was not created");
    raven::write_file(mission.worktree / "src" / "main.cpp", "int main() { return 2; }\n");
    raven::write_file(mission.worktree / "docs" / "new.md", "untracked change\n");
    raven::write_file(mission.worktree / "tool-binary", std::string("ELF\0\1\2", 6));
    const auto difference = store.diff(mission.id);
    expect(difference.find("return 2") != std::string::npos && difference.find("docs/new.md") != std::string::npos,
           "mission diff missed tracked or untracked changes");
    expect(difference.find(".agents") == std::string::npos && difference.find("wiki/Home.md") == std::string::npos,
           "shared agent or wiki context leaked into the mission patch");
    expect(difference.find("tool-binary") == std::string::npos, "untracked binary artifact leaked into the mission patch");
    store.apply_hunks(mission.id, {"src/main.cpp#0"});
    expect(raven::read_file(root / "src" / "main.cpp") == "int main() { return 2; }\n" &&
           !std::filesystem::exists(root / "docs" / "new.md"), "mission hunk applied the wrong files");
    expect(store.find(mission.id)->status == "partially-applied" && store.find(mission.id)->applied_hunks.size() == 1,
           "partial mission apply was not persisted");
    store.apply(mission.id);
    expect(raven::read_file(root / "src" / "main.cpp") == "int main() { return 2; }\n" &&
           raven::read_file(root / "docs" / "new.md") == "untracked change\n" &&
           !std::filesystem::exists(root / "tool-binary"), "mission did not complete the remaining hunks");
    store.load();
    expect(store.find(mission.id)->status == "applied", "applied mission state did not persist");
    bool rejected = false;
    try {
        store.apply(mission.id);
    } catch (const std::exception&) {
        rejected = true;
    }
    expect(rejected, "mission applied twice");

    const auto conflict = store.create(root, "Conflicting change");
    raven::write_file(conflict.worktree / "src" / "main.cpp", "int main() { return 3; }\n");
    raven::write_file(root / "src" / "main.cpp", "int main() { return 99; }\n");
    rejected = false;
    try {
        store.apply(conflict.id);
    } catch (const std::exception&) {
        rejected = true;
    }
    expect(rejected && raven::read_file(root / "src" / "main.cpp") == "int main() { return 99; }\n",
           "conflicting mission overwrote user work");
    store.discard(conflict.id);
    expect(!std::filesystem::exists(conflict.worktree) && store.missions().size() == 1, "mission discard did not clean up");
    std::filesystem::remove_all(root);
}

void recovery_tests() {
    const auto root = std::filesystem::temp_directory_path() / "raven-ide-recovery-tests";
    std::filesystem::remove_all(root);
    raven::RecoveryStore store(root);
    store.set_draft("unfinished prompt");
    store.set_editor("code", root / "src" / "main.cpp", "recovered edit", "saved version");
    raven::RecoveryStore loaded(root);
    expect(loaded.draft() == "unfinished prompt" && loaded.editors().size() == 1 &&
           loaded.editors()[0].content == "recovered edit" && loaded.editors()[0].saved_content == "saved version",
           "recovery data did not survive reload");
    loaded.clear_editor("code", root / "src" / "main.cpp");
    loaded.clear_draft();
    raven::RecoveryStore cleared(root);
    expect(cleared.draft().empty() && cleared.editors().empty(), "saved recovery data was not cleared");
    std::filesystem::remove_all(root);
}

void thread_tests() {
    const auto root = std::filesystem::temp_directory_path() / "raven-ide-thread-tests";
    std::filesystem::remove_all(root);
    raven::ThreadStore store(root);
    store.load();
    raven::ConversationThread alpha;
    alpha.id = "alpha";
    alpha.title = "First conversation";
    alpha.session_id = "session-alpha";
    alpha.transcript = "USER:\nFirst";
    alpha.harness = raven::Harness::KimiCode;
    alpha.updated_at = 10;
    alpha.messages = {{"YOU", "First", true}, {"ORCHESTRATOR", "Answer", false}};
    store.upsert(alpha);
    raven::ConversationThread beta;
    beta.id = "beta";
    beta.title = "Second conversation";
    beta.harness = raven::Harness::ClaudeCode;
    beta.updated_at = 20;
    beta.messages = {{"YOU", "Second", true}};
    store.upsert(beta);
    store.save();

    raven::ThreadStore loaded(root);
    loaded.load();
    expect(loaded.threads().size() == 2 && loaded.latest()->id == "beta", "thread recency ordering failed");
    const auto* restored = loaded.find("alpha");
    expect(restored && restored->session_id == "session-alpha" && restored->messages.size() == 2 &&
           restored->messages[1].text == "Answer" && restored->harness == raven::Harness::KimiCode,
           "thread messages or session did not survive reload");
    alpha.updated_at = 30;
    loaded.upsert(alpha);
    expect(loaded.latest()->id == "alpha", "updated thread did not move to recent history");
    loaded.find("alpha")->archived = true;
    loaded.save();
    expect(loaded.latest()->id == "beta", "archived thread remained in active recency");

    raven::ThreadStore archived(root);
    archived.load();
    const auto* archived_alpha = archived.find("alpha");
    expect(archived_alpha && archived_alpha->archived && archived_alpha->messages.size() == 2 &&
           archived_alpha->session_id == "session-alpha" && archived.latest()->id == "beta",
           "archive state or content did not survive reload");
    archived.find("alpha")->archived = false;
    archived.save();

    raven::ThreadStore restored_active(root);
    restored_active.load();
    expect(restored_active.latest()->id == "alpha" && !restored_active.find("alpha")->archived,
           "restored thread did not return to active recency");
    expect(std::filesystem::exists(root / ".stellar" / "threads.json"), "thread store was not persisted");
    std::filesystem::remove_all(root);
}

void parser_tests() {
    const auto schema = nlohmann::json::parse(raven::wiki_schema());
    expect(schema["required"].size() == 3 && schema["properties"]["graph"]["items"]["properties"]["op"]["enum"].size() == 2,
           "wiki graph schema is invalid");

    raven::ClaudeEventParser parser;
    const auto init = parser.parse(R"({"type":"system","subtype":"init","session_id":"abc","model":"sonnet"})");
    expect(init.size() == 1 && init[0].kind == raven::EventKind::SessionStarted && init[0].session_id == "abc",
           "session event was not parsed");

    const auto delta = parser.parse(R"({"type":"stream_event","session_id":"abc","event":{"type":"content_block_delta","delta":{"type":"text_delta","text":"hello"}}})");
    expect(delta.size() == 1 && delta[0].text == "hello", "assistant delta was not parsed");

    const auto task = parser.parse(R"({"type":"assistant","parent_tool_use_id":null,"message":{"id":"m1","content":[{"type":"tool_use","id":"t1","name":"Task","input":{"subagent_type":"Explore","description":"Inspect auth"}}]}})");
    expect(task.size() == 1 && task[0].kind == raven::EventKind::SubagentStarted && task[0].tool_id == "t1" &&
           task[0].name == "Explore" && task[0].detail == "Inspect auth", "subagent event was not parsed");

    const auto child = parser.parse(R"({"type":"stream_event","parent_tool_use_id":"t1","event":{"type":"content_block_delta","delta":{"type":"text_delta","text":"child"}}})");
    expect(child.size() == 1 && child[0].parent_tool_id == "t1", "subagent parent was not preserved");

    const auto result = parser.parse(R"({"type":"result","subtype":"success","result":"done","is_error":false})");
    expect(result.size() == 1 && result[0].kind == raven::EventKind::RunFinished && result[0].text == "done",
           "result event was not parsed");

    const auto structured = parser.parse(R"({"type":"result","result":"Returning the requested output.","structured_output":{"summary":"OK","edits":[]},"is_error":false})");
    expect(structured.size() == 1 && structured[0].text == R"({"edits":[],"summary":"OK"})",
           "structured output was not preferred");

    const auto usage = parser.parse(R"({"type":"result","subtype":"success","result":"done","total_cost_usd":0.125,"usage":{"input_tokens":100,"cache_creation_input_tokens":20,"cache_read_input_tokens":30,"output_tokens":45}})");
    expect(usage.size() == 1 && usage[0].has_usage && usage[0].cost_usd == 0.125 &&
           usage[0].input_tokens == 150 && usage[0].output_tokens == 45,
           "run cost or token usage was not parsed");

    raven::KimiEventParser kimi;
    const auto kimi_task = kimi.parse(R"({"role":"assistant","tool_calls":[{"type":"function","id":"tool_1","function":{"name":"Agent","arguments":"{\"description\":\"Inspect workspace\",\"subagent_type\":\"explore\",\"prompt\":\"Read only\"}"}}]})");
    expect(kimi_task.size() == 1 && kimi_task[0].kind == raven::EventKind::SubagentStarted &&
           kimi_task[0].tool_id == "tool_1" && kimi_task[0].name == "explore" && kimi_task[0].detail == "Inspect workspace",
           "Kimi subagent event was not parsed");
    const auto kimi_tool_result = kimi.parse(R"({"role":"tool","tool_call_id":"tool_1","content":"status: completed"})");
    expect(kimi_tool_result.size() == 1 && kimi_tool_result[0].kind == raven::EventKind::ToolFinished,
           "Kimi tool result was not parsed");
    const auto kimi_session = kimi.parse(R"({"role":"meta","type":"session.resume_hint","session_id":"session_abc","command":"kimi -r session_abc"})");
    expect(kimi_session.size() == 2 && kimi_session[0].kind == raven::EventKind::SessionStarted &&
           kimi_session[0].session_id == "session_abc" && kimi_session[1].kind == raven::EventKind::RunFinished,
           "Kimi session completion was not parsed");

    raven::KimiAcpEventParser acp;
    const auto thought = acp.parse(R"({"jsonrpc":"2.0","method":"session/update","params":{"sessionId":"s1","update":{"sessionUpdate":"agent_thought_chunk","content":{"type":"text","text":"I need"}}}})");
    expect(thought.size() == 1 && thought[0].kind == raven::EventKind::ThoughtDelta && thought[0].text == "I need" &&
           thought[0].session_id == "s1", "ACP thought chunk was not parsed");
    const auto message = acp.parse(R"({"jsonrpc":"2.0","method":"session/update","params":{"sessionId":"s1","update":{"sessionUpdate":"agent_message_chunk","content":{"type":"text","text":"OK"}}}})");
    expect(message.size() == 1 && message[0].kind == raven::EventKind::AssistantDelta && message[0].text == "OK",
           "ACP message chunk was not parsed");
    const auto acp_tool = acp.parse(R"({"jsonrpc":"2.0","method":"session/update","params":{"sessionId":"s1","update":{"sessionUpdate":"tool_call","toolCallId":"t1","title":"Bash","status":"in_progress","rawInput":{"command":"pwd"}}}})");
    expect(acp_tool.size() == 1 && acp_tool[0].kind == raven::EventKind::ToolStarted && acp_tool[0].tool_id == "t1" &&
           acp_tool[0].detail == "pwd", "ACP tool start was not parsed");
    const auto acp_write = acp.parse(R"({"jsonrpc":"2.0","method":"session/update","params":{"sessionId":"s1","update":{"sessionUpdate":"tool_call","toolCallId":"t2","title":"Write","status":"in_progress","rawInput":{"path":"main.cpp","content":"int main() {}"}}}})");
    expect(acp_write.size() == 1 && acp_write[0].detail == "main.cpp", "ACP file path was not parsed");
    const auto acp_progress = acp.parse(R"({"jsonrpc":"2.0","method":"session/update","params":{"sessionId":"s1","update":{"sessionUpdate":"tool_call_update","toolCallId":"t2","title":"Write","status":"in_progress","rawInput":{"path":"src/main.cpp"}}}})");
    expect(acp_progress.size() == 1 && acp_progress[0].kind == raven::EventKind::ToolStarted &&
           acp_progress[0].tool_id == "t2" && acp_progress[0].detail == "src/main.cpp",
           "ACP progress did not preserve late tool input");
    const auto acp_result = acp.parse(R"({"jsonrpc":"2.0","method":"session/update","params":{"sessionId":"s1","update":{"sessionUpdate":"tool_call_update","toolCallId":"t1","status":"completed","content":[{"type":"text","text":"/workspace"}]}}})");
    expect(acp_result.size() == 1 && acp_result[0].kind == raven::EventKind::ToolFinished && !acp_result[0].is_error &&
           acp_result[0].text == "/workspace", "ACP tool completion was not parsed");
    const auto acp_raw = acp.parse(R"({"jsonrpc":"2.0","method":"session/update","params":{"sessionId":"s1","update":{"sessionUpdate":"tool_call_update","toolCallId":"t3","status":"failed","rawOutput":"src/main.cpp:4:5: error: expected ';'\nnext line\n"}}})");
    expect(acp_raw.size() == 1 && acp_raw[0].text == "src/main.cpp:4:5: error: expected ';'\nnext line\n",
           "ACP raw output stayed JSON escaped");
    expect(raven::parse_diagnostics(acp_raw[0].text).size() == 1, "real ACP output did not produce a diagnostic");
}

void notification_tests() {
    const auto root = std::filesystem::temp_directory_path() / "raven-ide-notification-tests";
    std::filesystem::remove_all(root);
    raven::NotificationStore store(root);
    for (int i = 0; i < 55; ++i) {
        store.add({"", "Run " + std::to_string(i), "Body", "run", "agent", "", 1000 + i, false});
    }
    expect(store.entries().size() == 50 && store.entries().front().title == "Run 5" && store.entries().back().title == "Run 54",
           "notification history did not retain the newest 50 entries");
    raven::NotificationStore reloaded(root);
    expect(reloaded.entries().size() == 50 && reloaded.find(store.entries().back().id).has_value(),
           "notification history or action metadata did not survive reload");
    const auto latest_id = reloaded.entries().back().id;
    reloaded.update_body(latest_id, "Run cost · $0.25");
    expect(raven::NotificationStore(root).find(latest_id)->body == "Run cost · $0.25",
           "run receipt update was not persisted");
    reloaded.clear();
    expect(reloaded.entries().empty() && raven::NotificationStore(root).entries().empty(), "clearing notifications was not persisted");
    std::filesystem::remove_all(root);
}

void recall_tests() {
    const auto root = std::filesystem::temp_directory_path() / "raven-ide-recall-tests";
    std::filesystem::remove_all(root);
    raven::RecallStore store(root);
    expect(!store.storage_exists() && store.entries().empty(), "new Recall library was not empty");
    store.remember("  Fix the auth boundary  ", 10);
    store.remember("Fix the auth boundary", 20);
    store.remember("Write the release notes", 30);
    expect(store.entries().size() == 2 && store.storage_exists(), "Recall prompts were not persisted");
    const auto ranked = store.recommend();
    expect(ranked.size() == 2 && ranked[0]->text == "Fix the auth boundary" && ranked[0]->uses == 2,
           "Recall did not rank repeated prompts first");
    const auto docs = store.recommend("release");
    expect(docs.size() == 1 && store.toggle_pin(docs[0]->id), "Recall search or pin failed");
    expect(store.recommend()[0]->text == "Write the release notes", "pinned prompt did not move to the top");
    raven::RecallStore reloaded(root);
    expect(reloaded.entries().size() == 2 && reloaded.recommend()[0]->pinned &&
           reloaded.recommend("auth")[0]->uses == 2, "Recall ranking did not survive reload");
    reloaded.erase(reloaded.recommend("auth")[0]->id);
    expect(raven::RecallStore(root).entries().size() == 1, "forgetting a Recall prompt was not persisted");
    std::filesystem::remove_all(root);
}

void profile_tests() {
    const auto root = std::filesystem::temp_directory_path() / "raven-ide-profile-tests";
    std::filesystem::remove_all(root);
    raven::WorkspaceProfileStore store(root);
    store.data.interface_zoom = 1.35f;
    store.data.editor_zoom = 1.2f;
    store.data.theme = 16;
    store.data.harness = 1;
    store.data.run_mode = 2;
    store.data.claude_model = "opus";
    store.data.claude_effort = "xhigh";
    store.data.kimi_model = "kimi-code/kimi-for-coding";
    store.data.kimi_effort = "high";
    store.data.left_sidebar = false;
    store.data.vector_recall = false;
    store.data.watch_checks = true;
    store.data.do_not_disturb = true;
    store.data.opening_sound = false;
    store.data.notification_mutes["verification"] = true;
    store.data.shortcuts["memory.open"] = "Ctrl+Alt+M";
    store.save();
    raven::WorkspaceProfileStore reloaded(root);
    expect(reloaded.data.interface_zoom == 1.35f && reloaded.data.editor_zoom == 1.2f && reloaded.data.theme == 16,
           "appearance profile did not survive reload");
    expect(reloaded.data.harness == 1 && reloaded.data.run_mode == 2 && !reloaded.data.left_sidebar &&
           !reloaded.data.vector_recall && reloaded.data.watch_checks && reloaded.data.do_not_disturb && !reloaded.data.opening_sound,
           "workspace behavior profile did not survive reload");
    expect(reloaded.data.notification_mutes.at("verification") && reloaded.data.shortcuts.at("memory.open") == "Ctrl+Alt+M",
           "notification or shortcut profile did not survive reload");
    expect(reloaded.data.claude_model == "opus" && reloaded.data.claude_effort == "xhigh" &&
           reloaded.data.kimi_model == "kimi-code/kimi-for-coding" && reloaded.data.kimi_effort == "high",
           "model or effort profile did not survive reload");
    std::filesystem::remove_all(root);
}

void opening_audio_tests() {
    const auto root = std::filesystem::temp_directory_path() / "raven-ide-opening-audio-tests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto path = root / "opening.wav";
    std::ofstream output(path, std::ios::binary);
    const auto write16 = [&](int value) {
        output.put(static_cast<char>(value & 0xff));
        output.put(static_cast<char>((value >> 8) & 0xff));
    };
    const auto write32 = [&](std::uint32_t value) {
        write16(static_cast<int>(value & 0xffff));
        write16(static_cast<int>((value >> 16) & 0xffff));
    };
    output.write("RIFF", 4);
    write32(36 + 16000);
    output.write("WAVEfmt ", 8);
    write32(16);
    write16(1);
    write16(1);
    write32(8000);
    write32(16000);
    write16(2);
    write16(16);
    output.write("data", 4);
    write32(16000);
    for (int i = 0; i < 8000; ++i) write16(0);
    output.close();

    SDL_SetHint(SDL_HINT_AUDIODRIVER, "dummy");
    {
        raven::OpeningAudio audio;
        expect(audio.load(path) && audio.available(), "opening audio could not load a PCM WAV");
        audio.play();
        expect(audio.playing(), "opening audio did not start");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        audio.stop();
        for (int i = 0; i < 200 && audio.playing(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        expect(!audio.playing(), "opening audio did not finish its fade-out");
        audio.play();
        expect(audio.playing(), "opening audio did not resume after muting");
    }
    std::filesystem::remove_all(root);
}

void splash_tests() {
    raven::SplashScreen splash;
    splash.begin();
    expect(splash.active(), "splash did not begin");
    splash.advance(1.0, false);
    expect(splash.active(), "splash closed before its composed moment");
    splash.advance(20.0, false);
    expect(splash.active(), "complete poem closed before the reader skipped it");
    expect(!splash.advance(1.0, false) && !splash.animating(), "settled poem kept rendering continuously");
    expect(splash.advance(0.0, true), "reduced-motion poem did not present its settled composition");
    expect(splash.active() && !splash.advance(1.6, true), "reduced-motion poem did not wait quietly for Skip");
    splash.begin();
    REvent event{};
    event.type = R_EVENT_KEY_DOWN;
    event.key = R_KEY_ENTER;
    expect(splash.event(&event) && !splash.active(), "keyboard did not dismiss splash");
}

} // namespace

int main() {
    try {
        workspace_tests();
        agent_definition_tests();
        knowledge_tests();
        memory_tests();
        memory_universe_tests();
        composer_tests();
        verification_tests();
        checkpoint_tests();
        hunk_review_tests();
        mission_tests();
        recovery_tests();
        notification_tests();
        recall_tests();
        profile_tests();
        opening_audio_tests();
        splash_tests();
        thread_tests();
        parser_tests();
        std::cout << "All core tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
