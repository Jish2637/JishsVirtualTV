#pragma once
#include <unknwn.h>
#include <wrl/client.h>
#include <stdexcept>
namespace virtualtv {
// NVIDIA Streamline Programming Guide 5.3: documented native-interface query for
// overlays. No slGetNativeInterface export, SDK dependency or guessed ABI needed.
inline constexpr GUID streamline_native_iid{0xadec44e2,0x61f0,0x45c3,{0xad,0x9f,0x1b,0x37,0x37,0x92,0x84,0xff}};
template<class T> Microsoft::WRL::ComPtr<T> native_interface(T* incoming,bool& unwrapped){
    using Microsoft::WRL::ComPtr;
    unwrapped=false;ComPtr<T> current=incoming;
    if(!current)throw std::runtime_error("Null graphics interface");
    for(unsigned depth=0;depth<4;++depth){
        ComPtr<IUnknown> base;
        if(FAILED(current->QueryInterface(streamline_native_iid,reinterpret_cast<void**>(base.GetAddressOf())))||!base)return current;
        ComPtr<T> typed;
        if(FAILED(base.As(&typed)))throw std::runtime_error("Streamline native interface has an incompatible type");
        ComPtr<IUnknown> a,b;
        if(FAILED(current.As(&a))||FAILED(typed.As(&b)))throw std::runtime_error("Graphics native identity query failed");
        if(a==b)return current;
        current=typed;unwrapped=true;
    }
    throw std::runtime_error("Graphics proxy nesting exceeds safe bound");
}
}
