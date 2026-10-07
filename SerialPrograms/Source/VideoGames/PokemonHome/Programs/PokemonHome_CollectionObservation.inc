// Collection-only observations. No selection, placement, marking or release inputs.
static QJsonObject home_form_observation(const CollectedPokemonInfo& info, const HomeBoxVisualEvidence* visual_evidence){
    static const QByteArray bytes = []{
        QFile file(QString::fromStdString(RESOURCE_PATH() + "Pokemon/HomeCollectionReference.json"));
        if (!file.open(QIODevice::ReadOnly)) return QByteArray();
        return file.readAll();
    }();
    static const QString hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    static const bool valid = hash == HOME_COLLECTION_REFERENCE_SHA256;
    static const std::vector<HomeReferenceForm> forms = []{
        std::vector<HomeReferenceForm> result;
        if (!valid) return result;
        const auto document = QJsonDocument::fromJson(bytes).object();
        if (document.value("kind").toString() != "home_reference_catalog" || document.value("version").toInt() != 1) return result;
        for (const auto& value : document.value("forms").toArray()){
            const auto entry = value.toObject();
            HomeReferenceForm form;
            form.key = entry.value("key").toString().toStdString();
            form.species = entry.value("species").toString().toStdString();
            form.gender = entry.value("gender").toString().toStdString();
            form.dex = entry.value("dex").toInt();
            if (entry.value("home_available").isBool()) form.home_available = entry.value("home_available").toBool() ? 1 : -1;
            for (const auto& type : entry.value("types").toArray()) form.types.push_back(type.toString().toStdString());
            for (const auto& ability : entry.value("abilities").toArray()) form.abilities.push_back(ability.toString().toStdString());
            const auto visual = entry.value("visual_templates").toObject();
            form.visual_normal = visual.value("normal").toString().toStdString();
            form.visual_shiny = visual.value("shiny").toString().toStdString();
            result.push_back(std::move(form));
        }
        return result;
    }();
    QJsonObject result{{"key", QJsonValue::Null}, {"status", "unknown"},
        {"resource", "Pokemon/HomeCollectionReference.json"}, {"resource_sha256", hash},
        {"evidence", "reference_types_and_gender"}};
    if (!valid || forms.empty()){
        result.insert("reason", "reference_missing_or_checksum_mismatch");
        return result;
    }
    std::vector<std::string> types{POKEMON_TYPE_SLUGS().get_string(info.primary_type)};
    if (info.secondary_type != PokemonType::NONE) types.push_back(POKEMON_TYPE_SLUGS().get_string(info.secondary_type));
    const auto match = match_home_reference_form(forms, info.dex_number, info.name_slug,
        types, gender_to_string(info.gender), info.gmax, info.ability);
    if (match.ability_used) result.insert("evidence", "reference_types_gender_and_ability");
    QJsonArray candidates;
    for (const auto& key : match.candidates) candidates.append(QString::fromStdString(key));
    result.insert("candidates", candidates);
    if (!match.key.empty()){
        result.insert("key", QString::fromStdString(match.key));
        result.insert("status", "candidate_requires_review");
    }else result.insert("reason", QString::fromStdString(match.reason));
    if (match.reason != "ambiguous_visible_fields") return result;
    QJsonObject visual{{"status", "unknown"}, {"key", QJsonValue::Null},
        {"policy_version", 1}, {"identity_auto_approved", false},
        {"render", info.shiny ? "shiny" : "normal"},
        {"reference_resource_sha256", hash}, {"independent_frames", false}};
    auto unknown = [&](const char* reason){
        visual.insert("reason", reason); result.insert("visual", visual); return result;
    };
    if (!visual_evidence || !visual_evidence->first || !visual_evidence->second
        || !visual_evidence->independent_frames) return unknown("independent_box_frames_unavailable");
    visual.insert("independent_frames", true);
    static const bool assets_valid = []{
        const auto provenance = QJsonDocument::fromJson(bytes).object().value("provenance").toObject()
            .value("visual_assets").toObject().value("files").toObject();
        for (const char* name : {"AllHomeSprites.json", "AllHomeSprites.png"}){
            QFile file(QString::fromStdString(RESOURCE_PATH() + "Pokemon/" + name));
            if (!file.open(QIODevice::ReadOnly)) return false;
            const auto expected = provenance.value(name).toObject().value("sha256").toString();
            const auto actual = QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
            if (expected.isEmpty() || actual != expected) return false;
        }
        return true;
    }();
    if (!assets_valid) return unknown("visual_resource_missing_or_checksum_mismatch");
    std::vector<HomeVisualTemplate> templates;
    for (const auto& key : match.candidates){
        const auto it = std::find_if(forms.begin(), forms.end(), [&](const auto& form){return form.key == key;});
        if (it == forms.end()) return unknown("templates_missing");
        const auto& slug = info.shiny ? it->visual_shiny : it->visual_normal;
        if (slug.empty()) return unknown("templates_missing");
        const auto* sprite = Pokemon::ALL_POKEMON_HOME_SPRITES().get_nothrow(slug);
        if (!sprite || sprite->sprite.width() != 64 || sprite->sprite.height() != 64) return unknown("templates_missing");
        const auto& image = sprite->sprite;
        templates.push_back({key, home_visual_template({image.data(), image.width(), image.height(), image.bytes_per_row() / 4})});
    }
    visual.insert("templates_complete", true);
    auto evaluate = [&](const ImageViewRGB32& frame){
        return match_home_visual_icon({frame.data(), frame.width(), frame.height(), frame.bytes_per_row() / 4},
            visual_evidence->row, visual_evidence->column, templates);
    };
    const auto first = evaluate(*visual_evidence->first), second = evaluate(*visual_evidence->second);
    auto serialize = [](const HomeVisualMatch& value){
        QJsonArray scores;
        for (const auto& score : value.scores) scores.append(QJsonObject{{"key", QString::fromStdString(score.key)},
            {"distance", score.distance}, {"overlap", score.overlap}});
        return QJsonObject{{"key", QString::fromStdString(value.key)}, {"reason", QString::fromStdString(value.reason)},
            {"margin", value.margin}, {"scores", scores}};
    };
    visual.insert("frames", QJsonArray{serialize(first), serialize(second)});
    const auto key = agree_home_visual_frames(first, second, visual_evidence->independent_frames);
    if (key.empty()) return unknown("visual_frames_uncertain_or_disagree");
    visual.insert("key", QString::fromStdString(key));
    visual.insert("status", "candidate_requires_review");
    visual.insert("frames_agree", true);
    result.insert("visual", visual);
    result.insert("key", QString::fromStdString(key));
    result.insert("status", "candidate_requires_review");
    result.insert("evidence", "reference_fields_and_two_box_icons");
    result.remove("reason");
    return result;
}

static void confirm_home_form_fields(SingleSwitchProgramEnvironment& env, ProControllerContext& context,
    const CollectedPokemonInfo& info, Language language){
    context.wait_for_all_requests();
    const auto frame = env.console.video().snapshot();
    SummaryScreenDetector summary;
    if (!frame || !summary.detect(frame)){
        throw UserSetupError(env.console, "HOME collection summary disappeared before identity confirmation.");
    }
    SummaryReader reader;
    GigantamaxDetector gmax(COLOR_RED, &env.console.overlay());
    ShinyDetector shiny(COLOR_RED, &env.console.overlay());
    const auto types = read_pokemon_types(frame, ImageFloatBox{.615, .240, .071, .057}, PokemonTypeGeneration::GEN9);
    if (reader.read_national_dex(env.console, frame) != info.dex_number
        || types.first != info.primary_type || types.second != info.secondary_type
        || BoxGenderDetector::detect(frame) != info.gender || gmax.detect(frame) != info.gmax
        || shiny.detect(frame) != info.shiny){
        throw UserSetupError(env.console, "HOME collection identity fields disagreed across frames. Stopped before advancing; no form identity was approved.");
    }
    // These reviewed families use ability as identity evidence. Empty ability
    // observations remain unresolved; readable ones must agree on a second frame.
    if ((info.dex_number == 658 || info.dex_number == 718 || info.dex_number == 744)
        && !info.ability.empty()
        && reader.read_ability(env.console, language, frame) != info.ability){
        throw UserSetupError(env.console, "HOME collection ability disagreed across frames. Stopped before advancing; no form identity was approved.");
    }
}

static std::string home_judge_cycle_hint(const ImageViewRGB32& frame){
    const auto crop = to_blackwhite_rgb32_range(
        extract_box_reference(frame, ImageFloatBox{.69, .958, .145, .039}),
        true, 0xff000000, 0xff828282);
    return OCR::tesseract_ocr_read(Language::English, crop, OCR::PageSegMode::SINGLE_LINE);
}
static HomeJudgeCycleState home_judge_cycle_state(const ImageViewRGB32& frame){
    const auto hint = home_judge_cycle_hint(frame);
    std::string title;
    if (hint.find("View Stats") != std::string::npos){
        const auto crop = to_blackwhite_rgb32_range(
            extract_box_reference(frame, ImageFloatBox{.045, .176, .155, .039}),
            true, 0xffc0c0c0, 0xffffffff);
        title = OCR::tesseract_ocr_read(Language::English, crop, OCR::PageSegMode::SINGLE_LINE);
    }
    return parse_home_judge_cycle_hint(hint, title);
}
class HomeJudgeCycleWatcher : public VisualInferenceCallback{
public:
    explicit HomeJudgeCycleWatcher(HomeJudgeCycleState expected)
        : VisualInferenceCallback("HomeJudgeCycleWatcher"), m_expected(expected){}
    void make_overlays(VideoOverlaySet&) const override{}
    bool process_frame(const ImageViewRGB32& frame, WallClock) override{
        SummaryScreenDetector summary;
        const bool matched = frame && summary.detect(frame)
            && home_judge_cycle_state(frame) == m_expected;
        m_matches = matched ? m_matches + 1 : 0;
        return m_matches >= 2;
    }
private:
    HomeJudgeCycleState m_expected;
    size_t m_matches = 0;
};
static bool cycle_home_summary_verified(SingleSwitchProgramEnvironment& env, ProControllerContext& context,
    HomeJudgeCycleState expected){
    const auto started = std::chrono::steady_clock::now();
    pbf_press_button(context, BUTTON_Y, 80ms, 100ms);
    context.wait_for_all_requests();
    const auto queued = std::chrono::steady_clock::now();
    HomeJudgeCycleWatcher changed(expected);
    const bool confirmed = wait_until(env.console, context, 3s, {changed}) == 0;
    env.console.log("HOME_COLLECTION_TIMING action=summary_display_cycle queue_ms="
        + std::to_string(std::chrono::duration_cast<Milliseconds>(queued - started).count())
        + " total_ms=" + std::to_string(std::chrono::duration_cast<Milliseconds>(std::chrono::steady_clock::now() - started).count())
        + " result=" + (confirmed ? "confirmed" : "unreadable"));
    return confirmed;
}

class HomeNumericSummaryWatcher : public VisualInferenceCallback{
public:
    HomeNumericSummaryWatcher() : VisualInferenceCallback("HomeNumericSummaryWatcher"){}
    void make_overlays(VideoOverlaySet&) const override{}
    bool process_frame(const ImageViewRGB32& frame, WallClock) override{
        SummaryScreenDetector summary;
        SummaryReader reader;
        const bool matched = frame && summary.detect(frame) && !reader.read_language_of_origin(frame).empty();
        m_matches = matched ? m_matches + 1 : 0;
        return m_matches >= 2;
    }
private:
    size_t m_matches = 0;
};
static void ensure_home_numeric_summary(SingleSwitchProgramEnvironment& env, ProControllerContext& context){
    for (size_t attempt = 0; attempt < 3; ++attempt){
        const auto frame = env.console.video().snapshot();
        SummaryScreenDetector summary;
        if (!summary.detect(frame)) break;
        const auto state = home_judge_cycle_state(frame);
        if (state == HomeJudgeCycleState::JUDGE){
            const auto hint = home_judge_cycle_hint(frame);
            const auto next = hint.find("Condition") != std::string::npos
                ? HomeJudgeCycleState::CONDITION : HomeJudgeCycleState::NUMERIC;
            if (!cycle_home_summary_verified(env, context, next)) break;
            if (next == HomeJudgeCycleState::CONDITION) continue;
        }else if (state == HomeJudgeCycleState::CONDITION){
            if (!cycle_home_summary_verified(env, context, HomeJudgeCycleState::NUMERIC)) break;
        }else if (state == HomeJudgeCycleState::BASE_POINTS){
            if (!cycle_home_summary_verified(env, context, HomeJudgeCycleState::JUDGE)) break;
            continue;
        }
        HomeNumericSummaryWatcher numeric;
        if (wait_until(env.console, context, 1500ms, {numeric}) == 0) return;
        break;
    }
    throw UserSetupError(env.console, "Cannot confirm normal HOME summary view. Collection scanning stopped before advancing.");
}

class HomeJudgeScreenWatcher : public VisualInferenceCallback{
public:
    HomeJudgeScreenWatcher(Logger& logger, uint16_t dex)
        : VisualInferenceCallback("HomeJudgeScreenWatcher"), m_logger(logger), m_dex(dex){}
    void make_overlays(VideoOverlaySet&) const override{}
    bool process_frame(const ImageViewRGB32& frame, WallClock) override{
        SummaryScreenDetector summary;
        if (!frame || !summary.detect(frame)){matches = 0; return false;}
        const auto title = to_blackwhite_rgb32_range(
            extract_box_reference(frame, ImageFloatBox{.073, .176, .093, .039}),
            true, 0xffc0c0c0, 0xffffffff);
        raw_title = OCR::tesseract_ocr_read(Language::English, title, OCR::PageSegMode::SINGLE_LINE);
        if (raw_title.find("Judge") == std::string::npos){matches = 0; return false;}
        // This is a second species check while reading quality, not a claim
        // that species alone identifies duplicate individuals.
        SummaryReader reader;
        observed_dex = reader.read_national_dex(m_logger, frame);
        if (observed_dex != m_dex){
            matches = 0; return false;
        }
        return ++matches >= 2;
    }
    std::string raw_title;
    int observed_dex = -1;
    size_t matches = 0;
private:
    Logger& m_logger;
    uint16_t m_dex;
};

static QJsonObject read_home_collection_observation(
    SingleSwitchProgramEnvironment& env, ProControllerContext& context,
    const CollectedPokemonInfo& info, Language language, const HomeBoxVisualEvidence* visual_evidence,
    bool identity_only = false
){
    confirm_home_form_fields(env, context, info, language);
    QJsonObject form = home_form_observation(info, visual_evidence);
    form.insert("fields_confirmed", true);
    QJsonObject observation{{"version", 1}, {"form", form},
        {"visual_capture", QJsonObject{{"independent_frames", visual_evidence && visual_evidence->independent_frames},
                                        {"basis", "physical_box_grid"}}},
        {"judge", QJsonObject{{"status", "unknown"}}}, {"ivs", QJsonValue::Null}};
    if (identity_only) return observation;
    if (form.contains("visual")){
        const auto visual = form.value("visual").toObject();
        env.log("HOME_FORM_VISUAL dex=" + std::to_string(info.dex_number)
            + " status=" + visual.value("status").toString().toStdString()
            + " reason=" + visual.value("reason").toString().toStdString()
            + " identity_reviewed=false");
    }
    if (language != Language::English){
        observation.insert("judge", QJsonObject{{"status", "unsupported_language"}});
        return observation;
    }
    bool ready = false;
    for (size_t attempt = 0; attempt < 3; ++attempt){
        const auto frame = env.console.video().snapshot();
        SummaryScreenDetector summary;
        if (!summary.detect(frame)) break;
        const auto hint = home_judge_cycle_hint(frame);
        const auto state = home_judge_cycle_state(frame);
        if (state == HomeJudgeCycleState::JUDGE){
            HomeJudgeScreenWatcher judge(env.console, info.dex_number);
            ready = wait_until(env.console, context, 1500ms, {judge}) == 0;
            if (!ready){
                env.console.log("HOME_COLLECTION_QUALITY judge_check=unreadable title_ocr=" + judge.raw_title
                    + " expected_dex=" + std::to_string(info.dex_number) + " observed_dex=" + std::to_string(judge.observed_dex));
            }
            break;
        }
        if (state == HomeJudgeCycleState::UNKNOWN){
            // Preserve only failed/ambiguous views so unsupported game panels
            // can be distinguished from OCR failures without repeated full scans.
            const std::string diagnostic = "Screenshots/home-judge-unavailable-"
                + std::to_string(info.dex_number) + "-" + std::to_string(std::chrono::duration_cast<Milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count()) + ".png";
            if (frame && frame->save(diagnostic)){
                env.log("HOME_COLLECTION_QUALITY diagnostic_capture=" + diagnostic);
            }
            observation.insert("judge", QJsonObject{{"status", "unavailable_or_unreadable"},
                {"reason", "cycle_control_unreadable_or_absent"}, {"cycle_control_raw", QString::fromStdString(hint)}});
            ensure_home_numeric_summary(env, context);
            env.console.log("HOME_COLLECTION_QUALITY judge=unavailable_or_unreadable cycle_control=unreadable_or_absent");
            return observation;
        }
        const auto next = state == HomeJudgeCycleState::NUMERIC ? HomeJudgeCycleState::BASE_POINTS : HomeJudgeCycleState::JUDGE;
        if (!cycle_home_summary_verified(env, context, next)) break;
    }
    if (!ready){
        ensure_home_numeric_summary(env, context);
        observation.insert("judge", QJsonObject{{"status", "unavailable_or_unreadable"}});
        env.console.log("HOME_COLLECTION_QUALITY judge=unavailable_or_unreadable");
        return observation;
    }
    const char* names[] = {"hp", "attack", "defense", "special_attack", "special_defense", "speed"};
    const ImageFloatBox boxes[] = {
        {.137, .272, .126, .035}, {.260, .384, .133, .036}, {.260, .618, .133, .037},
        {.011, .384, .128, .036}, {.011, .618, .128, .037}, {.139, .644, .130, .038}
    };
    // Read independent captured frames. Disagreement and unknown labels are
    // retained as unreadable, never fuzzy-matched to an attractive IV value.
    std::array<std::array<std::string, 6>, 2> raw;
    for (size_t pass = 0; pass < 2; ++pass){
        if (pass) context.wait_for(160ms);
        const auto frame = env.console.video().snapshot();
        for (size_t stat = 0; stat < 6; ++stat){
            const auto crop = to_blackwhite_rgb32_range(
                extract_box_reference(frame, boxes[stat]), true, 0xff000000, 0xff828282);
            const auto enlarged = crop.scale_to(crop.width() * 2, crop.height() * 2);
            raw[pass][stat] = OCR::tesseract_ocr_read(Language::English, enlarged, OCR::PageSegMode::SINGLE_LINE);
        }
    }
    QJsonObject stats, exact;
    size_t read = 0;
    for (size_t stat = 0; stat < 6; ++stat){
        const auto first = parse_home_judge_rating(raw[0][stat]);
        const auto second = parse_home_judge_rating(raw[1][stat]);
        const bool confirmed = !first.label.empty() && first.label == second.label;
        QJsonObject value{{"raw", QString::fromStdString(raw[0][stat])},
            {"raw_confirmation", QString::fromStdString(raw[1][stat])}, {"status", confirmed ? "read" : "unreadable"},
            {"rating", QJsonValue::Null}, {"iv_min", QJsonValue::Null}, {"iv_max", QJsonValue::Null},
            {"hyper_trained", QJsonValue::Null}};
        exact.insert(names[stat], QJsonValue::Null);
        if (confirmed){
            ++read;
            value.insert("rating", QString::fromStdString(first.label));
            value.insert("hyper_trained", first.hyper_trained);
            if (first.minimum) value.insert("iv_min", *first.minimum);
            if (first.maximum) value.insert("iv_max", *first.maximum);
            if (first.exact()) exact.insert(names[stat], *first.exact());
        }
        if (!confirmed){
            env.console.log("HOME_COLLECTION_QUALITY unreadable_stat=" + std::string(names[stat])
                + " ocr_first=" + raw[0][stat] + " ocr_second=" + raw[1][stat]);
        }
        stats.insert(names[stat], value);
    }
    observation.insert("judge", QJsonObject{{"status", read == 6 ? "read" : "partial"},
        {"screen", "HOME_summary_Judge"}, {"language", "English"}, {"stats", stats}});
    observation.insert("ivs", exact);
    // Restore the language badge and numeric panel before the next reader.
    ensure_home_numeric_summary(env, context);
    env.console.log("HOME_COLLECTION_QUALITY judge_stats=" + std::to_string(read) + "/6 form="
        + observation.value("form").toObject().value("status").toString().toStdString());
    return observation;
}
