package io.github.pellcorp.grumpyscreen;

import android.view.Surface;

/** JNI bridge to libgrumpyscreen.so (app/src/main/cpp/grumpy_android.cpp). */
final class GrumpyNative {
    static {
        System.loadLibrary("grumpyscreen");
    }

    private GrumpyNative() {}

    /**
     * Hands the native side a surface to draw on. The first call starts
     * grumpyscreen with the given config files; later calls only swap the
     * surface.
     */
    static native void surfaceChanged(Surface surface, int width, int height,
                                      String configPath, String moonrakerPath,
                                      String overridePath);

    static native void surfaceDestroyed();

    /** The native UI survives Activity recreation within the same process. */
    static native boolean isStarted();

    /** The display's current refresh rate; the UI redraws at up to this. */
    static native void setRefreshRate(float hz);

    /** The height, in pixels, of the printer menu at the top of the nav strip. */
    static native void setMenuHeight(int px);

    static native void touch(int x, int y, boolean down);

    /** Who the native side asks to open the printer list (null when gone). */
    static native void setActivity(MainActivity activity);
}
