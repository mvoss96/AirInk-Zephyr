#pragma once

#include "sensors/battery.hpp"
#include "sensors/scd41.hpp"
#include "ui/display_ui.hpp"

/** @file
 * The AirInk device itself: display, sensor, battery, button, menu, and the loop that
 * ties them together. Everything the product does when nobody is holding the button.
 *
 * This is a module rather than a main() because it has two callers. The standalone
 * firmware runs it as its only thread (apps/standalone/main.cpp). The Matter build runs it on a
 * thread of its own, alongside the CHIP event loop and OpenThread, and taps the hooks
 * below to publish the same numbers over Thread (apps/matter/src/app_task.cpp).
 *
 * The loop owns the panel: LVGL is not thread-safe, and nothing outside run()'s thread
 * may call ui::. A caller that wants to change what is on screen -- the Matter build
 * does, for the link indicator -- leaves the value somewhere the loop can pick it up.
 */
namespace app
{
	/** Observers, called on run()'s thread once per cycle. Either may be null.
	 *
	 * They exist so a caller can forward the readings somewhere else without the loop
	 * learning what that somewhere is. Both are called from run()'s thread, so a hook
	 * that blocks stalls the measurement cadence and the panel with it.
	 */
	struct Hooks
	{
		/** A fresh reading. Not called on a read error, and not called on the nine of
		 * ten cycles that skip the CO2 read -- those carry the retained value, and
		 * republishing it would be a lie about its age. */
		void (*reading)(const Scd41Reading &r);

		/** The battery, every cycle, whatever else happened. */
		void (*battery)(const battery::State &bat);

		/** Drop every network the device is on, because the user confirmed it on the panel.
		 *
		 * The loop knows the gesture; only the caller knows what a network is. Null in a build
		 * with no radio -- which never shows the screen that asks. */
		void (*factory_reset)();

		/** The user is looking at the onboarding code: make it scannable.
		 *
		 * Called every time the Matter view opens on an uncommissioned device. The code on the
		 * panel is only half the invitation -- the other half is the radio actually listening, and
		 * that window closes an hour after boot (CONFIG_CHIP_BLE_ADVERTISING_DURATION). Without
		 * this, a device left alone overnight shows a QR that nothing will answer.
		 *
		 * Idempotent: the view can be opened again while the window is already open.
		 */
		void (*pairing_open)();

		/** The user pressed the code away on a device that has never been commissioned.
		 *
		 * They saw the invitation and declined it, so stop shouting: the caller is expected to cut
		 * the window that boot opened down to something short. Not "close it" -- they may be
		 * walking to fetch their phone -- just stop it standing open for the rest of the hour.
		 *
		 * Only ever called from the boot onboarding screen, and never once a fabric exists. */
		void (*pairing_dismissed)();

		/** Tell the network what unit the panel is now showing, because the user chose it here.
		 *
		 * Also called once at start-up, and that call is what settles who is in charge: the Matter
		 * cluster keeps a value of its own and reloads it on boot, so without this the controller's
		 * copy could quietly outvote the one the user set on the device. prefs is the authority; this
		 * pushes it outward. Null in a build with no network. */
		void (*publish_unit)(ui::TempUnit u);

		/** What unit the network thinks the panel should show.
		 *
		 * Polled once a measurement tick, because there is nothing better: the SDK's Unit
		 * Localization cluster is a closed singleton -- it persists and reports the value itself and
		 * offers the application no callback, and being an AttributeAccessInterface it does not go
		 * through MatterPostAttributeChangeCallback either. So a write from Home Assistant is not
		 * announced; it can only be noticed. Once per tick is as fast as the panel can redraw anyway.
		 *
		 * @param[out] out the controller's unit; untouched if this returns false
		 * @return whether a unit could be read at all
		 */
		bool (*unit_from_network)(ui::TempUnit *out);

		/** How strong the link to the mesh is, for the bars in the status bar.
		 *
		 * Polled once a measurement tick. A signal strength has no event to fire -- it simply drifts
		 * -- so there is nothing to subscribe to, and once every 30 s is far more often than a person
		 * moving a device needs. Null in a build with no radio; the status bar then keeps its token.
		 *
		 * @param[out] out average RSSI to the parent router in dBm; untouched if this returns false
		 * @return false when there is no parent to measure -- not joined, or joined and not a child
		 */
		bool (*link_rssi)(int8_t *out);
	};

	/** Install the observers. Call before run(). */
	void set_hooks(const Hooks &hooks);

	/** Name this build, for the boot splash. Call before run().
	 *
	 * Two builds of this firmware exist and they look alike everywhere else, so the panel says
	 * which one is on the board. A caller that says nothing gets "Standalone", which is what a
	 * build that installs no hooks and no codes is.
	 *
	 * The string is not copied. It must outlive run(), which never returns.
	 */
	void set_build_name(const char *name);

	/** Install the device's onboarding codes. Call before run().
	 *
	 * With them, the menu offers a "Pairing code" entry that puts the QR and the manual code on
	 * the panel. Without them -- a build with no radio -- that entry does not exist, and neither
	 * does the view or the QR's draw buffer. So this is what decides it, not a compile switch:
	 * the menu offers pairing when there is something to pair with.
	 *
	 * The strings are not copied. They must outlive run(), which never returns.
	 *
	 * @param qr     the onboarding payload ("MT:...")
	 * @param manual the same code for humans ("1234-567-8901")
	 */
	void set_pairing_codes(const char *qr, const char *manual);

	/** Whether the device is already on a Matter fabric, from any thread.
	 *
	 * The Matter view asks: while there is still something to scan it shows the QR, and once the
	 * device is on a fabric there is nothing to scan, so it says so instead. Recorded here rather
	 * than queried, because the panel belongs to run()'s thread and the fabric table does not.
	 *
	 * Always false in a build with no radio -- which never shows the view anyway.
	 */
	void set_commissioned(bool on_fabric);
	bool commissioned();

	/** Ask the radio to accept a commissioner, because the code is now on the panel.
	 *
	 * Calls Hooks::pairing_open, if there is one. The menu calls this when it opens the Matter
	 * view on an uncommissioned device; nothing calls it when the view closes, and that is
	 * deliberate -- see menu.cpp.
	 */
	void open_pairing();

	/** Tell the network the user picked a unit on the panel.
	 *
	 * Calls Hooks::publish_unit, if there is one. The menu calls this after it has changed the unit
	 * and written it down; a build with no network has no hook and this does nothing.
	 */
	void publish_unit(ui::TempUnit u);

	/** What this device can actually do. The menu asks before it offers: a row exists because there
	 * is something behind it, not because of a compile switch -- and a cursor must not stop on a row
	 * that is not drawn, or a tap would appear to do nothing.
	 *
	 * Both answer from what run()'s caller handed over, so they are only meaningful after set_hooks()
	 * and the pairing codes -- which is to say, from inside the loop, which is where the menu lives.
	 */
	bool has_radio();
	bool can_factory_reset();

	/** The last temperature the sensor gave us, in hundredths of a degree Celsius.
	 *
	 * For the temperature-offset editor, which predicts what the panel will read once the offset the
	 * user is turning takes effect -- so they can tap until it agrees with the thermometer in their
	 * hand instead of doing the arithmetic themselves.
	 *
	 * @return the reading, or INT32_MIN if the sensor has not answered yet
	 */
	int32_t last_temp_x100();

	/** Report the radio state for the status bar, from any thread.
	 *
	 * Only records the value -- the panel belongs to run()'s thread, which puts it up on
	 * its next cycle. So the indicator can lag a connection by up to one cycle, which is
	 * the same latency an e-paper refresh imposes anyway.
	 */
	void set_link(ui::Link state);

	/** Bring up the display, sensors and button, then loop over measurement cycles.
	 *
	 * Never returns. A missing display or button is survivable and only logged; a
	 * missing SCD41 is not -- there is nothing to show -- so that one parks the error
	 * view on screen and blocks forever.
	 */
	void run();
}
