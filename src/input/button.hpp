#pragma once
#include <stdint.h>

/** @file
 * The single user button (P0.11), classified into two gestures.
 *
 * One button is the whole input vocabulary of this device, so the meaning has to come
 * from how long it is held. The rule, everywhere: a tap dismisses, a hold commits.
 *
 * Zephyr's gpio-keys driver debounces and reports press/release; this module measures
 * the interval between them. Both the debounce and (in CONFIG_INPUT_MODE_SYNCHRONOUS)
 * the callback run on the system work queue, so a loop that blocks that queue loses
 * button presses -- which is why the measurement loop does not live there (main.cpp).
 */
namespace button
{
	// A hold longer than this is a Long press. Short enough to feel deliberate, long
	// enough that a tap never lands here by accident.
	constexpr int64_t LONG_PRESS_MS = 700;

	enum class Event : uint8_t
	{
		None, // the wait timed out
		Short,
		Long
	};

	/** Start delivering button events.
	 *
	 * @retval 0       the gpio-keys device is ready
	 * @retval -ENODEV no button in the devicetree
	 */
	int init();

	/** Wait for a gesture, but no longer than a deadline.
	 *
	 * Presses that arrive while the caller is busy are queued, not lost, so a tap
	 * during a five-second CO2 read is still acted upon afterwards.
	 *
	 * @param      deadline_ms absolute k_uptime_get() value to stop waiting at; a
	 *                         deadline already in the past polls without blocking
	 * @param[out] held_ms     optional: how long the button was actually down, which is
	 *                         the only way to see how close a gesture came to
	 *                         LONG_PRESS_MS. Untouched when the deadline passes first.
	 * @return the gesture, or Event::None if the deadline passed first
	 */
	Event wait_until(int64_t deadline_ms, uint16_t *held_ms = nullptr);

	/** Is the button held down right now?
	 *
	 * For hold-to-confirm countdowns, which need to know about the hold before it ends.
	 */
	bool is_down();
}
