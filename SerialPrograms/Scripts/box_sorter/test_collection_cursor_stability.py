"""Exercise the production watcher with controlled detector frame results."""
from pathlib import Path
import subprocess
import tempfile
source=(Path(__file__).resolve().parents[2] / 'Source/VideoGames/PokemonHome/Programs/PokemonHome_OverviewNavigation.inc').read_text()
start=source.index('class BoxSlotCursorWatcher :');end=source.index('\nbool send_box_slot_step',start)
code='''#include <cassert>
#include <cstddef>
struct GridPosition { int row=-1,column=-1; bool valid()const{return row>=0&&column>=0;} };
struct ImageViewRGB32 { GridPosition detected; };
struct VideoOverlaySet { void add(int,int){} };
using WallClock=int;
constexpr size_t GRID_ROWS=5,GRID_COLUMNS=6; constexpr int COLOR_BLUE=0;
int box_slot_cursor_region(size_t,size_t){return 0;}
GridPosition read_box_slot_cursor(const ImageViewRGB32& frame){return frame.detected;}
bool grid_position_matches(GridPosition a,GridPosition b){return a.valid()&&b.valid()&&a.row==b.row&&a.column==b.column;}
struct VisualInferenceCallback { explicit VisualInferenceCallback(const char*){} virtual void make_overlays(VideoOverlaySet&)const=0; virtual bool process_frame(const ImageViewRGB32&,WallClock)=0; };
'''+source[start:end]+'''
int main(){
 auto feed=[](BoxSlotCursorWatcher& w,GridPosition p){return w.process_frame(ImageViewRGB32{p},0);};
 BoxSlotCursorWatcher w;
 assert(!feed(w,{})); assert(!feed(w,{0,0})); assert(!feed(w,{0,0})); assert(feed(w,{0,0}));
 assert(w.frames()==4&&w.rejected()==1);
 BoxSlotCursorWatcher alternating;
 for(int i=0;i<20;++i)assert(!feed(alternating,{i%2,0}));
 BoxSlotCursorWatcher interrupted;
 assert(!feed(interrupted,{0,0}));assert(!feed(interrupted,{0,0}));assert(!feed(interrupted,{}));
 assert(!feed(interrupted,{0,0}));assert(!feed(interrupted,{0,0}));assert(feed(interrupted,{0,0}));
 BoxSlotCursorWatcher expected({0,0});
 for(int i=0;i<4;++i)assert(!feed(expected,{4,3}));
 assert(!feed(expected,{0,0}));assert(!feed(expected,{0,0}));assert(feed(expected,{0,0}));
}
'''
navigation = (Path(__file__).resolve().parents[2] / 'Source/VideoGames/PokemonHome/Programs/PokemonHome_BoxNavigation.cpp').read_text()
begin = navigation.index('        const auto confirm_started =', navigation.index('    bool cursor_confirmed = false;'))
end = navigation.index('\n    }\n    const auto queue_ms', begin)
loop = navigation[begin:end].replace('std::chrono::steady_clock::now()', 'test_now()')
code = code.replace('int main(){', 'int watcher_tests(){')
# The original main has implicit success; its renamed function returns explicitly.
code = code.rsplit('\n}', 1)[0] + '\nreturn 0;\n}\n'
code += r"""
#include <chrono>
#include <algorithm>
#include <vector>
using namespace std::chrono_literals;
using Milliseconds=std::chrono::milliseconds;
Milliseconds elapsed{0}, last_extra{0}; std::vector<bool> readings;size_t sample=0;
auto test_now(){return std::chrono::steady_clock::time_point(elapsed);}
template<class T> void pbf_wait(T&,Milliseconds time){elapsed+=time;}
struct Context{void wait_for_all_requests(){}};
bool cursor_is_at_slot(int,int,int){const bool value=readings.empty()?false:readings[std::min(sample,readings.size()-1)];++sample;return value;}
bool confirm(std::vector<bool> input){
 elapsed=0ms;sample=0;readings=input;
 int env=0;Context context;struct{int row=0,column=0;} dest_cursor;
 bool cursor_confirmed=false;Milliseconds extra_settle=0ms;
""" + loop + r"""
 last_extra=extra_settle; return cursor_confirmed;
}
int main(){
 watcher_tests();
 assert(confirm({true,false,true,true,true})); // Missed second frame recovers.
 assert(sample==5&&last_extra==100ms);
 assert(!confirm({false})); // Bounded persistent ambiguity.
 assert(elapsed<=2000ms);
 assert(!confirm({true,true,false})); // Two good frames cannot bypass a rejection.
 assert(confirm({true,true,true}));
 assert(last_extra==0ms);
}
"""
with tempfile.TemporaryDirectory(prefix='home-cursor-stability-') as directory:
    cpp = Path(directory) / 'test.cpp'
    executable = Path(directory) / 'test'
    cpp.write_text(code)
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print('PASS: production watcher and local guard handle transient/alternating frames, second-frame recovery and bounded timeout')
