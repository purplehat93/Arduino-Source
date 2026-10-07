#ifndef PokemonAutomation_PokemonHome_CollectionExecutionPolicy_H
#define PokemonAutomation_PokemonHome_CollectionExecutionPolicy_H

#include <optional>
#include "PokemonHome_CollectionObservationPolicy.h"
#include <QJsonArray>
#include <QJsonObject>

namespace PokemonAutomation{
namespace NintendoSwitch{
namespace PokemonHome{

inline constexpr const char* HOME_COLLECTION_REFERENCE_SHA256 =
    "c72834eb077ee1c2e2ff2cb1d8312d36dc723d703f9f4b00f0b7abba6cd93122";

inline std::optional<QJsonObject> home_collection_judge_record(const QJsonObject& observed){
    const auto observation = observed.value("collection_observation").toObject();
    const auto judge = observation.value("judge").toObject();
    const auto status = judge.value("status").toString();
    const char* names[] = {"hp", "attack", "defense", "special_attack", "special_defense", "speed"};
    QJsonObject result;
    for (const auto* stat : names) result.insert(stat, QJsonValue::Null);
    if (judge.isEmpty() && observation.value("judge").isUndefined()){
        return observation.value("ivs").isUndefined() || observation.value("ivs").isNull()
            ? std::optional<QJsonObject>(result) : std::nullopt;
    }
    if (status == "unknown" || status == "unsupported_language" || status == "unavailable_or_unreadable"){
        if (judge.contains("stats") || !observation.value("ivs").isNull()) return std::nullopt;
        return result;
    }
    if (status != "read" && status != "partial") return std::nullopt;
    const auto stats = judge.value("stats").toObject();
    if (stats.size() != 6) return std::nullopt;
    QJsonObject exact;
    size_t accepted = 0;
    for (const auto* name : names){
        const auto stat = stats.value(name).toObject();
        if (!stat.value("raw").isString() || !stat.value("raw_confirmation").isString()) return std::nullopt;
        exact.insert(name, QJsonValue::Null);
        if (stat.value("status") == "unreadable"){
            for (const auto* field : {"rating", "iv_min", "iv_max", "hyper_trained"}){
                if (!stat.value(field).isNull()) return std::nullopt;
            }
            continue;
        }
        if (stat.value("status") != "read") return std::nullopt;
        const auto first = parse_home_judge_rating(stat.value("raw").toString().toStdString());
        const auto second = parse_home_judge_rating(stat.value("raw_confirmation").toString().toStdString());
        if (first.label.empty() || first.label != second.label
            || stat.value("rating") != QString::fromStdString(first.label)
            || stat.value("iv_min") != (first.minimum ? QJsonValue(*first.minimum) : QJsonValue(QJsonValue::Null))
            || stat.value("iv_max") != (first.maximum ? QJsonValue(*first.maximum) : QJsonValue(QJsonValue::Null))
            || !stat.value("hyper_trained").isBool() || stat.value("hyper_trained") != first.hyper_trained){
            return std::nullopt;
        }
        ++accepted;
        if (const auto value = first.exact()) exact.insert(name, *value);
        result.insert(name, QJsonObject{{"rating", stat.value("rating")}, {"iv_min", stat.value("iv_min")},
            {"iv_max", stat.value("iv_max")}, {"hyper_trained", stat.value("hyper_trained")}});
    }
    if ((status == "read") != (accepted == 6) || observation.value("ivs") != exact) return std::nullopt;
    return result;
}

inline bool home_collection_judge_overlap(const QJsonObject& first, const QJsonObject& second){
    for (auto it = first.begin(); it != first.end(); ++it){
        const auto other = second.value(it.key());
        if (!it.value().isNull() && !other.isNull() && it.value() != other) return false;
    }
    return true;
}

inline bool home_collection_record_matches(QJsonObject expected, QJsonObject actual){
    const auto requirements = expected.take("collection_judge").toObject();
    const auto ratings = actual.take("collection_judge").toObject();
    if (expected != actual) return false;
    for (auto it = requirements.begin(); it != requirements.end(); ++it){
        // Unknown original ratings remain review data. They never certify a
        // positive requirement, and stronger fresh readings may be retained.
        if (!it.value().isNull() && it.value() != ratings.value(it.key())) return false;
    }
    return true;
}

// Species ordering compares every stable summary field; it does not assert
// exact-form identity or certify IVs. Metadata twins are kept stationary.
inline std::optional<QJsonObject> home_collection_species_record(
    const QJsonObject& observed, bool source_evidence = false
){
    const auto observation = observed.value("collection_observation").toObject();
    const auto dex = observed.value("dex");
    if ((!source_evidence && (observation.value("version") != 1 || observation.value("form").toObject().value("fields_confirmed") != true))
        || !dex.isDouble() || dex.toDouble() != dex.toInt() || dex.toInt() < 1 || dex.toInt() > 1025
        || observed.value("name").toString().isEmpty() || !observed.value("shiny").isBool()
        || !observed.value("gmax").isBool()) return std::nullopt;
    auto result = observed;result.remove("collection_observation");return result;
}

// Keep capture/Judge details in the immutable source fingerprint, but compare
// live identity using stable summary fields plus a newly confirmed form key.
inline std::optional<QJsonObject> home_collection_live_record(
    const QJsonObject& observed, const QString& reference_hash, bool verify_judge = false
){
    const auto observation = observed.value("collection_observation").toObject();
    const auto form = observation.value("form").toObject();
    const auto key = form.value("key");
    const auto candidates = form.value("candidates").toArray();
    const auto evidence = form.value("evidence").toString();
    if (observation.value("version") != 1 || !key.isString() || key.toString().isEmpty()
        || form.value("fields_confirmed") != true || form.value("status") != "candidate_requires_review"
        || (evidence != "reference_types_and_gender" && evidence != "reference_types_gender_and_ability")
        || form.value("resource") != "Pokemon/HomeCollectionReference.json"
        || form.value("resource_sha256") != reference_hash
        || candidates.size() != 1 || candidates.at(0) != key
        || !observed.value("shiny").isBool() || observed.value("gmax") != false){
        return std::nullopt;
    }
    auto record = observed;
    record.remove("collection_observation");
    record.insert("collection_form_key", key);
    if (verify_judge){
        const auto judge = home_collection_judge_record(observed);
        if (!judge) return std::nullopt;
        record.insert("collection_judge", *judge);
    }
    return record;
}

}
}
}
#endif
