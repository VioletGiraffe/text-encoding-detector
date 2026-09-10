#include "compiler/compiler_warnings_control.h"

#define CATCH_CONFIG_RUNNER
DISABLE_COMPILER_WARNINGS
#include "3rdparty/catch2/catch.hpp"

#include <QCoreApplication>
RESTORE_COMPILER_WARNINGS

// Catch2's own main creates no QCoreApplication, and the benchmarks feed the test executable itself in as
// binary input: applicationFilePath() is the only portable way to name it.
int main(int argc, char* argv[])
{
	const QCoreApplication application{ argc, argv };
	return Catch::Session().run(argc, argv);
}
