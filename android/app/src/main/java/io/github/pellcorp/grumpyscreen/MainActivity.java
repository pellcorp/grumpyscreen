package io.github.pellcorp.grumpyscreen;

import android.app.Activity;
import android.app.Dialog;
import android.content.ComponentName;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.content.res.ColorStateList;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.hardware.display.DisplayManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.text.InputType;
import android.util.Log;
import android.view.Display;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;

public class MainActivity extends Activity {
    private static final String TAG = "grumpyscreen";
    private static final String PREFS = "printer";
    private static final String KEY_UPDATE_CHECK = "update_check";
    private static final String KEY_UPDATE_PROMPT = "update_prompt";
    private static final long UPDATE_CHECK_INTERVAL_MS = 24 * 60 * 60 * 1000L;
    // a tablet can stay on the screen for weeks without a restart
    private static final long UPDATE_POLL_INTERVAL_MS = 6 * 60 * 60 * 1000L;
    private SharedPreferences prefs;
    private SavedPrinters saved;
    private Dialog printerDialog;
    private File configFile;
    private File moonrakerFile;
    private File overrideFile;
    private boolean started;
    private MenuButton menu;
    private boolean updateCheckRunning;
    private boolean updatePromptShowing;
    private boolean updateDownloading;
    // an update found while the printer dialog was open, shown after it closes
    private UpdateChecker.Release pendingUpdate;
    // an update waiting for the user to allow installs from this app
    private UpdateChecker.Release awaitingInstallPermission;
    private GrumpyTheme.ThemedDialog updateProgress;
    private ProgressBar updateProgressBar;
    private TextView updateProgressText;
    private final Handler updatePollHandler = new Handler(Looper.getMainLooper());
    private final Runnable updatePoll = new Runnable() {
        @Override
        public void run() {
            maybeCheckForUpdates();
            updatePollHandler.postDelayed(this, UPDATE_POLL_INTERVAL_MS);
        }
    };

    // the display can switch rates under us (power saving, a mode change)
    private final DisplayManager.DisplayListener displayListener = new DisplayManager.DisplayListener() {
        @Override public void onDisplayAdded(int displayId) {}
        @Override public void onDisplayRemoved(int displayId) {}
        @Override public void onDisplayChanged(int displayId) {
            Display d = getWindowManager().getDefaultDisplay();
            if (started && d != null && d.getDisplayId() == displayId) {
                GrumpyNative.setRefreshRate(d.getRefreshRate());
            }
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        saved = new SavedPrinters(prefs);
        GrumpyNative.setActivity(this);
        File files = getFilesDir();
        configFile = new File(files, "grumpyscreen.cfg");
        moonrakerFile = new File(files, "moonraker.cfg");
        // what the Settings panel saves; survives app updates
        overrideFile = new File(files, "grumpyscreen-override.cfg");

        try {
            // the bundled config is replaced on every start, so an app update
            // brings its new defaults; the user's changes live in the override
            copyAsset("grumpyscreen.cfg", configFile);
            // left by earlier versions, which bundled a macro list
            new File(files, "macros.cfg").delete();
        } catch (IOException e) {
            Log.e(TAG, "unable to install grumpyscreen.cfg", e);
            Toast.makeText(this, "Unable to install grumpyscreen.cfg", Toast.LENGTH_LONG).show();
            finish();
            return;
        }

        if (saved.hasCurrent()) {
            start();
        } else {
            showPrinterDialog(true);
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) goFullscreen();
    }

    /** Back opens the printer settings; it never leaves the screen. */
    @Override
    @SuppressWarnings("deprecation")
    public void onBackPressed() {
        showPrinterDialog(false);
    }

    private void start() {
        try {
            writeMoonrakerConfig(saved.currentHost(), saved.currentPort());
        } catch (IOException e) {
            Log.e(TAG, "unable to write moonraker.cfg", e);
        }
        started = true;
        preferHighestRefreshRate();
        getSystemService(DisplayManager.class).registerDisplayListener(displayListener, null);
        // full screen first, so the surface the display is sized from is
        // the whole screen and not the area between the system bars
        goFullscreen();
        FrameLayout root = new FrameLayout(this);
        root.addView(new GrumpySurfaceView(this, configFile, moonrakerFile, overrideFile),
                new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT,
                                             FrameLayout.LayoutParams.MATCH_PARENT));
        // The printer list belongs to the app, not to the printer's UI, so it
        // has its own button: the nav strip leaves a touch target's height
        // free at its top (placeMenuFromNative) and the button covers it
        GrumpyTheme theme = new GrumpyTheme(this, configFile, overrideFile);
        GrumpyNative.setMenuHeight(theme.dp(48));
        menu = new MenuButton(this, theme);
        menu.setOnClickListener(v -> showPrinterDialog(false));
        menu.setVisibility(View.GONE);
        root.addView(menu, new FrameLayout.LayoutParams(0, 0, Gravity.TOP | Gravity.START));
        setContentView(root);
        maybeCheckForUpdates();
    }

    /** Back from allowing installs: continue the update the user asked for. */
    @Override
    protected void onResume() {
        super.onResume();
        UpdateChecker.Release release = awaitingInstallPermission;
        if (release != null) {
            awaitingInstallPermission = null;
            if (ApkInstaller.allowed(this)) downloadAndInstall(release);
        }
        maybeCheckForUpdates();
        updatePollHandler.removeCallbacks(updatePoll);
        updatePollHandler.postDelayed(updatePoll, UPDATE_POLL_INTERVAL_MS);
    }

    @Override
    protected void onPause() {
        updatePollHandler.removeCallbacks(updatePoll);
        super.onPause();
    }

    /** The nav strip has laid itself out: its width and the room left at its top. */
    @SuppressWarnings("unused")
    void placeMenuFromNative(int width, int height) {
        runOnUiThread(() -> {
            if (menu == null) return;
            FrameLayout.LayoutParams lp = (FrameLayout.LayoutParams) menu.getLayoutParams();
            lp.width = width;
            lp.height = height;
            menu.setLayoutParams(lp);
            menu.setVisibility(View.VISIBLE);
        });
    }

    /** The printer list's ☰, drawn rather than taken from a font. */
    private static final class MenuButton extends View {
        private final Paint bar = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint pressed = new Paint(Paint.ANTI_ALIAS_FLAG);

        MenuButton(Activity activity, GrumpyTheme theme) {
            super(activity);
            bar.setColor(GrumpyTheme.opaque(theme.text));
            pressed.setColor(GrumpyTheme.alpha(theme.textDim, 0x50));
            setContentDescription("Printers");
            setClickable(true);
        }

        @Override
        protected void onDraw(Canvas canvas) {
            float cx = getWidth() / 2f, cy = getHeight() / 2f;
            float s = Math.min(getWidth(), getHeight());
            if (isPressed()) canvas.drawCircle(cx, cy, s * 0.45f, pressed);
            float half = s * 0.25f, t = s * 0.07f;
            for (int i = -1; i <= 1; i++) {
                float y = cy + i * s * 0.17f;
                canvas.drawRoundRect(cx - half, y - t / 2, cx + half, y + t / 2, t / 2, t / 2, bar);
            }
        }

        @Override
        protected void drawableStateChanged() {
            super.drawableStateChanged();
            invalidate();
        }
    }

    @Override
    protected void onDestroy() {
        GrumpyNative.setActivity(null);
        if (started) getSystemService(DisplayManager.class).unregisterDisplayListener(displayListener);
        updatePollHandler.removeCallbacks(updatePoll);
        dismissUpdateProgress();
        super.onDestroy();
    }

    /**
     * Many 90/120Hz phones run apps at 60Hz unless the window asks for more.
     * Ask for the fastest mode at the current resolution; the system may still
     * lower it (battery saver, thermal limits).
     */
    @SuppressWarnings("deprecation")
    private void preferHighestRefreshRate() {
        Display display = getWindowManager().getDefaultDisplay();
        Display.Mode current = display.getMode();
        Display.Mode best = current;
        for (Display.Mode m : display.getSupportedModes()) {
            if (m.getPhysicalWidth() == current.getPhysicalWidth()
                    && m.getPhysicalHeight() == current.getPhysicalHeight()
                    && m.getRefreshRate() > best.getRefreshRate()) {
                best = m;
            }
        }
        Log.i(TAG, "display modes: current " + current.getRefreshRate() + "Hz, requesting "
                + best.getRefreshRate() + "Hz");
        WindowManager.LayoutParams lp = getWindow().getAttributes();
        lp.preferredDisplayModeId = best.getModeId();
        getWindow().setAttributes(lp);
    }

    /** Settings, Restart GUI: start over in a fresh process. */
    @SuppressWarnings("unused")
    void restartFromNative() {
        runOnUiThread(() -> RestartActivity.restart(this));
    }

    private void showPrinterDialog(boolean firstRun) {
        if (printerDialog != null && printerDialog.isShowing()) return;
        // re-read each time: the user may have changed the theme in their override
        GrumpyTheme theme = new GrumpyTheme(this, configFile, overrideFile);
        int pad = theme.dp(16);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(pad, pad / 2, pad, 0);
        // keep focus (and the keyboard) off the address fields until tapped,
        // so filling them from a list does not scroll the dialog
        layout.setFocusableInTouchMode(true);
        layout.setDescendantFocusability(LinearLayout.FOCUS_BEFORE_DESCENDANTS);
        // a landscape phone is short: let the dialog body scroll
        ScrollView scroll = new ScrollView(this);
        scroll.addView(layout);

        EditText host = theme.input(new EditText(this));
        EditText port = theme.input(new EditText(this));
        final TextView[] selected = {null};
        final PrinterScanner.Printer[] selectedPrinter = {null};

        // a row in either list; tapping it selects that printer
        java.util.function.BiFunction<PrinterScanner.Printer, String, TextView> row = (p, text) -> {
            TextView item = theme.row(new TextView(this));
            item.setText(text);
            item.setClickable(true);
            item.setOnClickListener(v -> {
                if (selected[0] != null) theme.select(selected[0], false);
                theme.select(item, true);
                selected[0] = item;
                selectedPrinter[0] = p;
                host.setText(p.host);
                port.setText(String.valueOf(p.port));
                host.setError(null);
                port.setError(null);
            });
            return item;
        };

        // --- saved printers -------------------------------------------------
        java.util.Map<String, TextView> savedRows = new java.util.HashMap<>();
        java.util.List<PrinterScanner.Printer> savedList = saved.all();
        if (!savedList.isEmpty()) {
            TextView savedLabel = theme.text(new TextView(this), 14, true);
            savedLabel.setText("Your printers (long-press to remove)");
            layout.addView(savedLabel);
            LinearLayout savedBox = new LinearLayout(this);
            savedBox.setOrientation(LinearLayout.VERTICAL);
            layout.addView(savedBox);
            for (PrinterScanner.Printer p : savedList) {
                boolean current = saved.isCurrent(p);
                TextView item = row.apply(p, (current ? "● " : "○ ") + p
                        + (current ? "  — connected" : ""));
                item.setOnLongClickListener(v -> {
                    if (saved.isCurrent(p)) {
                        Toast.makeText(this, "Switch to another printer before removing this one",
                                Toast.LENGTH_SHORT).show();
                        return true;
                    }
                    theme.confirm(this, "Remove printer", "Remove " + p + " from your printers?",
                            "Remove", () -> {
                                saved.remove(p);
                                savedBox.removeView(item);
                                savedRows.remove(p.host + ":" + p.port);
                                if (selected[0] == item) {
                                    selected[0] = null;
                                    selectedPrinter[0] = null;
                                }
                            });
                    return true;
                });
                item.setTag(item.getText().toString());  // the label without a status
                savedBox.addView(item);
                savedRows.put(p.host + ":" + p.port, item);
                if (current) item.performClick();
            }
        }

        // --- printers found on the network --------------------------------
        TextView foundLabel = theme.text(new TextView(this), 14, true);
        foundLabel.setText(savedList.isEmpty() ? "Printers on this network" : "Other printers on this network");
        foundLabel.setPadding(0, savedList.isEmpty() ? 0 : pad, 0, 0);
        layout.addView(foundLabel);
        TextView status = theme.text(new TextView(this), 14, true);
        status.setPadding(0, pad / 4, 0, pad / 4);
        layout.addView(status);
        LinearLayout found = new LinearLayout(this);
        found.setOrientation(LinearLayout.VERTICAL);
        layout.addView(found);
        Button rescan = theme.button(new Button(this), false);
        rescan.setText("Search again");
        rescan.setEnabled(false);
        layout.addView(rescan, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT));

        final PrinterScanner[] scanner = {null};
        Runnable startScan = new Runnable() {
            @Override
            public void run() {
                if (scanner[0] != null) scanner[0].stop();
                // Ask each saved printer directly whether it is up: the scan
                // only sweeps this device's own subnet (and mDNS), so it
                // misses printers elsewhere, or behind a forwarded port
                for (PrinterScanner.Printer sp : savedList) {
                    if (saved.isCurrent(sp)) continue;
                    TextView item = savedRows.get(sp.host + ":" + sp.port);
                    if (item == null) continue;
                    item.setText(item.getTag() + "  — checking…");
                    new Thread(() -> {
                        boolean up;
                        try {
                            String body = PrinterScanner.httpGet(sp.host, sp.port, "/server/info", 2000);
                            up = body != null && new org.json.JSONObject(body).has("result");
                        } catch (Exception e) {
                            up = false;
                        }
                        final boolean online = up;
                        runOnUiThread(() -> {
                            if (savedRows.get(sp.host + ":" + sp.port) != item) return;  // removed
                            item.setText(item.getTag() + (online ? "  — online" : "  — offline"));
                        });
                    }).start();
                }
                found.removeAllViews();
                status.setText("Searching…");
                rescan.setEnabled(false);
                scanner[0] = new PrinterScanner(MainActivity.this, new PrinterScanner.Listener() {
                    @Override
                    public void onFound(PrinterScanner.Printer p) {
                        TextView known = savedRows.get(p.host + ":" + p.port);
                        if (known != null) {
                            // already saved: just show that it is reachable
                            if (!saved.isCurrent(p)) known.setText(known.getTag() + "  — online");
                            if (p.name != null && saved.find(p.host, p.port) != null
                                    && saved.find(p.host, p.port).name == null) {
                                saved.rename(p.host, p.port, p.name);
                            }
                            return;
                        }
                        TextView item = row.apply(p, p.toString());
                        found.addView(item);
                        // first run: preselect the first printer found
                        if (selectedPrinter[0] == null && host.getText().length() == 0) item.performClick();
                    }

                    @Override
                    public void onFinished(int probed) {
                        int n = found.getChildCount();
                        status.setText(n == 0
                                ? (savedRows.isEmpty() ? "No printers found. Enter the address below." : "No others found.")
                                : n == 1 ? "Found 1 printer." : "Found " + n + " printers.");
                        rescan.setEnabled(true);
                    }
                });
                scanner[0].start();
            }
        };
        rescan.setOnClickListener(v -> startScan.run());

        // --- or typed in ----------------------------------------------------
        TextView hostLabel = theme.text(new TextView(this), 14, true);
        hostLabel.setText("Or enter the Moonraker host or IP address");
        hostLabel.setPadding(0, pad, 0, 0);
        layout.addView(hostLabel);
        host.setSingleLine(true);
        host.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        host.setHint("192.168.1.50");
        if (host.getText().length() == 0 && saved.hasCurrent()) host.setText(saved.currentHost());
        layout.addView(host);

        TextView portLabel = theme.text(new TextView(this), 14, true);
        portLabel.setPadding(0, pad / 2, 0, 0);
        portLabel.setText("Port");
        layout.addView(portLabel);
        port.setSingleLine(true);
        port.setInputType(InputType.TYPE_CLASS_NUMBER);
        if (port.getText().length() == 0) port.setText(String.valueOf(saved.currentPort()));
        layout.addView(port);

        CheckBox home = theme.check(new CheckBox(this));
        home.setText("Offer GrumpyScreen as the home screen");
        home.setChecked(isHomeEnabled());
        layout.addView(home);

        GrumpyTheme.ThemedDialog td = theme.dialog(this, "Printers", scroll);
        Dialog dialog = td.dialog;
        dialog.setCancelable(!firstRun);
        if (firstRun) {
            td.addButton("Exit", false, v -> finish());
            td.addButton("Update app", false, v -> checkForUpdates(true));
        } else {
            td.addButton("Exit app", false, v -> finishAffinity());
            td.addButton("Update app", false, v -> checkForUpdates(true));
            td.spacer();
            td.addButton("Cancel", false, v -> dialog.dismiss());
        }
        if (firstRun) td.spacer();
        printerDialog = dialog;
        dialog.setOnDismissListener(d -> {
            if (scanner[0] != null) scanner[0].stop();
            printerDialog = null;
            goFullscreen();
            // an update found while this dialog was open waits until it closes
            if (pendingUpdate != null && !isFinishing() && !isDestroyed()) {
                UpdateChecker.Release release = pendingUpdate;
                pendingUpdate = null;
                showUpdatePrompt(release);
            }
        });
        td.addButton("Connect", true, v -> {
            String[] hp = parseHost(host.getText().toString(), port.getText().toString());
            if (hp == null) {
                host.setError("Enter the printer's address");
                return;
            }
            String newHost = hp[0];
            int newPort;
            try {
                newPort = Integer.parseInt(hp[1]);
            } catch (NumberFormatException e) {
                newPort = -1;
            }
            if (newPort <= 0 || newPort > 65535) {
                port.setError("1 - 65535");
                return;
            }
            setHomeEnabled(home.isChecked());
            PrinterScanner.Printer pick = selectedPrinter[0];
            String name = pick != null && pick.host.equals(newHost) && pick.port == newPort ? pick.name : null;
            boolean changed = !saved.hasCurrent() || !newHost.equals(saved.currentHost())
                    || newPort != saved.currentPort();
            saved.use(newHost, newPort, name);
            if (name == null) learnName(newHost, newPort);
            dialog.dismiss();
            if (!started) {
                start();
            } else if (changed) {
                // grumpyscreen reads its config once per process
                RestartActivity.restart(this);
            }
        });
        td.show();
        layout.requestFocus();
        startScan.run();
    }

    // --- updates -----------------------------------------------------------

    /** Once a day, quietly ask GitHub whether there is a newer release. */
    private void maybeCheckForUpdates() {
        // a development build (a commit hash, not a release tag) is not
        // nagged about; its Update app button still checks on demand
        if (!UpdateChecker.isReleaseVersion(BuildConfig.VERSION_NAME)) return;
        long last = prefs.getLong(KEY_UPDATE_CHECK, 0);
        if (System.currentTimeMillis() - last < UPDATE_CHECK_INTERVAL_MS) return;
        checkForUpdates(false);
    }

    private void checkForUpdates(boolean interactive) {
        if (updateCheckRunning) return;
        updateCheckRunning = true;
        // count a failed check too, so an offline device does not retry on
        // every start; the button in the printer dialog always checks
        prefs.edit().putLong(KEY_UPDATE_CHECK, System.currentTimeMillis()).apply();
        if (interactive) {
            Toast.makeText(this, "Checking for updates…", Toast.LENGTH_SHORT).show();
        }
        UpdateChecker.check((release, error) -> runOnUiThread(() -> {
            updateCheckRunning = false;
            if (isFinishing() || isDestroyed()) return;
            if (error != null) {
                Log.w(TAG, "update check failed: " + error);
                if (interactive) {
                    showUpdateMessage("Update check failed",
                            "Could not check GitHub for updates.\n\n" + error);
                }
                return;
            }
            if (release == null || !UpdateChecker.isNewer(release.tag, BuildConfig.VERSION_NAME)) {
                if (interactive) {
                    showUpdateMessage("GrumpyScreen is up to date",
                            release == null ? "You have the latest version."
                                    : "You have the latest version, " + release.tag + ".");
                }
                return;
            }
            if (updatePromptShowing) return;
            if (!interactive) {
                if (release.tag.equals(prefs.getString(KEY_UPDATE_PROMPT, null))) return;
                // do not stack the update over the printer list; it is shown
                // when that dialog closes
                if (printerDialog != null && printerDialog.isShowing()) {
                    pendingUpdate = release;
                    return;
                }
            }
            showUpdatePrompt(release);
        }));
    }

    private void showUpdatePrompt(final UpdateChecker.Release release) {
        if (updatePromptShowing) return;
        updatePromptShowing = true;
        GrumpyTheme theme = new GrumpyTheme(this, configFile, overrideFile);
        TextView body = theme.text(new TextView(this), 17, false);
        StringBuilder message = new StringBuilder();
        message.append("GrumpyScreen ").append(release.tag).append(" is available.");
        String current = BuildConfig.VERSION_NAME;
        if (UpdateChecker.isReleaseVersion(current) && !current.equals(release.tag)) {
            message.append("\n\nYou have ").append(current).append(".");
        } else if (!UpdateChecker.isReleaseVersion(current)) {
            message.append("\n\nYou are running development build ").append(current).append(".");
        }
        message.append("\n\nThe APK is downloaded from GitHub releases and installed by Android.")
                .append(" Your printers and settings are kept.");
        body.setText(message);
        body.setPadding(theme.dp(4), theme.dp(4), theme.dp(4), theme.dp(8));

        GrumpyTheme.ThemedDialog dialog = theme.dialog(this, "Update available", body);
        dialog.addButton("Later", false, v -> dialog.dialog.dismiss());
        dialog.addButton("Download", true, v -> {
            dialog.dialog.dismiss();
            downloadAndInstall(release);
        });
        dialog.dialog.setOnDismissListener(d -> {
            updatePromptShowing = false;
            prefs.edit().putString(KEY_UPDATE_PROMPT, release.tag).apply();
            goFullscreen();
        });
        dialog.show();
    }

    private void downloadAndInstall(UpdateChecker.Release release) {
        if (updateDownloading) return;
        if (!ApkInstaller.allowed(this)) {
            awaitingInstallPermission = release;
            showInstallPermission();
            return;
        }
        updateDownloading = true;
        showUpdateProgress(release);
        new Thread(() -> {
            File apk;
            try {
                apk = UpdateChecker.download(this, release, percent ->
                        runOnUiThread(() -> {
                            if (updateProgressBar == null) return;
                            updateProgressBar.setProgress(Math.max(0, percent));
                            if (updateProgressText != null && percent >= 0) {
                                updateProgressText.setText(
                                        "Downloading " + release.tag + "… " + percent + "%");
                            }
                        }));
            } catch (Exception e) {
                Log.e(TAG, "unable to download update", e);
                runOnUiThread(() -> {
                    dismissUpdateProgress();
                    updateDownloading = false;
                    if (!isFinishing() && !isDestroyed()) {
                        showUpdateMessage("Update failed",
                                e.getMessage() == null ? e.toString() : e.getMessage());
                    }
                });
                return;
            }
            final File downloaded = apk;
            runOnUiThread(() -> {
                dismissUpdateProgress();
                updateDownloading = false;
                if (!isFinishing() && !isDestroyed()) installDownloaded(downloaded);
            });
        }, "update-download").start();
    }

    /** The session copy is quick but still file work, so keep it off the UI thread. */
    private void installDownloaded(File apk) {
        new Thread(() -> {
            try {
                ApkInstaller.install(this, apk);
            } catch (Exception e) {
                Log.e(TAG, "unable to start the update install", e);
                runOnUiThread(() -> {
                    if (!isFinishing() && !isDestroyed()) {
                        showUpdateMessage("Update failed",
                                e.getMessage() == null ? e.toString() : e.getMessage());
                    }
                });
            }
        }, "update-install").start();
    }

    private void showUpdateProgress(UpdateChecker.Release release) {
        GrumpyTheme theme = new GrumpyTheme(this, configFile, overrideFile);
        LinearLayout body = new LinearLayout(this);
        body.setOrientation(LinearLayout.VERTICAL);
        body.setPadding(theme.dp(4), theme.dp(4), theme.dp(4), theme.dp(4));
        TextView text = theme.text(new TextView(this), 16, false);
        text.setText("Downloading " + release.tag + "…");
        body.addView(text);
        ProgressBar bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        bar.setMax(100);
        bar.setProgress(0);
        bar.setProgressTintList(ColorStateList.valueOf(GrumpyTheme.opaque(theme.primary)));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        lp.topMargin = theme.dp(12);
        body.addView(bar, lp);

        GrumpyTheme.ThemedDialog dialog = theme.dialog(this, "Updating GrumpyScreen", body);
        dialog.dialog.setCancelable(false);
        updateProgress = dialog;
        updateProgressBar = bar;
        updateProgressText = text;
        dialog.show();
    }

    private void dismissUpdateProgress() {
        if (updateProgress != null) updateProgress.dialog.dismiss();
        updateProgress = null;
        updateProgressBar = null;
        updateProgressText = null;
    }

    private void showInstallPermission() {
        GrumpyTheme theme = new GrumpyTheme(this, configFile, overrideFile);
        TextView body = theme.text(new TextView(this), 17, false);
        body.setText("Android needs your permission before GrumpyScreen can install its update.\n\n"
                + "Turn on \"Allow from this source\" for GrumpyScreen, then come back.");
        body.setPadding(theme.dp(4), theme.dp(4), theme.dp(4), theme.dp(8));
        GrumpyTheme.ThemedDialog dialog = theme.dialog(this, "Allow app installs", body);
        dialog.addButton("Cancel", false, v -> {
            awaitingInstallPermission = null;
            dialog.dialog.dismiss();
        });
        dialog.addButton("Open settings", true, v -> {
            dialog.dialog.dismiss();
            openInstallPermissionSettings();
        });
        dialog.dialog.setOnDismissListener(d -> goFullscreen());
        dialog.show();
    }

    private void openInstallPermissionSettings() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            try {
                startActivity(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                        Uri.parse("package:" + getPackageName())));
                return;
            } catch (RuntimeException ignored) {
                // not every device has the per-app page; fall back below
            }
        }
        try {
            startActivity(new Intent(Settings.ACTION_SECURITY_SETTINGS));
        } catch (RuntimeException ignored) {
            Toast.makeText(this, "Allow installs for GrumpyScreen in Settings",
                    Toast.LENGTH_LONG).show();
        }
    }

    private void showUpdateMessage(String title, String message) {
        GrumpyTheme theme = new GrumpyTheme(this, configFile, overrideFile);
        TextView body = theme.text(new TextView(this), 17, false);
        body.setText(message);
        body.setPadding(theme.dp(4), theme.dp(4), theme.dp(4), theme.dp(8));
        GrumpyTheme.ThemedDialog dialog = theme.dialog(this, title, body);
        dialog.addButton("OK", true, v -> dialog.dialog.dismiss());
        dialog.dialog.setOnDismissListener(d -> goFullscreen());
        dialog.show();
    }

    /** A typed-in printer gets its hostname as its name once it answers. */
    private void learnName(String host, int port) {
        new Thread(() -> {
            String body = PrinterScanner.httpGet(host, port, "/printer/info");
            try {
                String name = new org.json.JSONObject(body).getJSONObject("result").optString("hostname", "");
                if (!name.isEmpty()) saved.rename(host, port, name);
            } catch (Exception ignored) {
                // not reachable yet; it keeps showing its address
            }
        }).start();
    }

    /** Accepts "host", "host:port" or a pasted URL such as http://host:7125/. */
    static String[] parseHost(String raw, String port) {
        String h = raw.trim();
        int scheme = h.indexOf("://");
        if (scheme >= 0) h = h.substring(scheme + 3);
        int slash = h.indexOf('/');
        if (slash >= 0) h = h.substring(0, slash);
        int colon = h.lastIndexOf(':');
        if (colon > 0 && h.indexOf(':') == colon) {  // not an IPv6 literal
            port = h.substring(colon + 1);
            h = h.substring(0, colon);
        }
        if (h.isEmpty()) return null;
        return new String[]{h, port.trim()};
    }

    private void writeMoonrakerConfig(String host, int port) throws IOException {
        File thumbs = new File(getCacheDir(), "thumbnails");
        //noinspection ResultOfMethodCallIgnored
        thumbs.mkdirs();
        String cfg = "[moonraker]\n"
                + "host: " + host + "\n"
                + "port: " + port + "\n"
                // not on the printer, so thumbnails are downloaded to here
                + "thumbnail_path: " + thumbs.getAbsolutePath() + "\n";
        try (OutputStream out = new FileOutputStream(moonrakerFile)) {
            out.write(cfg.getBytes(StandardCharsets.UTF_8));
        }
    }

    private void copyAsset(String name, File dest) throws IOException {
        try (InputStream in = getAssets().open(name); OutputStream out = new FileOutputStream(dest)) {
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
        }
    }

    private ComponentName homeAlias() {
        return new ComponentName(this, getPackageName() + ".HomeAlias");
    }

    private boolean isHomeEnabled() {
        return getPackageManager().getComponentEnabledSetting(homeAlias())
                == PackageManager.COMPONENT_ENABLED_STATE_ENABLED;
    }

    private void setHomeEnabled(boolean enabled) {
        getPackageManager().setComponentEnabledSetting(homeAlias(),
                enabled ? PackageManager.COMPONENT_ENABLED_STATE_ENABLED
                        : PackageManager.COMPONENT_ENABLED_STATE_DEFAULT,
                PackageManager.DONT_KILL_APP);
    }

    @SuppressWarnings("deprecation")
    private void goFullscreen() {
        // the insets controller needs the decor; it may not exist before setContentView
        getWindow().getDecorView();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            getWindow().setDecorFitsSystemWindows(false);
            WindowInsetsController c = getWindow().getInsetsController();
            if (c != null) {
                c.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                            | View.SYSTEM_UI_FLAG_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        }
    }
}
