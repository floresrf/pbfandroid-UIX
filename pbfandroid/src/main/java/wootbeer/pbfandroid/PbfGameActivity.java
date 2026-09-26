package wootbeer.pbfandroid;

import wootbeer.descore.DescoreGameActivity;
import wootbeer.descore.DescoreGameConfig;

/** Pinball Fantasies' game screen: the shared descore game shell, configured by {@link PbfConfig}. */
public class PbfGameActivity extends DescoreGameActivity {
	private static final DescoreGameConfig CONFIG = PbfConfig.create();

	@Override
	protected DescoreGameConfig getConfig() {
		return CONFIG;
	}
}
