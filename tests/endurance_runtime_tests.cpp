#include "endurance_controller.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::string wait_status(raven::EnduranceController& controller, const std::vector<raven::EnduranceWorkflow>& workflows,
                        const std::string& expected, int milliseconds = 6000) {
    for (int elapsed = 0; elapsed < milliseconds; elapsed += 10) {
        controller.tick(workflows);
        if (controller.active_run() && controller.active_run()->status == expected) return expected;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return controller.active_run() ? controller.active_run()->status : "missing";
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "raven-endurance-runtime-tests";
    const auto bin = std::filesystem::temp_directory_path() / "raven-endurance-fake-bin";
    std::filesystem::remove_all(root);
    std::filesystem::remove_all(bin);
    std::filesystem::create_directories(root);
    std::filesystem::create_directories(bin);
    const auto fake = bin / "claude";
    {
        std::ofstream script(fake);
        script << "#!/bin/bash\n"
               << "sleep 0.04\n"
               << "echo '{\"type\":\"system\",\"subtype\":\"init\",\"session_id\":\"fake-'$$'\"}'\n"
               << "if [[ \"$RAVEN_TEST_FAIL_REVIEW\" == \"1\" && \"$*\" == *\"STEP: review\"* ]]; then\n"
               << "  echo '{\"type\":\"result\",\"subtype\":\"error\",\"is_error\":true,\"result\":\"simulated review failure\"}'\n"
               << "  exit 0\n"
               << "fi\n"
               << "if [[ \"$*\" == *\"STEP: implement\"* ]]; then echo \"endurance-$RANDOM\" > endurance-runtime-output.txt; fi\n"
               << "echo '{\"type\":\"assistant\",\"session_id\":\"fake-'$$'\",\"message\":{\"id\":\"message\",\"content\":[{\"type\":\"text\",\"text\":\"verified fake output\"}]}}'\n"
               << "echo '{\"type\":\"result\",\"subtype\":\"success\",\"is_error\":false,\"result\":\"done\",\"total_cost_usd\":0.01,\"usage\":{\"input_tokens\":5,\"output_tokens\":7}}'\n";
    }
    std::filesystem::permissions(fake, std::filesystem::perms::owner_exec | std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
    const auto fake_kimi = bin / "kimi";
    {
        std::ofstream script(fake_kimi);
        script << "#!/bin/bash\n"
               << "echo 'simulated Kimi startup diagnostic' >&2\n"
               << "exit 2\n";
    }
    std::filesystem::permissions(fake_kimi, std::filesystem::perms::owner_exec | std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
    const auto* previous_path = std::getenv("PATH");
    const std::string path = bin.string() + ":" + (previous_path ? previous_path : "");
    setenv("PATH", path.c_str(), 1);

    try {
        raven::Workspace workspace(root);
        workspace.initialize();
        raven::CheckpointStore checkpoints(root);
        raven::GitMissionStore missions(root);
        checkpoints.load();
        missions.load();
        raven::EndurancePreferences preferences;
        raven::EnduranceController controller(workspace, checkpoints, missions, {}, preferences);
        int successes = 0;
        int failure_notifications = 0;
        controller.set_callbacks([] {}, [&](const auto& title, const auto&, bool, const auto&) {
            if (title == "Endurance failed") ++failure_notifications;
        }, [&](const auto&) { ++successes; });
        controller.load();
        auto workflow = raven::endurance_default_workflow();
        workflow.path = "mission.yaml";
        workflow.default_harness = "claude";
        std::string error;
        expect(controller.start(workflow, "Test the runtime", false, error), error.c_str());
        expect(wait_status(controller, {}, "waiting") == "waiting", "runtime did not reach its approval gate");
        expect(raven::endurance_result(*controller.active_run(), "research").status == "succeeded", "research did not complete");
        expect(raven::endurance_result(*controller.active_run(), "review").status == "succeeded", "review did not complete");
        expect(controller.approve(true, error), error.c_str());
        expect(wait_status(controller, {}, "succeeded") == "succeeded", "approved runtime did not complete");
        expect(successes == 1, "success callback did not fire exactly once");
        expect(raven::endurance_result(*controller.active_run(), "implement").output.find("verified fake output") != std::string::npos,
               "agent output was not persisted");
        expect(checkpoints.checkpoints().size() == 1, "Build step did not create one checkpoint");
        expect(controller.runs().size() == 1, "runtime history is missing its first run");

        setenv("RAVEN_TEST_FAIL_REVIEW", "1", 1);
        expect(controller.start(workflow, "Test failure recovery", false, error), error.c_str());
        expect(wait_status(controller, {}, "failed") == "failed", "simulated failure did not propagate");
        const auto failed_id = controller.active_run()->id;
        unsetenv("RAVEN_TEST_FAIL_REVIEW");
        expect(controller.resume(failed_id, error), error.c_str());
        expect(wait_status(controller, {}, "waiting") == "waiting", "resumed run did not reach approval again");
        expect(controller.approve(true, error), error.c_str());
        expect(wait_status(controller, {}, "succeeded") == "succeeded", "resumed run did not complete");

        auto retry_workflow = workflow;
        retry_workflow.steps[1].retries = 1;
        setenv("RAVEN_TEST_FAIL_REVIEW", "1", 1);
        expect(controller.start(retry_workflow, "Test retry budget", false, error), error.c_str());
        expect(wait_status(controller, {}, "failed") == "failed", "retry budget did not end in failure");
        expect(raven::endurance_result(*controller.active_run(), "review").attempt == 2, "retry budget was not honored");
        const auto notifications_at_failure = failure_notifications;
        for (int tick = 0; tick < 20; ++tick) controller.tick({});
        expect(failure_notifications == notifications_at_failure, "failed run emitted duplicate completion notifications");
        unsetenv("RAVEN_TEST_FAIL_REVIEW");

        auto acp_workflow = workflow;
        acp_workflow.default_harness = "kimi";
        acp_workflow.steps.resize(1);
        expect(controller.start(acp_workflow, "Test ACP diagnostics", false, error), error.c_str());
        expect(wait_status(controller, {}, "failed") == "failed", "ACP startup failure did not propagate");
        expect(controller.active_run()->error.find("simulated Kimi startup diagnostic") != std::string::npos,
               "ACP process diagnostics were discarded");

        workflow.recurrence.enabled = true;
        workflow.recurrence.missed_runs = "skip";
        raven::EnduranceStore store(root);
        store.save_schedule({{workflow.path.generic_string(), {raven::endurance_now() - 3600, 0, false}}});
        raven::EnduranceController skipped(workspace, checkpoints, missions, {}, preferences);
        skipped.load();
        skipped.tick({workflow});
        expect(!skipped.has_active_run(), "missed skip policy started an overdue run");
        expect(skipped.schedule(workflow.path)->next_at > raven::endurance_now(), "missed skip did not advance the schedule");

        workflow.recurrence.missed_runs = "run_once";
        store.save_schedule({{workflow.path.generic_string(), {raven::endurance_now() - 3600, 0, false}}});
        raven::EnduranceController catch_up(workspace, checkpoints, missions, {}, preferences);
        catch_up.set_callbacks([] {}, [](const auto&, const auto&, bool, const auto&) {}, [](const auto&) {});
        catch_up.load();
        catch_up.tick({workflow});
        expect(catch_up.has_active_run(), "run_once policy did not start a catch-up run");
        expect(catch_up.active_run()->scheduled, "catch-up run was not marked as scheduled");
        catch_up.stop();

        std::filesystem::remove_all(root);
        std::filesystem::remove_all(bin);
        std::cout << "All Endurance runtime tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        std::filesystem::remove_all(root);
        std::filesystem::remove_all(bin);
        return 1;
    }
}
