#include "native_placement_reader.h"
#include <cassert>
#include <cstring>
#include <limits>
using namespace virtualtv::native_placement_reader;
int main(){
    assert(valid_span(0x10000,0x10000));
    assert(valid_span(0x10000,0x10000+record_bytes*128));
    assert(!valid_span(0x10000,0x10000+record_bytes*maximum_records+record_bytes));
    assert(!valid_span(0x10000,0x10000+1));
    assert(!valid_span(0x10000,0x10000-1));
    std::array<unsigned char,record_bytes> bytes{};
    const std::uint64_t entity=0x123412345678ull;
    const std::uint32_t group=167;
    std::memcpy(bytes.data()+0x10,&entity,sizeof entity);
    std::memcpy(bytes.data()+0x28,&group,sizeof group);
    bytes[0x3a]=1;
    const auto a=decode(bytes);
    assert(a.entity==entity&&a.group==group&&a.active==1);
    assert(same_object(a,a));
    auto b=a;b.entity++;
    assert(!same_object(a,b));
    b=a;b.group++;
    assert(!same_object(a,b));
    b=a;b.active=0x7f;
    assert(!same_object(a,b));
}
