/* Some basic utils */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

namespace util
{

static constexpr const char* GREEN_BG = "\e[102m";
static constexpr const char* GREEN_FG = "\e[92m";
static constexpr const char* RED_BG = "\e[101m";
static constexpr const char* RED_FG = "\e[91m";
static constexpr const char* RESET = "\e[0m";

extern unsigned int PassedTests;
extern unsigned int TotalTests;

static bool SupportsColors()
{
	static bool init = false;
	static bool supports_colors = false;
	
	if(!init)
	{
		init = true;
		const char* truecolor = getenv("COLORTERM");
		const char* term = getenv("TERM");
		if(!strcmp(truecolor, "truecolor") || !strcmp(term, "xterm-256color"))
			supports_colors = true;
	}
	return supports_colors;
}

static void SetStreamColor(FILE* stream, const char* color)
{
	fprintf(stream, color);
}

static void ReportPass(const char* file, unsigned line, const char* fmt, ...)
{
	printf("%sPASSED%s [%s:%u] ", GREEN_FG, RESET, file, line);
	va_list vl;
	va_start(vl, fmt);
	vprintf(fmt, vl);
	va_end(vl);
	fputc('\n', stdout);
}

static void ReportFail(const char* file, unsigned line, const char* fmt, ...)
{
	printf("%sFAILED%s [%s:%u] ", RED_FG, RESET, file, line);
	va_list vl;
	va_start(vl, fmt);
	vprintf(fmt, vl);
	va_end(vl);
	fputc('\n', stdout);
}

}

#define REPORT_PASS(...) do { util::ReportPass(__FUNCTION__, __LINE__, __VA_ARGS__); util::TotalTests++; util::PassedTests++; } while(0)
#define REPORT_FAIL(...) do { util::ReportFail(__FUNCTION__, __LINE__, __VA_ARGS__); util::TotalTests++; } while(0)