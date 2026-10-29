#include "dx12_renderer.h"
#include "tv_runtime.h"
#include <string_view>
int main(int argc,char** argv){wchar_t log_path[32768]{};const auto n=GetEnvironmentVariableW(L"JISH_TV_TEST_LOG",log_path,32768);if(n&&n<32768)virtualtv::initialize_log(log_path);return virtualtv::renderer_gpu_test(argc>1&&std::string_view(argv[1])=="--hardware")?0:1;}
