#include "check.hpp"

void test_ema();
void test_battery_curve();
void test_quantize();

int main()
{
	struct
	{
		const char *name;
		void (*run)();
	} suites[] = {
		{"ema", test_ema},
		{"battery_curve", test_battery_curve},
		{"quantize", test_quantize},
	};

	/* Any FAIL lines print from inside the suite, just above its own verdict. */
	for (auto &s : suites)
	{
		const int before = g_failures;
		s.run();
		std::printf("%-16s %s\n", s.name, (g_failures == before) ? "ok" : "FAILED");
	}

	std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
	return (g_failures == 0) ? 0 : 1;
}
