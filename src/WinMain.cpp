#include <Windows.h>
#include "Application.h"
#include <exception>
#include <fstream>

namespace
{
    bool SmokeAutomationActive()
    {
        wchar_t value[32]{};
        return GetEnvironmentVariableW(L"AQUA_SMOKE_AUDIT", value, 32) ||
            GetEnvironmentVariableW(L"AQUA_SMOKE_REFERENCE", value, 32) ||
            GetEnvironmentVariableW(L"AQUA_SMOKE_APPEARANCE", value, 32);
    }
}

int CALLBACK wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
	const bool automation = SmokeAutomationActive();
	try {
		Application* app = new Application();
		const int result = app->Run();
		delete app;
		if (automation)
		{
			std::ofstream log("smoke-automation-exit.txt");
			log << "normal_return=" << result << '\n';
		}
		return result;
	}
	catch (DxException& e)
	{
        if (automation)
        {
            std::wofstream log("smoke-audit-error.txt");
            log << e.ToString();
            std::ofstream exitLog("smoke-automation-exit.txt");
            exitLog << "caught_dx_exception\n";
            return 3;
        }
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
	catch (const std::exception& e)
	{
        if (automation)
        {
            std::ofstream log("smoke-automation-error.txt");
            log << e.what();
            std::ofstream exitLog("smoke-automation-exit.txt");
            exitLog << "caught_std_exception\n";
            return 3;
        }
		MessageBoxA(nullptr, e.what(), "Application error", MB_OK);
		return 0;
	}

}
