/*
 * doctest's entry point, alone in its translation unit so the framework is compiled
 * once rather than per suite. Everything else in tests/ is TEST_CASEs.
 *
 *   ./run.ps1                       run everything
 *   ./run.ps1 --list-test-cases     what is there
 *   ./run.ps1 -tc="Ema:*"           run one group
 *   ./run.ps1 -s                    show successful assertions too
 */
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>
