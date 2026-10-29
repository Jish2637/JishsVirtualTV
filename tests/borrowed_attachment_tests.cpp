#include "tv_runtime.h"
#include <cassert>
#include <sstream>
#include <iostream>
using namespace virtualtv;
int main(){
    attachment_store::Record record{123,4,0,{}};record.asset="screen";record.has_origin=true;record.origin={10,20,30};
    restore_persistent_outputs({record});const auto before=persistent_outputs_snapshot();assert(before.second.size()==1);
    set_borrowed_outputs(true,{});assert(borrowed_presentation_active()&&borrowed_outputs_snapshot().empty()&&borrowed_screens_snapshot().empty());
    const Identity id{1,2,3,4};request_select(id);request_attach();request_detach();
    assert(!request_add_output(id,OutputKind::Screen,{})&&!request_add_output(id,OutputKind::Speaker,{}));
    assert(!detach_one_output(id)&&!remove_output(id,OutputKind::Screen));
    clear_extra_outputs();clear_persistent_outputs();Screen changed;changed.width=8;set_screen_for(id,changed);
    assert(output_assignments_snapshot().empty());auto during=persistent_outputs_snapshot();assert(during.first==before.first&&during.second.size()==1&&during.second[0].asset=="screen");
    set_borrowed_outputs(false,{});assert(!borrowed_presentation_active());auto after=persistent_outputs_snapshot();assert(after.first==before.first&&after.second.size()==1);
    // Naming metadata alone never queues a screen or speaker.
    publish_remote_names({{id,"asset/key","Remote object",11,22,33}});assert(placement_names_snapshot().size()==1&&output_assignments_snapshot().empty());
    publish_remote_names({});assert(placement_names_snapshot().empty());stop_native();assert(!borrowed_presentation_active());
    std::cout<<"Native borrowed presentation: commands rejected, personal manifest preserved, pending outputs hidden, naming metadata never attaches, cleanup passed\n";
}
