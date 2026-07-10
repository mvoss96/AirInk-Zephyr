#include "button.hpp"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/input/input.h>

namespace
{
	const struct device *const btn_dev = DEVICE_DT_GET(DT_PATH(buttons));

	struct Gesture
	{
		uint8_t kind;    // button::Event
		uint16_t held_ms; // capped; only ever read by a log line
	};

	/* A queue, not a k_event: two taps in quick succession are two gestures, and an
	 * event bit would silently merge them. Four deep is more than a human produces
	 * during the longest thing we block on (a ~5 s CO2 read). */
	K_MSGQ_DEFINE(gesture_q, sizeof(Gesture), 4, 4);

	int64_t press_ms;
	bool down;
	bool long_fired; // the hold already became an event; the release is not a tap

	/** Fire the hold while the finger is still down.
	 *
	 * Classifying on release would mean the user holds, sees nothing happen, lets go,
	 * and only then the screen changes. Runs in timer (ISR) context, so it does the one
	 * thing that is safe there: enqueue.
	 */
	void long_press_expiry(struct k_timer *t)
	{
		ARG_UNUSED(t);

		Gesture g = {.kind = (uint8_t)button::Event::Long,
					 .held_ms = (uint16_t)button::LONG_PRESS_MS};
		long_fired = true;
		k_msgq_put(&gesture_q, &g, K_NO_WAIT);
	}
	K_TIMER_DEFINE(long_timer, long_press_expiry, nullptr);

	/** Turn press/release into a gesture.
	 *
	 * Runs in the reporting context (CONFIG_INPUT_MODE_SYNCHRONOUS: the gpio-keys
	 * debounce work item), so it only timestamps and posts -- never blocks, never
	 * touches the panel.
	 *
	 * @param ev   the input event; anything but our key is ignored
	 * @param user unused
	 */
	void on_input(struct input_event *ev, void *user)
	{
		ARG_UNUSED(user);

		if (ev->type != INPUT_EV_KEY || ev->code != INPUT_KEY_0)
		{
			return;
		}

		if (ev->value)
		{
			press_ms = k_uptime_get();
			down = true;
			long_fired = false;
			k_timer_start(&long_timer, K_MSEC(button::LONG_PRESS_MS), K_NO_WAIT);
			return;
		}
		if (!down)
		{
			return; // a release we never saw the press for (e.g. held across boot)
		}

		// Stop before reading the flag: afterwards the timer cannot fire any more, so
		// if it beat us to it, long_fired is already set and this release is not a tap.
		k_timer_stop(&long_timer);
		down = false;
		if (long_fired)
		{
			return;
		}

		const int64_t held = k_uptime_get() - press_ms;
		Gesture g = {
			.kind = (uint8_t)button::Event::Short,
			.held_ms = (uint16_t)((held > UINT16_MAX) ? UINT16_MAX : held),
		};
		k_msgq_put(&gesture_q, &g, K_NO_WAIT); // full queue: drop, never block
	}

	INPUT_CALLBACK_DEFINE(btn_dev, on_input, nullptr);

} // namespace

int button::init()
{
	return device_is_ready(btn_dev) ? 0 : -ENODEV;
}

button::Event button::wait_until(int64_t deadline_ms, uint16_t *held_ms)
{
	const int64_t now = k_uptime_get();
	const k_timeout_t timeout = (deadline_ms <= now) ? K_NO_WAIT : K_MSEC(deadline_ms - now);

	Gesture g;
	if (k_msgq_get(&gesture_q, &g, timeout) != 0)
	{
		return Event::None;
	}
	if (held_ms)
	{
		*held_ms = g.held_ms;
	}
	return (Event)g.kind;
}

bool button::is_down()
{
	return down;
}
