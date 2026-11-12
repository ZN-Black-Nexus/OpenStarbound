package io.github.openstarbound.mobile;

import org.libsdl.app.SDLActivity;

public class GameActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "main" };
    }
}
