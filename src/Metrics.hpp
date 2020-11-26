/**
 * 
 * metrics.hpp 
 * 
 * Simple metrics tracking
 * 
 */ 
#pragma once

#include <list>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>

class ResourceContext
{
private:
	FILE* m_proc;

	struct Entry_t {
		unsigned long long time;
		unsigned long long rssSize;
		unsigned long long vMemSize;
	};
	std::list<Entry_t> m_entries;
public:
	ResourceContext()
	{
		m_proc = fopen("/proc/self/status", "r");
		assert(m_proc);
	}

	void LogUsage(const char* stage)
	{
		timespec tp;
		clock_gettime(CLOCK_MONOTONIC, &tp);
		Entry_t ent;
		ent.time = tp.tv_sec * 1000000000;
		ent.time += tp.tv_nsec;
		
		fseek(m_proc, SEEK_SET, 0);
		char buf[4096];
		size_t read = fread(&buf, sizeof(buf), 1, m_proc);

		for(char* tok = strtok(buf, "\n"); tok; tok = strtok(NULL, "\n")) {
			if(strncmp(tok, "RssAnon", 7) == 0) {

			}
			else if(strncmp(tok, "VmSize", 6) == 0) {
				
			}
		}
	}

	void Report()
	{

	}
};