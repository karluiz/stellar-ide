#include "endurance_controller.hpp"

#include <algorithm>
#include <sstream>

namespace raven {
namespace {

std::string quoted(const std::filesystem::path& path) {
    std::string result = "'";
    for (const char c : path.string()) {
        if (c == '\'') result += "'\\''";
        else result += c;
    }
    return result + "'";
}

std::string bounded_output(std::string value) {
    if (value.size() > 64 * 1024) value.resize(64 * 1024);
    return value;
}

} // namespace

EnduranceController::EnduranceController(Workspace& workspace, CheckpointStore& checkpoints, GitMissionStore& missions,
                                         std::filesystem::path knowledge_binary, EndurancePreferences preferences)
    : workspace_(workspace), checkpoints_(checkpoints), missions_(missions), knowledge_binary_(std::move(knowledge_binary)),
      preferences_(std::move(preferences)), store_(workspace.root()) {}

void EnduranceController::set_callbacks(ChangeCallback changed, NotifyCallback notify, SuccessCallback succeeded) {
    changed_ = std::move(changed);
    notify_ = std::move(notify);
    succeeded_ = std::move(succeeded);
}

void EnduranceController::load() {
    runs_ = store_.load_runs();
    for (auto& run : runs_) {
        if (run.status != "running") continue;
        bool interrupted = false;
        for (auto& [id, result] : run.steps) {
            if (result.status == "running") {
                result.status = "stopped";
                result.error = "Stellar closed before this step completed";
                result.finished_at = endurance_now();
                interrupted = true;
            }
        }
        if (interrupted) {
            run.status = "stopped";
            run.error = "Interrupted by application shutdown; resume to continue";
            store_.save_run(run, "interrupted");
        }
    }
    schedule_ = store_.load_schedule();
}

const EnduranceRun* EnduranceController::find_run(const std::string& id) const {
    if (active_run_ && active_run_->id == id) return &*active_run_;
    const auto found = std::find_if(runs_.begin(), runs_.end(), [&](const auto& run) { return run.id == id; });
    return found == runs_.end() ? nullptr : &*found;
}

const EnduranceStep* EnduranceController::step(const std::string& id) const {
    if (!active_run_) return nullptr;
    const auto found = std::find_if(active_run_->snapshot.steps.begin(), active_run_->snapshot.steps.end(), [&](const auto& item) { return item.id == id; });
    return found == active_run_->snapshot.steps.end() ? nullptr : &*found;
}

RunOptions EnduranceController::options(const EnduranceStep& step, const EnduranceStepResult& result, const std::string& prompt) const {
    RunOptions options;
    options.working_directory = workspace_.root();
    if (step.isolation == "worktree" && active_run_ && !active_run_->worktree_mission_id.empty()) {
        if (const auto* mission = missions_.find(active_run_->worktree_mission_id)) options.working_directory = mission->worktree;
    }
    const auto harness_name = step.harness.empty() ? active_run_->snapshot.default_harness : step.harness;
    options.harness = harness_name == "claude" ? Harness::ClaudeCode : Harness::KimiCode;
    options.agent = step.agent;
    options.agent_file = workspace_.agents_root() / (step.agent + ".md");
    options.knowledge_binary = knowledge_binary_;
    options.read_only = step.mode != "build";
    options.session_id = result.session_id;
    if (options.harness == Harness::ClaudeCode) {
        options.model = step.model.empty() ? preferences_.claude_model : step.model;
        options.effort = step.effort.empty() ? preferences_.claude_effort : step.effort;
    } else {
        options.model = step.model.empty() ? preferences_.kimi_model : step.model;
        options.effort = step.effort.empty() ? preferences_.kimi_effort : step.effort;
    }
    const auto tool = quoted(knowledge_binary_) + " --workspace " + quoted(workspace_.root());
    std::string instructions;
    if (step.mode == "build") {
        instructions = "Act as a coding agent within this Endurance workflow. Inspect the relevant code, implement the requested outcome, and run relevant build and tests before responding. Do not stop at a plan. Keep narration concise and finish with changes and checks actually run. Do not edit wiki/ directly; reviewed knowledge is handled once after the workflow.\n";
    } else if (step.mode == "review") {
        instructions = "Act as a read-only reviewer within this Endurance workflow. Inspect relevant implementation and report actionable findings ordered by severity with paths and line references. Do not edit, create, delete, or stage files. If there are no material findings, say so and mention residual test gaps.\n";
    } else {
        instructions = "Answer this Endurance step with read-only investigation. Inspect relevant code when needed and do not edit, create, delete, or stage files. Return a direct, useful result for downstream steps.\n";
    }
    instructions += "Accepted workspace knowledge is available with " + tool + " context \"your query\".\n\n";
    options.prompt = std::move(instructions);
    try {
        if (const auto knowledge = workspace_.knowledge_context(prompt, preferences_.vector_recall); !knowledge.empty()) {
            options.prompt += "RELEVANT GRAPH CONTEXT:\n" + knowledge + "\n";
        }
    } catch (...) {
    }
    options.prompt += "ENDURANCE WORKFLOW: " + active_run_->workflow_name + "\nSTEP: " + step.id + "\n\n" + prompt;
    return options;
}

bool EnduranceController::start(const EnduranceWorkflow& workflow, const std::string& input, bool scheduled, std::string& error) {
    if (busy()) {
        error = "Finish or stop the active Endurance run before starting another";
        return false;
    }
    if (workflow.input_required && input.empty()) {
        error = "A task input is required for this workflow";
        return false;
    }
    if (const auto errors = endurance_validate(workflow); !errors.empty()) {
        error = errors.front();
        return false;
    }
    const auto available_agents = workspace_.agents();
    for (const auto& step : workflow.steps) {
        if (step.type == "agent" && std::none_of(available_agents.begin(), available_agents.end(), [&](const auto& agent) { return agent.name == step.agent; })) {
            error = "Agent not found for step " + step.id + ": " + step.agent;
            return false;
        }
    }
    try {
        active_run_ = store_.create_run(workflow, input, scheduled);
        if (std::any_of(workflow.steps.begin(), workflow.steps.end(), [](const auto& step) { return step.isolation == "worktree"; })) {
            const auto repositories = workspace_.repositories();
            if (repositories.empty()) {
                error = "A Git repository is required for worktree isolation";
                active_run_.reset();
                return false;
            }
            const auto mission = missions_.create(repositories.front(), "Endurance · " + workflow.name);
            active_run_->worktree_mission_id = mission.id;
            active_run_->isolation = "worktree";
        }
        replace_history(*active_run_);
        record("started");
        schedule_ready();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        active_run_.reset();
        return false;
    }
}

bool EnduranceController::launch(const EnduranceStep& step, std::string& error) {
    auto& result = endurance_result(*active_run_, step.id);
    ++result.attempt;
    result.status = "running";
    result.started_at = endurance_now();
    result.finished_at = 0;
    result.output.clear();
    result.error.clear();
    result.session_id.clear();
    saw_delta_[step.id] = false;
    if (step.mode == "build" && step.isolation == "workspace") {
        try {
            result.checkpoint_id = checkpoints_.begin("Endurance · " + active_run_->workflow_name + " · " + step.id);
        } catch (const std::exception& checkpoint_error) {
            if (notify_) notify_("Checkpoint unavailable", checkpoint_error.what(), true, active_run_->id);
        }
    }
    try {
        const auto prompt = endurance_render_prompt(*active_run_, step);
        auto run_options = options(step, result, prompt);
        if (!processes_[step.id].start(run_options, error)) {
            finish_process(step.id, true, error);
            return false;
        }
        record("step-started:" + step.id);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        finish_process(step.id, true, error);
        return false;
    }
}

void EnduranceController::finish_process(const std::string& id, bool failed, const std::string& detail) {
    if (!active_run_) return;
    const auto* current = step(id);
    if (!current) return;
    auto& result = endurance_result(*active_run_, id);
    result.finished_at = endurance_now();
    result.error = failed ? detail : "";
    if (!result.checkpoint_id.empty()) {
        try { (void)checkpoints_.finish(result.checkpoint_id, failed ? "failed" : "completed"); } catch (...) {}
    }
    if (failed && result.attempt <= current->retries) {
        result.status = "ready";
        result.session_id.clear();
        record("step-retry:" + id);
        return;
    }
    result.status = failed ? "failed" : "succeeded";
    if (failed) {
        active_run_->error = id + ": " + detail;
        endurance_block_downstream(*active_run_, id);
    }
    record(failed ? "step-failed:" + id : "step-succeeded:" + id);
}

void EnduranceController::schedule_ready() {
    if (!active_run_ || active_run_->status == "stopped" || active_run_->status == "succeeded") return;
    for (const auto& current : active_run_->snapshot.steps) {
        auto& result = endurance_result(*active_run_, current.id);
        if (result.status != "ready") continue;
        const bool blocked = std::any_of(current.needs.begin(), current.needs.end(), [&](const auto& need) {
            const auto& dependency = endurance_result(*active_run_, need);
            return dependency.status == "failed" || dependency.status == "stopped" || dependency.status == "blocked" || dependency.status == "skipped";
        });
        if (blocked) {
            result.status = "blocked";
            record("step-blocked:" + current.id);
        }
    }
    for (const auto& current : active_run_->snapshot.steps) {
        auto& result = endurance_result(*active_run_, current.id);
        if (result.status != "ready" || !endurance_dependencies_succeeded(*active_run_, current)) continue;
        if (current.type == "approval") {
            result.status = "waiting";
            active_run_->status = "waiting";
            record("approval-requested:" + current.id);
            if (notify_) notify_("Endurance approval required", active_run_->workflow_name + "\n" + current.prompt, false, active_run_->id);
            continue;
        }
        if (processes_.size() >= static_cast<size_t>(active_run_->snapshot.concurrency)) continue;
        if (current.mode == "build") {
            const auto build_running = std::any_of(processes_.begin(), processes_.end(), [&](const auto& item) {
                const auto* running = step(item.first);
                return running && running->mode == "build";
            });
            if (build_running) continue;
        }
        active_run_->status = "running";
        std::string error;
        if (!launch(current, error)) break;
    }
    if (!processes_.empty()) return;
    const bool waiting = std::any_of(active_run_->steps.begin(), active_run_->steps.end(), [](const auto& item) { return item.second.status == "waiting"; });
    const bool running = std::any_of(active_run_->steps.begin(), active_run_->steps.end(), [](const auto& item) { return item.second.status == "running"; });
    const bool ready = std::any_of(active_run_->snapshot.steps.begin(), active_run_->snapshot.steps.end(), [&](const auto& current) {
        return endurance_result(*active_run_, current.id).status == "ready" && endurance_dependencies_succeeded(*active_run_, current);
    });
    if (waiting || running || ready) return;
    const bool failed = std::any_of(active_run_->steps.begin(), active_run_->steps.end(), [](const auto& item) { return item.second.status == "failed"; });
    const bool stopped = std::any_of(active_run_->steps.begin(), active_run_->steps.end(), [](const auto& item) { return item.second.status == "stopped"; });
    const bool complete = std::all_of(active_run_->steps.begin(), active_run_->steps.end(), [](const auto& item) { return item.second.status == "succeeded"; });
    if (failed) finish_run("failed");
    else if (stopped) finish_run("stopped");
    else if (complete) finish_run("succeeded");
}

void EnduranceController::tick(const std::vector<EnduranceWorkflow>& workflows) {
    if (active_run_) {
        std::vector<std::string> finished;
        for (auto& [id, process] : processes_) {
            auto& result = endurance_result(*active_run_, id);
            bool run_finished = false;
            bool failed = false;
            std::string detail;
            for (const auto& event : process.drain()) {
                switch (event.kind) {
                    case EventKind::SessionStarted:
                        result.session_id = event.session_id;
                        break;
                    case EventKind::AssistantDelta:
                        saw_delta_[id] = true;
                        result.output = bounded_output(result.output + event.text);
                        output_dirty_ = true;
                        break;
                    case EventKind::AssistantMessage:
                        if (saw_delta_[id]) result.output = event.text;
                        else if (!result.output.empty()) result.output += "\n\n" + event.text;
                        else result.output = event.text;
                        result.output = bounded_output(result.output);
                        output_dirty_ = true;
                        break;
                    case EventKind::RunFinished:
                        run_finished = true;
                        failed = event.is_error;
                        detail = event.text.empty() ? event.detail : event.text;
                        if (event.has_usage) {
                            result.cost_usd = event.cost_usd;
                            result.input_tokens = event.input_tokens;
                            result.output_tokens = event.output_tokens;
                        }
                        break;
                    case EventKind::Error:
                        if (!event.text.empty()) result.error = event.text;
                        break;
                    default:
                        break;
                }
                if (event.has_usage) {
                    result.cost_usd = event.cost_usd;
                    result.input_tokens = event.input_tokens;
                    result.output_tokens = event.output_tokens;
                }
            }
            const auto* current = step(id);
            if (!run_finished && current && endurance_now() - result.started_at > current->timeout_seconds) {
                process.stop();
                run_finished = true;
                failed = true;
                detail = "Step timed out after " + std::to_string(current->timeout_seconds) + " seconds";
            }
            if (run_finished) {
                finish_process(id, failed, detail.empty() ? (failed ? "Agent process failed" : "") : detail);
                finished.push_back(id);
            }
        }
        for (const auto& id : finished) {
            processes_.erase(id);
            saw_delta_.erase(id);
        }
        if (output_dirty_ && changed_) changed_();
        if (output_dirty_ && endurance_now() - last_output_save_ >= 2) {
            store_.save_run(*active_run_);
            replace_history(*active_run_);
            last_output_save_ = endurance_now();
            output_dirty_ = false;
        }
        schedule_ready();
    }
    tick_schedule(workflows);
}

void EnduranceController::stop() {
    if (!active_run_) return;
    for (auto& [id, process] : processes_) process.stop();
    processes_.clear();
    saw_delta_.clear();
    for (auto& [id, result] : active_run_->steps) {
        if (result.status == "running") {
            result.status = "stopped";
            result.finished_at = endurance_now();
            result.error = "Stopped by user";
            if (!result.checkpoint_id.empty()) {
                try { (void)checkpoints_.finish(result.checkpoint_id, "stopped"); } catch (...) {}
            }
        }
        if (result.status == "waiting") result.status = "stopped";
    }
    active_run_->status = "stopped";
    active_run_->error = "Stopped by user; resume to continue";
    record("stopped");
}

bool EnduranceController::resume(const std::string& run_id, std::string& error) {
    if (busy()) {
        error = "Finish or stop the active Endurance run before resuming another";
        return false;
    }
    const auto found = std::find_if(runs_.begin(), runs_.end(), [&](const auto& run) { return run.id == run_id; });
    if (found == runs_.end()) {
        error = "Endurance run not found";
        return false;
    }
    active_run_ = *found;
    endurance_reset_for_resume(*active_run_);
    record("resumed");
    schedule_ready();
    return true;
}

bool EnduranceController::approve(bool approved, std::string& error) {
    if (!active_run_) {
        error = "No active Endurance approval";
        return false;
    }
    const auto found = std::find_if(active_run_->snapshot.steps.begin(), active_run_->snapshot.steps.end(), [&](const auto& step) {
        return step.type == "approval" && endurance_result(*active_run_, step.id).status == "waiting";
    });
    if (found == active_run_->snapshot.steps.end()) {
        error = "This run has no approval step";
        return false;
    }
    auto& result = endurance_result(*active_run_, found->id);
    if (result.status != "waiting") {
        error = "This approval is not waiting";
        return false;
    }
    if (approved) {
        result.status = "succeeded";
        active_run_->status = "running";
        record("approval-accepted:" + found->id);
    } else {
        result.status = "stopped";
        active_run_->status = "stopped";
        active_run_->error = "Rejected by user";
        endurance_block_downstream(*active_run_, found->id);
        record("approval-rejected:" + found->id);
    }
    schedule_ready();
    return true;
}

bool EnduranceController::busy() const {
    return !processes_.empty() || (active_run_ && (active_run_->status == "running" || active_run_->status == "waiting"));
}

bool EnduranceController::busy_workflow(const std::filesystem::path& workflow_path) const {
    return busy() && active_run_ && active_run_->workflow_path == workflow_path.generic_string();
}

const EnduranceScheduleState* EnduranceController::schedule(const std::filesystem::path& workflow_path) const {
    const auto found = schedule_.find(workflow_path.generic_string());
    return found == schedule_.end() ? nullptr : &found->second;
}

void EnduranceController::update_schedule(const EnduranceWorkflow& workflow) {
    const auto key = workflow.path.generic_string();
    if (!workflow.recurrence.enabled) {
        schedule_.erase(key);
    } else {
        auto& state = schedule_[key];
        state.next_at = endurance_next_cron(workflow.recurrence.expression, endurance_now(), workflow.recurrence.timezone);
        state.queued = false;
    }
    store_.save_schedule(schedule_);
    if (changed_) changed_();
}

void EnduranceController::tick_schedule(const std::vector<EnduranceWorkflow>& workflows) {
    bool dirty = false;
    const auto now = endurance_now();
    for (const auto& workflow : workflows) {
        const auto key = workflow.path.generic_string();
        if (!workflow.recurrence.enabled) {
            dirty = schedule_.erase(key) || dirty;
            continue;
        }
        auto& state = schedule_[key];
        if (state.next_at == 0) {
            state.next_at = endurance_next_cron(workflow.recurrence.expression, now, workflow.recurrence.timezone);
            dirty = true;
        }
        if (state.next_at > now) continue;
        const bool overdue = now - state.next_at >= 60;
        if (busy_workflow(workflow.path)) {
            if (workflow.recurrence.overlap == "queue") state.queued = true;
            else state.queued = false;
            state.next_at = endurance_next_cron(workflow.recurrence.expression, now, workflow.recurrence.timezone);
            dirty = true;
            continue;
        }
        if (state.queued || !overdue || workflow.recurrence.missed_runs == "run_once") {
            std::string error;
            if (start(workflow, workflow.input_default, true, error)) {
                state.last_at = now;
                state.queued = false;
            } else if (notify_) {
                notify_("Scheduled Endurance run could not start", workflow.name + "\n" + error, true, key);
            }
        }
        state.next_at = endurance_next_cron(workflow.recurrence.expression, now, workflow.recurrence.timezone);
        dirty = true;
    }
    if (dirty) store_.save_schedule(schedule_);
}

void EnduranceController::finish_run(const std::string& status) {
    if (!active_run_) return;
    active_run_->status = status;
    active_run_->finished_at = endurance_now();
    record("run-" + status);
    if (notify_) notify_("Endurance " + status, active_run_->workflow_name, status == "failed", active_run_->id);
    if (status == "succeeded") {
        try {
            workspace_.stage_run_episode(active_run_->id, active_run_->workflow_name, "Endurance", transcript(), "completed");
        } catch (...) {
        }
        if (succeeded_) succeeded_(*active_run_);
    }
}

std::string EnduranceController::transcript() const {
    if (!active_run_) return {};
    std::string result = "WORKFLOW: " + active_run_->workflow_name + "\nINPUT:\n" + active_run_->input;
    for (const auto& current : active_run_->snapshot.steps) {
        const auto& result_step = endurance_result(*active_run_, current.id);
        result += "\n\nSTEP " + current.id + " [" + result_step.status + "]:\n" + result_step.output;
        if (!result_step.error.empty()) result += "\nERROR: " + result_step.error;
    }
    return result;
}

void EnduranceController::record(const std::string& event, bool persist) {
    if (!active_run_) return;
    active_run_->updated_at = endurance_now();
    if (persist) store_.save_run(*active_run_, event);
    replace_history(*active_run_);
    output_dirty_ = false;
    if (changed_) changed_();
}

void EnduranceController::replace_history(const EnduranceRun& run) {
    const auto found = std::find_if(runs_.begin(), runs_.end(), [&](const auto& item) { return item.id == run.id; });
    if (found == runs_.end()) runs_.insert(runs_.begin(), run);
    else *found = run;
    if (runs_.size() > 100) runs_.resize(100);
}

} // namespace raven
