#include "context.hpp"

#if defined(USE_JEMALLOC)
#include "jemalloc/jemalloc.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

using namespace tengine;

Context ctx;

static void signal_handler(int signum)
{
    ctx.stop();
}

int main(int argc, char **argv)
{
	const char *config_file = NULL;
	if (argc > 1)
	{
		config_file = argv[1];
    }
	else
	{
		fprintf(stderr, "%s\n", "no config file.\n");
		return 1;
	}

    //signal(SIGINT, signal_handler);
	//signal(SIGTERM, signal_handler);
	//signal(SIGABRT, signal_handler);

#ifndef _WIN32
	signal(SIGPIPE, SIG_IGN);
#endif

#if defined(USE_JEMALLOC)
	je_init();
#endif

	int ret = ctx.start(config_file);
	if (ret < 0)
		return 1;

	ctx.join();

#ifdef USE_JEMALLOC
	je_uninit();
#endif

    fprintf(stderr, "%s\n", "closed!!!");

	return 0;
}
