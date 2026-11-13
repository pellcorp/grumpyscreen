package io.github.pellcorp.grumpyscreen;

import android.app.Activity;
import android.app.Dialog;
import android.content.ActivityNotFoundException;
import android.content.ComponentName;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.hardware.display.DisplayManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.Process;
import android.text.Editable;
import android.text.InputFilter;
import android.text.InputType;
import android.text.TextWatcher;
import android.text.method.DigitsKeyListener;
import android.util.Log;
import android.view.Display;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
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
    static final String EXTRA_CONNECT = "connect_selected_printer";
    static final String EXTRA_CONFIG = "printer_config";
    private SavedPrinters saved;
    private Dialog printerDialog;
    private File configFile;
    private File moonrakerFile;
    private File overrideFile;
    private boolean started;
    private boolean exitRequested;
    private MenuButton menu;
    private boolean updateCheckRunning;
    private boolean configDownloadRunning;
    private int connectionAttempt;

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
        saved = new SavedPrinters(getSharedPreferences(PREFS, MODE_PRIVATE));
        GrumpyNative.setActivity(this);
        File files = getFilesDir();
        configFile = new File(files, "grumpyscreen.cfg");
        moonrakerFile = new File(files, "moonraker.cfg");
        // A session copy of the printer's INI, never a source of local settings.
        overrideFile = new File(getCacheDir(), "grumpyscreen.ini");

        try {
            // the bundled config is replaced on every start, so an app update
            // brings its new defaults; user configuration lives on the printer
            copyAsset("grumpyscreen.cfg", configFile);
            // left by earlier versions, which bundled a macro list
            new File(files, "macros.cfg").delete();
            new File(files, "grumpyscreen-override.cfg").delete();
            if (!GrumpyNative.isStarted()) {
                clearPrinterConfig();
            }
        } catch (IOException e) {
            Log.e(TAG, "unable to prepare configuration", e);
            Toast.makeText(this, "Unable to prepare configuration", Toast.LENGTH_LONG).show();
            finish();
            return;
        }

        boolean preparedRestart = RestartActivity.consumePreparedRestart(this);
        boolean connect = preparedRestart && getIntent().getBooleanExtra(EXTRA_CONNECT, false);
        byte[] printerConfig = getIntent().getByteArrayExtra(EXTRA_CONFIG);
        getIntent().removeExtra(EXTRA_CONNECT);
        getIntent().removeExtra(EXTRA_CONFIG);
        if (GrumpyNative.isStarted() || (connect && saved.hasCurrent() && savedInstanceState == null)) {
            try {
                // Activity recreation reattaches the running UI and keeps
                // the configuration already loaded for this session.
                if (!GrumpyNative.isStarted()) {
                    installPrinterConfig(printerConfig);
                }
                startUi();
            } catch (IOException e) {
                Log.w(TAG, "unable to prepare printer configuration", e);
                showInitialPrinters();
                Toast.makeText(this, "Unable to prepare printer configuration: " + e.getMessage(),
                        Toast.LENGTH_LONG).show();
            }
        } else {
            showInitialPrinters();
        }
    }

    @Override
    protected void onRestart() {
        super.onRestart();
        if (!started) {
            showPrinterDialog(true);
        }
    }

    @Override
    protected void onStop() {
        // A background download must not reopen the printer UI after resuming.
        cancelConnection();
        if (printerDialog != null) {
            printerDialog.dismiss();
        }
        super.onStop();
    }

    private void showInitialPrinters() {
        GrumpyTheme theme = new GrumpyTheme(this, configFile, overrideFile);
        View background = new View(this);
        background.setBackgroundColor(GrumpyTheme.opaque(theme.bg));
        setContentView(background);
        showPrinterDialog(true);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            goFullscreen();
        }
    }

    /** Back opens the printer settings; it never leaves the screen. */
    @Override
    @SuppressWarnings("deprecation")
    public void onBackPressed() {
        showPrinterDialog(!started);
    }

    private void connectToPrinter(String host, int port, String name, boolean home,
                                  Dialog dialog, java.util.function.Consumer<IOException> onError) {
        if (configDownloadRunning) {
            return;
        }
        configDownloadRunning = true;
        final int attempt = ++connectionAttempt;
        new Thread(() -> {
            try {
                byte[] config = PrinterConfig.download(host, port);
                runOnUiThread(() -> {
                    if (!isCurrentAttempt(attempt)) {
                        return;
                    }
                    configDownloadRunning = false;
                    try {
                        boolean restart = started || GrumpyNative.isStarted();
                        if (!restart) {
                            installPrinterConfig(config);
                        }
                        saved.use(host, port, name);
                        setHomeEnabled(home);
                        if (name == null) {
                            learnName(host, port);
                        }
                        if (restart) {
                            // Carry this download into the new process; the
                            // running UI keeps its own configuration until then.
                            RestartActivity.restart(this, config);
                        } else {
                            startUi();
                        }
                        dialog.dismiss();
                    } catch (IOException e) {
                        onError.accept(e);
                    }
                });
            } catch (IOException e) {
                runOnUiThread(() -> {
                    if (!isCurrentAttempt(attempt)) {
                        return;
                    }
                    configDownloadRunning = false;
                    onError.accept(e);
                });
            }
        }, "printer-config").start();
    }

    private boolean isCurrentAttempt(int attempt) {
        return attempt == connectionAttempt && !isFinishing() && !isDestroyed();
    }

    private void cancelConnection() {
        connectionAttempt++;
        configDownloadRunning = false;
    }

    private void clearPrinterConfig() throws IOException {
        PrinterConfig.install(overrideFile, null);
    }

    private void installPrinterConfig(byte[] config) throws IOException {
        PrinterConfig.install(overrideFile, config);
        if (config == null) {
            Toast.makeText(this, "No grumpyscreen.ini found; using defaults",
                    Toast.LENGTH_LONG).show();
        }
    }

    private void startUi() throws IOException {
        if (!GrumpyNative.isStarted()) {
            writeMoonrakerConfig(saved.currentHost(), saved.currentPort());
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
    }

    /** The native navigation controls the menu's bounds and visibility. */
    @SuppressWarnings("unused")
    void placeMenuFromNative(int width, int height, boolean visible) {
        runOnUiThread(() -> {
            if (menu == null) {
                return;
            }
            FrameLayout.LayoutParams lp = (FrameLayout.LayoutParams) menu.getLayoutParams();
            lp.width = width;
            lp.height = height;
            menu.setLayoutParams(lp);
            menu.setVisibility(visible ? View.VISIBLE : View.GONE);
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
            if (isPressed()) {
                canvas.drawCircle(cx, cy, s * 0.45f, pressed);
            }
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
        connectionAttempt++;
        if (printerDialog != null) {
            printerDialog.dismiss();
        }
        GrumpyNative.setActivity(null);
        if (started) {
            getSystemService(DisplayManager.class).unregisterDisplayListener(displayListener);
        }
        super.onDestroy();
        // The native UI and its Moonraker connection live for the process's
        // lifetime. An explicit exit stops them after the Activity cleans up.
        if (exitRequested) {
            Process.killProcess(Process.myPid());
        }
    }

    private void exitApp() {
        exitRequested = true;
        if (printerDialog != null) {
            printerDialog.dismiss();
        }
        finishAndRemoveTask();
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

    private void showPrinterDialog(boolean requireSelection) {
        if (printerDialog != null && printerDialog.isShowing()) {
            return;
        }
        // The printer's downloaded theme also styles the app's own dialogs.
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
        final Runnable[] checkSelection = {null};
        final PrinterConnectionCheck[] connection = {null};

        // a row in either list; tapping it selects that printer
        java.util.function.BiFunction<PrinterScanner.Printer, String, TextView> row = (p, text) -> {
            TextView item = theme.row(new TextView(this));
            item.setText(text);
            item.setTag(text);
            item.setClickable(true);
            item.setOnClickListener(v -> {
                if (selected[0] != null) {
                    theme.select(selected[0], false);
                }
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
                TextView item = row.apply(p, p.toString());
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
                                connection[0].remove(p.host, p.port);
                                if (selected[0] == item) {
                                    selected[0] = null;
                                    selectedPrinter[0] = null;
                                }
                            });
                    return true;
                });
                savedBox.addView(item);
                savedRows.put(p.host + ":" + p.port, item);
                if (current) {
                    item.performClick();
                }
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

        java.util.Map<String, TextView> foundRows = new java.util.HashMap<>();
        final PrinterScanner[] scanner = {null};
        Runnable startScan = new Runnable() {
            @Override
            public void run() {
                if (scanner[0] != null) {
                    scanner[0].stop();
                }
                connection[0].refresh();
                if (checkSelection[0] != null) {
                    checkSelection[0].run();
                }
                status.setText("Searching…");
                rescan.setEnabled(false);
                scanner[0] = new PrinterScanner(MainActivity.this, new PrinterScanner.Listener() {
                    @Override
                    public void onFound(PrinterScanner.Printer p) {
                        TextView known = savedRows.get(p.host + ":" + p.port);
                        if (known == null) {
                            known = foundRows.get(p.host + ":" + p.port);
                        }
                        if (known != null) {
                            if (p.name != null && saved.find(p.host, p.port) != null
                                    && saved.find(p.host, p.port).name == null) {
                                saved.rename(p.host, p.port, p.name);
                            }
                            connection[0].found(p.host, p.port);
                            return;
                        }
                        TextView item = row.apply(p, p.toString());
                        found.addView(item);
                        foundRows.put(p.host + ":" + p.port, item);
                        connection[0].found(p.host, p.port);
                        // first run: preselect the first printer found
                        if (selectedPrinter[0] == null && host.getText().length() == 0) {
                            item.performClick();
                        }
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
        host.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        host.setSingleLine(true);
        host.setImeOptions(EditorInfo.IME_ACTION_NEXT | EditorInfo.IME_FLAG_NO_EXTRACT_UI);
        host.setFilters(new InputFilter[]{(source, start, end, dest, dstart, dend) -> {
            for (int i = start; i < end; i++) {
                char c = source.charAt(i);
                // Keep pasted URLs, host:port and IPv6 addresses supported too.
                if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z')
                        && !(c >= '0' && c <= '9') && ".-:[]/".indexOf(c) < 0) {
                    return dest.subSequence(dstart, dend);
                }
            }
            return null;
        }});
        host.setHint("192.168.1.50");
        if (host.getText().length() == 0 && saved.hasCurrent()) {
            host.setText(saved.currentHost());
        }
        layout.addView(host);

        TextView portLabel = theme.text(new TextView(this), 14, true);
        portLabel.setPadding(0, pad / 2, 0, 0);
        portLabel.setText("Port");
        layout.addView(portLabel);
        port.setInputType(InputType.TYPE_CLASS_NUMBER);
        port.setKeyListener(DigitsKeyListener.getInstance("0123456789"));
        port.setSingleLine(true);
        port.setImeOptions(EditorInfo.IME_ACTION_DONE | EditorInfo.IME_FLAG_NO_EXTRACT_UI);
        port.setHint("1 - 65535");
        if (port.getText().length() == 0) {
            port.setText(String.valueOf(saved.currentPort()));
        }
        port.setFilters(new InputFilter[]{new InputFilter.LengthFilter(5)});
        layout.addView(port);

        TextView connectionStatus = theme.text(new TextView(this), 14, true);
        connectionStatus.setPadding(0, pad / 2, 0, 0);
        layout.addView(connectionStatus);

        CheckBox home = theme.check(new CheckBox(this));
        home.setText("Offer GrumpyScreen as the home screen");
        home.setChecked(isHomeEnabled());
        layout.addView(home);

        GrumpyTheme.ThemedDialog td = theme.dialog(this, "Printers", scroll);
        Dialog dialog = td.dialog;
        dialog.setCancelable(!requireSelection);
        td.addButton("Exit", false, v -> exitApp());
        td.addButton("Check for updates", false, v -> checkForUpdates());
        if (!requireSelection) {
            td.spacer();
            td.addButton("Cancel", false, v -> dialog.dismiss());
        }
        if (requireSelection) {
            td.spacer();
        }
        printerDialog = dialog;
        Handler checks = new Handler(Looper.getMainLooper());
        Runnable refreshPrinters = new Runnable() {
            @Override
            public void run() {
                connection[0].refresh();
                checks.postDelayed(this, 5000);
            }
        };
        Button connect = td.addButton("Connect", true, v -> {
            if (configDownloadRunning || !connection[0].canConnect()) {
                return;
            }
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
            PrinterScanner.Printer pick = selectedPrinter[0];
            String name = pick != null && pick.host.equals(newHost) && pick.port == newPort ? pick.name : null;
            checks.removeCallbacks(checkSelection[0]);
            Button button = (Button) v;
            button.setText("Connecting…");
            button.setEnabled(false);
            connectionStatus.setText("Loading printer configuration…");
            connectToPrinter(newHost, newPort, name, home.isChecked(), dialog, error -> {
                Log.w(TAG, "unable to load printer configuration", error);
                button.setText("Connect");
                button.setEnabled(connection[0].canConnect());
                connectionStatus.setText("Could not load grumpyscreen.ini from " + newHost
                        + ":" + hp[1] + ".\n\n" + error.getMessage());
            });
        });
        connect.setEnabled(false);
        connection[0] = new PrinterConnectionCheck(
                task -> new Thread(task, "printer-status").start(), this::runOnUiThread,
                PrinterScanner::isOnline, (checkedHost, checkedPort, state) -> {
                    String label;
                    switch (state) {
                        case ONLINE: label = "Online"; break;
                        case OFFLINE: label = "Offline"; break;
                        default: label = ""; break;
                    }
                    TextView item = savedRows.get(checkedHost + ":" + checkedPort);
                    if (item == null) {
                        item = foundRows.get(checkedHost + ":" + checkedPort);
                    }
                    if (item != null) {
                        item.setText(item.getTag().toString() + (label.isEmpty() ? "" : "  — "
                                + label.toLowerCase(java.util.Locale.ROOT)));
                    }
                    if (checkedHost == null || connection[0].isSelected(checkedHost, checkedPort)) {
                        connect.setEnabled(!configDownloadRunning && connection[0].canConnect());
                        if (!configDownloadRunning) {
                            connectionStatus.setText(label);
                        }
                    }
                });
        checkSelection[0] = () -> {
            cancelConnection();
            connect.setText("Connect");
            checks.removeCallbacks(checkSelection[0]);
            String[] hp = parseHost(host.getText().toString(), port.getText().toString());
            if (hp == null) {
                connection[0].clearSelection();
                return;
            }
            try {
                int selectedPort = Integer.parseInt(hp[1]);
                if (selectedPort > 0 && selectedPort <= 65535) {
                    connection[0].select(hp[0], selectedPort);
                    return;
                }
            } catch (NumberFormatException ignored) {
                // Keep Connect disabled until the address and port are valid.
            }
            connection[0].clearSelection();
        };
        TextWatcher addressChanged = new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
            @Override public void onTextChanged(CharSequence s, int start, int before, int count) {}
            @Override public void afterTextChanged(Editable text) {
                cancelConnection();
                connect.setText("Connect");
                checks.removeCallbacks(checkSelection[0]);
                connection[0].clearSelection();
                checks.postDelayed(checkSelection[0], 350);
            }
        };
        host.addTextChangedListener(addressChanged);
        port.addTextChangedListener(addressChanged);
        dialog.setOnDismissListener(d -> {
            cancelConnection();
            connection[0].cancel();
            checks.removeCallbacksAndMessages(null);
            if (scanner[0] != null) {
                scanner[0].stop();
            }
            printerDialog = null;
            goFullscreen();
        });
        for (PrinterScanner.Printer p : savedList) {
            connection[0].watch(p.host, p.port);
        }
        td.show();
        layout.requestFocus();
        startScan.run();
        checks.postDelayed(refreshPrinters, 5000);
    }

    // --- updates -----------------------------------------------------------

    /** Ask GitHub only when the user presses Check for updates. */
    private void checkForUpdates() {
        if (updateCheckRunning) {
            return;
        }
        updateCheckRunning = true;
        Toast.makeText(this, "Checking for updates…", Toast.LENGTH_SHORT).show();
        UpdateChecker.check((release, error) -> runOnUiThread(() -> {
            updateCheckRunning = false;
            if (isFinishing() || isDestroyed()) {
                return;
            }
            if (error != null) {
                Log.w(TAG, "update check failed: " + error);
                showUpdateMessage("Update check failed",
                        "Could not check GitHub for updates.\n\n" + error);
                return;
            }
            if (!UpdateChecker.isNewer(release.tag, BuildConfig.VERSION_NAME)) {
                showUpdateMessage("GrumpyScreen is up to date",
                        "You have the latest version, " + release.tag + ".");
                return;
            }
            showUpdatePrompt(release);
        }));
    }

    private void showUpdatePrompt(final UpdateChecker.Release release) {
        GrumpyTheme theme = new GrumpyTheme(this, configFile, overrideFile);
        TextView body = theme.text(new TextView(this), 17, false);
        body.setText("GrumpyScreen " + release.tag + " is available.\n\n"
                + "Installed version: " + BuildConfig.VERSION_NAME + ".\n\n"
                + "Open the GitHub release in your browser, download grumpyscreen.apk,"
                + " then open it to install the update. Your printers and settings are kept.");
        body.setPadding(theme.dp(4), theme.dp(4), theme.dp(4), theme.dp(8));

        GrumpyTheme.ThemedDialog dialog = theme.dialog(this, "Update available", body);
        dialog.addButton("Later", false, v -> dialog.dialog.dismiss());
        dialog.addButton("Open release", true, v -> {
            dialog.dialog.dismiss();
            openUpdateRelease(release);
        });
        dialog.dialog.setOnDismissListener(d -> goFullscreen());
        dialog.show();
    }

    /** Leave downloads and installation to the browser and Android. */
    private void openUpdateRelease(UpdateChecker.Release release) {
        try {
            startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(release.pageUrl))
                    .addCategory(Intent.CATEGORY_BROWSABLE));
        } catch (ActivityNotFoundException e) {
            Log.w(TAG, "unable to open the release page", e);
            showUpdateMessage("Cannot open browser",
                    "Download grumpyscreen.apk from this release and install it manually:\n\n"
                            + release.pageUrl);
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
                if (!name.isEmpty()) {
                    saved.rename(host, port, name);
                }
            } catch (Exception ignored) {
                // not reachable yet; it keeps showing its address
            }
        }).start();
    }

    /** Accepts "host", "host:port" or a pasted URL such as http://host:7125/. */
    static String[] parseHost(String raw, String port) {
        String h = raw.trim();
        int scheme = h.indexOf("://");
        if (scheme >= 0) {
            h = h.substring(scheme + 3);
        }
        int slash = h.indexOf('/');
        if (slash >= 0) {
            h = h.substring(0, slash);
        }
        int colon = h.lastIndexOf(':');
        if (colon > 0 && h.indexOf(':') == colon) {  // not an IPv6 literal
            port = h.substring(colon + 1);
            h = h.substring(0, colon);
        }
        if (h.isEmpty()) {
            return null;
        }
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
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
            }
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
