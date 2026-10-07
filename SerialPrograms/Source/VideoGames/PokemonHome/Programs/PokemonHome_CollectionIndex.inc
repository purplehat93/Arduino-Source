// Included with the shared scanner. One app invocation, one durable box at a time.
static void run_home_collection_index(SingleSwitchProgramEnvironment& env, ProControllerContext& context,
    const std::string& base, Language language, Milliseconds video_delay, Milliseconds game_delay,
    size_t requested_first, size_t count, bool resume
){
    const QString checkpoint_path=QString::fromStdString(base+"-index-checkpoint.json");
    QJsonObject state;
    if(resume){
        QJsonParseError error;
        const auto document=QJsonDocument::fromJson(read_plan_bytes(env,base+"-index-checkpoint.json"),&error);
        if(error.error!=QJsonParseError::NoError || !document.isObject()) plan_error(env,"Invalid inventory checkpoint JSON.");
        state=document.object();
        try{home_index_validate(state);}catch(const std::exception& e){plan_error(env,e.what());}
        if((requested_first && requested_first!=size_t(state.value("first_box").toInt()))
            || count!=size_t(state.value("box_count").toInt()) || int(language)!=state.value("language").toInt()){
            plan_error(env,"Inventory range/language differs from the checkpoint. Keep the original full-range settings.");
        }
    }else{
        if(QFile::exists(checkpoint_path)) plan_error(env,"Inventory checkpoint exists. Enable Resume Inventory Index or use a new output basename.");
        for(const auto& suffix:{".json","-scan.json","-workspace.json"}){
            if(QFile::exists(QString::fromStdString(base+suffix))) plan_error(env,"Collection output exists. Use a new basename to preserve its edits.");
        }
    }
    auto completed=resume?home_index_completed(state):0;
    // A user can stop mid-summary. Only recognized summary/menu states are dismissed.
    if(resume && completed<count){
        SummaryScreenDetector summary;
        HomeSummaryEntryWatcher menu_move(HomeSummaryEntryWatcher::State::MENU_MOVE);
        HomeSummaryEntryWatcher menu_summary(HomeSummaryEntryWatcher::State::MENU_SUMMARY);
        if(summary.detect(env.console.video().snapshot()) || wait_until(env.console,context,500ms,{menu_move,menu_summary})>=0){
            pbf_press_button(context,BUTTON_B,80ms,game_delay);context.wait_for_all_requests();
        }
    }
    BoxNavigationTiming timing(game_delay);timing.local_cursor_delay=std::max(timing.local_cursor_delay,480ms);
    if(!resume || completed<count){
        BoxViewWatcher ordinary(&env.console.overlay());
        if(wait_until(env.console,context,Seconds(4),{ordinary})!=0) plan_error(env,"Start inventory indexing in ordinary HOME box view with nothing held.");
        const auto current=read_home_box_identity(env,context);
        const size_t first=resume?size_t(state.value("first_box").toInt()):(requested_first?requested_first:(current?current->first:0));
        if(!current || !first || first+count-1>current->second || (resume && current->second!=size_t(state.value("total_boxes").toInt()))) plan_error(env,"Cannot confirm the inventory physical range.");
        if(!resume){
            const auto scan_id="home-collection-"+QString::number(std::chrono::duration_cast<Milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
            const QJsonObject scan{{"version",1},{"scan_id",scan_id},{"physical_start_box",double(first)},
                {"physical_end_box",double(first-1)},{"total_boxes",double(current->second)},{"box_count",0},
                {"coordinate_basis","zero_based_relative_to_physical_start_box"},{"box_identity","physical_counter_not_editable_label"},
                {"source_export",QString::fromStdString(base+".json")}};
            const QJsonObject workspace{{"version",1},{"kind","home_collection_workspace"},
                {"source_inventory",QJsonObject{{"version",1},{"slots",QJsonArray{}},{"pokemon",QJsonObject{}},{"scan_context",scan}}},
                {"annotations",QJsonObject{}},{"policy",QJsonObject{{"extras","review"},{"release_enabled",false}}}};
            state=home_index_create(workspace,first,count,current->second,int(language));
            if(!home_index_save(checkpoint_path,state)) plan_error(env,"Cannot create inventory checkpoint. No scan started.");
        }
    }
    const size_t first=size_t(state.value("first_box").toInt()), total=size_t(state.value("total_boxes").toInt());
    env.log("HOME_INDEX action="+std::string(resume?"resume":"start")+" first_box="+std::to_string(first)+" last_box="+std::to_string(first+count-1)+" completed_boxes="+std::to_string(completed)+" saves=after_each_box moves=0 releases=0");
    const std::vector<SortingRule> preferences{{SortingRuleType::DexNo,false}};
    while(completed<count){
        const size_t physical=first+completed;
        move_home_box_verified(env,context,physical,timing,false);
        const auto origin=read_home_box_identity(env,context);
        if(!origin || origin->first!=physical || origin->second!=total) plan_error(env,"Inventory next box was not confirmed.");
        std::vector<QJsonObject> observations;
        const auto data=scan_home_range(env,context,1,*origin,preferences,video_delay,game_delay,language,timing,&observations);
        QTemporaryFile scratch(QString::fromStdString(base+"-index-working-XXXXXX.json"));
        if(!scratch.open()) plan_error(env,"Cannot create inventory serialization scratch file.");
        const auto scratch_path=scratch.fileName().toStdString();scratch.close();
        save_boxes_data_to_json(data,scratch_path);auto observed=read_slot_export(env,scratch_path);
        if(observed.size()!=30 || observations.size()!=30) plan_error(env,"Incomplete inventory box was not checkpointed.");
        for(qsizetype i=0;i<observed.size();++i){
            if(observed.at(i).isNull()) continue;
            auto record=observed.at(i).toObject();record.insert("collection_observation",observations.at(size_t(i)));observed[i]=record;
        }
        try{state=home_index_append_box(state,observed);}catch(const std::exception& e){plan_error(env,e.what());}
        if(!home_index_save(checkpoint_path,state)) plan_error(env,"Cannot atomically save inventory box. Stopping before advancing.");
        completed=home_index_completed(state);
        env.log("HOME_INDEX box=saved physical_box="+std::to_string(physical)+" completed_boxes="+std::to_string(completed)+" total_boxes="+std::to_string(count));
        env.log_to_ui("Inventory saved: "+std::to_string(completed)+" / "+std::to_string(count)+" boxes. Next physical box: "+std::to_string(first+completed));
    }
    const auto workspace=state.value("workspace").toObject();const auto inventory=workspace.value("source_inventory").toObject();
    const auto layout=inventory.value("slots").toArray();const auto catalog=inventory.value("pokemon").toObject();QJsonArray rows;
    for(qsizetype i=0;i<layout.size();++i){
        auto row=layout.at(i).isNull()?QJsonObject{}:catalog.value(layout.at(i).toString()).toObject();
        row.insert("index",double(i));row.insert("box",double(i/30));row.insert("row",double(i/6%5));row.insert("column",double(i%6));rows.append(row);
    }
    auto export_file=[&](const std::string& suffix,const QJsonDocument& document){
        const auto path=QString::fromStdString(base+suffix);
        if(QFile::exists(path)){
            QFile existing(path);
            if(!existing.open(QIODevice::ReadOnly) || QJsonDocument::fromJson(existing.readAll())!=document) plan_error(env,"Existing inventory export differs; manual edits were not overwritten.");
            return;
        }
        QSaveFile file(path);file.setDirectWriteFallback(false);const auto bytes=document.toJson(QJsonDocument::Indented);
        if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()) plan_error(env,"Inventory is checkpointed, but final export failed. Resume to finish exporting.");
    };
    export_file(".json",QJsonDocument(rows));export_file("-scan.json",QJsonDocument(inventory.value("scan_context").toObject()));export_file("-workspace.json",QJsonDocument(workspace));
    env.log("HOME_COLLECTION result=indexed occupied="+std::to_string(catalog.size())+" slots="+std::to_string(layout.size())+" checkpoint=complete moves=0 releases=0");
}
