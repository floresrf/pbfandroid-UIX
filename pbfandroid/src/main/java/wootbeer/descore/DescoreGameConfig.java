package wootbeer.descore;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * Everything a descore-shell game has to tell the shared launcher/game classes about itself.
 * A game supplies one of these (see e.g. wootbeer.pbfandroid.PbfConfig) and gets the asset
 * picker, the copy-into-private-storage step, and the game/render-thread shell for free.
 *
 * Game data is proprietary and is never bundled: the player points the launcher at a folder
 * holding their own copy, and the launcher copies only the files named here into private app
 * storage under {@code <filesDir>/<rootDirName>/<unit.dirName>/}. Native code then finds
 * everything with plain relative or absolute file paths, no Android storage APIs involved.
 *
 * A "unit" is one independently playable/verifiable chunk of game data: a game with several
 * separately playable parts can use one unit per part; Pinball Fantasies is one unit (the intro
 * plus the four tables).
 */
public final class DescoreGameConfig {

	public static final class Unit {
		public final int id;
		public final String title;
		/** Sub-directory of the game's private root this unit's files are copied into. */
		public final String dirName;
		/** Lower-case file names that must be present and non-empty for the unit to be ready. */
		public final String[] requiredFiles;
		/** Lower-case file names copied if found but not needed for "ready". */
		public final String[] optionalFiles;
		/** Lower-case extensions (with the dot) - every file with one is copied if found. */
		public final String[] optionalExtensions;

		public Unit(int id, String title, String dirName, String[] requiredFiles,
				String[] optionalFiles, String[] optionalExtensions) {
			this.id = id;
			this.title = title;
			this.dirName = dirName;
			this.requiredFiles = requiredFiles;
			this.optionalFiles = optionalFiles;
			this.optionalExtensions = optionalExtensions;
		}

		boolean wants(String lowerName) {
			for (String f : requiredFiles) {
				if (f.equals(lowerName)) return true;
			}
			for (String f : optionalFiles) {
				if (f.equals(lowerName)) return true;
			}
			for (String ext : optionalExtensions) {
				if (lowerName.endsWith(ext)) return true;
			}
			return false;
		}
	}

	/** Shown in the launcher's header. */
	public final String title;
	/** Name passed to System.loadLibrary(): the game's own native shared library. */
	public final String nativeLibrary;
	/** Directory under getFilesDir() holding every unit's copied data. */
	public final String rootDirName;
	/** One-line hint on the picker screen naming the kinds of files to look for. */
	public final String pickerHint;
	public final List<Unit> units;
	/**
	 * When true and the game has a single unit whose required AND optional files are all in private
	 * storage, the launcher skips its own screens and starts the game straight away (the picker only
	 * comes back if a file goes missing, e.g. after clearing the app's data or a reinstall).
	 */
	public final boolean autoLaunchWhenComplete;

	public DescoreGameConfig(String title, String nativeLibrary, String rootDirName,
			String pickerHint, List<Unit> units) {
		this(title, nativeLibrary, rootDirName, pickerHint, units, false);
	}

	public DescoreGameConfig(String title, String nativeLibrary, String rootDirName,
			String pickerHint, List<Unit> units, boolean autoLaunchWhenComplete) {
		this.autoLaunchWhenComplete = autoLaunchWhenComplete;
		this.title = title;
		this.nativeLibrary = nativeLibrary;
		this.rootDirName = rootDirName;
		this.pickerHint = pickerHint;
		this.units = Collections.unmodifiableList(new ArrayList<>(units));
	}

	public Unit unitById(int id) {
		for (Unit u : units) {
			if (u.id == id) return u;
		}
		return units.get(0);
	}
}
