#define NO_TEST_MAIN
#include "3rdparty/catch2/test_main.hpp"

DISABLE_COMPILER_WARNINGS
#include <QCoreApplication>
RESTORE_COMPILER_WARNINGS

// The benchmarks feed the test executable itself in as binary input, and applicationFilePath() is the only
// portable way to name it: hence a QCoreApplication around the session.
int main(int argc, char* argv[])
{
	const QCoreApplication application{ argc, argv };
	return runCatchSession(argc, argv);
}
