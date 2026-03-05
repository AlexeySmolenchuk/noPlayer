#include "noPlayer.h"

int main(int argc, char**argv)
{
	// Create application instance and run until window close.
	NoPlayer noPlayerApp;

	if (argc > 1)
		noPlayerApp.init(argv[1]);

	noPlayerApp.run();

	return 0;
}
