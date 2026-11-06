#include "endurance.hpp"
#include "workspace.hpp"

#include <algorithm>
#include <ctime>
#include <iostream>
#include <stdexcept>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::int64_t timestamp(int year, int month, int day, int hour, int minute) {
    std::tm time{};
    time.tm_year = year - 1900;
    time.tm_mon = month - 1;
    time.tm_mday = day;
    time.tm_hour = hour;
    time.tm_min = minute;
    return timegm(&time);
}

} // namespace

int main() {
    try {
        auto workflow = raven::endurance_default_workflow();
        expect(raven::endurance_validate(workflow).empty(), "default Endurance workflow is invalid");
        expect(workflow.steps.size() == 5, "default Endurance workflow does not contain five steps");
        workflow.path = "mission.yaml";
        workflow.recurrence.enabled = true;
        const auto source = raven::endurance_workflow_yaml(workflow);
        const auto parsed = raven::endurance_parse_workflow(source, workflow.path);
        expect(parsed.name == workflow.name && parsed.steps.size() == workflow.steps.size(), "workflow YAML round-trip failed");
        expect(parsed.recurrence.enabled && parsed.recurrence.expression == "0 9 * * 1-5", "recurrence YAML round-trip failed");
        expect(parsed.steps[1].needs.at(0) == "research" && parsed.steps[4].mode == "review", "step YAML round-trip failed");
        expect(parsed.input_default.find("verifiable improvement") != std::string::npos, "default task was not preserved");

        bool rejected = false;
        try { (void)raven::endurance_parse_workflow("version: 1\nunknown: true\nsteps: []\n"); } catch (...) { rejected = true; }
        expect(rejected, "unknown YAML fields were accepted");

        auto invalid = workflow;
        invalid.steps.push_back(invalid.steps[0]);
        expect(!raven::endurance_validate(invalid).empty(), "duplicate step IDs were accepted");
        invalid = workflow;
        invalid.steps[0].needs = {"verify"};
        expect(!raven::endurance_validate(invalid).empty(), "dependency cycle was accepted");
        invalid = workflow;
        invalid.steps.push_back(invalid.steps[1]);
        invalid.steps.back().id = "independent-build";
        invalid.steps.back().mode = "build";
        invalid.steps.back().needs.clear();
        expect(!raven::endurance_validate(invalid).empty(), "parallel Build steps were accepted");
        invalid = workflow;
        invalid.input_default.clear();
        expect(!raven::endurance_validate(invalid).empty(), "recurring workflow without a default task was accepted");

        const auto root = std::filesystem::temp_directory_path() / "raven-endurance-tests";
        std::filesystem::remove_all(root);
        raven::EnduranceStore store(root);
        auto stored = store.create_workflow();
        stored.recurrence.enabled = true;
        store.save_workflow(stored);
        expect(store.load_workflows().size() == 1, "workflow did not persist");
        auto run = store.create_run(stored, "Implement durable workflows");
        auto& research = raven::endurance_result(run, "research");
        research.status = "succeeded";
        research.output = "Use the existing process and checkpoint boundaries.";
        const auto review = std::find_if(run.snapshot.steps.begin(), run.snapshot.steps.end(), [](const auto& step) { return step.id == "review"; });
        const auto prompt = raven::endurance_render_prompt(run, *review);
        expect(prompt.find(research.output) != std::string::npos, "step output template was not rendered");
        expect(prompt.find("Implement durable workflows") == std::string::npos, "unrelated input leaked into review template");
        raven::endurance_block_downstream(run, "review");
        expect(raven::endurance_result(run, "approval").status == "blocked" && raven::endurance_result(run, "verify").status == "blocked",
               "failed dependency did not block descendants");
        raven::endurance_reset_for_resume(run);
        expect(raven::endurance_result(run, "approval").status == "ready" && run.status == "running", "resume did not requeue descendants");
        store.save_run(run, "tested");
        const auto loaded = store.load_run(run.id);
        expect(loaded.input == run.input && raven::endurance_result(loaded, "research").output == research.output,
               "run state or output did not survive reload");
        expect(store.load_runs().size() == 1, "run history did not reload");
        rejected = false;
        try { (void)store.load_run("../outside"); } catch (...) { rejected = true; }
        expect(rejected, "run path traversal was accepted");

        std::map<std::string, raven::EnduranceScheduleState> schedules;
        schedules["mission.yaml"] = {1234, 1200, true};
        store.save_schedule(schedules);
        const auto restored = store.load_schedule();
        expect(restored.at("mission.yaml").next_at == 1234 && restored.at("mission.yaml").queued, "schedule state did not persist");

        const auto after = timestamp(2026, 11, 3, 12, 0);
        const auto next = raven::endurance_next_cron("0 9 * * 1-5", after, "UTC");
        expect(next == timestamp(2026, 11, 4, 9, 0), "weekday cron selected the wrong occurrence");
        const auto hourly = raven::endurance_next_cron("*/15 * * * *", after, "UTC");
        expect(hourly == timestamp(2026, 11, 3, 12, 15), "step cron selected the wrong occurrence");
        rejected = false;
        try { (void)raven::endurance_next_cron("invalid", after); } catch (...) { rejected = true; }
        expect(rejected, "invalid cron was accepted");

        std::filesystem::remove_all(root);
        std::cout << "All Endurance tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
