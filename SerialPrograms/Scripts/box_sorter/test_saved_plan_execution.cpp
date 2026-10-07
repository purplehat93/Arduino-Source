// Standalone Qt Core test of the production executor with a modeled console.
// This verifies checkpoint safety; it does not validate visual detectors.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSaveFile>
#include <QTemporaryDir>
#include "../../Source/VideoGames/PokemonHome/Programs/PokemonHome_CollectionExecutionPolicy.h"
using namespace PokemonAutomation::NintendoSwitch::PokemonHome;

using namespace std::chrono_literals;
using Milliseconds = std::chrono::milliseconds;
using Seconds = std::chrono::seconds;
struct FakeConsole{int value = 0; int& overlay(){return value;}};
struct SingleSwitchProgramEnvironment{
    FakeConsole console;
    std::vector<std::string> logs;
    void log(std::string text){logs.push_back(text);}
};
struct UserSetupError : std::runtime_error{
    UserSetupError(FakeConsole&, const std::string& text) : std::runtime_error(text){}
};
struct BoxCursor{
    size_t box, row, column;
    BoxCursor(size_t index) : box(index / 30), row(index / 6 % 5), column(index % 6){}
    BoxCursor(size_t b, size_t r, size_t c) : box(b), row(r), column(c){}
};
struct BoxNavigationTiming{};
struct CollectedPokemonInfo{};
struct ProControllerContext{
    QJsonArray observed;
    size_t cursor = 0, picked = 0;
    unsigned pickups = 0;
    bool held = false, refuse_placement = false;
    void wait_for_all_requests(){}
};
constexpr int BUTTON_Y = 1;
void pbf_press_button(ProControllerContext& context, int, Milliseconds, Milliseconds){
    if (!context.held){context.picked = context.cursor; context.held = true; context.pickups++;}
    else if (!context.refuse_placement){
        auto item = context.observed.at(context.picked);
        context.observed.replace(context.picked, context.observed.at(context.cursor));
        context.observed.replace(context.cursor, item);
        context.held = false;
    }
}
BoxCursor move_cursor_to(SingleSwitchProgramEnvironment&, ProControllerContext& context,
    BoxCursor, BoxCursor target, Milliseconds, BoxNavigationTiming*, bool = false){
    context.cursor = target.box * 30 + target.row * 6 + target.column;
    return target;
}
struct BoxViewWatcher{explicit BoxViewWatcher(int*){}};
int wait_until(FakeConsole&, ProControllerContext& context, Seconds, std::initializer_list<BoxViewWatcher>){return context.held ? -1 : 0;}
void save_boxes_data_to_json(std::vector<std::optional<CollectedPokemonInfo>>, const std::string&){
    throw std::runtime_error("Unused export stub");
}
#include "../../Source/VideoGames/PokemonHome/Programs/PokemonHome_SavedPlanExecution.inc"

void check(bool condition, const char* message){if (!condition) throw std::runtime_error(message);}
template<class F> void rejected(F function){
    bool failed = false;
    try{function();}catch(const UserSetupError&){failed = true;}
    check(failed, "Expected fail-closed rejection");
}
void write_json(const std::string& path, const QJsonObject& object){
    QFile file(QString::fromStdString(path));
    check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Open test fixture");
    auto bytes = QJsonDocument(object).toJson();
    check(file.write(bytes) == bytes.size(), "Write fixture");
}
QJsonObject fixture(bool identical = false){
    QJsonArray layout{"a", "b"};
    while(layout.size() < 30) layout.append(QJsonValue::Null);
    QJsonObject first{{"name", "alpha"}, {"dex", 1}};
    QJsonObject second = identical ? first : QJsonObject{{"name", "beta"}, {"dex", 2}};
    QJsonObject range{{"physical_start_box", 31}, {"physical_end_box", 31}, {"total_boxes", 200}, {"box_count", 1}};
    auto context = range;
    context.insert("version", 1); context.insert("scan_id", "test");
    context.insert("coordinate_basis", "zero_based_relative_to_physical_start_box");
    context.insert("box_identity", "physical_counter_not_editable_label");
    QJsonObject catalog{{"a", first}, {"b", second}};
    SavedHomePlan plan;
    plan.source = layout; plan.catalog = catalog; plan.physical_range = range;
    return QJsonObject{{"version", 1},
        {"inventory", QJsonObject{{"version", 1}, {"slots", layout}, {"pokemon", catalog}, {"scan_context", context}}},
        {"target", layout}, {"source_validation", QJsonObject{{"version", 1}, {"physical_range", range}, {"layout_sha256", plan_layout_hash(plan, layout)}}},
        {"moves", QJsonArray{
            QJsonObject{{"source", 0}, {"destination", 1}, {"expected_source", "a"}, {"expected_destination", "b"}},
            QJsonObject{{"source", 1}, {"destination", 0}, {"expected_source", "a"}, {"expected_destination", "b"}}}}};
}

QJsonObject collection_fixture(){
    auto root = fixture();
    auto inventory = root.value("inventory").toObject();
    auto catalog = inventory.value("pokemon").toObject();
    QJsonObject form_keys;
    for (const auto& id : catalog.keys()){
        auto record = catalog.value(id).toObject();
        record.insert("shiny", false); record.insert("gmax", false);
        const auto key = id + "-form"; form_keys.insert(id, key);
        const QJsonObject form{{"key", key}, {"candidates", QJsonArray{key}}, {"fields_confirmed", true},
            {"status", "candidate_requires_review"}, {"evidence", "reference_types_and_gender"},
            {"resource", "Pokemon/HomeCollectionReference.json"}, {"resource_sha256", QString(64, '0')}};
        record.insert("collection_observation", QJsonObject{{"version", 1}, {"form", form}, {"judge", "old capture"}});
        catalog.insert(id, record);
    }
    inventory.insert("pokemon", catalog); root.insert("inventory", inventory);
    auto validation = root.value("source_validation").toObject();
    SavedHomePlan source; source.catalog = catalog; source.source = inventory.value("slots").toArray();
    source.physical_range = validation.value("physical_range").toObject();
    validation.insert("layout_sha256", plan_layout_hash(source, source.source)); root.insert("source_validation", validation);
    root.insert("execution_blocked", false); root.insert("collection_review", QJsonObject{{"release_enabled", false}});
    root.insert("collection_execution", QJsonObject{{"version", 1}, {"mode", "metadata_form_v1"},
        {"reference_sha256", HOME_COLLECTION_REFERENCE_SHA256}, {"form_keys", form_keys},
        {"quality_annotations_verified", false}, {"release_enabled", false}, {"stationary_review_occurrences", QJsonArray{}}});
    return root;
}

QJsonObject species_fixture(bool twins = false){
    auto root = collection_fixture();root.remove("collection_review");
    auto inventory = root.value("inventory").toObject();auto catalog = inventory.value("pokemon").toObject();
    QJsonObject requirements;
    for (const auto& id : catalog.keys()){
        auto record = catalog.value(id).toObject();record.insert("dex", id == "a" ? 2 : 1);
        auto observation = record.value("collection_observation").toObject();auto form = observation.value("form").toObject();
        form.insert("key", QJsonValue::Null);form.insert("status", "unknown");observation.insert("form", form);record.insert("collection_observation", observation);
        catalog.insert(id, record);
    }
    if (twins) catalog.insert("b", catalog.value("a"));
    for (const auto& id : catalog.keys()){
        const auto record=catalog.value(id).toObject();requirements.insert(id,QJsonObject{{"dex",record.value("dex")},{"name",record.value("name")}});
    }
    inventory.insert("pokemon",catalog);root.insert("inventory",inventory);
    auto target=inventory.value("slots").toArray();if(!twins){target[0]="b";target[1]="a";}
    root.insert("target",target);root.insert("moves",twins?QJsonArray{}:QJsonArray{QJsonObject{{"source",0},{"destination",1},{"expected_source","a"},{"expected_destination","b"}}});
    auto contract=root.value("collection_execution").toObject();contract.remove("form_keys");contract.insert("mode","metadata_species_v1");
    contract.insert("ordering","national_dex_species");contract.insert("copies","keep_all");contract.insert("species_requirements",requirements);
    contract.insert("stationary_review_occurrences",twins?QJsonArray{"a","b"}:QJsonArray{});root.insert("collection_execution",contract);
    SavedHomePlan source;source.source=inventory.value("slots").toArray();source.catalog=catalog;source.physical_range=root.value("source_validation").toObject().value("physical_range").toObject();
    auto validation=root.value("source_validation").toObject();validation.insert("layout_sha256",plan_layout_hash(source,source.source));root.insert("source_validation",validation);return root;
}
void test_species_execution(const std::string& path){
    SingleSwitchProgramEnvironment env;auto root=species_fixture();write_json(path,root);auto plan=load_saved_home_plan(env,path);
    check(plan.collection_species&&!plan.collection_judge,"Species mode was not selected");
    ProControllerContext context;context.observed=plan_observed(plan,plan.source);BoxCursor cursor(0);BoxNavigationTiming timing;
    auto execute=[&](bool resume,bool corrupt){
        execute_saved_home_plan(env,context,plan,path,{},cursor,timing,240ms,false,resume,1,[&](size_t index)->QJsonValue{
            auto value=context.observed.at(index);if(corrupt&&!value.isNull()){auto r=value.toObject();r.insert("name","wrong-species");return r;}return value;
        },{},false);
    };
    rejected([&]{execute(false,true);});check(context.pickups==0,"Species mismatch caused pickup");
    execute(true,false);check(context.pickups==1,"Species move failed");execute(true,false);check(context.pickups==1,"Completed species resume repeated a move");
    auto invalid=root;auto contract=invalid.value("collection_execution").toObject();contract.insert("copies","discard_extras");invalid.insert("collection_execution",contract);
    write_json(path,invalid);rejected([&]{load_saved_home_plan(env,path);});
    invalid=root;invalid.insert("collection_review",QJsonObject{});write_json(path,invalid);rejected([&]{load_saved_home_plan(env,path);});
    invalid=root;invalid.insert("target",root.value("inventory").toObject().value("slots"));invalid.insert("moves",QJsonArray{});
    write_json(path,invalid);rejected([&]{load_saved_home_plan(env,path);});
    root=species_fixture(true);write_json(path,root);plan=load_saved_home_plan(env,path);check(plan.stationary_review_occurrences.size()==2,"Metadata twins were not stationary");
    contract=root.value("collection_execution").toObject();contract.insert("stationary_review_occurrences",QJsonArray{});root.insert("collection_execution",contract);
    write_json(path,root);rejected([&]{load_saved_home_plan(env,path);});
    std::cout<<"PASS: species ordering with unknown forms/IVs, mismatch before pickup, resume without rescan, stationary twins and contract rejection.\n";
}


void test_species_tracked_copies(const std::string& path){
    auto root=species_fixture();auto inventory=root.value("inventory").toObject();
    const auto templates=inventory.value("pokemon").toObject();QJsonObject catalog,requirements;QJsonArray source,target,moves;
    for(int i=0;i<30;i++){
        const auto id=QString("copy-%1").arg(i);const auto record=templates.value(i<15?"a":"b");
        catalog.insert(id,record);requirements.insert(id,QJsonObject{{"dex",record.toObject().value("dex")},{"name",record.toObject().value("name")}});
        source.append(id);target.append(QString("copy-%1").arg((i+15)%30));
    }
    for(int i=0;i<15;i++)moves.append(QJsonObject{{"source",i},{"destination",i+15},{"expected_source",source.at(i)},{"expected_destination",source.at(i+15)}});
    inventory.insert("slots",source);inventory.insert("pokemon",catalog);root.insert("inventory",inventory);root.insert("target",target);root.insert("moves",moves);
    auto contract=root.value("collection_execution").toObject();contract.insert("mode","metadata_species_v2");contract.insert("duplicate_policy","tracked_slot_history");contract.insert("species_requirements",requirements);root.insert("collection_execution",contract);
    SavedHomePlan initial;initial.source=source;initial.catalog=catalog;initial.physical_range=root.value("source_validation").toObject().value("physical_range").toObject();
    auto validation=root.value("source_validation").toObject();validation.insert("layout_sha256",plan_layout_hash(initial,source));root.insert("source_validation",validation);
    SingleSwitchProgramEnvironment env;write_json(path,root);auto plan=load_saved_home_plan(env,path);
    check(plan.stationary_review_occurrences.isEmpty(),"Tracked copies were frozen");
    ProControllerContext context;context.observed=plan_observed(plan,source);BoxCursor cursor(0);BoxNavigationTiming timing;int reads=0;
    // Simulate an interruption after physical placement, before journal commit.
    rejected([&]{execute_saved_home_plan(env,context,plan,path,{},cursor,timing,240ms,false,false,1,[&](size_t i)->QJsonValue{
        if(++reads==3)throw UserSetupError(env.console,"Interrupted after placement");return context.observed.at(i);
    },{},false);});
    check(context.pickups==1&&!context.held,"Interrupted twin move did not place correctly");
    execute_saved_home_plan(env,context,plan,path,{},cursor,timing,240ms,false,true,0,[&](size_t i){return context.observed.at(i);},{},false);
    check(context.pickups==15,"Resume repeated an already placed copy or skipped a swap");
    check(context.observed==plan_observed(plan,target),"Full box twin layout was not sorted");
    execute_saved_home_plan(env,context,plan,path,{},cursor,timing,240ms,false,true,0,[&](size_t i){return context.observed.at(i);},{},false);
    check(context.pickups==15,"Completed full-box resume moved copies again");
    auto invalid=root;auto ambiguous=moves;ambiguous.prepend(QJsonObject{{"source",0},{"destination",1},{"expected_source",source.at(0)},{"expected_destination",source.at(1)}});
    invalid.insert("moves",ambiguous);write_json(path,invalid);rejected([&]{load_saved_home_plan(env,path);});
    invalid=root;contract.remove("duplicate_policy");invalid.insert("collection_execution",contract);write_json(path,invalid);rejected([&]{load_saved_home_plan(env,path);});
    std::cout<<"PASS: all 30 slots occupied, metadata twins moved, interruption after placement reconciled, completed resume and ambiguous-swap rejection.\n";
}

QJsonObject judge_evidence(const char* rating){
    const auto parsed = parse_home_judge_rating(rating);
    QJsonObject stats, ivs;
    for (const auto* name : {"hp", "attack", "defense", "special_attack", "special_defense", "speed"}){
        stats.insert(name, QJsonObject{{"status", "read"}, {"raw", rating}, {"raw_confirmation", rating}, {"rating", rating},
            {"iv_min", parsed.minimum ? QJsonValue(*parsed.minimum) : QJsonValue(QJsonValue::Null)},
            {"iv_max", parsed.maximum ? QJsonValue(*parsed.maximum) : QJsonValue(QJsonValue::Null)},
            {"hyper_trained", parsed.hyper_trained}});
        ivs.insert(name, parsed.exact() ? QJsonValue(*parsed.exact()) : QJsonValue(QJsonValue::Null));
    }
    return QJsonObject{{"judge", QJsonObject{{"status", "read"}, {"stats", stats}}}, {"ivs", ivs}};
}

QJsonObject with_judge(QJsonObject record, const char* rating){
    auto observation = record.value("collection_observation").toObject();
    const auto evidence = judge_evidence(rating);
    observation.insert("judge", evidence.value("judge")); observation.insert("ivs", evidence.value("ivs"));
    record.insert("collection_observation", observation); return record;
}

QJsonObject quality_fixture(){
    auto root = collection_fixture(); auto inventory = root.value("inventory").toObject();
    auto catalog = inventory.value("pokemon").toObject();
    auto first = with_judge(catalog.value("a").toObject(), "Hyper trained!");
    auto observation = first.value("collection_observation").toObject();
    auto form = observation.value("form").toObject(); form.insert("resource_sha256", HOME_COLLECTION_REFERENCE_SHA256);
    observation.insert("form", form); first.insert("collection_observation", observation);
    catalog.insert("a", first); catalog.insert("b", with_judge(first, "Best"));
    inventory.insert("pokemon", catalog); root.insert("inventory", inventory);
    auto contract = root.value("collection_execution").toObject(); contract.insert("mode", "metadata_form_judge_v1");
    contract.insert("form_keys", QJsonObject{{"a", "a-form"}, {"b", "a-form"}});
    contract.insert("judge_requirements", QJsonObject{{"a", *home_collection_judge_record(catalog.value("a").toObject())},
        {"b", *home_collection_judge_record(catalog.value("b").toObject())}});
    root.insert("collection_execution", contract);
    SavedHomePlan source; source.source = inventory.value("slots").toArray(); source.catalog = catalog;
    source.physical_range = root.value("source_validation").toObject().value("physical_range").toObject();
    auto validation = root.value("source_validation").toObject(); validation.insert("layout_sha256", plan_layout_hash(source, source.source));
    root.insert("source_validation", validation); return root;
}

void test_quality_execution(const std::string& path){
    SingleSwitchProgramEnvironment env; const auto root = quality_fixture(); write_json(path, root);
    const auto plan = load_saved_home_plan(env, path);
    check(plan.collection_judge && plan.stationary_review_occurrences.isEmpty(), "Distinct Judge ratings failed to distinguish copies");
    ProControllerContext context; context.observed = plan_observed(plan, plan.source);
    BoxCursor cursor(0); BoxNavigationTiming timing;
    auto execute = [&](bool resume, int corrupt){
        size_t reads = 0;
        execute_saved_home_plan(env, context, plan, path, context.observed, cursor, timing, 240ms, false, resume, 1,
            [&](size_t index)->QJsonValue{
                ++reads;
                if ((corrupt == 1 && reads == 1) || (corrupt == 2 && reads == 3)){
                    return with_judge(context.observed.at(index).toObject(), "No Good");
                }
                return context.observed.at(index);
            });
    };
    rejected([&]{execute(false, 1);}); check(context.pickups == 0, "Wrong IV precheck caused pickup");
    QFile::remove(QString::fromStdString(path + ".checkpoint.json"));
    execute(false, 0); execute(true, 0); execute(true, 0);
    check(context.pickups == 2 && !context.held, "Judge-aware bounded resume repeated or failed moves");
    QFile::remove(QString::fromStdString(path + ".checkpoint.json"));
    rejected([&]{execute(false, 2);});
    const auto checkpoint = QJsonDocument::fromJson(read_plan_bytes(env, path + ".checkpoint.json")).object();
    check(checkpoint.value("status") == "in_flight" && checkpoint.value("completed_moves") == 0,
        "Wrong IV postcheck committed progress");
    execute(true, 0); check(context.pickups == 4, "Judge-aware interrupted placement did not reconcile");
    auto expected = plan_live_slot(env, plan, plan.catalog.value("a"), true).toObject();
    auto mask = expected.value("collection_judge").toObject(); mask.insert("hp", QJsonValue::Null);
    expected.insert("collection_judge", mask); auto actual = expected; mask.insert("hp", QJsonObject{{"rating", "Best"}});
    actual.insert("collection_judge", mask);
    check(home_collection_record_matches(expected, actual), "Unknown review rating refused stronger fresh evidence");
    for (int defect = 0; defect < 3; ++defect){
        auto bad = plan.catalog.value("b").toObject(); auto observation = bad.value("collection_observation").toObject();
        auto judge = observation.value("judge").toObject(); auto stats = judge.value("stats").toObject(); auto hp = stats.value("hp").toObject();
        if (defect == 0) hp.insert("raw_confirmation", "8est");
        if (defect == 1) hp.insert("hyper_trained", true);
        if (defect == 2) hp.insert("iv_min", 30);
        stats.insert("hp", hp); judge.insert("stats", stats); observation.insert("judge", judge); bad.insert("collection_observation", observation);
        check(!home_collection_judge_record(bad), "Malformed Judge evidence was accepted");
    }
    std::cout << "PASS: source Judge requirements, IV-distinguished copies, pre/post IV refusal, bounded resume, interrupted reconciliation, unknown review masks, Hyper Training separation.\n";
}

void test_collection_execution(const std::string& path){
    SingleSwitchProgramEnvironment env;
    auto root = collection_fixture(); write_json(path, root);
    auto plan = load_saved_home_plan(env, path);
    ProControllerContext context; context.observed = plan_observed(plan, plan.source);
    for (qsizetype i = 0; i < context.observed.size(); ++i){
        if (context.observed.at(i).isNull()) continue;
        auto record = context.observed.at(i).toObject();
        auto observation = record.value("collection_observation").toObject();
        auto form = observation.value("form").toObject();
        form.insert("resource_sha256", HOME_COLLECTION_REFERENCE_SHA256); observation.insert("form", form);
        observation.insert("judge", "fresh OCR differs"); record.insert("collection_observation", observation);
        context.observed[i] = record;
    }
    BoxCursor cursor(0); BoxNavigationTiming timing;
    auto execute = [&](bool dry, bool resume, bool bad_precheck = false, bool bad_postcheck = false){
        size_t reads = 0;
        execute_saved_home_plan(env, context, plan, path, context.observed, cursor, timing, 240ms,
            dry, resume, 1, [&](size_t index){
                ++reads;
                return bad_precheck || (bad_postcheck && reads > 2) ? QJsonValue(QJsonValue::Null) : context.observed.at(index);
            });
    };
    execute(true, false); check(context.pickups == 0, "Collection verification picked up Pokemon");
    execute(false, false); execute(false, true);
    check(context.pickups == 2 && !context.held, "Collection bounded move/resume failed");
    execute(false, true); check(context.pickups == 2, "Completed collection plan repeated moves");
    QFile::remove(QString::fromStdString(path + ".checkpoint.json"));
    rejected([&]{execute(false, false, true);});
    check(context.pickups == 2, "Bad precheck caused collection pickup");
    const auto ready = QJsonDocument::fromJson(read_plan_bytes(env, path + ".checkpoint.json")).object();
    check(ready.value("status") == "ready", "Precheck marked an unstarted move in flight");
    QFile::remove(QString::fromStdString(path + ".checkpoint.json"));
    rejected([&]{execute(false, false, false, true);});
    check(context.pickups == 3, "Failed postcheck did not exercise placement");
    const auto uncertain = QJsonDocument::fromJson(read_plan_bytes(env, path + ".checkpoint.json")).object();
    check(uncertain.value("status") == "in_flight" && uncertain.value("completed_moves") == 0,
        "Failed collection postcheck committed a boundary");
    for (int test = 0; test < 4; ++test){
        auto invalid = root;
        if (test == 0) invalid.remove("collection_execution");
        if (test == 1){auto contract = invalid.value("collection_execution").toObject(); contract.insert("reference_sha256", "wrong"); invalid.insert("collection_execution", contract);}
        if (test == 2){auto contract = invalid.value("collection_execution").toObject(); contract.insert("quality_annotations_verified", true); invalid.insert("collection_execution", contract);}
        if (test == 3){auto contract = invalid.value("collection_execution").toObject(); auto keys = contract.value("form_keys").toObject(); keys.insert("a", "wrong"); contract.insert("form_keys", keys); invalid.insert("collection_execution", contract);}
        write_json(path, invalid); rejected([&]{load_saved_home_plan(env, path);});
    }
    std::cout << "PASS: collection contracts, historical source evidence, fresh Judge variance, bounded execution/resume, pre-pick refusal, uncertain placement, altered contract rejection.\n";
}


void test_resume_without_scan(const std::string& path, bool judge){
    SingleSwitchProgramEnvironment env;
    write_json(path, judge ? quality_fixture() : collection_fixture());
    const auto plan = load_saved_home_plan(env, path);
    ProControllerContext context;
    context.observed = plan_observed(plan, plan.source);
    for (qsizetype i = 0; i < context.observed.size(); ++i){
        if (context.observed.at(i).isNull()) continue;
        auto record = context.observed.at(i).toObject();
        auto observation = record.value("collection_observation").toObject();
        auto form = observation.value("form").toObject();
        form.insert("resource_sha256", HOME_COLLECTION_REFERENCE_SHA256);
        observation.insert("form", form); record.insert("collection_observation", observation);
        context.observed[i] = record;
    }
    const auto original = context.observed;
    BoxCursor cursor(0); BoxNavigationTiming timing;
    std::vector<size_t> reads;
    auto execute = [&](bool resume, bool bad_post = false){
        reads.clear();
        execute_saved_home_plan(env, context, plan, path, {}, cursor, timing, 240ms,
            false, resume, 1, [&](size_t index)->QJsonValue{
                check(index < 2, "Execution audited an unrelated slot");
                reads.push_back(index);
                if (bad_post && reads.size() > 2) return QJsonValue::Null;
                return context.observed.at(index);
            }, {}, false);
    };
    // Even an unrelated changed slot is deliberately not audited.
    context.observed.replace(29, QJsonObject{{"name", "unrelated change"}});
    execute(false); check(reads.size() == 4 && context.pickups == 1, "First run did not use only move checks");
    const auto middle = context.observed;
    execute(true); check(reads.size() == 4 && context.pickups == 2, "Resume rescanned or repeated progress");
    execute(true); check(reads.empty() && context.pickups == 2, "Completed resume read slots or repeated a move");
    auto state = plan.source; state.replace(0, "b"); state.replace(1, "a");
    write_json(path + ".checkpoint.json", QJsonObject{{"version", 1}, {"plan_sha256", plan.file_hash},
        {"completed_moves", 1}, {"status", "ready"}, {"layout_sha256", plan_layout_hash(plan, state)}});
    context.observed = middle; context.observed.replace(1, QJsonValue::Null);
    rejected([&]{execute(true);}); check(context.pickups == 2, "Wrong resumed Pokemon was picked up");
    // A placement followed by a failed postcheck leaves an in-flight journal.
    QFile::remove(QString::fromStdString(path + ".checkpoint.json"));
    context.observed = original;
    rejected([&]{execute(false, true);}); check(context.pickups == 3, "Postcheck failure did not place the move");
    execute(true);
    check(context.pickups == 4 && reads.size() == 6, "Interrupted placed move was repeated or full-scanned");
    // A journal written before pickup is resolved from the same two slots.
    write_json(path + ".checkpoint.json", QJsonObject{{"version", 1}, {"plan_sha256", plan.file_hash},
        {"completed_moves", 0}, {"status", "in_flight"}, {"layout_sha256", plan_layout_hash(plan, plan.source)}});
    context.observed = original;
    execute(true); check(context.pickups == 5 && reads.size() == 6, "Interrupted unstarted move failed local reconciliation");
    context.observed.replace(0, QJsonValue::Null); context.observed.replace(1, QJsonValue::Null);
    write_json(path + ".checkpoint.json", QJsonObject{{"version", 1}, {"plan_sha256", plan.file_hash},
        {"completed_moves", 0}, {"status", "in_flight"}, {"layout_sha256", plan_layout_hash(plan, plan.source)}});
    rejected([&]{execute(true);}); check(context.pickups == 5, "Ambiguous interrupted state caused pickup");
    auto checkpoint = QJsonDocument::fromJson(read_plan_bytes(env, path + ".checkpoint.json")).object();
    checkpoint.insert("plan_sha256", "wrong"); write_json(path + ".checkpoint.json", checkpoint);
    rejected([&]{execute(true);}); check(reads.empty(), "Wrong-plan checkpoint performed console reads");
    std::cout << "PASS: " << (judge ? "Judge" : "form")
        << " execution/resume without a scan, untouched slots skipped, pickup refusal, interrupted before/after, ambiguous state, wrong checkpoint.\n";
}

void test_stationary_collection_copies(const std::string& path){
    SingleSwitchProgramEnvironment env;
    auto root = collection_fixture();
    auto inventory = root.value("inventory").toObject();
    auto catalog = inventory.value("pokemon").toObject();
    auto layout = inventory.value("slots").toArray();
    auto contract = root.value("collection_execution").toObject();
    auto keys = contract.value("form_keys").toObject();
    auto record = catalog.value("a").toObject(); record.insert("name", "gamma"); record.insert("dex", 3);
    auto observation = record.value("collection_observation").toObject();
    auto form = observation.value("form").toObject(); form.insert("key", "gamma-form"); form.insert("candidates", QJsonArray{"gamma-form"});
    observation.insert("form", form); record.insert("collection_observation", observation);
    catalog.insert("c", record); catalog.insert("d", record); layout.replace(2, "c"); layout.replace(3, "d");
    keys.insert("c", "gamma-form"); keys.insert("d", "gamma-form"); contract.insert("form_keys", keys);
    contract.insert("stationary_review_occurrences", QJsonArray{"c", "d"}); root.insert("collection_execution", contract);
    inventory.insert("slots", layout); inventory.insert("pokemon", catalog); root.insert("inventory", inventory); root.insert("target", layout);
    auto validation = root.value("source_validation").toObject();
    SavedHomePlan source; source.source = layout; source.catalog = catalog; source.physical_range = validation.value("physical_range").toObject();
    validation.insert("layout_sha256", plan_layout_hash(source, layout)); root.insert("source_validation", validation);
    write_json(path, root); const auto plan = load_saved_home_plan(env, path);
    check(plan.stationary_review_occurrences == QStringList{"c", "d"}, "Stationary duplicates were not preserved");
    auto invalid = root;
    invalid.insert("moves", QJsonArray{
        QJsonObject{{"source", 2}, {"destination", 0}, {"expected_source", "c"}, {"expected_destination", "a"}},
        QJsonObject{{"source", 0}, {"destination", 2}, {"expected_source", "c"}, {"expected_destination", "a"}}});
    write_json(path, invalid); rejected([&]{load_saved_home_plan(env, path);});
    invalid = root; invalid.insert("collection_review", QJsonObject{{"assignments", QJsonArray{QJsonObject{{"occurrence", "c"}}}}});
    write_json(path, invalid); rejected([&]{load_saved_home_plan(env, path);});
    std::cout << "PASS: identical extras retained in fixed review slots, duplicate movement and assignment refused.\n";
}

int main(int argc, char** argv){
    try{
        if (argc == 2){
            SingleSwitchProgramEnvironment env;
            const auto plan = load_saved_home_plan(env, argv[1]);
            check(plan.collection_execution, "Expected a collection execution plan");
            std::cout << "PASS: external collection plan validated first_box=" << plan.first_box
                << " boxes=" << plan.box_count << " moves=" << plan.moves.size()
                << " stationary_review_copies=" << plan.stationary_review_occurrences.size() << '\n';
            return 0;
        }
        QTemporaryDir directory;
        check(directory.isValid(), "Temporary directory");
        const auto path = (directory.path() + "/plan.json").toStdString();
        const auto checkpoint_path = path + ".checkpoint.json";
        SingleSwitchProgramEnvironment env;
        auto root = fixture();
        write_json(path, root);
        auto plan = load_saved_home_plan(env, path);
        auto preview = root;
        preview.insert("execution_blocked", true);
        write_json(path, preview);
        rejected([&]{load_saved_home_plan(env, path);});
        write_json(path, root);
        check_home_plan_output_paths(env, path, (directory.path() + "/fresh").toStdString());
        rejected([&]{check_home_plan_output_paths(env, path, (directory.path() + "/plan").toStdString());});
        ProControllerContext context;
        context.observed = plan_observed(plan, plan.source);
        BoxCursor cursor(0);
        BoxNavigationTiming timing;
        auto execute = [&](bool dry, bool resume, size_t limit = 1, bool bad_read = false){
            execute_saved_home_plan(env, context, plan, path, context.observed, cursor, timing, 240ms,
                dry, resume, limit, [&](size_t slot){return bad_read ? QJsonValue::Null : context.observed.at(slot);});
        };
        auto checkpoint = [&](size_t count, QString status, QJsonArray state){
            write_json(checkpoint_path, QJsonObject{{"version", 1}, {"plan_sha256", plan.file_hash},
                {"completed_moves", (double)count}, {"status", status}, {"layout_sha256", plan_layout_hash(plan, state)}});
        };
        execute(true, false);
        check(context.pickups == 0 && !QFile::exists(QString::fromStdString(checkpoint_path)), "Dry run mutated state");
        execute(false, false);
        check(context.pickups == 1 && !context.held && context.observed != plan_observed(plan, plan.source), "Bounded execution failed");
        rejected([&]{execute(false, false);});
        check(context.pickups == 1, "Existing checkpoint caused pickup");
        execute(false, true);
        check(context.pickups == 2 && !context.held && context.observed == plan_observed(plan, plan.source), "Resume did not restore target");
        execute(false, true);
        check(context.pickups == 2, "Completed plan repeated a move");
        auto mid = plan.source; mid.replace(0, "b"); mid.replace(1, "a");
        checkpoint(1, "in_flight", mid);
        execute(false, true);
        check(context.pickups == 2, "Already placed interrupted move repeated");
        checkpoint(0, "in_flight", plan.source);
        execute(true, true);
        check(context.pickups == 2, "Dry reconciliation performed pickup");
        context.observed.replace(0, QJsonObject{{"name", "changed"}});
        rejected([&]{execute(false, true);});
        check(context.pickups == 2, "Mismatched source caused pickup");
        context.observed = plan_observed(plan, plan.source);
        QFile::remove(QString::fromStdString(checkpoint_path));
        rejected([&]{execute(false, false, 1, true);});
        auto uncertain = QJsonDocument::fromJson(read_plan_bytes(env, checkpoint_path)).object();
        check(uncertain.value("status") == "in_flight" && uncertain.value("completed_moves").toInt() == 0, "Failed affected-slot check committed progress");
        context.observed = plan_observed(plan, plan.source);
        QFile::remove(QString::fromStdString(checkpoint_path));
        context.refuse_placement = true;
        rejected([&]{execute(false, false);});
        check(context.held, "Placement failure was hidden");
        check(QJsonDocument::fromJson(read_plan_bytes(env, checkpoint_path)).object().value("status") == "in_flight", "Held failure committed progress");
        context.refuse_placement = false; context.held = false;
        for (int test = 0; test < 4; test++){
            auto invalid = root;
            if (test == 0){auto v = invalid.value("source_validation").toObject(); v.insert("layout_sha256", "wrong"); invalid.insert("source_validation", v);}
            else{
                auto moves = invalid.value("moves").toArray(); auto move = moves.at(0).toObject();
                if (test == 1) move.insert("kind", "group");
                if (test == 2) move.insert("source", 30);
                if (test == 3) move.insert("expected_source", "b");
                moves.replace(0, move); invalid.insert("moves", moves);
            }
            write_json(path, invalid); rejected([&]{load_saved_home_plan(env, path);});
        }
        root = fixture(true); write_json(path, root); plan = load_saved_home_plan(env, path);
        context.observed = plan_observed(plan, plan.source);
        checkpoint(0, "in_flight", plan.source);
        rejected([&]{execute(true, true);});
        test_species_execution((directory.path() + "/species.json").toStdString());
        test_species_tracked_copies((directory.path() + "/tracked-species.json").toStdString());
        test_quality_execution((directory.path() + "/quality.json").toStdString());
        test_collection_execution((directory.path() + "/collection.json").toStdString());
        test_resume_without_scan((directory.path() + "/no-scan-form.json").toStdString(), false);
        test_resume_without_scan((directory.path() + "/no-scan-judge.json").toStdString(), true);
        test_stationary_collection_copies((directory.path() + "/duplicates.json").toStdString());
        std::cout << "PASS: native plan validation, dry run, bounded execution, resume, completed resume, interrupted before/after, mismatched source, failed postconditions, held failure, ambiguous duplicates.\n";
    }catch(const std::exception& error){std::cerr << error.what() << '\n'; return 1;}
}
