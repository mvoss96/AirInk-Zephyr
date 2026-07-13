#include "app.hpp"

/** The standalone firmware: the device is the whole program, so main is the loop.
 *
 * The Matter build has the same loop, but not as main -- there it is one thread among
 * the CHIP event loop and OpenThread (matter/src/app_task.cpp).
 */
int main(void)
{
	app::run();
}
