#pragma once
#include <stdexcept>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
namespace PokemonAutomation{ namespace NintendoSwitch{ namespace PokemonHome{
inline QJsonObject home_index_seal(QJsonObject state){
    state.remove("sha256");
    const auto bytes = QJsonDocument(state).toJson(QJsonDocument::Compact);
    state.insert("sha256", QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()));
    return state;
}
inline void home_index_require(bool ok, const char* error){if (!ok) throw std::runtime_error(error);}
inline size_t home_index_completed(const QJsonObject& state){
    return size_t(state.value("workspace").toObject().value("source_inventory").toObject().value("slots").toArray().size()) / 30;
}
inline void home_index_validate(const QJsonObject& state){
    home_index_require(state.value("sha256").isString() && home_index_seal(state).value("sha256") == state.value("sha256"), "Inventory checkpoint fingerprint differs.");
    home_index_require(state.value("version") == 1 && state.value("kind") == "home_collection_index_checkpoint", "Unsupported inventory checkpoint.");
    auto integer = [&](const char* key){
        auto value = state.value(key); int n = value.toInt(-1);
        home_index_require(value.isDouble() && value.toDouble() == n, "Invalid inventory checkpoint range.");return n;
    };
    const int first = integer("first_box"), count = integer("box_count"), total = integer("total_boxes");
    home_index_require(first > 0 && count > 0 && total <= 300 && first + count - 1 <= total, "Invalid inventory checkpoint range.");
    home_index_require(integer("language") > 0 && state.value("workspace").isObject(), "Missing inventory checkpoint language/workspace.");
    auto workspace = state.value("workspace").toObject();
    home_index_require(workspace.value("version") == 1 && workspace.value("kind") == "home_collection_workspace"
        && workspace.value("policy").toObject() == QJsonObject{{"extras", "review"}, {"release_enabled", false}}, "Inventory checkpoint policy differs.");
    auto inventory = workspace.value("source_inventory").toObject();
    auto layout = inventory.value("slots").toArray(); auto catalog = inventory.value("pokemon").toObject();
    auto annotations = workspace.value("annotations").toObject(); auto context = inventory.value("scan_context").toObject();
    home_index_require(inventory.value("version") == 1 && inventory.value("slots").isArray()
        && inventory.value("pokemon").isObject() && workspace.value("annotations").isObject()
        && layout.size() % 30 == 0 && layout.size() / 30 <= count, "Invalid saved inventory length.");
    const int completed = int(layout.size() / 30);
    home_index_require(context.value("version") == 1 && context.value("physical_start_box") == first
        && context.value("physical_end_box") == first + completed - 1 && context.value("box_count") == completed
        && context.value("total_boxes") == total && !context.value("scan_id").toString().isEmpty()
        && context.value("coordinate_basis") == "zero_based_relative_to_physical_start_box"
        && context.value("box_identity") == "physical_counter_not_editable_label"
        && !context.value("source_export").toString().isEmpty(), "Saved inventory physical prefix differs.");
    QSet<QString> ids;
    for (auto value : layout){
        if (value.isNull()) continue;
        home_index_require(value.isString() && !ids.contains(value.toString()), "Repeated inventory occurrence.");
        const auto id = value.toString(); ids.insert(id);
        home_index_require(catalog.value(id).isObject() && annotations.value(id).isObject(), "Missing inventory occurrence data.");
        const auto record = catalog.value(id).toObject();
        home_index_require(!record.value("name").toString().isEmpty() && record.value("dex").toInt() > 0, "Unreadable inventory identity.");
    }
    home_index_require(ids.size() == catalog.size() && catalog.keys() == annotations.keys(), "Inventory catalog/annotations differ.");
}
inline QJsonObject home_index_create(QJsonObject workspace, size_t first, size_t count, size_t total, int language){
    auto state = home_index_seal(QJsonObject{{"version", 1}, {"kind", "home_collection_index_checkpoint"},
        {"first_box", double(first)}, {"box_count", double(count)}, {"total_boxes", double(total)},
        {"language", language}, {"workspace", workspace}});
    home_index_validate(state);return state;
}
inline QJsonObject home_index_append_box(QJsonObject state, const QJsonArray& observed){
    home_index_validate(state);
    home_index_require(observed.size() == 30 && home_index_completed(state) < size_t(state.value("box_count").toInt()), "Cannot append this inventory box.");
    auto workspace = state.value("workspace").toObject();auto inventory = workspace.value("source_inventory").toObject();
    auto layout = inventory.value("slots").toArray();auto catalog = inventory.value("pokemon").toObject();auto annotations = workspace.value("annotations").toObject();
    for (auto record : observed){
        if (record.isNull()){layout.append(QJsonValue::Null);continue;}
        home_index_require(record.isObject(), "Invalid scanned inventory record.");
        const auto id = QString("pokemon-%1").arg(layout.size(), 5, 10, QChar('0'));
        layout.append(id);catalog.insert(id,record);
        auto observation = record.toObject().value("collection_observation").toObject();
        annotations.insert(id,QJsonObject{{"form_key",observation.value("form").toObject().value("key")},
            {"identity_reviewed",false},{"ivs",observation.value("ivs")},{"iv_judge",observation.value("judge")},
            {"protected",false},{"note",""}});
    }
    auto context = inventory.value("scan_context").toObject();context.insert("box_count",int(layout.size()/30));
    context.insert("physical_end_box",state.value("first_box").toInt()+int(layout.size()/30)-1);
    inventory.insert("slots",layout);inventory.insert("pokemon",catalog);inventory.insert("scan_context",context);
    workspace.insert("source_inventory",inventory);workspace.insert("annotations",annotations);state.insert("workspace",workspace);
    state=home_index_seal(state);home_index_validate(state);return state;
}
inline bool home_index_save(const QString& path, const QJsonObject& state){
    home_index_validate(state);QSaveFile file(path);file.setDirectWriteFallback(false);
    const auto bytes=QJsonDocument(state).toJson(QJsonDocument::Indented);
    return file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size() && file.commit();
}
}}}
