#include "dx12_native.h"
#include <cassert>
#include <vector>
using Microsoft::WRL::ComPtr;
using namespace virtualtv;
struct __declspec(uuid("e0514032-aefd-4a93-b170-c97cbe5841df")) ITyped : IUnknown { virtual void unused()=0; };
namespace {
unsigned alive{};
class Node final:public ITyped {
    ULONG refs_{1};ComPtr<IUnknown> target_;bool typed_;
public:
    explicit Node(IUnknown* target=nullptr,bool typed=false):target_(target),typed_(typed){++alive;}
    ~Node(){--alive;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(iid==__uuidof(IUnknown))*out=static_cast<IUnknown*>(this);
        else if(iid==__uuidof(ITyped)&&typed_)*out=static_cast<ITyped*>(this);
        else if(iid==streamline_native_iid&&target_){*out=target_.Get();target_->AddRef();return S_OK;}
        else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release() override{const auto n=--refs_;if(!n)delete this;return n;}
    void unused() override{}
};
ComPtr<IUnknown> make(IUnknown* target=nullptr,bool typed=false){ComPtr<IUnknown> result;result.Attach(new Node(target,typed));return result;}
}
int main(){
    {auto native=make();auto other=make();auto proxy=make(native.Get());bool changed{};
     auto a=native_interface(native.Get(),changed);assert(!changed&&a==native);
     auto b=native_interface(proxy.Get(),changed);assert(changed&&b==native&&b!=other);
     auto nested=make(proxy.Get());auto c=native_interface(nested.Get(),changed);assert(changed&&c==native);
     auto typed_proxy=make(native.Get(),true);ComPtr<ITyped> typed;assert(SUCCEEDED(typed_proxy.As(&typed)));
     bool rejected=false;try{native_interface(typed.Get(),changed);}catch(const std::exception&){rejected=true;}assert(rejected);
     std::vector<ComPtr<IUnknown>> chain{native};for(unsigned i=0;i<5;++i)chain.push_back(make(chain.back().Get()));
     rejected=false;try{native_interface(chain.back().Get(),changed);}catch(const std::exception&){rejected=true;}assert(rejected);
     rejected=false;try{native_interface<IUnknown>(nullptr,changed);}catch(const std::exception&){rejected=true;}assert(rejected);
    }
    assert(alive==0); // Every GUID query and typed conversion owns/releases its COM reference.
}
