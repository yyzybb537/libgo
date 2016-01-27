#pragma once
#ifndef _WIN32
#include <unistd.h>
#include <boost/algorithm/string.hpp>
#else
#include <Windows.h>
#include <Psapi.h>
#pragma comment(lib,"Psapi.lib")
#endif

#include <fstream>
#include <string>
#include <vector>

static const char* bytes_level[] = {
    "B",
    "KB",
    "MB",
    "GB",
    "TB",
    "PB",
};

struct pinfo
{
    uint64_t virt_high = 0; // 虚拟内存峰值
    uint64_t virt = 0;      // 虚拟内存
    uint64_t rss_high = 0;  // 物理内存峰值
    uint64_t rss = 0;       // 物理内存
    uint64_t swap = 0;      // swap内存使用量

    pinfo()
    {
#ifndef _WIN32
        pid_t pid = getpid();
        char filename[128];
        sprintf(filename, "/proc/%d/status", pid);
        std::ifstream in(filename);
        std::string finfo;
        int ch;
        while ((ch = in.get()) != EOF)
            finfo += (char)ch;
        std::vector<std::string> lines;
        boost::split(lines, finfo, boost::is_any_of("\r\n"));
        for (auto &s : lines)
        {
            if (boost::starts_with(s, "VmPeak")) {
                sscanf(s.c_str(), "VmPeak: %llu KB", (long long unsigned int*)&virt_high);
            } else if (boost::starts_with(s, "VmSize")) {
                sscanf(s.c_str(), "VmSize: %llu KB", (long long unsigned int*)&virt);
            } else if (boost::starts_with(s, "VmHWM")) {
                sscanf(s.c_str(), "VmHWM: %llu KB", (long long unsigned int*)&rss_high);
            } else if (boost::starts_with(s, "VmRSS")) {
                sscanf(s.c_str(), "VmRSS: %llu KB", (long long unsigned int*)&rss);
            } else if (boost::starts_with(s, "VmSwap")) {
                sscanf(s.c_str(), "VmSwap: %llu KB", (long long unsigned int*)&swap);
            } 
        }
#else
		PROCESS_MEMORY_COUNTERS meminfo;
		GetProcessMemoryInfo(GetCurrentProcess(), &meminfo, sizeof(meminfo));
		rss_high = meminfo.PeakWorkingSetSize / 1024;
		rss = meminfo.WorkingSetSize / 1024;
		virt_high = virt = 0;
#endif
    }

    uint64_t get_virt()
    {
        return virt;
    }

    uint64_t get_mem()
    {
        return rss + swap;
    }

    std::string get_virt_str()
    {
        double v = virt;
        int level = 1;
        while (v > 1024) {
            v = v / 1024;
            ++level;
        }
        return std::to_string(v) + " " + bytes_level[level];
    }

    std::string get_mem_str()
    {
        double v = rss + swap;
        int level = 1;
        while (v > 1024) {
            v = v / 1024;
            ++level;
        }
        return std::to_string(v) + " " + bytes_level[level];
    }

};

