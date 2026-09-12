#include "../src/Renderer/OfflineResponseMarker.h"
#include <iostream>

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    const std::filesystem::path root(argv[1]);
    if (!std::filesystem::create_directories(root)) return 2;
    auto make = [&](const char* name) { auto p=root/name; std::filesystem::create_directory(p); return p; };
    auto reject = [&](const std::filesystem::path& p, const char* expected) {
        try { WaitForOfflineResponse(p,43,std::chrono::milliseconds(80)); }
        catch (const std::runtime_error& e) {
            if (std::string(e.what()).find(expected)==std::string::npos) throw;
            return;
        }
        throw std::runtime_error("Unexpected acceptance");
    };
    for (const auto& item : {std::pair{"wrong","42"}, {"empty",""}, {"malformed","43junk"}})
    {
        auto p=make(item.first); { std::ofstream f(p/"response.ready"); f << item.second; }
        reject(p,"content mismatch");
    }
    auto missing=make("missing"); reject(missing,"timed out");
    auto worker=make("worker"); { std::ofstream f(worker/"error.txt"); f << "deliberate"; }
    reject(worker,"worker rejected");
    auto delayed=make("delayed");
    std::thread publish([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        { std::ofstream f(delayed/"response.tmp"); f << "43"; }
        std::filesystem::rename(delayed/"response.tmp",delayed/"response.ready");
    });
    WaitForOfflineResponse(delayed,43); publish.join();
    auto locked=make("locked");
    HANDLE file=CreateFileW((locked/"response.ready").c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return 3;
    DWORD count; if(!WriteFile(file,"43",2,&count,nullptr)||count!=2) return 3;
    // The historical ifstream path maps this controlled open failure to reply=0.
    { std::ifstream old(locked/"response.ready"); unsigned reply=0; old>>reply;
      if(!old.fail() || reply!=0) return 4; }
    std::thread unlock([&] { std::this_thread::sleep_for(std::chrono::milliseconds(60)); CloseHandle(file); });
    WaitForOfflineResponse(locked,43); unlock.join();
    std::cout << "PASS: 7 cases; legacy open-failure conflation reproduced; sharing conflict recovered\n";
}
