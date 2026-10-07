#include <iostream>
#include <QFile>
#include <QTemporaryDir>
#include "../../Source/VideoGames/PokemonHome/Programs/PokemonHome_CollectionIndexCheckpoint.h"
using namespace PokemonAutomation::NintendoSwitch::PokemonHome;
void check(bool value){if(!value)throw std::runtime_error("Index checkpoint test failed");}
template<class F> void rejected(F f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed);}
QJsonObject workspace(){
    return {{"version",1},{"kind","home_collection_workspace"},{"policy",QJsonObject{{"extras","review"},{"release_enabled",false}}},
        {"annotations",QJsonObject{}},{"source_inventory",QJsonObject{{"version",1},{"slots",QJsonArray{}},{"pokemon",QJsonObject{}},
        {"scan_context",QJsonObject{{"version",1},{"physical_start_box",52},{"physical_end_box",51},{"total_boxes",200},{"box_count",0},
        {"scan_id","test"},{"coordinate_basis","zero_based_relative_to_physical_start_box"},{"box_identity","physical_counter_not_editable_label"},{"source_export","test.json"}}}}}};
}
int main(int argc,char** argv){
    auto state=home_index_create(workspace(),52,2,200,1);check(home_index_completed(state)==0);
    QJsonArray box;for(int i=0;i<30;++i)box.append(QJsonValue::Null);
    box[4]=QJsonObject{{"name","pichu"},{"dex",172},{"collection_observation",QJsonObject{{"form",QJsonObject{{"key","pichu"}}},{"judge",QJsonObject{{"status","unknown"}}},{"ivs",QJsonValue::Null}}}};
    QTemporaryDir directory;check(directory.isValid());auto path=directory.path()+"/index.json";check(home_index_save(path,state));
    auto saved=home_index_append_box(state,box);check(home_index_completed(saved)==1);check(home_index_save(path,saved));
    // A partial next box cannot advance or overwrite the durable first box.
    QJsonArray partial;partial.append(box[4]);rejected([&]{home_index_append_box(saved,partial);});
    QFile file(path);check(file.open(QIODevice::ReadOnly));auto loaded=QJsonDocument::fromJson(file.readAll()).object();file.close();home_index_validate(loaded);check(loaded==saved);
    auto finished=home_index_append_box(loaded,box);check(home_index_completed(finished)==2);rejected([&]{home_index_append_box(finished,box);});
    auto bad=loaded;bad.insert("first_box",53);rejected([&]{home_index_validate(bad);});
    bad=loaded;bad.insert("box_count",1.5);bad=home_index_seal(bad);rejected([&]{home_index_validate(bad);});
    auto w=loaded.value("workspace").toObject();auto inv=w.value("source_inventory").toObject();auto layout=inv.value("slots").toArray();layout[5]=layout[4];inv.insert("slots",layout);w.insert("source_inventory",inv);bad=loaded;bad.insert("workspace",w);bad=home_index_seal(bad);rejected([&]{home_index_validate(bad);});
    std::cout<<"PASS: per-box save/reload, interrupted-box retention, completed resume, tamper/range/duplicate rejection\n";
    // One-time migration from a verified merged inventory into the app's journal.
    if(argc==3){
        QFile source(QString::fromLocal8Bit(argv[1]));check(source.open(QIODevice::ReadOnly));auto imported=QJsonDocument::fromJson(source.readAll()).object();
        auto inventory=imported.value("source_inventory").toObject();auto context=inventory.value("scan_context").toObject();
        QString checkpoint=QString::fromLocal8Bit(argv[2]);check(checkpoint.endsWith("-index-checkpoint.json"));
        context.insert("source_export",checkpoint.left(checkpoint.size()-QString("-index-checkpoint.json").size())+".json");
        inventory.insert("scan_context",context);imported.insert("source_inventory",inventory);
        auto migrated=home_index_create(imported,52,149,200,1);check(!QFile::exists(checkpoint));check(home_index_save(checkpoint,migrated));
        std::cout<<"MIGRATED completed_boxes="<<home_index_completed(migrated)<<" first=52 last=200 language=English\n";
    }
}
