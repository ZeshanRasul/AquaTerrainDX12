#pragma once
#include <Windows.h>
#include "Application.h"

int CALLBACK wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
	Application* app = new Application();
	app->Run();
	delete app;
}