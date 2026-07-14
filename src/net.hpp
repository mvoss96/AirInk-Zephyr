#pragma once

#include <stdint.h>

#include "sensors/battery.hpp"
#include "sensors/scd41.hpp"
#include "ui/display_ui.hpp" // ui::TempUnit

/** @file
 * The device's edge to a network, whichever network that is.
 *
 * app.cpp measures, waits and paints; everything it says to the world outside the panel -- and
 * everything that world says back -- goes through here. The module is deliberately ignorant of what
 * the network IS: the Matter build installs the hooks below (apps/matter/src/app_task.cpp) and this
 * file forwards; the standalone build installs nothing, and every function here quietly answers "no".
 * That is why the loop has no #ifdefs: absence of a radio is a value, not a build flavour.
 *
 * Threading, in one rule: the hooks are called on the loop's thread, and the setters marked so below
 * are called from the network's own threads. The setters only store -- a plain word store is atomic
 * on this core -- and the loop picks the values up on its next pass, which is as fast as e-paper can
 * show them anyway.
 */
namespace net
{
	/** What a network build provides. Any pointer may be null; a null hook is a feature the build
	 * does not have. All of them are called on the loop's thread, so a hook that blocks stalls the
	 * measurement cadence and the panel with it. */
	struct Hooks
	{
		/** A reading worth telling the network about. publish_reading() decides which those are;
		 * the value handed over is always the full-resolution one. */
		void (*reading)(const Scd41Reading &r);

		/** The battery, every cycle, whatever else happened. */
		void (*battery)(const battery::State &bat);

		/** Drop every network the device is on, because the user confirmed it on the panel.
		 * The loop knows the gesture; only the network knows what a network is. */
		void (*factory_reset)();

		/** The user is looking at the onboarding code: make it scannable.
		 *
		 * Called every time the Matter view opens on an uncommissioned device. The code on the
		 * panel is only half the invitation -- the other half is the radio actually listening, and
		 * that window closes an hour after boot (CONFIG_CHIP_BLE_ADVERTISING_DURATION). Without
		 * this, a device left alone overnight shows a QR that nothing will answer.
		 *
		 * Idempotent: the view can be opened again while the window is already open. */
		void (*pairing_open)();

		/** The user pressed the code away on a device that has never been commissioned.
		 *
		 * They saw the invitation and declined it, so stop shouting: the network is expected to cut
		 * the window that boot opened down to something short. Not "close it" -- they may be
		 * walking to fetch their phone -- just stop it standing open for the rest of the hour.
		 *
		 * Only ever called from the boot onboarding screen, and never once a fabric exists. */
		void (*pairing_dismissed)();

		/** Tell the network what unit the panel shows. prefs calls this (via publish_unit()) every
		 * time the unit changes on the device, boot included -- and the boot call is what settles
		 * who is in charge: the Matter cluster reloads a copy of its own, so a device that only
		 * ever listened would let the controller's copy win every restart. prefs is the authority;
		 * the cluster is the mirror. */
		void (*publish_unit)(ui::TempUnit u);

		/** What unit the network thinks the panel should show.
		 *
		 * Polled (see poll()), because there is nothing better: the SDK's Unit Localization cluster
		 * is a closed singleton -- it persists and reports the value itself and offers the
		 * application no callback, and being an AttributeAccessInterface it does not go through
		 * MatterPostAttributeChangeCallback either. A write from Home Assistant is not announced;
		 * it can only be noticed.
		 *
		 * @param[out] out the controller's unit; untouched if this returns false
		 * @return whether a unit could be read at all */
		bool (*unit_from_network)(ui::TempUnit *out);

		/** How strong the link to the mesh is, for the bars in the status bar.
		 *
		 * Polled (see poll()): a signal strength has no event to fire -- it simply drifts.
		 *
		 * @param[out] out average RSSI to the parent router in dBm; untouched if this returns false
		 * @return false when there is no parent to measure -- not joined, or joined and not a child */
		bool (*link_rssi)(int8_t *out);
	};

	/** Install the network. Call before app::run(); never, in this firmware, called twice. */
	void set_hooks(const Hooks &hooks);

	/** Install the device's onboarding codes. Call before app::run().
	 *
	 * This is what makes the device HAVE a radio, as far as the rest of the firmware is concerned:
	 * the menu offers a "Matter" row, the boot flow shows the QR, the splash carries the mark --
	 * all of it keyed on these codes existing, not on a compile switch (has_radio()).
	 *
	 * The strings are not copied. They must outlive the loop, which never returns.
	 *
	 * @param qr     the onboarding payload ("MT:...")
	 * @param manual the same code for humans ("1234-567-8901")
	 */
	void set_pairing_codes(const char *qr, const char *manual);

	/** The codes, for the panel's Matter view. Null on a build without them. */
	const char *pair_qr();
	const char *pair_manual();

	/** Whether there is a network to speak of at all. The menu asks before it offers: a row exists
	 * because there is something behind it. */
	bool has_radio();
	bool can_factory_reset();

	/** Whether the device is already on a fabric. Written from the network's threads (it rides
	 * along in the battery hook, which holds the stack lock); read by the loop and the menu. */
	void set_commissioned(bool on_fabric);
	bool commissioned();

	/** Whether the device currently has a Thread link. Written from the network's threads on
	 * connectivity events; poll() gates the signal bars on it, so a lost link empties the status
	 * bar on the loop's next pass rather than leaving stale bars standing on the panel. */
	void set_thread_connected(bool up);

	/** Forwarders for the gestures. Each calls its hook, if there is one, and does nothing
	 * otherwise -- so the loop and the menu never test for the network's presence themselves. */
	void open_pairing();
	void dismiss_pairing();
	void factory_reset();
	void publish_unit(ui::TempUnit u);

	/** Hand a fresh reading to the network -- if it is worth saying.
	 *
	 * Not every tick, and not only every tenth. Every tenth (with the CO2 read) left a controller
	 * up to five minutes behind the panel; every tick was MEASURED to cost 16.2 mAs per cycle --
	 * +54 uA, 112 days down to 98. Fourteen days for freshness nobody had asked for.
	 *
	 * So: when the reading actually moves. The panel already decides that -- it deduplicates on
	 * what it can show -- and a still room moves a tenth of a degree once or twice in five minutes,
	 * not ten times. The quantization decides WHEN to speak, never WHAT to say: the value handed to
	 * the hook is the full-resolution one, and the threshold is in Celsius and whole percent
	 * regardless of what the panel displays.
	 *
	 * @param r         the reading, full resolution
	 * @param fresh_co2 this tick ran the CO2 single-shot, so the CO2 number is genuinely new --
	 *                  that always goes, whatever the temperature did
	 */
	void publish_reading(const Scd41Reading &r, bool fresh_co2);

	/** The battery, and whatever rides along in the hook (the Matter build refreshes its view of
	 * the fabric table here). Every cycle. */
	void publish_battery(const battery::State &bat);

	/** Ask the network what it has to say, once a cycle: the unit (adopted into prefs if it moved)
	 * and the signal strength (onto the status bar). A poll and not a callback because neither has
	 * an event to hang one on -- see the hooks above.
	 *
	 * Touches the panel, so: loop's thread only.
	 */
	void poll();
}
