#pragma once
#include <chrono>
#include <thread>
#include <stdio.h>

#ifdef _WIN32
#include <stdlib.h>
struct exit_pause {
	~exit_pause()
	{
		system("pause");
	}
} g_exit;
#endif
