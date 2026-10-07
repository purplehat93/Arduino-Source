// Collection execution uses the same journal and move engine as Box Sorter.
static void run_home_collection_plan(SingleSwitchProgramEnvironment& env, ProControllerContext& context,
    const std::string& path, const std::string& output, Language language,
    Milliseconds video_delay, Milliseconds game_delay, bool verify_only, bool resume, size_t limit
){
    const auto plan = load_saved_home_plan(env, path);
    if (!plan.collection_execution) plan_error(env, "Collection Manager requires a collection execution contract.");
    check_home_plan_output_paths(env, path, output);
    for (const auto& suffix : {".json", "-scan.json", "-slot-verification.json"}){
        if (QFile::exists(QString::fromStdString(output + suffix))){
            plan_error(env, "Execution output already exists. Choose a new output basename.");
        }
    }
    // A failed pre-pickup summary check can leave this Pokemon menu open.
    // Resume may dismiss it; unknown screens and held Pokemon still fail closed.
    if (resume){
        HomeSummaryEntryWatcher summary_menu(HomeSummaryEntryWatcher::State::MENU_SUMMARY);
        HomeSummaryEntryWatcher move_menu(HomeSummaryEntryWatcher::State::MENU_MOVE);
        if (wait_until(env.console, context, 500ms, {summary_menu, move_menu}) >= 0){
            pbf_press_button(context, BUTTON_B, 80ms, game_delay);
            context.wait_for_all_requests();
            env.log("HOME_PLAN action=resume_menu_dismiss menu=pokemon");
        }
    }
    BoxViewWatcher ordinary(&env.console.overlay());
    if (wait_until(env.console, context, Seconds(4), {ordinary}) != 0){
        plan_error(env, "Open ordinary HOME box view with no Pokemon held.");
    }
    const auto current = read_home_box_identity(env, context);
    if (!current || current->second != plan.total_boxes) plan_error(env, "Cannot confirm this HOME box count.");
    BoxNavigationTiming timing(game_delay);
    timing.local_cursor_delay = std::max(timing.local_cursor_delay, 480ms);
    move_home_box_verified(env, context, plan.first_box, timing, false);
    const auto origin = read_home_box_identity(env, context);
    if (!origin || origin->first != plan.first_box || origin->second != plan.total_boxes){
        plan_error(env, "Cannot confirm the collection plan's physical scan origin.");
    }
    const std::vector<SortingRule> preferences{{SortingRuleType::DexNo, false}};
    QJsonArray fresh;
    BoxCursor cursor{0, 0, 0}; // Verified navigation above returns to the first slot.
    if (verify_only){
        std::vector<QJsonObject> observations;
        const auto data = scan_home_range(env, context, plan.box_count, *origin, preferences,
            video_delay, game_delay, language, timing, &observations, !plan.collection_judge);
        save_boxes_data_to_json(data, output + ".json");
        fresh = read_slot_export(env, output + ".json");
        if ((size_t)fresh.size() != observations.size()) plan_error(env, "Execution observations differ from the fresh scan length.");
        for (qsizetype i = 0; i < fresh.size(); ++i){
            if (fresh.at(i).isNull()) continue;
            auto record = fresh.at(i).toObject(); record.insert("collection_observation", observations.at(i)); fresh[i] = record;
        }
        // Preserve fresh evidence for diagnosis without changing the source plan.
        QJsonArray rows;
        for (qsizetype i = 0; i < fresh.size(); ++i){
            auto row = fresh.at(i).toObject();
            row.insert("index", i); row.insert("box", i / 30);
            row.insert("row", i / 6 % 5); row.insert("column", i % 6); rows.append(row);
        }
        QSaveFile evidence(QString::fromStdString(output + ".json"));
        evidence.setDirectWriteFallback(false);
        const auto bytes = QJsonDocument(rows).toJson(QJsonDocument::Indented);
        if (!evidence.open(QIODevice::WriteOnly) || evidence.write(bytes) != bytes.size() || !evidence.commit()){
            plan_error(env, "Cannot save fresh collection verification evidence. No pickup was attempted.");
        }
        cursor = BoxCursor{plan.box_count - 1, 0, 0};
    }else{
        env.log("HOME_COLLECTION warning=leave_boxes_unchanged full_scan=skipped verification=per_move");
    }
    auto read_slot = [&](size_t index){
        return read_home_saved_plan_slot(env, context, index, cursor, timing, game_delay,
            language, preferences, output + "-slot-verification.json", true, plan.collection_judge);
    };
    execute_saved_home_plan(env, context, plan, path, fresh, cursor, timing, game_delay,
        verify_only, resume, limit, read_slot, {}, verify_only);
    env.log(std::string("HOME_COLLECTION action=execute verification=")
        + (plan.collection_species ? "species_and_stable_summary_fields" : plan.collection_judge ? "metadata_forms_and_judge" : "metadata_forms") + " releases=0");
}

// Two held round trips with the same copy, placed only back in its source.
void compare_home_held_routes(SingleSwitchProgramEnvironment& env,
    ProControllerContext& context, Milliseconds game_delay){
    BoxViewWatcher ordinary(&env.console.overlay());
    if (wait_until(env.console, context, Seconds(4), {ordinary}) != 0){
        plan_error(env, "Start held routing comparison in ordinary box view with nothing held.");
    }
    const auto origin = read_home_box_identity(env, context);
    if (!origin || origin->second != 200) plan_error(env, "Cannot confirm the held comparison's physical origin.");
    BoxNavigationTiming timing(game_delay);
    move_home_box_verified(env, context, origin->first, timing, false);
    BoxCursor cursor{0, 0, 0};
    const std::vector<SortingRule> preferences{{SortingRuleType::DexNo, false}};
    const auto basename = "home-held-routing-" + std::to_string(
        std::chrono::duration_cast<Milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    auto read_source = [&](){
        return read_home_saved_plan_slot(env, context, 0, cursor, timing, game_delay,
            Language::English, preferences, basename + "-slot-verification.json", true, false);
    };
    SavedHomePlan identity;
    identity.collection_execution = true;
    identity.reference_hash = HOME_COLLECTION_REFERENCE_SHA256;
    const auto initial = plan_live_slot(env, identity, read_source());
    if (initial.isNull()) plan_error(env, "The first slot must contain an identifiable Pokemon for held comparison.");
    const size_t distance = 70;
    const size_t target = origin->first + distance <= 200 ? origin->first + distance : origin->first - distance;
    // Use absolute physical indices in this diagnostic, including backwards routes.
    cursor.box = origin->first - 1;
    env.log("HOME_SPEED diagnostic=held_routing_comparison result=started start_box=" + std::to_string(origin->first));
    for (bool automatic : {false, true}){
        // Summary reading can advance HOME's cursor. Align it again just as
        // the production executor does before every pickup.
        cursor = move_cursor_to(env, context, cursor, BoxCursor{origin->first - 1, 0, 0},
            game_delay, &timing);
        pbf_press_button(context, BUTTON_Y, 80ms, std::max(576ms, game_delay + 240ms));
        context.wait_for_all_requests();
        if (!confirm_home_held_cursor(env, context, origin->first, 0, 0)){
            plan_error(env, "Cannot confirm held source. No travel was attempted.");
        }
        const auto started = std::chrono::steady_clock::now();
        cursor = move_cursor_to(env, context, cursor, BoxCursor{target - 1, 0, 0}, game_delay,
            automatic ? &timing : nullptr, true);
        const auto arrived = std::chrono::steady_clock::now();
        cursor = move_cursor_to(env, context, cursor, BoxCursor{origin->first - 1, 0, 0}, game_delay,
            automatic ? &timing : nullptr, true);
        const auto restored = std::chrono::steady_clock::now();
        // No placement at the distant endpoint; only return to the source slot.
        if (!confirm_home_held_cursor(env, context, origin->first, 0, 0)){
            plan_error(env, "Source destination not confirmed. Stopped without placement.");
        }
        pbf_press_button(context, BUTTON_Y, 80ms, std::max(576ms, game_delay + 240ms));
        context.wait_for_all_requests();
        if (wait_until(env.console, context, Seconds(5), {ordinary}) != 0){
            plan_error(env, "Source restoration did not return to ordinary box view.");
        }
        cursor.box = 0;
        if (!plan_live_matches(identity, initial, plan_live_slot(env, identity, read_source()))){
            plan_error(env, "Source identity differs after the held round trip.");
        }
        cursor.box = origin->first - 1;
        env.log("HOME_SPEED route=" + std::string(automatic ? "auto" : "linear")
            + " held_mode=pokemon distance=" + std::to_string(distance)
            + " start_box=" + std::to_string(origin->first) + " target_box=" + std::to_string(target)
            + " outbound_ms=" + std::to_string(std::chrono::duration_cast<Milliseconds>(arrived - started).count())
            + " return_ms=" + std::to_string(std::chrono::duration_cast<Milliseconds>(restored - arrived).count())
            + " total_ms=" + std::to_string(std::chrono::duration_cast<Milliseconds>(restored - started).count())
            + " restored=yes identity=verified");
    }
    env.log("HOME_SPEED diagnostic=held_routing_comparison result=completed restored=yes releases=0"
        " distant_placements=0 evidence=" + basename + "-slot-verification.json");
}

#include "PokemonHome_CollectionIndex.inc"

// Included by BoxSorter.cpp to reuse its verified scanner without duplicating it.
CollectionManager_Descriptor::CollectionManager_Descriptor()
    : SingleSwitchProgramDescriptor(
        "PokemonHome:CollectionManager", STRING_POKEMON + " Home", "Collection Manager",
        "", "Index an editable inventory or execute a reviewed collection plan. Retains all extra Pokemon for review.",
        ProgramControllerClass::StandardController_RequiresPrecision, FeedbackType::REQUIRED,
        AllowCommandsWhenRunning::DISABLE_COMMANDS
    ){}

CollectionManager::CollectionManager()
    : FIRST_BOX("<b>First Physical Box to Index (0 = current):</b><br>Navigate to this physical box before indexing. Box labels are ignored. Applies only when Collection Plan File is empty.",
        LockMode::LOCK_WHILE_RUNNING, 0, 0, MAX_HOME_BOXES)
    , BOX_NUMBER("<b>Number of Boxes to Index:</b><br>Scan this full range in one run. Saves after every box; Stop retains completed boxes.",
        LockMode::LOCK_WHILE_RUNNING, 1, 1, MAX_HOME_BOXES)
    , HOME_LANGUAGE("<b>HOME Language:</b>", {Language::English, Language::Japanese, Language::Spanish,
        Language::French, Language::German, Language::Italian, Language::Korean,
        Language::ChineseSimplified, Language::ChineseTraditional}, LockMode::LOCK_WHILE_RUNNING)
    , OUTPUT_FILE(false, "<b>Collection Output:</b><br>New file basename. Creates source, scan context and an editable workspace. Records form candidates and observed Judge ratings; unreadable fields remain unknown.",
        LockMode::LOCK_WHILE_RUNNING, "home-collection", "home-collection")
    , RESUME_INDEX("<b>Resume Inventory Index:</b><br>Continue the same output basename and full range from its saved box checkpoint. Leave indexed boxes unchanged. Applies when Collection Plan File is empty.", LockMode::LOCK_WHILE_RUNNING, false)
    , SAVED_PLAN(false, "<b>Collection Plan File (optional):</b><br>Leave empty to index. Load a plan prepared with verified form requirements to check or execute it. Do not move, add, remove or release Pokemon in the planned boxes after indexing or while paused. Execution checks affected Pokemon as it moves and stops on a mismatch.",
        LockMode::LOCK_WHILE_RUNNING, "", "")
    , VERIFY_ONLY("<b>Verify Plan Only:</b><br>Scan and check the current layout without moving Pokemon.", LockMode::LOCK_WHILE_RUNNING, true)
    , RESUME_PLAN("<b>Resume Collection Plan:</b><br>Continue from the saved checkpoint without reindexing. Leave the planned boxes unchanged while paused. Each move checks the affected Pokemon; a mismatch stops execution.", LockMode::LOCK_WHILE_RUNNING, false)
    , MOVES_PER_RUN("<b>Maximum Moves This Run:</b><br>Pause unheld after this many verified moves. Zero runs the complete plan.", LockMode::LOCK_WHILE_RUNNING, 1, 0, 65535)
    , VIDEO_DELAY("<b>Capture Card Delay:</b>", LockMode::LOCK_WHILE_RUNNING, "400 ms")
    , GAME_DELAY("<b>HOME App Delay:</b>", LockMode::LOCK_WHILE_RUNNING, "240 ms")
{
    PA_ADD_OPTION(FIRST_BOX);
    PA_ADD_OPTION(BOX_NUMBER);
    PA_ADD_OPTION(HOME_LANGUAGE);
    PA_ADD_OPTION(OUTPUT_FILE);
    PA_ADD_OPTION(RESUME_INDEX);
    PA_ADD_OPTION(SAVED_PLAN);
    PA_ADD_OPTION(VERIFY_ONLY);
    PA_ADD_OPTION(RESUME_PLAN);
    PA_ADD_OPTION(MOVES_PER_RUN);
    PA_ADD_OPTION(VIDEO_DELAY);
    PA_ADD_OPTION(GAME_DELAY);
}

void CollectionManager::program(SingleSwitchProgramEnvironment& env, ProControllerContext& context){
    StartProgramChecks::check_performance_class_wired_or_wireless(context);
    const std::string base = OUTPUT_FILE;
    if (base.empty()) throw UserSetupError(env.console, "Choose a new collection output basename.");
    const std::string path = SAVED_PLAN;
    if (!path.empty()){
        run_home_collection_plan(env, context, path, base, HOME_LANGUAGE, VIDEO_DELAY.get(), GAME_DELAY.get(),
            (bool)VERIFY_ONLY, (bool)RESUME_PLAN, (size_t)MOVES_PER_RUN);
        return;
    }
    run_home_collection_index(env, context, base, HOME_LANGUAGE, VIDEO_DELAY.get(), GAME_DELAY.get(),
        (size_t)FIRST_BOX, (size_t)BOX_NUMBER, (bool)RESUME_INDEX);
}
