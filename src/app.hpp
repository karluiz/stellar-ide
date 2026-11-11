#pragma once

#include "checkpoints.hpp"
#include "claude.hpp"
#include "composer.hpp"
#include "endurance_controller.hpp"
#include "memory.hpp"
#include "missions.hpp"
#include "notifications.hpp"
#include "opening_audio.hpp"
#include "profile.hpp"
#include "recall.hpp"
#include "recovery.hpp"
#include "splash.hpp"
#include "theme_gallery.hpp"
#include "tesseract.hpp"
#include "threads.hpp"
#include "workspace.hpp"

#include <Raven.h>

#include <chrono>
#include <future>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace raven {

class App {
public:
    App(std::filesystem::path workspace, bool demo);
    ~App();

    bool create();
    void play_opening_score();
    void select_harness(Harness harness);
    void select_run_mode(int index);
    void select_watch_checks(bool enabled);
    void select_theme(int index);
    void start_with_prompt(const std::string& prompt);
    void open_endurance();
    void open_memory();
    void open_tesseract();
    void start_tesseract_with_prompt(const std::string& prompt);
    void start_endurance_with_task(const std::string& task);
    void open_code_file(const std::filesystem::path& path, int line = 0);
    void pump();
    Rid window() const;
    bool has_animation_frames() const;
    bool has_run_timers() const;

private:
    Workspace workspace_;
    ThreadStore thread_store_;
    RecallStore recall_store_;
    CheckpointStore checkpoint_store_;
    GitMissionStore mission_store_;
    RecoveryStore recovery_store_;
    NotificationStore notification_store_;
    WorkspaceProfileStore profile_store_;
    bool capturing_shortcut_ = false;
    std::vector<std::string> notification_rows_;
    bool recovery_save_pending_ = false;
    std::chrono::steady_clock::time_point recovery_save_due_;
    std::string active_mission_id_;
    std::string selected_mission_id_;
    std::vector<std::string> mission_rows_;
    std::vector<std::string> mission_repository_rows_;
    bool loading_missions_ = false;
    EnduranceController endurance_;
    EnduranceStore endurance_store_;
    std::vector<EnduranceWorkflow> endurance_workflows_;
    std::map<std::string, int> endurance_nodes_;
    std::map<std::string, int> endurance_inputs_;
    std::map<std::string, int> endurance_outputs_;
    std::vector<std::string> endurance_run_rows_;
    int endurance_workflow_index_ = -1;
    int endurance_step_index_ = -1;
    int endurance_run_index_ = -1;
    bool endurance_loading_ = false;
    bool endurance_yaml_pending_ = false;
    bool endurance_graph_sync_pending_ = false;
    std::chrono::steady_clock::time_point endurance_yaml_due_;
    ComposerIndex composer_index_;
    ThemeGallery theme_gallery_;
    TesseractCanvas tesseract_;
    SplashScreen splash_;
    OpeningAudio opening_audio_;
    bool opening_sound_ = true;
    bool demo_;
    Rid window_ = R_INVALID_ID;
    Rid current_message_ = R_INVALID_ID;
    Rid pending_message_ = R_INVALID_ID;
    Rid activity_expander_ = R_INVALID_ID;
    Rid activity_rows_ = R_INVALID_ID;
    struct ImageAttachment {
        std::filesystem::path path;
        std::string mime_type;
        int width = 0;
        int height = 0;
        std::uintmax_t bytes = 0;
    };
    std::string current_text_;
    std::optional<ImageAttachment> composer_image_;
    std::optional<ImageAttachment> tesseract_image_;
    std::optional<ImageAttachment> queued_image_;
    std::optional<ImageAttachment> last_image_;
    Rid composer_texture_ = R_INVALID_ID;
    Rid tesseract_texture_ = R_INVALID_ID;
    std::vector<std::string> context_attachment_paths_;
    std::map<std::string, Rid> tool_activity_status_;
    int activity_count_ = 0;
    int activity_failed_ = 0;
    bool saw_delta_ = false;
    bool composer_completion_pending_ = false;
    float editor_zoom_ = 1.0f;
    float interface_zoom_ = 1.15f;
    int theme_index_ = 0;
    bool memory_animation_active_ = false;
    bool gallery_animation_active_ = false;
    int settings_return_tab_ = 0;
    int settings_return_more_tab_ = 0;
    bool left_sidebar_visible_ = true;
    bool right_sidebar_visible_ = false;
    bool compact_interface_ = false;
    bool compact_restore_left_ = true;
    bool compact_restore_right_ = true;
    float left_sidebar_share_ = 0.21f;
    float center_workspace_share_ = 0.58f;
    float right_sidebar_share_ = 0.21f;

    ClaudeProcess lead_process_;
    ClaudeProcess wiki_process_;
    Harness harness_ = Harness::ClaudeCode;
    Harness run_harness_ = Harness::ClaudeCode;
    enum class RunMode { Build, Ask, Review };
    RunMode run_mode_ = RunMode::Build;
    bool run_tesseract_ = false;
    bool queued_tesseract_ = false;
    int run_setup_return_tab_ = 0;
    std::string run_tesseract_context_;
    std::string queued_tesseract_context_;
    std::optional<std::string> tesseract_context_override_;
    std::string queued_prompt_;
    std::string last_prompt_;
    bool last_run_failed_ = false;
    std::chrono::steady_clock::time_point run_started_;
    double run_elapsed_seconds_ = 0.0;
    int displayed_elapsed_seconds_ = -1;
    double run_cost_usd_ = -1.0;
    std::uint64_t run_input_tokens_ = 0;
    std::uint64_t run_output_tokens_ = 0;
    bool run_usage_reported_ = false;
    std::string active_run_notification_id_;
    std::chrono::steady_clock::time_point last_autosave_notification_;
    std::string current_action_ = "Waiting for a task";
    std::string session_id_;
    std::string wiki_output_;
    std::string transcript_text_;
    std::string run_title_;
    std::string current_thread_id_;
    std::vector<ThreadMessage> thread_messages_;
    std::vector<std::string> thread_rows_;
    bool show_archived_ = false;
    std::vector<std::string> recall_rows_;
    std::string active_checkpoint_id_;
    std::string selected_checkpoint_id_;
    std::vector<std::string> checkpoint_rows_;
    std::vector<VerificationCheck> active_checks_;
    std::future<VerificationCheck> rerun_future_;
    bool verification_rerun_running_ = false;
    bool watch_checks_ = false;
    bool vector_recall_ = true;
    bool watch_due_pending_ = false;
    std::chrono::steady_clock::time_point watch_due_;
    std::string rerun_checkpoint_id_;
    std::string rerun_check_id_;
    std::vector<Diagnostic> verification_diagnostics_;
    std::vector<std::string> checkpoint_hunk_ids_;
    std::vector<std::string> mission_hunk_ids_;
    int current_verification_ = -1;
    enum class PaletteMode { Commands, Files, Content };
    struct PaletteEntry {
        std::string action;
        std::filesystem::path path;
        int line = 0;
    };
    PaletteMode palette_mode_ = PaletteMode::Commands;
    std::vector<PaletteEntry> palette_entries_;
    bool palette_search_pending_ = false;
    int palette_return_tab_ = 0;
    std::chrono::steady_clock::time_point palette_search_due_;

    std::vector<std::filesystem::path> repositories_;
    std::vector<AgentDefinition> agents_;
    std::vector<WikiNote> notes_;
    std::vector<int> agent_rows_;
    std::vector<int> note_rows_;
    int current_agent_ = -1;
    int current_note_ = -1;
    bool current_agent_modified_ = false;
    bool current_note_modified_ = false;
    bool loading_agent_ = false;
    bool loading_note_ = false;
    std::vector<GraphOperation> knowledge_records_;
    int current_knowledge_ = -1;
    MemoryUniverse memory_;
    std::filesystem::path current_code_path_;
    bool current_code_modified_ = false;
    bool loading_code_ = false;
    bool code_autosave_pending_ = false;
    bool agent_autosave_pending_ = false;
    bool note_autosave_pending_ = false;
    std::chrono::steady_clock::time_point code_autosave_due_;
    std::chrono::steady_clock::time_point agent_autosave_due_;
    std::chrono::steady_clock::time_point note_autosave_due_;

    std::optional<WikiProposal> proposal_;
    int lead_node_ = 0;
    int lead_output_ = 0;
    std::map<std::string, int> subagent_nodes_;
    std::map<std::string, Rid> subagent_status_;
    std::map<std::string, Rid> subagent_markers_;
    std::map<std::string, Rid> subagent_kinds_;

    Rid id(const char* name) const;
    void set_status(const std::string& text);
    void refresh_workspace();
    void refresh_lists();
    void refresh_threads();
    void toggle_archived_threads();
    void archive_selected_thread();
    void new_thread();
    void open_thread(const std::string& id);
    void save_thread();
    void refresh_composer_index();
    void composer_changed();
    void complete_composer(const char* prefix);
    bool composer_token_complete() const;
    void update_context_summary();
    bool paste_image(bool tesseract);
    void remove_image(bool tesseract);
    void clear_images();
    void refresh_image_attachment(bool tesseract);
    std::optional<ImageAttachment>& image_for(bool tesseract);
    Rid& texture_for(bool tesseract);
    void clear_context();
    void remove_context_file();
    void compact_context();
    void seed_recall();
    void open_recall();
    void close_recall();
    void refresh_recall();
    void use_recall();
    void toggle_recall_pin();
    void erase_recall();
    void open_palette(PaletteMode mode);
    void close_palette();
    void palette_changed();
    void refresh_palette();
    void open_palette_result();
    void pump_palette();
    void reset_session();
    void recover();
    void schedule_recovery_save();
    void pump_recovery();
    void reset_graph();
    void load_demo();
    void change_harness(int index);
    void change_run_mode(int index);
    void change_model(int index);
    void change_effort(int index);
    void refresh_model_controls();
    std::string current_model() const;
    std::string current_effort() const;
    void toggle_appearance();
    void dismiss_splash();
    void toggle_splash_sound();
    void update_splash_sound_control();
    void refresh_tesseract_chrome();
    void begin_tesseract_text_edit();
    void finish_tesseract_text_edit(bool commit);
    void load_endurance();
    void refresh_endurance_workflows(int selected = -1);
    void select_endurance_workflow(int index);
    EnduranceWorkflow* current_endurance_workflow();
    EnduranceStep* current_endurance_step();
    const EnduranceRun* selected_endurance_run() const;
    void save_endurance_workflow();
    bool validate_endurance_yaml(bool apply);
    void edit_endurance_yaml();
    void rebuild_endurance_graph();
    void fit_endurance_graph();
    void refresh_endurance_node_states();
    void select_endurance_step(const std::string& id);
    void select_endurance_step(int index);
    void update_endurance_step();
    void add_endurance_step();
    void duplicate_endurance_step();
    void delete_endurance_step();
    void layout_endurance_graph();
    void toggle_endurance_recurrence();
    void update_endurance_schedule_controls();
    void save_endurance_recurrence();
    void start_endurance_run();
    void stop_endurance_run();
    void resume_endurance_run();
    void approve_endurance_run(bool approved);
    void open_endurance_run(int index);
    void refresh_endurance_runs();
    void refresh_endurance_run_details();
    void pump_endurance();
    void open_theme_gallery();
    void close_theme_gallery();
    void move_theme_gallery(int delta);
    void update_composer_focus();
    const char* harness_name() const;
    void open_settings();
    void close_settings();
    void toggle_settings();
    void toggle_vector_recall();
    void load_profile();
    void apply_profile();
    void save_profile();
    void apply_custom_shortcuts();
    void refresh_shortcut_editor();
    void begin_shortcut_rebind();
    void reset_shortcut();
    bool capture_shortcut(const REvent* event);
    bool suppress_replaced_shortcut(const REvent* event) const;
    void apply_message_style(Rid card, bool user);

    void start_run();
    void stop_run();
    void cancel_queued();
    void update_queue();
    void maybe_start_queued();
    void retry_run();
    void update_run_timer();
    void set_current_action(const std::string& action);
    void update_run_usage(const ClaudeEvent* event = nullptr, bool accumulate = false);
    std::string run_usage_summary() const;
    void notify_event(const std::string& title, const std::string& body, bool error = false,
                      const std::string& kind = "run", const std::string& action_name = "agent", const std::string& target = {});
    void refresh_notifications();
    void select_notification(int index);
    void open_notification();
    void clear_notifications();
    void notify_autosave_error(const std::string& error);
    void notify_run_finished(bool error);
    void start_wiki_curator();
    void handle_lead_event(const ClaudeEvent& event);
    void handle_wiki_event(const ClaudeEvent& event);
    Rid add_message(const std::string& role, const std::string& text, bool user = false);
    void show_pending(const std::string& role, const std::string& text);
    void dismiss_pending();
    void add_tool_activity(const ClaudeEvent& event);
    void update_activity_summary(const std::string& current = {});
    void finish_activity(bool error);
    void add_subagent(const ClaudeEvent& event);
    void finish_tool(const ClaudeEvent& event);
    void update_graph_summary();

    void open_workspace();
    void open_workspace_path(const std::filesystem::path& path);
    void refresh_missions();
    void create_mission();
    void review_mission(const std::string& id);
    void apply_mission();
    void apply_mission_hunk();
    void discard_mission();
    void return_from_mission();
    void finish_checkpoint(const std::string& status);
    void refresh_checkpoint_history(const std::string& selected = {});
    void open_checkpoint(const std::string& id);
    void show_checkpoint_diff(int index);
    void open_checkpoint_file();
    void open_verification(int index);
    void open_diagnostic();
    void rerun_verification();
    void toggle_watch_checks();
    void schedule_watch();
    void pump_verification();
    void restore_checkpoint();
    void restore_checkpoint_hunk();
    void open_agent(int index);
    bool save_agent(bool automatic = false);
    void agent_modified();
    void delete_agent();
    void new_agent();
    void open_note(int index);
    bool save_note(bool automatic = false);
    void note_modified();
    void new_note();
    void update_preview();
    void open_proposal();
    void accept_proposal();
    void discard_proposal();
    void refresh_knowledge();
    void open_knowledge(int index);
    void refresh_memory_details();
    void forget_memory();
    bool save_code_file(bool automatic = false);
    void code_modified();
    void pump_autosave();
    bool flush_autosave();
    bool can_discard_code();
    int active_view(Rid view) const;
    void set_active_view(Rid view, int index);
    void refresh_navigation();
    void show_context(int index);
    void set_sidebar_visibility(bool left, bool visible);
    void toggle_sidebar(bool left);
    void update_interface_density();
    void zoom_editors(float amount);
    void zoom_interface(float amount);

    static void action(Rid sender, const char* action, void* user);
    static void list_selection(Rid list, int index, void* user);
    static void tab_changed(Rid tabs, int index, void* user);
    static void wiki_link(Rid label, const char* url, void* user);
    static void frame(double delta, void* user);
    static void file_accepted(Rid browser, const char* path, void* user);
    static int window_event(Rid window, const REvent* event, void* user);
};

} // namespace raven
