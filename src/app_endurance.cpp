#include "app.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <regex>
#include <sstream>

namespace raven {
namespace {

std::string join_labels(const std::vector<std::string>& values, const std::string& separator = ", ") {
    std::string result;
    for (const auto& value : values) {
        if (!result.empty()) result += separator;
        result += value;
    }
    return result;
}

std::string short_text(const std::string& value, size_t length = 110) {
    return value.size() <= length ? value : value.substr(0, length - 1) + "…";
}

std::string time_label(std::int64_t timestamp, const std::string& timezone = "local") {
    if (timestamp == 0) return "Manual trigger only";
    const std::time_t raw = static_cast<std::time_t>(timestamp);
    std::tm time{};
    if (timezone == "UTC") gmtime_r(&raw, &time); else localtime_r(&raw, &time);
    char buffer[96];
    std::strftime(buffer, sizeof(buffer), "%a %d %b · %H:%M", &time);
    return std::string(buffer) + (timezone == "UTC" ? " UTC" : "");
}

std::string unique_step_id(const EnduranceWorkflow& workflow, const std::string& base) {
    int suffix = 1;
    std::string id;
    do { id = base + "-" + std::to_string(suffix++); } while (std::any_of(workflow.steps.begin(), workflow.steps.end(), [&](const auto& step) { return step.id == id; }));
    return id;
}

std::string pin_step(const std::map<std::string, int>& pins, int pin) {
    for (const auto& [id, candidate] : pins) if (candidate == pin) return id;
    return {};
}

} // namespace

EnduranceWorkflow* App::current_endurance_workflow() {
    return endurance_workflow_index_ >= 0 && endurance_workflow_index_ < static_cast<int>(endurance_workflows_.size())
        ? &endurance_workflows_[endurance_workflow_index_] : nullptr;
}

EnduranceStep* App::current_endurance_step() {
    auto* workflow = current_endurance_workflow();
    return workflow && endurance_step_index_ >= 0 && endurance_step_index_ < static_cast<int>(workflow->steps.size())
        ? &workflow->steps[endurance_step_index_] : nullptr;
}

const EnduranceRun* App::selected_endurance_run() const {
    if (endurance_run_index_ >= 0 && endurance_run_index_ < static_cast<int>(endurance_run_rows_.size())) {
        return endurance_.find_run(endurance_run_rows_[endurance_run_index_]);
    }
    return endurance_.active_run();
}

void App::open_endurance() {
    dismiss_splash();
    set_sidebar_visibility(false, false);
    set_active_view(id("center.tabs"), 2);
    set_active_view(id("endurance.views"), 0);
    rebuild_endurance_graph();
    endurance_graph_sync_pending_ = true;
    Rwidget_focus(id("endurance.graph"));
}

void App::start_endurance_with_task(const std::string& task) {
    open_endurance();
    Rtext_input_set_text(id("endurance.input"), task.c_str());
    start_endurance_run();
}

void App::load_endurance() {
    endurance_.refresh_preferences({profile_store_.data.kimi_model, profile_store_.data.kimi_effort,
                                    profile_store_.data.claude_model, profile_store_.data.claude_effort, vector_recall_});
    endurance_.set_callbacks(
        [this] {
            refresh_endurance_runs();
            refresh_endurance_node_states();
        },
        [this](const std::string& title, const std::string& body, bool error, const std::string& target) {
            notify_event(title, body, error, "mission", "endurance", target);
        },
        [this](const EnduranceRun& run) {
            if (proposal_) return;
            try {
                const auto staged = workspace_.knowledge().staged();
                if (staged.empty()) return;
                proposal_ = workspace_.validate_proposal("Review Endurance memory · " + run.workflow_name, {}, staged);
                Rlabel_set_text(id("proposal.summary"), proposal_->summary.c_str());
                Rwidget_set_visibility(id("proposal.bar"), R_VISIBLE);
                notify_event("Stellar · Endurance memory ready", proposal_->summary, false, "knowledge", "proposal");
            } catch (const std::exception& error) {
                set_status(std::string("Endurance completed · memory proposal unavailable: ") + error.what());
            }
        });
    endurance_.load();
    endurance_workflows_ = endurance_store_.load_workflows();
    if (endurance_workflows_.empty()) endurance_workflows_.push_back(endurance_store_.create_workflow(harness_ == Harness::KimiCode ? "kimi" : "claude"));
    refresh_endurance_workflows(0);
    refresh_endurance_runs();

    const auto graph = id("endurance.graph");
    Rnode_graph_set_type_color(graph, "context", 0.56f, 0.84f, 0.91f);
    Rnode_graph_set_selection_callback(graph, [](Rid, int node, void* user) {
        auto& app = *static_cast<App*>(user);
        for (const auto& [step, candidate] : app.endurance_nodes_) if (candidate == node) app.select_endurance_step(step);
        app.show_context(4);
    }, this);
    Rnode_graph_set_activated_callback(graph, [](Rid, int node, void* user) {
        auto& app = *static_cast<App*>(user);
        for (const auto& [step, candidate] : app.endurance_nodes_) if (candidate == node) app.select_endurance_step(step);
        app.show_context(4);
    }, this);
    Rnode_graph_set_moved_callback(graph, [](Rid, int node, void* user) {
        auto& app = *static_cast<App*>(user);
        auto* workflow = app.current_endurance_workflow();
        if (!workflow || app.endurance_loading_) return;
        for (auto& step : workflow->steps) {
            if (app.endurance_nodes_[step.id] != node) continue;
            float x = 0.0f, y = 0.0f;
            Rnode_graph_get_node_position(app.id("endurance.graph"), node, &x, &y);
            step.x = x;
            step.y = y;
        }
        app.save_endurance_workflow();
    }, this);
    Rnode_graph_set_connect_callback(graph, [](Rid, int output, int input, void* user) -> int {
        auto& app = *static_cast<App*>(user);
        const auto* workflow = app.current_endurance_workflow();
        if (!workflow || app.endurance_.busy()) return R_FALSE;
        const auto source = pin_step(app.endurance_outputs_, output);
        const auto target = pin_step(app.endurance_inputs_, input);
        if (source.empty() || target.empty() || source == target) return R_FALSE;
        auto candidate = *workflow;
        for (auto& step : candidate.steps) if (step.id == target) step.needs.push_back(source);
        const auto errors = endurance_validate(candidate);
        const bool invalid = std::any_of(errors.begin(), errors.end(), [](const auto& error) {
            return error.find("cycle") != std::string::npos || error.find("itself") != std::string::npos ||
                error.find("repeats dependency") != std::string::npos || error.find("Parallel Build") != std::string::npos;
        });
        return invalid ? R_FALSE : R_TRUE;
    }, this);
    Rnode_graph_set_linked_callback(graph, [](Rid graph, int link, void* user) {
        auto& app = *static_cast<App*>(user);
        auto* workflow = app.current_endurance_workflow();
        if (!workflow) return;
        const auto source = pin_step(app.endurance_outputs_, Rnode_graph_get_link_output(graph, link));
        const auto target = pin_step(app.endurance_inputs_, Rnode_graph_get_link_input(graph, link));
        for (auto& step : workflow->steps) {
            if (step.id == target && std::find(step.needs.begin(), step.needs.end(), source) == step.needs.end()) step.needs.push_back(source);
        }
        app.save_endurance_workflow();
        app.endurance_graph_sync_pending_ = true;
    }, this);
    Rnode_graph_set_unlinked_callback(graph, [](Rid graph, int link, void* user) {
        auto& app = *static_cast<App*>(user);
        auto* workflow = app.current_endurance_workflow();
        if (!workflow) return;
        const auto source = pin_step(app.endurance_outputs_, Rnode_graph_get_link_output(graph, link));
        const auto target = pin_step(app.endurance_inputs_, Rnode_graph_get_link_input(graph, link));
        for (auto& step : workflow->steps) {
            if (step.id != target) continue;
            step.needs.erase(std::remove(step.needs.begin(), step.needs.end(), source), step.needs.end());
        }
        app.save_endurance_workflow();
        app.endurance_graph_sync_pending_ = true;
    }, this);
    Rnode_graph_set_removed_callback(graph, [](Rid, int node, void* user) {
        auto& app = *static_cast<App*>(user);
        auto* workflow = app.current_endurance_workflow();
        if (!workflow || app.endurance_.busy()) return;
        std::string removed;
        for (const auto& [step, candidate] : app.endurance_nodes_) if (candidate == node) removed = step;
        workflow->steps.erase(std::remove_if(workflow->steps.begin(), workflow->steps.end(), [&](const auto& step) { return step.id == removed; }), workflow->steps.end());
        for (auto& step : workflow->steps) step.needs.erase(std::remove(step.needs.begin(), step.needs.end(), removed), step.needs.end());
        app.endurance_step_index_ = -1;
        app.save_endurance_workflow();
        app.endurance_graph_sync_pending_ = true;
    }, this);
    Rtext_editor_set_changed_callback(id("endurance.yaml"), [](Rid, void* user) {
        static_cast<App*>(user)->edit_endurance_yaml();
    }, this);
    Rtext_editor_set_changed_callback(id("endurance.step.prompt"), [](Rid, void* user) {
        static_cast<App*>(user)->update_endurance_step();
    }, this);
    Rlist_view_set_selection_changed_callback(id("endurance.run.list"), [](Rid, int index, void* user) {
        static_cast<App*>(user)->open_endurance_run(index);
    }, this);
}

void App::refresh_endurance_workflows(int selected) {
    endurance_workflows_ = endurance_store_.load_workflows();
    Rcombo_box_clear_items(id("endurance.workflow"));
    for (const auto& workflow : endurance_workflows_) Rcombo_box_add_item(id("endurance.workflow"), workflow.name.c_str());
    if (selected < 0) selected = std::clamp(endurance_workflow_index_, 0, static_cast<int>(endurance_workflows_.size()) - 1);
    select_endurance_workflow(selected);
}

void App::select_endurance_workflow(int index) {
    if (index < 0 || index >= static_cast<int>(endurance_workflows_.size())) return;
    if (endurance_yaml_pending_ && !validate_endurance_yaml(true)) return;
    endurance_workflow_index_ = index;
    auto* workflow = current_endurance_workflow();
    endurance_loading_ = true;
    Rcombo_box_set_selected(id("endurance.workflow"), index);
    Rlabel_set_text(id("endurance.title"), workflow->name.c_str());
    Rlabel_set_text(id("endurance.save"), ("Saved to endurance/" + workflow->path.generic_string()).c_str());
    Rlabel_set_text(id("endurance.yaml.path"), ("endurance/" + workflow->path.generic_string()).c_str());
    Rtext_editor_set_text(id("endurance.yaml"), endurance_workflow_yaml(*workflow).c_str());
    Rtext_input_set_text(id("endurance.input"), workflow->input_default.c_str());
    Rlabel_set_text(id("endurance.yaml.status"), "Valid workflow · no cycles");
    endurance_yaml_pending_ = false;
    update_endurance_schedule_controls();
    rebuild_endurance_graph();
    if (!workflow->steps.empty()) select_endurance_step(workflow->steps.front().id);
    set_active_view(id("right.tabs"), 0);
    endurance_loading_ = false;
}

void App::save_endurance_workflow() {
    auto* workflow = current_endurance_workflow();
    if (!workflow || endurance_loading_) return;
    if (const auto errors = endurance_validate(*workflow); !errors.empty()) {
        Rlabel_set_text(id("endurance.yaml.status"), errors.front().c_str());
        set_status(errors.front());
        return;
    }
    try {
        endurance_store_.save_workflow(*workflow);
        endurance_loading_ = true;
        Rtext_editor_set_text(id("endurance.yaml"), endurance_workflow_yaml(*workflow).c_str());
        endurance_loading_ = false;
        endurance_yaml_pending_ = false;
        Rlabel_set_text(id("endurance.save"), ("Saved to endurance/" + workflow->path.generic_string()).c_str());
        Rlabel_set_text(id("endurance.yaml.status"), "Valid workflow · no cycles");
    } catch (const std::exception& error) {
        endurance_loading_ = false;
        set_status(std::string("Workflow could not be saved · ") + error.what());
    }
}

bool App::validate_endurance_yaml(bool apply) {
    try {
        auto candidate = endurance_parse_workflow(Rtext_editor_get_text(id("endurance.yaml")), current_endurance_workflow()->path);
        for (const auto& step : candidate.steps) {
            if (step.type == "agent" && std::none_of(agents_.begin(), agents_.end(), [&](const auto& agent) { return agent.name == step.agent; })) {
                throw std::runtime_error("Agent not found for step " + step.id + ": " + step.agent);
            }
        }
        Rlabel_set_text(id("endurance.yaml.status"), ("Valid workflow · " + std::to_string(candidate.steps.size()) + " steps · no cycles").c_str());
        if (apply) {
            const auto selected = endurance_step_index_ >= 0 && endurance_step_index_ < static_cast<int>(candidate.steps.size())
                ? candidate.steps[endurance_step_index_].id : std::string();
            *current_endurance_workflow() = std::move(candidate);
            save_endurance_workflow();
            endurance_.update_schedule(*current_endurance_workflow());
            rebuild_endurance_graph();
            if (!selected.empty()) select_endurance_step(selected);
            update_endurance_schedule_controls();
        }
        return true;
    } catch (const std::exception& error) {
        Rlabel_set_text(id("endurance.yaml.status"), error.what());
        set_status(std::string("Endurance YAML is invalid · ") + error.what());
        return false;
    }
}

void App::edit_endurance_yaml() {
    if (endurance_loading_) return;
    endurance_yaml_pending_ = true;
    endurance_yaml_due_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(900);
    Rlabel_set_text(id("endurance.yaml.status"), "Editing YAML · validation pending");
}

void App::rebuild_endurance_graph() {
    auto* workflow = current_endurance_workflow();
    if (!workflow) return;
    endurance_loading_ = true;
    const auto graph = id("endurance.graph");
    Rnode_graph_clear(graph);
    endurance_nodes_.clear();
    endurance_inputs_.clear();
    endurance_outputs_.clear();
    Rnode_graph_set_read_only(graph, endurance_.busy());
    Rnode_graph_set_flow_visible(graph, endurance_.busy());
    float center_x = 0.0f, center_y = 0.0f, center_width = 0.0f, center_height = 0.0f;
    Rwidget_get_rect(id("center.workspace"), &center_x, &center_y, &center_width, &center_height);
    const bool vertical = center_width < 1100.0f;
    Rlabel_set_text(id("endurance.canvas.status"), vertical
        ? "Compact vertical canvas · select a step or drag to pan."
        : "Select a step to inspect it, or connect outputs to downstream inputs.");
    int step_index = 0;
    for (const auto& step : workflow->steps) {
        const float display_x = vertical ? 60.0f : step.x;
        const float display_y = vertical ? 40.0f + step_index++ * 125.0f : step.y;
        const int node = Rnode_graph_add_node(graph, step.id.c_str(), display_x, display_y);
        endurance_nodes_[step.id] = node;
        Rnode_graph_set_node_name(graph, node, step.id.c_str());
        auto content = Rwidget_create(R_WIDGET_VBOX);
        Rpanel_set_spacing(content, 4);
        auto context = Rwidget_add_child(content, Rlabel_create((step.type == "approval" ? "Human gate" : step.agent + " · " + step.mode).c_str()));
        Rlabel_set_text_size(context, 10);
        auto status = Rwidget_add_child(content, Rlabel_create("Ready"));
        Rlabel_set_text_size(status, 10);
        Rnode_graph_set_node_content(graph, node, content);
        const int input = Rnode_graph_add_input(graph, node, "needs", "context");
        const int output = Rnode_graph_add_output(graph, node, "output", "context");
        Rnode_graph_set_pin_multiple(graph, input, R_TRUE);
        endurance_inputs_[step.id] = input;
        endurance_outputs_[step.id] = output;
    }
    for (const auto& step : workflow->steps) {
        for (const auto& need : step.needs) Rnode_graph_connect(graph, endurance_outputs_[need], endurance_inputs_[step.id]);
    }
    const int builds = static_cast<int>(std::count_if(workflow->steps.begin(), workflow->steps.end(), [](const auto& step) { return step.mode == "build"; }));
    const int agents = static_cast<int>(std::count_if(workflow->steps.begin(), workflow->steps.end(), [](const auto& step) { return step.type == "agent"; }));
    Rlabel_set_text(id("endurance.summary"), (std::to_string(workflow->steps.size()) + " steps · " + std::to_string(agents) +
        " agent steps · " + std::to_string(builds) + " write steps").c_str());
    fit_endurance_graph();
    refresh_endurance_node_states();
    endurance_loading_ = false;
}

void App::fit_endurance_graph() {
    const auto graph = id("endurance.graph");
    if (endurance_nodes_.empty()) {
        Rnode_graph_set_view(graph, 0.0f, 0.0f, 1.0f);
        return;
    }
    float min_x = 100000.0f, min_y = 100000.0f, max_x = -100000.0f, max_y = -100000.0f;
    for (const auto& [id, node] : endurance_nodes_) {
        float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
        Rnode_graph_get_node_position(graph, node, &x, &y);
        Rnode_graph_get_node_size(graph, node, &width, &height);
        min_x = std::min(min_x, x);
        min_y = std::min(min_y, y);
        max_x = std::max(max_x, x + width);
        max_y = std::max(max_y, y + height);
    }
    float view_x = 0.0f, view_y = 0.0f, view_width = 900.0f, view_height = 520.0f;
    Rwidget_get_rect(graph, &view_x, &view_y, &view_width, &view_height);
    if (view_width < 100.0f) view_width = 900.0f;
    if (view_height < 100.0f) view_height = 900.0f;
    const float zoom = std::clamp(std::min((view_width - 70.0f) / std::max(max_x - min_x + 50.0f, 1.0f),
                                          (view_height - 70.0f) / std::max(max_y - min_y + 50.0f, 1.0f)), 0.65f, 1.0f);
    Rnode_graph_set_view(graph, (min_x + max_x) * 0.5f, (min_y + max_y) * 0.5f, zoom);
}

void App::refresh_endurance_node_states() {
    const auto graph = id("endurance.graph");
    if (!Rwidget_is_valid(graph)) return;
    Rnode_graph_set_read_only(graph, endurance_.busy());
    Rnode_graph_set_flow_visible(graph, endurance_.busy());
    const auto* run = endurance_.active_run();
    for (const auto& [step_id, node] : endurance_nodes_) {
        std::string status = "Ready";
        bool active = false;
        if (run) {
            const auto found = run->steps.find(step_id);
            if (found != run->steps.end()) {
                status = found->second.status;
                active = status == "running";
            }
        }
        Rnode_graph_set_node_active(graph, node, active);
        Rnode_graph_set_node_badge(graph, node, active ? "run" : status == "failed" ? "!" : status == "succeeded" ? "done" : status == "waiting" ? "gate" : "");
        const auto content = Rnode_graph_get_node_content(graph, node);
        if (Rwidget_is_valid(content) && Rwidget_get_child_count(content) > 1) {
            Rlabel_set_text(Rwidget_get_child(content, 1), status.c_str());
        }
    }
}

void App::select_endurance_step(const std::string& id) {
    auto* workflow = current_endurance_workflow();
    if (!workflow) return;
    const auto found = std::find_if(workflow->steps.begin(), workflow->steps.end(), [&](const auto& step) { return step.id == id; });
    if (found != workflow->steps.end()) select_endurance_step(static_cast<int>(found - workflow->steps.begin()));
}

void App::select_endurance_step(int index) {
    auto* workflow = current_endurance_workflow();
    if (!workflow || index < 0 || index >= static_cast<int>(workflow->steps.size())) return;
    endurance_step_index_ = index;
    auto& step = workflow->steps[index];
    endurance_loading_ = true;
    Rlabel_set_text(id("endurance.step.title"), step.id.c_str());
    Rlabel_set_text(id("endurance.step.description"), (step.type == "approval" ? "Human approval before downstream execution" : step.agent + " · " + step.mode).c_str());
    Rtext_input_set_text(id("endurance.step.name"), step.id.c_str());
    Rcombo_box_clear_items(id("endurance.step.agent"));
    int agent_index = 0;
    for (int i = 0; i < static_cast<int>(agents_.size()); ++i) {
        Rcombo_box_add_item(id("endurance.step.agent"), agents_[i].name.c_str());
        if (agents_[i].name == step.agent) agent_index = i;
    }
    Rcombo_box_set_selected(id("endurance.step.agent"), agent_index);
    Rcombo_box_set_selected(id("endurance.step.harness"), step.harness == "claude" ? 1 : step.harness == "kimi" ? 2 : 0);
    Rcombo_box_set_selected(id("endurance.step.mode"), step.mode == "build" ? 0 : step.mode == "review" ? 2 : 1);
    Rcombo_box_set_selected(id("endurance.step.isolation"), step.isolation == "worktree" ? 1 : 0);
    Rtext_editor_set_text(id("endurance.step.prompt"), step.prompt.c_str());
    Rlabel_set_text(id("endurance.step.needs"), step.needs.empty() ? "None · entry step" : join_labels(step.needs).c_str());
    const int timeouts[] = {300, 900, 1800, 3600};
    int timeout_index = 1;
    for (int i = 0; i < 4; ++i) if (step.timeout_seconds == timeouts[i]) timeout_index = i;
    Rcombo_box_set_selected(id("endurance.step.timeout"), timeout_index);
    Rcombo_box_set_selected(id("endurance.step.retries"), std::clamp(step.retries, 0, 2));
    const bool agent = step.type == "agent";
    for (const auto* control : {"endurance.step.agent", "endurance.step.harness", "endurance.step.mode", "endurance.step.isolation"}) {
        Rwidget_set_enabled(id(control), agent && !endurance_.busy());
    }
    for (const auto* control : {"endurance.step.name", "endurance.step.prompt", "endurance.step.timeout", "endurance.step.retries"}) {
        Rwidget_set_enabled(id(control), !endurance_.busy());
    }
    endurance_loading_ = false;
}

void App::update_endurance_step() {
    auto* step = current_endurance_step();
    if (!step || endurance_loading_) return;
    const auto previous_id = step->id;
    step->id = Rtext_input_get_text(id("endurance.step.name"));
    const int agent = Rcombo_box_get_selected(id("endurance.step.agent"));
    if (agent >= 0 && agent < static_cast<int>(agents_.size())) step->agent = agents_[agent].name;
    const int harness = Rcombo_box_get_selected(id("endurance.step.harness"));
    step->harness = harness == 1 ? "claude" : harness == 2 ? "kimi" : "";
    const int mode = Rcombo_box_get_selected(id("endurance.step.mode"));
    step->mode = mode == 0 ? "build" : mode == 2 ? "review" : "ask";
    step->isolation = Rcombo_box_get_selected(id("endurance.step.isolation")) == 1 ? "worktree" : "workspace";
    step->prompt = Rtext_editor_get_text(id("endurance.step.prompt"));
    const int timeouts[] = {300, 900, 1800, 3600};
    step->timeout_seconds = timeouts[std::clamp(Rcombo_box_get_selected(id("endurance.step.timeout")), 0, 3)];
    step->retries = std::clamp(Rcombo_box_get_selected(id("endurance.step.retries")), 0, 2);
    if (previous_id != step->id) {
        for (auto& other : current_endurance_workflow()->steps) {
            for (auto& need : other.needs) if (need == previous_id) need = step->id;
        }
        endurance_step_index_ = -1;
    }
    save_endurance_workflow();
}

void App::add_endurance_step() {
    auto* workflow = current_endurance_workflow();
    if (!workflow || endurance_.busy()) return;
    EnduranceStep step;
    step.id = unique_step_id(*workflow, "step");
    step.prompt = "Describe this step's objective.";
    step.x = 120.0f;
    step.y = 120.0f + static_cast<float>(workflow->steps.size() % 4) * 140.0f;
    workflow->steps.push_back(step);
    save_endurance_workflow();
    rebuild_endurance_graph();
    select_endurance_step(step.id);
    show_context(4);
}

void App::duplicate_endurance_step() {
    auto* workflow = current_endurance_workflow();
    auto* step = current_endurance_step();
    if (!workflow || !step || endurance_.busy()) return;
    auto copy = *step;
    copy.id = unique_step_id(*workflow, step->id + "-copy");
    copy.x += 36.0f;
    copy.y += 36.0f;
    workflow->steps.push_back(copy);
    save_endurance_workflow();
    rebuild_endurance_graph();
    select_endurance_step(copy.id);
}

void App::delete_endurance_step() {
    auto* workflow = current_endurance_workflow();
    auto* step = current_endurance_step();
    if (!workflow || !step || endurance_.busy()) return;
    const auto removed = step->id;
    workflow->steps.erase(std::remove_if(workflow->steps.begin(), workflow->steps.end(), [&](const auto& item) { return item.id == removed; }), workflow->steps.end());
    for (auto& other : workflow->steps) other.needs.erase(std::remove(other.needs.begin(), other.needs.end(), removed), other.needs.end());
    endurance_step_index_ = -1;
    save_endurance_workflow();
    rebuild_endurance_graph();
    if (!workflow->steps.empty()) select_endurance_step(workflow->steps.front().id);
}

void App::layout_endurance_graph() {
    auto* workflow = current_endurance_workflow();
    if (!workflow || endurance_.busy()) return;
    std::map<std::string, int> levels;
    std::function<int(const EnduranceStep&)> level = [&](const EnduranceStep& step) {
        if (levels.count(step.id)) return levels[step.id];
        int result = 0;
        for (const auto& need : step.needs) {
            const auto found = std::find_if(workflow->steps.begin(), workflow->steps.end(), [&](const auto& other) { return other.id == need; });
            if (found != workflow->steps.end()) result = std::max(result, level(*found) + 1);
        }
        levels[step.id] = result;
        return result;
    };
    std::map<int, int> rows;
    for (auto& step : workflow->steps) {
        const int depth = level(step);
        step.x = 60.0f + depth * 250.0f;
        step.y = 70.0f + rows[depth]++ * 160.0f;
    }
    save_endurance_workflow();
    rebuild_endurance_graph();
}

void App::toggle_endurance_recurrence() {
    const auto panel = id("endurance.schedule");
    const bool open = Rwidget_get_visibility(panel) != R_VISIBLE;
    Rwidget_set_visibility(panel, open ? R_VISIBLE : R_COLLAPSED);
    if (open) update_endurance_schedule_controls();
}

void App::update_endurance_schedule_controls() {
    auto* workflow = current_endurance_workflow();
    if (!workflow) return;
    const auto& recurrence = workflow->recurrence;
    int frequency = 4;
    if (recurrence.expression == "0 * * * *") frequency = 0;
    else {
        const std::regex daily("([0-9]+) ([0-9]+) \\* \\* \\*");
        const std::regex weekdays("([0-9]+) ([0-9]+) \\* \\* 1-5");
        const std::regex weekly("([0-9]+) ([0-9]+) \\* \\* 1");
        if (std::regex_match(recurrence.expression, daily)) frequency = 1;
        else if (std::regex_match(recurrence.expression, weekdays)) frequency = 2;
        else if (std::regex_match(recurrence.expression, weekly)) frequency = 3;
    }
    Rcombo_box_set_selected(id("endurance.schedule.frequency"), frequency);
    if (frequency > 0 && frequency < 4) {
        int minute = 0, hour = 0;
        std::stringstream(recurrence.expression) >> minute >> hour;
        std::ostringstream time;
        time << (hour < 10 ? "0" : "") << hour << ':' << (minute < 10 ? "0" : "") << minute;
        Rtext_input_set_text(id("endurance.schedule.time"), time.str().c_str());
    }
    Rcheckbox_set_checked(id("endurance.schedule.enabled"), recurrence.enabled);
    Rcombo_box_set_selected(id("endurance.schedule.timezone"), recurrence.timezone == "UTC" ? 1 : 0);
    Rcombo_box_set_selected(id("endurance.schedule.overlap"), recurrence.overlap == "queue" ? 1 : 0);
    Rcombo_box_set_selected(id("endurance.schedule.missed"), recurrence.missed_runs == "run_once" ? 1 : 0);
    Rtext_input_set_text(id("endurance.schedule.cron"), recurrence.expression.c_str());
    Rwidget_set_visibility(id("endurance.schedule.time"), frequency == 0 || frequency == 4 ? R_COLLAPSED : R_VISIBLE);
    Rwidget_set_visibility(id("endurance.schedule.cron"), frequency == 4 ? R_VISIBLE : R_COLLAPSED);
    Rbutton_set_text(id("endurance.recurrence.toggle"), recurrence.enabled ? "Scheduled" : "Recurrence");
    const auto* schedule = endurance_.schedule(workflow->path);
    Rlabel_set_text(id("endurance.schedule.next"), schedule ? ("Next run · " + time_label(schedule->next_at, recurrence.timezone)).c_str() : "Manual trigger only");
    Rlabel_set_text(id("endurance.schedule.summary"), recurrence.enabled
        ? (recurrence.expression + " · " + recurrence.timezone + " · overlap " + recurrence.overlap + " · missed " + recurrence.missed_runs).c_str()
        : "Recurrence is disabled. Manual runs remain available.");
}

void App::save_endurance_recurrence() {
    auto* workflow = current_endurance_workflow();
    if (!workflow) return;
    auto& recurrence = workflow->recurrence;
    recurrence.enabled = Rcheckbox_is_checked(id("endurance.schedule.enabled"));
    recurrence.timezone = Rcombo_box_get_selected(id("endurance.schedule.timezone")) == 1 ? "UTC" : "local";
    recurrence.overlap = Rcombo_box_get_selected(id("endurance.schedule.overlap")) == 1 ? "queue" : "skip";
    recurrence.missed_runs = Rcombo_box_get_selected(id("endurance.schedule.missed")) == 1 ? "run_once" : "skip";
    const int frequency = Rcombo_box_get_selected(id("endurance.schedule.frequency"));
    const auto time = std::string(Rtext_input_get_text(id("endurance.schedule.time")));
    const std::regex time_pattern("([01][0-9]|2[0-3]):([0-5][0-9])");
    if (frequency > 0 && frequency < 4 && !std::regex_match(time, time_pattern)) {
        set_status("Recurrence time must use 24-hour HH:MM format");
        return;
    }
    int hour = 9, minute = 0;
    if (frequency > 0 && frequency < 4) std::sscanf(time.c_str(), "%d:%d", &hour, &minute);
    if (frequency == 0) recurrence.expression = "0 * * * *";
    else if (frequency == 1) recurrence.expression = std::to_string(minute) + " " + std::to_string(hour) + " * * *";
    else if (frequency == 2) recurrence.expression = std::to_string(minute) + " " + std::to_string(hour) + " * * 1-5";
    else if (frequency == 3) recurrence.expression = std::to_string(minute) + " " + std::to_string(hour) + " * * 1";
    else recurrence.expression = Rtext_input_get_text(id("endurance.schedule.cron"));
    if (const auto errors = endurance_validate(*workflow); !errors.empty()) {
        set_status(errors.front());
        return;
    }
    try {
        save_endurance_workflow();
        endurance_.update_schedule(*workflow);
        Rwidget_set_visibility(id("endurance.schedule"), R_COLLAPSED);
        update_endurance_schedule_controls();
        set_status(recurrence.enabled ? "Recurrence saved · " + time_label(endurance_.schedule(workflow->path)->next_at, recurrence.timezone) : "Recurrence disabled · manual trigger retained");
    } catch (const std::exception& error) {
        set_status(std::string("Recurrence could not be saved · ") + error.what());
    }
}

void App::start_endurance_run() {
    if (endurance_yaml_pending_ && !validate_endurance_yaml(true)) return;
    save_endurance_workflow();
    auto* workflow = current_endurance_workflow();
    if (!workflow) return;
    if (endurance_.busy()) {
        set_status("Finish or stop the active Endurance run before starting another");
        set_active_view(id("endurance.views"), 2);
        return;
    }
    std::string error;
    if (!endurance_.start(*workflow, Rtext_input_get_text(id("endurance.input")), false, error)) {
        set_status(error);
        return;
    }
    endurance_run_index_ = 0;
    set_active_view(id("endurance.views"), 2);
    refresh_endurance_runs();
    set_status("Endurance started · " + workflow->name);
}

void App::stop_endurance_run() {
    endurance_.stop();
    refresh_endurance_runs();
    set_status("Endurance stopped · resume is available");
}

void App::resume_endurance_run() {
    const auto* run = selected_endurance_run();
    if (!run) return;
    std::string error;
    if (!endurance_.resume(run->id, error)) {
        set_status(error);
        return;
    }
    set_active_view(id("endurance.views"), 2);
    refresh_endurance_runs();
    set_status("Endurance resumed");
}

void App::approve_endurance_run(bool approved) {
    std::string error;
    if (!endurance_.approve(approved, error)) set_status(error);
    refresh_endurance_runs();
}

void App::open_endurance_run(int index) {
    if (index < 0 || index >= static_cast<int>(endurance_run_rows_.size())) return;
    endurance_run_index_ = index;
    refresh_endurance_run_details();
}

void App::refresh_endurance_runs() {
    const auto previous = selected_endurance_run() ? selected_endurance_run()->id : std::string();
    endurance_run_rows_.clear();
    Rlist_view_clear_items(id("endurance.run.list"));
    Rcombo_box_clear_items(id("endurance.run.select"));
    int selected = -1;
    for (const auto& run : endurance_.runs()) {
        endurance_run_rows_.push_back(run.id);
        if (run.id == previous) selected = static_cast<int>(endurance_run_rows_.size()) - 1;
        Rlist_view_add_item(id("endurance.run.list"), ("#" + run.id.substr(run.id.size() - 6) + " · " + run.status + " · " + short_text(run.workflow_name, 22)).c_str());
        Rcombo_box_add_item(id("endurance.run.select"), (run.id + " · " + run.status + " · " + short_text(run.workflow_name, 36)).c_str());
    }
    if (selected < 0 && endurance_.active_run()) {
        const auto found = std::find(endurance_run_rows_.begin(), endurance_run_rows_.end(), endurance_.active_run()->id);
        if (found != endurance_run_rows_.end()) selected = static_cast<int>(found - endurance_run_rows_.begin());
    }
    endurance_run_index_ = selected;
    Rlist_view_set_selected(id("endurance.run.list"), selected);
    Rcombo_box_set_selected(id("endurance.run.select"), selected);
    refresh_endurance_run_details();
}

void App::refresh_endurance_run_details() {
    if (!Rwidget_is_valid(id("endurance.run.steps"))) return;
    float center_x = 0.0f, center_y = 0.0f, center_width = 0.0f, center_height = 0.0f;
    Rwidget_get_rect(id("center.workspace"), &center_x, &center_y, &center_width, &center_height);
    const bool compact = center_width < 650.0f;
    // With no runs there is nothing to pick: the empty-state message takes the full width instead of
    // sitting beside an empty history column.
    const bool has_runs = !endurance_run_rows_.empty();
    Rwidget_set_visibility(id("endurance.run.history"), compact || !has_runs ? R_COLLAPSED : R_VISIBLE);
    Rwidget_set_visibility(id("endurance.run.select"), compact && has_runs ? R_VISIBLE : R_COLLAPSED);
    const auto* run = selected_endurance_run();
    Rwidget_clear_children(id("endurance.run.steps"));
    if (!run) {
        Rlabel_set_text(id("endurance.run.title"), "No runs yet");
        Rlabel_set_text(id("endurance.run.subtitle"), "Run this workflow to create an inspectable execution.");
        Rwidget_set_visibility(id("endurance.approval"), R_COLLAPSED);
        Rwidget_set_visibility(id("endurance.run.stop"), R_COLLAPSED);
        Rwidget_set_visibility(id("endurance.run.resume"), R_COLLAPSED);
        return;
    }
    Rlabel_set_text(id("endurance.run.title"), (run->id + " · " + run->workflow_name).c_str());
    Rlabel_set_text(id("endurance.run.subtitle"), (run->status + " · " + (run->scheduled ? "scheduled" : "manual") + " · " + run->isolation +
        (run->error.empty() ? "" : " · " + run->error)).c_str());
    const bool active = endurance_.active_run() && endurance_.active_run()->id == run->id;
    Rwidget_set_visibility(id("endurance.approval"), active && run->status == "waiting" ? R_VISIBLE : R_COLLAPSED);
    Rwidget_set_visibility(id("endurance.run.stop"), active && (run->status == "running" || run->status == "waiting") ? R_VISIBLE : R_COLLAPSED);
    Rwidget_set_visibility(id("endurance.run.resume"), run->status == "failed" || run->status == "stopped" ? R_VISIBLE : R_COLLAPSED);
    for (const auto& step : run->snapshot.steps) {
        const auto& result = endurance_result(*run, step.id);
        auto box = Rwidget_add_child(id("endurance.run.steps"), Rwidget_create(R_WIDGET_VBOX));
        Rpanel_set_spacing(box, 5);
        auto heading = Rwidget_add_child(box, Rwidget_create(R_WIDGET_HBOX));
        Rpanel_set_spacing(heading, 9);
        auto title = Rwidget_add_child(heading, Rlabel_create(step.id.c_str()));
        Rlabel_set_text_size(title, 13);
        Rslot_set_fill(title, 1);
        auto status = Rwidget_add_child(heading, Rlabel_create(result.status.c_str()));
        Rlabel_set_text_size(status, 10);
        auto meta = Rwidget_add_child(box, Rlabel_create((step.type == "approval" ? "Human gate" : step.agent + " · " + step.harness + " · " + step.mode +
            " · attempt " + std::to_string(result.attempt)).c_str()));
        Rlabel_set_text_size(meta, 10);
        if (!result.output.empty()) {
            auto output = Rwidget_add_child(box, Rlabel_create(short_text(result.output, 5000).c_str()));
            Rlabel_set_text_size(output, 11);
            Rlabel_set_wrap(output, R_TRUE);
        }
        if (!result.error.empty()) {
            auto error = Rwidget_add_child(box, Rlabel_create(result.error.c_str()));
            Rlabel_set_text_size(error, 11);
            Rlabel_set_wrap(error, R_TRUE);
        }
        if (!result.checkpoint_id.empty()) {
            auto checkpoint = Rwidget_add_child(box, Rlabel_create(("Checkpoint · " + result.checkpoint_id).c_str()));
            Rlabel_set_text_size(checkpoint, 10);
        }
        Rwidget_add_child(box, Rwidget_create(R_WIDGET_SEPARATOR));
    }
}

void App::pump_endurance() {
    if (endurance_graph_sync_pending_) {
        endurance_graph_sync_pending_ = false;
        rebuild_endurance_graph();
    }
    if (endurance_yaml_pending_ && std::chrono::steady_clock::now() >= endurance_yaml_due_) {
        endurance_yaml_pending_ = false;
        validate_endurance_yaml(true);
    }
    endurance_.tick(endurance_workflows_);
}

} // namespace raven
