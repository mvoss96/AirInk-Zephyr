#pragma once

#include <stdint.h>

#include "sensors/battery.hpp"
#include "sensors/scd41.hpp"
#include "ui/temp_unit.hpp" // ui::TempUnit

/** @file
 * The device's edge to a network, whichever network that is.
 *
 * app.cpp measures, waits and paints; everything it says to the outside world goes through here.
 * The Matter build installs the hooks below (apps/matter/src/app_task.cpp); the standalone build
 * installs nothing and every function quietly answers "no" -- which is why the loop has no #ifdefs.
 *
 * Threading: hooks are called on the loop's thread (a hook that blocks stalls the panel). The
 * set_* functions marked below are called from the network's threads; they only store a word,
 * which is atomic on this core, and the loop reads it on its next pass.
 */
namespace net
{
	/** What a network build provides. Any pointer may be null: a null hook is a feature this build
	 * does not have. */
	struct Hooks
	{
		/** A reading worth telling the network about (publish_reading() decides which). */
		void (*reading)(const Scd41Reading &r);

		/** The battery, every cycle. */
		void (*battery)(const battery::State &bat);

		/** Drop every network the device is on; the user confirmed it on the panel. */
		void (*factory_reset)();

		/** The onboarding code is on the panel: start listening. The boot window closes an hour
		 * after boot (CONFIG_CHIP_BLE_ADVERTISING_DURATION); without this, a device on a shelf
		 * shows a QR nothing will answer. Idempotent. */
		void (*pairing_open)();

		/** The user dismissed the QR screen on a device with no fabric: cut the advertising window
		 * short, but do not close it -- they may be fetching their phone. (Cutting a menu-opened
		 * 10-minute window to 10 minutes is a harmless no-op; the hour the boot opened is the
		 * case that matters.) Never called once a fabric exists. */
		void (*pairing_dismissed)();

		/** Tell the network the panel's unit. Also called at boot (via prefs::apply_all()), which
		 * is what makes prefs the authority and the cluster the mirror -- the cluster reloads its
		 * own copy on boot and would otherwise win every restart. */
		void (*publish_unit)(ui::TempUnit u);

		/** The unit the network thinks the panel should show. Polled by poll(): the SDK's Unit
		 * Localization server is a closed singleton with no callback, and its writes bypass
		 * MatterPostAttributeChangeCallback -- a write can only be noticed, never announced.
		 * @return false if no unit could be read; *out untouched */
		bool (*unit_from_network)(ui::TempUnit *out);

		/** Average RSSI to the parent router, in dBm. Polled by poll(); false = no parent. */
		bool (*link_rssi)(int8_t *out);
	};

	/** Install the network. Call before app::run(). */
	void set_hooks(const Hooks &hooks);

	/** Install the onboarding codes ("MT:..." / "1234-567-8901"). Not copied; call before
	 * app::run(). Their presence is what makes the device HAVE a radio -- menu row, QR view,
	 * splash mark all key on it (has_radio()), not on a compile switch. */
	void set_pairing_codes(const char *qr, const char *manual);

	/** The codes for the panel's Matter view; null without a radio. */
	const char *pair_qr();
	const char *pair_manual();

	/** Whether there is a network to speak of. The menu asks before it offers a row. */
	bool has_radio();
	bool can_factory_reset();

	/** Whether the device is on a fabric. Written from the network's threads -- on the Matter
	 * build, seeded at boot and then kept current by a fabric-table delegate. */
	void set_commissioned(bool on_fabric);
	bool commissioned();

	/** Whether the Thread link is up. Written from the network's threads; poll() gates the signal
	 * bars on it, so a lost link empties the status bar on the loop's next pass. */
	void set_thread_connected(bool up);

	/** Gesture forwarders: each calls its hook if there is one, so callers never test for the
	 * network themselves. */
	void open_pairing();
	void dismiss_pairing();
	void factory_reset();
	void publish_unit(ui::TempUnit u);

	/** What unit the network thinks the panel should show (see Hooks::unit_from_network). The loop
	 * polls this and hands the answer to prefs::adopt() -- the bridge lives in the loop so that
	 * prefs and net never call each other. @return false = no network, or no unit to read */
	bool unit_from_network(ui::TempUnit *out);

	/** Publish when the CO2 is fresh or the displayed reading moved. Measured: publishing every
	 * tick costs 14 days of battery; every tenth left the controller 5 min behind the panel. The
	 * quantization decides WHEN, never WHAT -- the hook gets the full-resolution reading, and the
	 * threshold is in Celsius/whole percent regardless of the unit on the panel. */
	void publish_reading(const Scd41Reading &r, bool fresh_co2);

	/** The battery, every cycle. */
	void publish_battery(const battery::State &bat);

	/** Pull the signal strength onto the status bar, once a cycle. A poll, because a signal
	 * strength has no event to subscribe to. Touches the panel: loop's thread only. */
	void poll_signal();
}
