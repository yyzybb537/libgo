#pragma once
#include <chrono>
#include <thread>
#include <string>
#include <stdio.h>

#ifdef _WIN32
#include <stdlib.h>
struct exit_pause {
	~exit_pause()
	{
        std::string commandline(GetCommandLineA());
        //printf("commandline: [%s]\n", commandline.c_str());
        if (std::count(commandline.begin(), commandline.end(), ' ') <= 1)
		    system("pause");
	}
} g_exit;
#endif
