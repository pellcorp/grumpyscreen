package io.github.pellcorp.grumpyscreen;

import android.annotation.SuppressLint;
import android.content.Context;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

import java.io.File;

/** The full-screen surface grumpyscreen renders into. */
@SuppressLint("ViewConstructor")
public class GrumpySurfaceView extends SurfaceView implements SurfaceHolder.Callback {
    private final File configFile;
    private final File moonrakerFile;
    private final File overrideFile;
    // The UI's resolution is fixed by the first surface it draws to
    private boolean started;

    public GrumpySurfaceView(Context context, File configFile, File moonrakerFile, File overrideFile) {
        super(context);
        this.configFile = configFile;
        this.moonrakerFile = moonrakerFile;
        this.overrideFile = overrideFile;
        getHolder().addCallback(this);
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {}

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        // On a cold start from a portrait launcher the surface first comes up
        // portrait and turns landscape (the activity is sensorLandscape) a
        // moment later. Starting on the portrait one would lay the UI out for
        // it and stretch that over the landscape screen, so wait for the
        // turn, or a second if the screen never does.
        if (!started && width < height) {
            postDelayed(() -> {
                if (started || !holder.getSurface().isValid()) {
                    return;
                }
                android.graphics.Rect r = holder.getSurfaceFrame();
                deliver(holder, r.width(), r.height());
            }, 1000);
            return;
        }
        deliver(holder, width, height);
    }

    private void deliver(SurfaceHolder holder, int width, int height) {
        started = true;
        if (getDisplay() != null) {
            GrumpyNative.setRefreshRate(getDisplay().getRefreshRate());
        }
        GrumpyNative.surfaceChanged(holder.getSurface(), width, height,
                configFile.getAbsolutePath(), moonrakerFile.getAbsolutePath(),
                overrideFile.getAbsolutePath());
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        GrumpyNative.surfaceDestroyed();
    }

    @SuppressLint("ClickableViewAccessibility")
    @Override
    public boolean onTouchEvent(MotionEvent event) {
        int action = event.getActionMasked();
        boolean down = action != MotionEvent.ACTION_UP && action != MotionEvent.ACTION_CANCEL;
        // LVGL here is single touch: follow the first finger
        GrumpyNative.touch((int) event.getX(0), (int) event.getY(0), down);
        return true;
    }
}
