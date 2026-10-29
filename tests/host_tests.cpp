#include <iostream>
namespace virtualtv {bool host_bridge_test();}
int main(){if(!virtualtv::host_bridge_test()){std::cerr<<"Embedded menu context/allocator or drawing test failed\n";return 1;}std::cout<<"Real TV widgets drawn over four synthetic host frames; original menu chaining, ABI guards, context and allocator restoration passed\n";}
