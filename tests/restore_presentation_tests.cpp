#include "tv_screen_selection.h"
#include <cassert>
#include <vector>
using namespace virtualtv;
struct Sample {bool attached{};int id{};};
int main(){
    Sample scene{};
    assert(!choose_presentation_screen(scene,std::vector<Sample>{{false,5}}));
    assert(!scene.attached);
    assert(choose_presentation_screen(scene,std::vector<Sample>{{false,5},{true,42},{true,99}}));
    assert(scene.attached&&scene.id==42); // saved extra can render without manually attaching primary
    scene={true,7};assert(choose_presentation_screen(scene,std::vector<Sample>{{true,42}}));
    assert(scene.id==7); // true primary wins when present
}
