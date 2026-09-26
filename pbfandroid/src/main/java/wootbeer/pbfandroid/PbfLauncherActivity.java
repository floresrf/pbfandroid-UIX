package wootbeer.pbfandroid;

import wootbeer.descore.DescoreGameActivity;
import wootbeer.descore.DescoreGameConfig;
import wootbeer.descore.DescoreLauncherActivity;

/** Pinball Fantasies' launcher: the shared descore asset picker, configured by {@link PbfConfig}. */
public class PbfLauncherActivity extends DescoreLauncherActivity {
	private static final DescoreGameConfig CONFIG = PbfConfig.create();

	@Override
	protected DescoreGameConfig getConfig() {
		return CONFIG;
	}

	@Override
	protected Class<? extends DescoreGameActivity> getGameActivityClass() {
		return PbfGameActivity.class;
	}
}
