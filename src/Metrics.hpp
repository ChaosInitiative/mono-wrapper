/**
 * 
 * metrics.hpp 
 * 
 * Simple metrics tracking
 * 
 */ 
#pragma once

#include <list>
#include <vector>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include <sys/resource.h>

class ResourceContext
{
private:
	struct Entry_t {
		unsigned long long time;
		unsigned long long rssSize;
		const char* name;
	};
	std::vector<Entry_t> m_entries;
	Entry_t activePoint;
public:
	ResourceContext()
	{
	}

	void BeginPoint(const char* name)
	{
		activePoint.name = name;
		timespec tp;
		clock_gettime(CLOCK_MONOTONIC, &tp);
		activePoint.time = tp.tv_sec * 1000000000;
		activePoint.time += tp.tv_nsec;
		
		rusage usage;
		getrusage(RUSAGE_SELF, &usage);
		activePoint.rssSize = usage.ru_idrss;
	}

	void EndPoint()
	{
		timespec tp;
		clock_gettime(CLOCK_MONOTONIC, &tp);
		unsigned long long t = tp.tv_sec * 1000000000;
		t += tp.tv_nsec;

		rusage usage;
		getrusage(RUSAGE_SELF, &usage);
		auto rss = activePoint.rssSize;
		auto time = activePoint.time;
		activePoint.rssSize = usage.ru_idrss - rss;
		activePoint.time = t - time;

		m_entries.push_back(activePoint);
		memset(&activePoint, 0, sizeof(Entry_t));
	}


	void Report()
	{
		for(Entry_t e : m_entries) {
			printf("\nTest: %s\n", e.name);
			printf("\tTime: %llu ns (%f ms)\n", e.time, (e.time / 1e6f));
			printf("\tMem Usage Change: %f kb\n", e.rssSize / 1e3f);
		}
	}
};