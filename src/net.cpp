#include "net.hpp"

#include <zephyr/kernel.h>

#include "prefs.hpp"
#include "ui/quantize.hpp"

namespace
{
	net::Hooks hooks;

	// Not copied: string literals or static buffers owned by the network build (see net.hpp).
	const char *qr_code;
	const char *manual_code;

	// Written by the network's threads, read by the loop. A plain word store is atomic on this
	// core, and a stale read costs nothing worse than one cycle of a stale indicator.
	bool thread_up;
	bool is_commissioned;

	/* What the status bar last drew, kept here because the quantizer's hysteresis needs the level
	 * being held -- and this is its only caller. -1 = the corner is empty. */
	int prev_bars = -1;

	/* What was last sent, quantized to what a person could tell apart. See publish_reading(). */
	int32_t pub_temp_x100;
	uint16_t pub_hum_x100;
	bool have_published;
} // namespace

void net::set_hooks(const Hooks &h)
{
	hooks = h;
}

void net::set_pairing_codes(const char *qr, const char *manual)
{
	qr_code = qr;
	manual_code = manual;
}

const char *net::pair_qr()
{
	return qr_code;
}

const char *net::pair_manual()
{
	return manual_code;
}

bool net::has_radio()
{
	return qr_code != nullptr;
}

bool net::can_factory_reset()
{
	return hooks.factory_reset != nullptr;
}

void net::set_commissioned(bool on_fabric)
{
	is_commissioned = on_fabric;
}

bool net::commissioned()
{
	return is_commissioned;
}

void net::set_thread_connected(bool up)
{
	thread_up = up;
}

void net::open_pairing()
{
	if (hooks.pairing_open)
	{
		hooks.pairing_open();
	}
}

void net::dismiss_pairing()
{
	if (hooks.pairing_dismissed)
	{
		hooks.pairing_dismissed();
	}
}

void net::factory_reset()
{
	if (hooks.factory_reset)
	{
		hooks.factory_reset();
	}
}

void net::publish_unit(ui::TempUnit u)
{
	if (hooks.publish_unit)
	{
		hooks.publish_unit(u);
	}
}

void net::publish_reading(const Scd41Reading &r, bool fresh_co2)
{
	if (!hooks.reading)
	{
		return;
	}

	const int32_t t_q = ui::quantize_temp_x100(r.temp_x100);
	const uint16_t h_q = ui::quantize_hum_x100(r.hum_x100);
	const bool moved = !have_published || t_q != pub_temp_x100 || h_q != pub_hum_x100;

	if (fresh_co2 || moved)
	{
		hooks.reading(r);
		pub_temp_x100 = t_q;
		pub_hum_x100 = h_q;
		have_published = true;
	}
}

void net::publish_battery(const battery::State &bat)
{
	if (hooks.battery)
	{
		hooks.battery(bat);
	}
}

/** Adopt a temperature unit the controller set, if it did.
 *
 * The other half of the menu's toggle. A user with the phone in their hand may well change the unit
 * from Home Assistant rather than walk to the device, and the panel has to agree with what the app
 * shows -- a thermometer that disagrees with its own record is worse than one with no app at all.
 *
 * What comes in goes into prefs, which repaints the panel and writes it down -- so the device boots on
 * the last thing anyone chose, whoever chose it and wherever. adopt() and not set(), because the one
 * thing that must NOT happen is telling the network what the network has just told us.
 */
static void pull_unit()
{
	if (!hooks.unit_from_network)
	{
		return; // no network, no second opinion
	}

	// Compared against the store: a value that has not moved would be dropped by prefs anyway; the
	// point of the guard is the log line below, which would otherwise appear every half minute for
	// ever.
	ui::TempUnit u;
	if (!hooks.unit_from_network(&u) || (int32_t)u == prefs::get(prefs::Unit))
	{
		return;
	}

	printk("[PREFS] temp unit %s (from the network)\n", u == ui::TempUnit::Fahrenheit ? "F" : "C");
	prefs::adopt(prefs::Unit, (int32_t)u);
}

/** Ask the radio how well it is heard, and put the answer on the status bar as 0..4 bars.
 *
 * Everything short of a measured, joined link comes out as -1 -- no radio in this build, not
 * joined, or joined with no parent to be a child of yet -- and -1 draws nothing: an empty corner is
 * the honest amount to say, and four hollow outlines would claim "attached, no signal", a real and
 * much worse state.
 *
 * The quantizer's hysteresis (ui::quantize_signal_bars) is fed from here, because this is its only
 * caller: a link parked on a threshold must not flip the panel back and forth at ~3 mAs a go.
 */
static void pull_signal()
{
	// A lost link empties the corner. Anything less final -- one failed RSSI read on a link that is
	// still up -- leaves the bars standing: the last measurement is still the best answer there is,
	// and flapping the panel over a hiccup would cost ~3 mAs a flap.
	if (!thread_up)
	{
		prev_bars = -1;
		ui::set_signal_bars(-1);
		return;
	}

	int8_t rssi;
	if (!hooks.link_rssi || !hooks.link_rssi(&rssi))
	{
		return;
	}

	const int bars = ui::quantize_signal_bars(rssi, prev_bars);

	// Log only when the bars actually move. The dBm behind them drifts a little every half minute
	// and saying so every time would drown the log in noise -- but the moment the panel changes is
	// worth a line, because that is the line you read when you are deciding where to put the device.
	if (bars != prev_bars)
	{
		printk("[SIG] %d dBm -> %d bars\n", rssi, bars);
	}

	prev_bars = bars;
	ui::set_signal_bars(bars);
}

void net::poll()
{
	pull_unit();
	pull_signal();
}
