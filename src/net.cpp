#include "net.hpp"

#include <zephyr/kernel.h>

#include "ui/display_ui.hpp"
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

	/* What was last sent, at the resolution the panel shows. See publish_reading(). */
	uint16_t pub_co2_ppm;
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

void net::publish_reading(const Scd41Reading &r)
{
	if (!hooks.reading)
	{
		return;
	}

	// The same key the panel dedups on: CO2 to the ppm, T and RH to what is displayed. So the
	// controller and the panel change in the same moment, and neither ever hears about a change
	// the other could not show.
	const int32_t t_q = ui::quantize_temp_x100(r.temp_x100);
	const uint16_t h_q = ui::quantize_hum_x100(r.hum_x100);

	if (have_published && r.co2_ppm == pub_co2_ppm && t_q == pub_temp_x100 && h_q == pub_hum_x100)
	{
		return; // nothing a person could tell apart has changed
	}

	hooks.reading(r);
	pub_co2_ppm = r.co2_ppm;
	pub_temp_x100 = t_q;
	pub_hum_x100 = h_q;
	have_published = true;
}

void net::publish_battery(const battery::State &bat)
{
	if (hooks.battery)
	{
		hooks.battery(bat);
	}
}

bool net::unit_from_network(ui::TempUnit *out)
{
	return hooks.unit_from_network && hooks.unit_from_network(out);
}

/** Ask the radio how well it is heard; onto the status bar as 0..4 bars, -1 for no link at all.
 * The quantizer's hysteresis memory lives here, at its only caller. */
void net::poll_signal()
{
	// Only a LOST link empties the corner. A failed RSSI read on a live link leaves the bars
	// standing -- the last measurement is still the best answer, and flapping costs ~3 mAs a go.
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
	if (bars != prev_bars)
	{
		// Log only movement: the dBm drift every half minute is noise, the moment the panel
		// changes is the line you read when deciding where to put the device.
		printk("[SIG] %d dBm -> %d bars\n", rssi, bars);
	}

	prev_bars = bars;
	ui::set_signal_bars(bars);
}
