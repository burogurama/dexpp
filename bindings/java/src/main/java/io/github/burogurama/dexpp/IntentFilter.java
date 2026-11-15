package io.github.burogurama.dexpp;

import java.util.List;

/**
 * An {@code <intent-filter>} of a manifest {@link Component}.
 *
 * @param actions the {@code android:name} of each {@code <action>}
 * @param categories the {@code android:name} of each {@code <category>}
 * @param isLauncher true for the filter that puts an activity in the launcher: action {@code
 *     android.intent.action.MAIN} with category {@code android.intent.category.LAUNCHER}
 */
public record IntentFilter(List<String> actions, List<String> categories, boolean isLauncher) {
    /** Copies the lists into immutable lists. */
    public IntentFilter {
        actions = List.copyOf(actions);
        categories = List.copyOf(categories);
    }

    /**
     * Tells whether the filter lists this action.
     *
     * @param name an action name, such as {@code "android.intent.action.VIEW"}
     * @return true when the filter lists it
     */
    public boolean hasAction(String name) {
        return actions.contains(name);
    }
}
