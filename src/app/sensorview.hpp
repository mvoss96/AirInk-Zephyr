#pragma once

/** @file
 * The readings view: measure the SCD41 and put the numbers on screen.
 *
 * The counterpart to app/menu.hpp. Neither module knows the other exists; main.cpp owns
 * the mode and switches between them. Like the menu, this one stages only its own view
 * and never refreshes the panel.
 */
namespace sensorview
{
	/** Read the SCD41 and stage the view.
	 *
	 * A full CO2 single-shot on every tenth call, a ~1000x cheaper T+RH read otherwise;
	 * the last CO2 value stays on screen in between. At main's 30 s cadence that is one
	 * CO2 read every five minutes.
	 *
	 * On a read error the error view is staged instead, and the cadence does not advance.
	 *
	 * @param force_co2 read CO2 now and restart the cadence, whatever the count says --
	 *                  after a recalibration the retained value is from the old one
	 */
	void measure(bool force_co2 = false);

	/** Re-select the view without measuring.
	 *
	 * The widgets still hold whatever the last measure() wrote, so coming back from the
	 * menu costs a partial refresh and nothing else.
	 */
	void show();
}
