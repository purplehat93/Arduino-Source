// Included inside the HOME namespace by BoxSorter.cpp.
namespace{
[[noreturn]] void plan_error(SingleSwitchProgramEnvironment& env, const std::string& message){
    throw UserSetupError(env.console, "Saved HOME plan: " + message);
}

QByteArray plan_canonical(const QJsonValue& value){
    if (value.isObject()){
        const auto object = value.toObject();
        auto keys = object.keys();
        keys.sort();
        QByteArray result = "{";
        for (const auto& key : keys){
            if (result.size() > 1) result += ',';
            result += plan_canonical(key) + ':' + plan_canonical(object.value(key));
        }
        return result + '}';
    }
    if (value.isArray()){
        QByteArray result = "[";
        for (const auto& item : value.toArray()){
            if (result.size() > 1) result += ',';
            result += plan_canonical(item);
        }
        return result + ']';
    }
    if (value.isDouble() && value.toDouble() == std::floor(value.toDouble())){
        return QByteArray::number(value.toInteger());
    }
    const QByteArray encoded = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return encoded.mid(1, encoded.size() - 2);
}

QString plan_hash(const QByteArray& bytes){
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QByteArray read_plan_bytes(SingleSwitchProgramEnvironment& env, const std::string& path){
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024 * 1024){
        plan_error(env, "Cannot read bounded JSON file: " + path);
    }
    return file.readAll();
}

size_t plan_integer(SingleSwitchProgramEnvironment& env, QJsonValue value, size_t maximum){
    const double number = value.toDouble(-1);
    if (!value.isDouble() || number < 0 || number > maximum || std::floor(number) != number){
        plan_error(env, "Invalid integer or out-of-range coordinate.");
    }
    return (size_t)number;
}

struct SavedHomeMove{size_t source, destination;};
struct SavedHomePlan{
    size_t first_box, total_boxes, box_count;
    QJsonArray source;
    QJsonObject catalog, physical_range;
    std::vector<SavedHomeMove> moves;
    QString file_hash;
    bool collection_execution = false;
    bool collection_judge = false;
    bool collection_species = false;
    QString reference_hash;
    QJsonObject form_keys;
    QStringList stationary_review_occurrences;
};

void check_home_plan_output_paths(SingleSwitchProgramEnvironment& env,
    const std::string& plan_path, const std::string& output_base){
    auto resolved = [](const std::string& path){
        QFileInfo info(QString::fromStdString(path));
        QString result = info.canonicalFilePath();
        if (result.isEmpty()){
            const QString directory = QFileInfo(info.absolutePath()).canonicalFilePath();
            result = (directory.isEmpty() ? info.absolutePath() : directory) + "/" + info.fileName();
        }
#ifdef Q_OS_WIN
        result = result.toCaseFolded();
#endif
        return result;
    };
    for (const auto& suffix : {".json", "-sorted.json", "-scan.json", "-slot-verification.json"}){
        const auto output = resolved(output_base + suffix);
        if (output == resolved(plan_path) || output == resolved(plan_path + ".checkpoint.json")){
            plan_error(env, "Output filename would overwrite the saved plan or checkpoint. Choose another output basename.");
        }
    }
}

QJsonArray plan_observed(const SavedHomePlan& plan, const QJsonArray& layout){
    QJsonArray observed;
    for (const auto& id : layout){
        observed.append(id.isNull() ? QJsonValue(QJsonValue::Null) : plan.catalog.value(id.toString()));
    }
    return observed;
}

QString plan_layout_hash(const SavedHomePlan& plan, const QJsonArray& layout){
    return plan_hash(plan_canonical(QJsonObject{{"physical_range", plan.physical_range}, {"slots", plan_observed(plan, layout)}}));
}

QJsonValue plan_live_slot(SingleSwitchProgramEnvironment& env, const SavedHomePlan& plan,
    const QJsonValue& value, bool source_evidence = false){
    if (!plan.collection_execution || value.isNull()) return value;
    if (!value.isObject()) plan_error(env, "Invalid collection slot record.");
    const auto record = value.toObject();
    if (plan.collection_species){
        const auto result = home_collection_species_record(record, source_evidence);
        if (!result) plan_error(env, "Species slot lacks confirmed stable summary fields.");
        return *result;
    }
    const auto hash = source_evidence
        ? record.value("collection_observation").toObject().value("form").toObject().value("resource_sha256").toString()
        : plan.reference_hash;
    if (hash.size() != 64 || std::any_of(hash.begin(), hash.end(), [](QChar c){
            return !(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f');
        })) plan_error(env, "Collection reference digest is invalid.");
    const auto result = home_collection_live_record(record, hash, plan.collection_judge);
    if (!result) plan_error(env, "Collection slot lacks a confirmed metadata form under the required reference.");
    return *result;
}

QJsonArray plan_live_slots(SingleSwitchProgramEnvironment& env, const SavedHomePlan& plan,
    const QJsonArray& records, bool source_evidence = false){
    QJsonArray result;
    for (const auto& value : records) result.append(plan_live_slot(env, plan, value, source_evidence));
    return result;
}

QJsonArray plan_expected_live(SingleSwitchProgramEnvironment& env, const SavedHomePlan& plan, const QJsonArray& layout){
    return plan_live_slots(env, plan, plan_observed(plan, layout), true);
}

bool plan_live_matches(const SavedHomePlan& plan, const QJsonValue& expected, const QJsonValue& actual){
    if (!plan.collection_judge || expected.isNull() || actual.isNull()) return expected == actual;
    return expected.isObject() && actual.isObject()
        && home_collection_record_matches(expected.toObject(), actual.toObject());
}

bool plan_layout_matches(const SavedHomePlan& plan, const QJsonArray& expected, const QJsonArray& actual){
    if (expected.size() != actual.size()) return false;
    for (qsizetype i = 0; i < expected.size(); ++i){
        if (!plan_live_matches(plan, expected.at(i), actual.at(i))) return false;
    }
    return true;
}

SavedHomePlan load_saved_home_plan(SingleSwitchProgramEnvironment& env, const std::string& path){
    const auto bytes = read_plan_bytes(env, path);
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) plan_error(env, "Invalid plan JSON.");
    const auto root = document.object();
    if (root.contains("execution_blocked") && root.value("execution_blocked") != false){
        plan_error(env, "This is a preview plan whose collection identity requirements are not verified for live execution.");
    }
    if (plan_integer(env, root.value("version"), 1) != 1) plan_error(env, "Unsupported plan version.");
    const auto inventory = root.value("inventory").toObject();
    if (plan_integer(env, inventory.value("version"), 1) != 1) plan_error(env, "Unsupported inventory version.");
    SavedHomePlan plan;
    plan.file_hash = plan_hash(bytes);
    plan.source = inventory.value("slots").toArray();
    plan.catalog = inventory.value("pokemon").toObject();
    if (plan.source.isEmpty() || plan.source.size() % 30) plan_error(env, "Plan needs complete boxes.");
    const auto context = inventory.value("scan_context").toObject();
    if (plan_integer(env, context.value("version"), 1) != 1
        || context.value("coordinate_basis") != "zero_based_relative_to_physical_start_box"
        || context.value("box_identity") != "physical_counter_not_editable_label"
        || context.value("scan_id").toString().isEmpty()) plan_error(env, "Missing physical scan context.");
    plan.first_box = plan_integer(env, context.value("physical_start_box"), 300);
    plan.total_boxes = plan_integer(env, context.value("total_boxes"), 300);
    plan.box_count = plan_integer(env, context.value("box_count"), 300);
    const auto last = plan_integer(env, context.value("physical_end_box"), 300);
    if (!plan.first_box || !plan.box_count || plan.box_count != (size_t)plan.source.size() / 30
        || last != plan.first_box + plan.box_count - 1 || last > plan.total_boxes){
        plan_error(env, "Invalid physical range.");
    }
    for (const auto& key : {"physical_start_box", "physical_end_box", "total_boxes", "box_count"}){
        plan.physical_range.insert(key, context.value(key));
    }
    auto validate_layout = [&](const QJsonArray& layout){
        if (layout.size() != plan.source.size()) plan_error(env, "Layout length changed.");
        QStringList ids;
        for (const auto& id : layout){
            if (id.isNull()) continue;
            if (!id.isString() || !plan.catalog.value(id.toString()).isObject() || ids.contains(id.toString())){
                plan_error(env, "Invalid or repeated occurrence ID.");
            }
            ids.append(id.toString());
        }
        ids.sort();
        auto catalog_ids = plan.catalog.keys();
        catalog_ids.sort();
        if (ids != catalog_ids) plan_error(env, "Layout must preserve every occurrence.");
    };
    validate_layout(plan.source);
    const auto target = root.value("target").toArray();
    validate_layout(target);
    const auto validation = root.value("source_validation").toObject();
    if (plan_integer(env, validation.value("version"), 1) != 1
        || validation.value("physical_range").toObject() != plan.physical_range
        || validation.value("layout_sha256").toString() != plan_layout_hash(plan, plan.source)){
        plan_error(env, "Source fingerprint was changed or is missing. Regenerate the plan.");
    }
    if (root.contains("collection_review") || root.contains("collection_execution")){
        const auto contract = root.value("collection_execution").toObject();
        if (root.value("execution_blocked") != false || contract.value("version") != 1
            || (contract.value("mode") != "metadata_form_v1" && contract.value("mode") != "metadata_form_judge_v1" && contract.value("mode") != "metadata_species_v1" && contract.value("mode") != "metadata_species_v2")
            || contract.value("reference_sha256") != HOME_COLLECTION_REFERENCE_SHA256
            || contract.value("quality_annotations_verified") != false || contract.value("release_enabled") != false){
            plan_error(env, "Collection plans require the supported live form-verification contract.");
        }
        plan.collection_execution = true;
        const bool tracked_species_copies = contract.value("mode") == "metadata_species_v2";
        plan.collection_species = contract.value("mode") == "metadata_species_v1" || tracked_species_copies;
        if (tracked_species_copies && contract.value("duplicate_policy") != "tracked_slot_history"){
            plan_error(env, "Species v2 requires slot-history copy tracking.");
        }
        if (plan.collection_species && (contract.value("ordering") != "national_dex_species"
            || contract.value("copies") != "keep_all" || root.contains("collection_review"))){
            plan_error(env, "Species ordering cannot fill exact-form or quality collection assignments.");
        }
        plan.collection_judge = contract.value("mode") == "metadata_form_judge_v1";
        if (root.value("collection_review").toObject().value("judge_verification") == true && !plan.collection_judge){
            plan_error(env, "IV-aware copy selection requires live Judge verification.");
        }
        plan.reference_hash = contract.value("reference_sha256").toString();
        plan.form_keys = contract.value(plan.collection_species ? "species_requirements" : "form_keys").toObject();
        auto ids = plan.form_keys.keys(); ids.sort();
        auto catalog_ids = plan.catalog.keys(); catalog_ids.sort();
        if (ids != catalog_ids) plan_error(env, "Every collection occurrence needs a verified form requirement.");
        QMap<QByteArray, QStringList> signatures;
        QMap<QByteArray, QStringList> base_signatures;
        const auto judge_requirements = contract.value("judge_requirements").toObject();
        if (plan.collection_judge && judge_requirements.keys() != plan.catalog.keys()){
            plan_error(env, "Every quality-aware occurrence needs source Judge requirements.");
        }
        for (const auto& id : catalog_ids){
            const auto live = plan_live_slot(env, plan, plan.catalog.value(id), true).toObject();
            const QJsonValue identity = plan.collection_species
                ? QJsonValue(QJsonObject{{"dex", live.value("dex")}, {"name", live.value("name")}})
                : live.value("collection_form_key");
            if (identity != plan.form_keys.value(id)){
                plan_error(env, "Source form differs from its collection execution requirement.");
            }
            if (plan.collection_judge && live.value("collection_judge") != judge_requirements.value(id)){
                plan_error(env, "Judge requirement differs from independent source readings.");
            }
            if (plan.collection_species && plan.catalog.value(id).toObject().value("collection_observation")
                .toObject().value("form").toObject().value("fields_confirmed") != true){
                plan.stationary_review_occurrences.append(id);
            }
            auto base = live; base.remove("collection_judge");
            base_signatures[plan_canonical(base)].append(id);
            const auto signature = plan_canonical(live);
            signatures[signature].append(id);
        }
        for (const auto& ids_for_signature : signatures){
            if (!tracked_species_copies && ids_for_signature.size() > 1) plan.stationary_review_occurrences.append(ids_for_signature);
        }
        if (plan.collection_judge){
            for (const auto& ids_for_base : base_signatures){
                for (qsizetype i = 0; i < ids_for_base.size(); ++i){
                    for (qsizetype j = i + 1; j < ids_for_base.size(); ++j){
                        if (home_collection_judge_overlap(judge_requirements.value(ids_for_base[i]).toObject(),
                            judge_requirements.value(ids_for_base[j]).toObject())){
                            plan.stationary_review_occurrences.append(ids_for_base[i]);
                            plan.stationary_review_occurrences.append(ids_for_base[j]);
                        }
                    }
                }
            }
            plan.stationary_review_occurrences.removeDuplicates();
            for (const auto& id : catalog_ids){
                bool selected = false;
                for (const auto& entry : root.value("collection_review").toObject().value("assignments").toArray()){
                    selected |= entry.toObject().value("occurrence") == id;
                }
                bool displaced = false;
                for (qsizetype i = 0; i < plan.source.size(); ++i){
                    if (plan.source.at(i) == id) displaced = target.at(i) != id;
                }
                if (selected || displaced){
                    const auto requirements = judge_requirements.value(id).toObject();
                    for (auto it = requirements.begin(); it != requirements.end(); ++it){
                        if (it.value().isNull()) plan_error(env, "Selected or moved copies need all six confirmed Judge ratings.");
                    }
                }
            }
        }
        plan.stationary_review_occurrences.removeDuplicates();
        plan.stationary_review_occurrences.sort();
        QStringList declared;
        if (!contract.value("stationary_review_occurrences").isArray()) plan_error(env, "Missing stationary review copy requirements.");
        for (const auto& id : contract.value("stationary_review_occurrences").toArray()){
            if (!id.isString()) plan_error(env, "Invalid stationary review copy.");
            declared.append(id.toString());
        }
        declared.sort();
        if (plan.collection_species){
            for (const auto& id : plan.stationary_review_occurrences){
                if (!declared.contains(id)) plan_error(env, "Required review copies must remain stationary.");
            }
            for (const auto& id : declared){
                if (!plan.catalog.contains(id)) plan_error(env, "Unknown stationary species occurrence.");
            }
            plan.stationary_review_occurrences = declared;
        }else if (declared != plan.stationary_review_occurrences) plan_error(env, "Indistinguishable review copies must be declared accurately.");
        for (qsizetype i = 0; i < plan.source.size(); ++i){
            if (plan.stationary_review_occurrences.contains(plan.source.at(i).toString()) && target.at(i) != plan.source.at(i)){
                plan_error(env, "Indistinguishable review copies must stay in their source slots.");
            }
        }
        if (plan.collection_species){
            int previous_dex = 0;bool empty_seen = false;
            for (qsizetype i = 0; i < target.size(); ++i){
                if (plan.stationary_review_occurrences.contains(plan.source.at(i).toString())) continue;
                const auto id = target.at(i);
                if (id.isNull()){empty_seen = true;continue;}
                const int dex = plan.catalog.value(id.toString()).toObject().value("dex").toInt();
                if (empty_seen || dex < previous_dex) plan_error(env, "Species target must be National Dex ordered outside stationary review slots.");
                previous_dex = dex;
            }
        }
        for (const auto& value : root.value("collection_review").toObject().value("assignments").toArray()){
            if (plan.stationary_review_occurrences.contains(value.toObject().value("occurrence").toString())){
                plan_error(env, "Indistinguishable review copies cannot fill collection assignments.");
            }
        }
    }
    if (!root.value("moves").isArray()) plan_error(env, "Missing moves.");
    QJsonArray state = plan.source;
    for (const auto& entry : root.value("moves").toArray()){
        if (!entry.isObject()) plan_error(env, "Invalid move.");
        const auto move = entry.toObject();
        const auto kind = move.value("kind");
        if (!kind.isUndefined() && kind != "move") plan_error(env, "This GUI executor supports individual swaps and moves only.");
        const size_t source = plan_integer(env, move.value("source"), state.size() - 1);
        const size_t destination = plan_integer(env, move.value("destination"), state.size() - 1);
        if (source == destination || state.at(source).isNull() || state.at(source) != move.value("expected_source")
            || (kind == "move" ? !state.at(destination).isNull() : state.at(destination) != move.value("expected_destination"))){
            plan_error(env, "Move preconditions do not match the recorded layout.");
        }
        if (plan.stationary_review_occurrences.contains(state.at(source).toString())
            || plan.stationary_review_occurrences.contains(state.at(destination).toString())){
            plan_error(env, "Moves may not touch indistinguishable review copies.");
        }
        if (root.value("collection_execution").toObject().value("mode") == "metadata_species_v2"
            && !state.at(destination).isNull()
            && plan_live_slot(env, plan, plan.catalog.value(state.at(source).toString()), true)
                == plan_live_slot(env, plan, plan.catalog.value(state.at(destination).toString()), true)){
            plan_error(env, "Do not swap indistinguishable copies: interruption recovery would be ambiguous.");
        }
        if (plan.collection_judge){
            for (const auto& id : {state.at(source), state.at(destination)}){
                if (id.isNull()) continue;
                const auto requirements = root.value("collection_execution").toObject()
                    .value("judge_requirements").toObject().value(id.toString()).toObject();
                for (auto it = requirements.begin(); it != requirements.end(); ++it){
                    if (it.value().isNull()) plan_error(env, "Moves may not touch copies with unknown Judge ratings.");
                }
            }
        }
        const auto item = state.at(source);
        state.replace(source, state.at(destination));
        state.replace(destination, item);
        plan.moves.push_back({source, destination});
    }
    if (state != target) plan_error(env, "Moves do not reach the target layout.");
    return plan;
}

QJsonArray read_slot_export(SingleSwitchProgramEnvironment& env, const std::string& path){
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(read_plan_bytes(env, path), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) plan_error(env, "Invalid fresh scan export.");
    const auto records = document.array();
    QJsonArray observed_slots;
    std::vector<bool> seen(records.size(), false);
    for (qsizetype i = 0; i < records.size(); i++) observed_slots.append(QJsonValue::Null);
    for (const auto& record : records){
        if (!record.isObject() || records.isEmpty()) plan_error(env, "Invalid exported slot.");
        auto object = record.toObject();
        const size_t index = plan_integer(env, object.value("index"), records.size() - 1);
        if (seen[index] || plan_integer(env, object.value("box"), 300) != index / 30
            || plan_integer(env, object.value("row"), 4) != index / 6 % 5
            || plan_integer(env, object.value("column"), 5) != index % 6) plan_error(env, "Invalid fresh scan coordinates.");
        seen[index] = true;
        for (const auto& key : {"index", "box", "row", "column"}) object.remove(key);
        observed_slots.replace(index, object.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(object));
    }
    return observed_slots;
}

QJsonValue single_slot_json(SingleSwitchProgramEnvironment& env,
    const std::optional<CollectedPokemonInfo>& info, const std::string& path){
    save_boxes_data_to_json({info}, path);
    return read_slot_export(env, path).at(0);
}

void execute_saved_home_plan(SingleSwitchProgramEnvironment& env, ProControllerContext& context,
    const SavedHomePlan& plan, const std::string& path, const QJsonArray& fresh,
    BoxCursor& cursor, BoxNavigationTiming& timing, Milliseconds game_delay,
    bool dry_run, bool resume, size_t limit, const std::function<QJsonValue(size_t)>& read_slot,
    const std::function<void()>& move_verified = {}, bool verify_full_layout = true
){
    const QString checkpoint_path = QString::fromStdString(path + ".checkpoint.json");
    QJsonArray state = plan.source;
    size_t completed = 0;
    QString status = "ready";
    if (!verify_full_layout && (!plan.collection_execution || dry_run)){
        plan_error(env, "Per-move verification requires an executing collection plan.");
    }
    const auto live_fresh = verify_full_layout ? plan_live_slots(env, plan, fresh) : QJsonArray{};
    if (resume){
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(read_plan_bytes(env, path + ".checkpoint.json"), &error);
        const auto checkpoint = document.object();
        if (error.error != QJsonParseError::NoError || !document.isObject()
            || plan_integer(env, checkpoint.value("version"), 1) != 1
            || checkpoint.value("plan_sha256").toString() != plan.file_hash){
            plan_error(env, "Checkpoint belongs to another plan or is invalid.");
        }
        completed = plan_integer(env, checkpoint.value("completed_moves"), plan.moves.size());
        status = checkpoint.value("status").toString();
        if (status != "ready" && status != "in_flight" && status != "complete") plan_error(env, "Unknown checkpoint state.");
        if ((status == "complete") != (completed == plan.moves.size()) && status != "ready"){
            plan_error(env, "Inconsistent checkpoint boundary.");
        }
        for (size_t i = 0; i < completed; i++){
            const auto move = plan.moves[i];
            const auto item = state.at(move.source);
            state.replace(move.source, state.at(move.destination));
            state.replace(move.destination, item);
        }
        if (checkpoint.value("layout_sha256").toString() != plan_layout_hash(plan, state)){
            plan_error(env, "Checkpoint layout does not match its move boundary.");
        }
        if (status == "in_flight"){
            if (completed >= plan.moves.size()) plan_error(env, "Invalid in-flight boundary.");
            const auto move = plan.moves[completed];
            auto next = state;
            const auto item = next.at(move.source);
            next.replace(move.source, next.at(move.destination));
            next.replace(move.destination, item);
            bool before, after;
            if (verify_full_layout){
                before = plan_layout_matches(plan, plan_expected_live(env, plan, state), live_fresh);
                after = plan_layout_matches(plan, plan_expected_live(env, plan, next), live_fresh);
            }else{
                // Only the interrupted move can be between journal boundaries.
                // Read its two slots once, without auditing the rest of HOME.
                const auto source = plan_live_slot(env, plan, read_slot(move.source));
                const auto destination = plan_live_slot(env, plan, read_slot(move.destination));
                const auto expected_before = plan_expected_live(env, plan, state);
                const auto expected_after = plan_expected_live(env, plan, next);
                before = plan_live_matches(plan, expected_before.at(move.source), source)
                    && plan_live_matches(plan, expected_before.at(move.destination), destination);
                after = plan_live_matches(plan, expected_after.at(move.source), source)
                    && plan_live_matches(plan, expected_after.at(move.destination), destination);
            }
            if (before == after) plan_error(env, "Interrupted move is ambiguous. Resolve the held state and regenerate a plan.");
            if (after){state = next; completed++;}
            env.log("HOME_PLAN interrupted_move=reconciled completed_moves=" + std::to_string(completed));
        }
    }else if (!dry_run && QFile::exists(checkpoint_path)){
        plan_error(env, "A checkpoint already exists. Enable resume or use a new plan filename.");
    }
    if (verify_full_layout && !plan_layout_matches(plan, plan_expected_live(env, plan, state), live_fresh)){
        plan_error(env, "Fresh physical layout differs from this move boundary. No pickup was attempted.");
    }
    env.log(std::string("HOME_PLAN source=") + (verify_full_layout ? "verified" : "saved_plan")
        + " verification=" + (verify_full_layout ? "full_scan" : "per_move")
        + " completed_moves=" + std::to_string(completed) + " dry_run=" + (dry_run ? "yes" : "no"));
    if (dry_run) return;
    auto checkpoint = [&](const QString& boundary){
        QSaveFile file(checkpoint_path);
        file.setDirectWriteFallback(false);
        const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"plan_sha256", plan.file_hash},
            {"completed_moves", (double)completed}, {"status", boundary},
            {"layout_sha256", plan_layout_hash(plan, state)}}).toJson(QJsonDocument::Indented);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()){
            plan_error(env, "Cannot atomically save the checkpoint. Stopping before further movement.");
        }
    };
    checkpoint(completed == plan.moves.size() ? "complete" : "ready");
    const size_t stop = completed + std::min(plan.moves.size() - completed, limit ? limit : plan.moves.size());
    while (completed < stop){
        const auto move = plan.moves[completed];
        if (plan.collection_execution){
            const auto expected_before = plan_expected_live(env, plan, state);
            if (!plan_live_matches(plan, expected_before.at(move.source), plan_live_slot(env, plan, read_slot(move.source)))
                || !plan_live_matches(plan, expected_before.at(move.destination), plan_live_slot(env, plan, read_slot(move.destination)))){
                plan_error(env, "Affected slots changed before pickup. Move was not started.");
            }
        }
        checkpoint("in_flight");
        cursor = move_cursor_to(env, context, cursor, BoxCursor(move.source), game_delay, &timing);
        pbf_press_button(context, BUTTON_Y, 80ms, std::max(576ms, game_delay + 240ms));
        context.wait_for_all_requests();
        cursor = move_cursor_to(env, context, cursor, BoxCursor(move.destination), game_delay, &timing, true);
        pbf_press_button(context, BUTTON_Y, 80ms, std::max(576ms, game_delay + 240ms));
        context.wait_for_all_requests();
        BoxViewWatcher unheld(&env.console.overlay());
        if (wait_until(env.console, context, Seconds(5), {unheld}) != 0){
            plan_error(env, "Placement did not return to ordinary box view; move remains in flight.");
        }
        auto next = state;
        const auto item = next.at(move.source);
        next.replace(move.source, next.at(move.destination));
        next.replace(move.destination, item);
        const auto expected = plan_expected_live(env, plan, next);
        if (!plan_live_matches(plan, expected.at(move.source), plan_live_slot(env, plan, read_slot(move.source)))
            || !plan_live_matches(plan, expected.at(move.destination), plan_live_slot(env, plan, read_slot(move.destination)))){
            plan_error(env, "Affected slots do not match the planned move. Checkpoint remains in flight.");
        }
        state = next;
        completed++;
        checkpoint(completed == plan.moves.size() ? "complete" : "ready");
        if (move_verified) move_verified();
        env.log("HOME_PLAN move=verified completed_moves=" + std::to_string(completed));
    }
    env.log("HOME_PLAN result=" + std::string(completed == plan.moves.size() ? "complete" : "paused_unheld")
        + " completed_moves=" + std::to_string(completed));
}
}
