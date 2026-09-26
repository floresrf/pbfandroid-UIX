package wootbeer.pbfandroid;

import java.util.ArrayList;
import java.util.List;

import wootbeer.descore.DescoreGameConfig;

/**
 * Pinball Fantasies' description for the shared descore launcher: what the player has to supply
 * from their own copy of the DOS game, and where the native library that runs it lives.
 *
 * The DOS release keeps everything flat in one folder (the FANTASY directory of a 21STCENT
 * install): INTRO.PRG plus one TABLEn.PRG/TABLEn.MOD pair per table. The launcher scans the picked
 * folder recursively, so pointing at the whole install works too. Party Land (table 1) is the one
 * required table; the other three are picked up when present, so a partial copy still plays.
 */
final class PbfConfig {
	private PbfConfig() {
	}

	static final int UNIT_GAME = 1;

	static DescoreGameConfig create() {
		List<DescoreGameConfig.Unit> units = new ArrayList<>();
		units.add(new DescoreGameConfig.Unit(
				UNIT_GAME,
				"Pinball Fantasies",
				"game",
				new String[]{"table1.prg", "table1.mod"},
				new String[]{"intro.prg", "intro.mod", "mod2.mod",
						"table2.prg", "table2.mod",
						"table3.prg", "table3.mod",
						"table4.prg", "table4.mod"},
				new String[]{}));
		return new DescoreGameConfig(
				"Pinball Fantasies",
				"pbfhost",
				"pbf",
				"Select the folder that contains your own copy of the game.\n\n"
						+ "Copy all of the .prg files and all of the .mod files from the game's folder "
						+ "(intro.prg, table1.prg - table4.prg, intro.mod, mod2.mod and "
						+ "table1.mod - table4.mod). Nothing else is needed.",
				units,
				true);   // once all 11 files are in place, skip the picker on every later launch
	}
}
