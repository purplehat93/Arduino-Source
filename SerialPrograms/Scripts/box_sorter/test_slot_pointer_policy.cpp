#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <vector>
#include "../../Source/VideoGames/PokemonHome/Programs/PokemonHome_SlotPointerPolicy.h"
using namespace PokemonAutomation::NintendoSwitch::PokemonHome;
int main(int argc, char** argv){
    for (auto size : {std::pair<size_t,size_t>{1280,720}, {1920,1080}}){
        const auto w=size.first,h=size.second;
        for (uint32_t color : {0xffff6800u,0xff20a0f0u}){
            const bool orange=color==0xffff6800u;
            const uint32_t low=orange?0xffbe2800u:0xff006ebeu, high=orange?0xffffa050u:0xff50e6ffu;
            for (size_t row=0;row<5;++row) for(size_t col=0;col<6;++col){
                std::vector<uint32_t> pixels(w*h,0xffffffff);
                const double center=w*(.073+.072*col),base=.105*row;
                for(size_t y=size_t(h*(.150+base));y<size_t(h*(.173+base));++y){
                    const double radius=w*.56*(.173+base-double(y)/h);
                    for(size_t x=size_t(center-radius);x<=size_t(center+radius);++x)pixels[y*w+x]=color;
                }
                assert(home_slot_pointer_shape(pixels.data(),w,h,w,row,col,low,high));
                assert(!home_slot_pointer_shape(pixels.data(),w,h,w,(row+1)%5,col,low,high));
            }
            // Centered colored artwork with constant width is not a pointer.
            std::vector<uint32_t> rectangle(w*h,0xffffffff);
            for(size_t y=size_t(h*.150);y<size_t(h*.175);++y)
                for(size_t x=size_t(w*.063);x<size_t(w*.083);++x)rectangle[y*w+x]=color;
            assert(!home_slot_pointer_shape(rectangle.data(),w,h,w,0,0,low,high));
        }
    }
    assert(!home_slot_pointer_shape(nullptr,1920,1080,1920,0,0,0,0));
    // Optional private packed RGB32 capture: assert only its actual first slot
    // is pointer-shaped. No private Pokemon captures are committed.
    if(argc==4){
        const size_t w=std::stoul(argv[2]),h=std::stoul(argv[3]);std::vector<uint32_t> pixels(w*h);
        std::ifstream input(argv[1],std::ios::binary);input.read(reinterpret_cast<char*>(pixels.data()),pixels.size()*4);assert(input);
        for(size_t row=0;row<5;++row)for(size_t col=0;col<6;++col){
            const bool found=home_slot_pointer_shape(pixels.data(),w,h,w,row,col,0xffbe2800,0xffffa050)
                ||home_slot_pointer_shape(pixels.data(),w,h,w,row,col,0xff006ebe,0xff50e6ff);
            assert(found==(row==0&&col==0));
        }
    }
    std::cout<<"PASS: orange/blue pointer geometry in all slots at 720p/1080p; artwork rejected\n";
}
