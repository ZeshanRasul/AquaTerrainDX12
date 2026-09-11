#include <Windows.h>
#include "Application.h"
#include <fstream>

int CALLBACK wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
	
	try {
		Application* app = new Application();
		const int result = app->Run();
		delete app;
		return result;
	}
	catch (DxException& e)
	{
        wchar_t audit[32]{};
        if (GetEnvironmentVariableW(L"AQUA_SMOKE_AUDIT", audit, 32) ||
            GetEnvironmentVariableW(L"AQUA_SMOKE_REFERENCE", audit, 32))
        {
            std::wofstream log("smoke-audit-error.txt");
            log << e.ToString();
            return 3;
        }
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}

}
