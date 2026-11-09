package io.github.pellcorp.grumpyscreen;

import android.app.Activity;
import android.app.Dialog;
import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.util.HashMap;
import java.util.Map;

/**
 * grumpyscreen's look for the app's own (Android) screens: the [theme]
 * colours from grumpyscreen.cfg and the user's override, and Montserrat, the
 * font LVGL's built-in fonts come from.
 */
final class GrumpyTheme {
    final int bg, surface, raised, border, text, textDim, primary, onPrimary, danger;
    final Typeface font;
    private final float density;

    GrumpyTheme(Context context, File... configs) {
        Map<String, Integer> c = new HashMap<>();
        for (File f : configs) readTheme(f, c);
        bg = c.getOrDefault("bg_colour", 0x282B30);
        surface = c.getOrDefault("surface_colour", bg);
        raised = c.getOrDefault("raised_colour", 0x2F3237);
        border = c.getOrDefault("border_colour", raised);
        text = c.getOrDefault("text_colour", 0xFAFAFA);
        textDim = c.getOrDefault("text_dim_colour", 0x9E9E9E);
        primary = c.getOrDefault("primary_colour", 0x2196F3);
        onPrimary = c.getOrDefault("on_primary_colour", 0xFFFFFF);
        danger = c.getOrDefault("danger_colour", 0xD32F2F);
        Typeface tf;
        try {
            tf = Typeface.createFromAsset(context.getAssets(), "Montserrat-Medium.ttf");
        } catch (RuntimeException e) {
            tf = Typeface.DEFAULT;
        }
        font = tf;
        density = context.getResources().getDisplayMetrics().density;
    }

    /** Later files win, as the override does over the main config. */
    private static void readTheme(File f, Map<String, Integer> out) {
        if (f == null || !f.exists()) return;
        try (BufferedReader r = new BufferedReader(new FileReader(f))) {
            boolean inTheme = false;
            String line;
            while ((line = r.readLine()) != null) {
                String t = line.trim();
                if (t.isEmpty() || t.startsWith("#") || t.startsWith(";")) continue;
                if (t.startsWith("[")) {
                    inTheme = t.equals("[theme]");
                    continue;
                }
                int sep = t.indexOf(':') >= 0 ? t.indexOf(':') : t.indexOf('=');
                if (!inTheme || sep < 0) continue;
                String key = t.substring(0, sep).trim();
                String val = t.substring(sep + 1).trim();
                int hash = val.indexOf('#');
                if (hash >= 0) val = val.substring(0, hash).trim();
                try {
                    out.put(key, Integer.decode(val) & 0xFFFFFF);
                } catch (NumberFormatException ignored) {
                    // not a colour
                }
            }
        } catch (Exception ignored) {
            // unreadable: defaults
        }
    }

    static int opaque(int rgb) {
        return 0xFF000000 | rgb;
    }

    static int alpha(int rgb, int a) {
        return (a << 24) | (rgb & 0xFFFFFF);
    }

    int dp(float v) {
        return Math.round(v * density);
    }

    private GradientDrawable box(int fill, int radiusDp, int strokeColour) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(fill);
        d.setCornerRadius(dp(radiusDp));
        if (strokeColour != 0) d.setStroke(dp(1.5f), strokeColour);
        return d;
    }

    TextView text(TextView v, float sp, boolean dim) {
        v.setTypeface(font);
        v.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        v.setTextColor(opaque(dim ? textDim : text));
        return v;
    }

    /** A selectable list row: plain, accent tint when selected. */
    TextView row(TextView v) {
        text(v, 17, false);
        v.setPadding(dp(14), dp(12), dp(14), dp(12));
        v.setBackground(box(Color.TRANSPARENT, 7, 0));
        return v;
    }

    void select(TextView row, boolean on) {
        row.setBackground(box(on ? alpha(primary, 0x55) : Color.TRANSPARENT, 7, 0));
    }

    EditText input(EditText v) {
        text(v, 17, false);
        v.setHintTextColor(opaque(textDim));
        v.setPadding(dp(12), dp(10), dp(12), dp(10));
        StateListDrawable bgd = new StateListDrawable();
        bgd.addState(new int[]{android.R.attr.state_focused}, box(opaque(raised), 7, opaque(primary)));
        bgd.addState(new int[]{}, box(opaque(raised), 7, opaque(border)));
        v.setBackground(bgd);
        if (android.os.Build.VERSION.SDK_INT >= 29) {
            GradientDrawable cursor = new GradientDrawable();
            cursor.setColor(opaque(primary));
            cursor.setSize(dp(2), 0);
            v.setTextCursorDrawable(cursor);
        }
        return v;
    }

    /** grumpyscreen's grey button; {@code accent} for the main action. */
    Button button(Button b, boolean accent) {
        b.setAllCaps(false);
        b.setTypeface(font);
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
        int fill = accent ? primary : raised;
        int pressed = accent ? alpha(primary, 0xB0) : alpha(textDim, 0x60);
        StateListDrawable d = new StateListDrawable();
        d.addState(new int[]{-android.R.attr.state_enabled}, box(alpha(raised, 0x80), 7, 0));
        d.addState(new int[]{android.R.attr.state_pressed}, box(pressed, 7, 0));
        d.addState(new int[]{}, box(opaque(fill), 7, 0));
        b.setBackground(d);
        b.setStateListAnimator(null);
        b.setTextColor(new ColorStateList(
                new int[][]{{-android.R.attr.state_enabled}, {}},
                new int[]{alpha(textDim, 0x90), opaque(accent ? onPrimary : text)}));
        b.setPadding(dp(18), dp(10), dp(18), dp(10));
        // no square system focus box over the rounded button
        if (android.os.Build.VERSION.SDK_INT >= 26) b.setDefaultFocusHighlightEnabled(false);
        b.setMinHeight(dp(44));
        b.setMinimumHeight(dp(44));
        return b;
    }

    CheckBox check(CheckBox c) {
        text(c, 16, false);
        c.setButtonTintList(new ColorStateList(
                new int[][]{{android.R.attr.state_checked}, {}},
                new int[]{opaque(primary), opaque(textDim)}));
        return c;
    }

    /**
     * A dialog in grumpyscreen's style: a rounded surface panel with a title,
     * the given body and a row of buttons.
     */
    final class ThemedDialog {
        final Dialog dialog;
        private final LinearLayout buttons;
        private boolean spacer;

        ThemedDialog(Activity activity, String title, View body) {
            dialog = new Dialog(activity);
            dialog.requestWindowFeature(Window.FEATURE_NO_TITLE);
            LinearLayout panel = new LinearLayout(activity);
            panel.setOrientation(LinearLayout.VERTICAL);
            panel.setBackground(box(opaque(surface), 12, opaque(border)));
            panel.setPadding(dp(20), dp(16), dp(20), dp(14));

            TextView t = text(new TextView(activity), 22, false);
            t.setText(title);
            t.setPadding(dp(4), 0, 0, dp(8));
            panel.addView(t);
            // the body scrolls; cap it so title and buttons always fit on a
            // short landscape screen
            final int maxBody = activity.getResources().getDisplayMetrics().heightPixels - dp(150);
            android.widget.FrameLayout cap = new android.widget.FrameLayout(activity) {
                @Override
                protected void onMeasure(int widthSpec, int heightSpec) {
                    super.onMeasure(widthSpec, View.MeasureSpec.makeMeasureSpec(
                            Math.max(maxBody, dp(80)), View.MeasureSpec.AT_MOST));
                }
            };
            cap.addView(body);
            panel.addView(cap, new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

            buttons = new LinearLayout(activity);
            buttons.setOrientation(LinearLayout.HORIZONTAL);
            buttons.setGravity(Gravity.CENTER_VERTICAL);
            buttons.setPadding(0, dp(12), 0, 0);
            panel.addView(buttons);

            dialog.setContentView(panel);
            Window w = dialog.getWindow();
            if (w != null) {
                w.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
                int width = activity.getResources().getDisplayMetrics().widthPixels;
                w.setLayout(Math.min(width - dp(32), dp(620)), ViewGroup.LayoutParams.WRAP_CONTENT);
                w.addFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
                w.setDimAmount(0.6f);
            }
        }

        /** Buttons added after this sit at the right. */
        ThemedDialog spacer() {
            View s = new View(dialog.getContext());
            buttons.addView(s, new LinearLayout.LayoutParams(0, 1, 1f));
            spacer = true;
            return this;
        }

        Button addButton(String label, boolean accent, View.OnClickListener onClick) {
            Button b = button(new Button(dialog.getContext()), accent);
            b.setText(label);
            b.setOnClickListener(onClick);
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
            if (buttons.getChildCount() > 0) lp.leftMargin = dp(10);
            buttons.addView(b, lp);
            return b;
        }

        void show() {
            if (!spacer) {
                // right-align the buttons
                View s = new View(dialog.getContext());
                buttons.addView(s, 0, new LinearLayout.LayoutParams(0, 1, 1f));
            }
            dialog.show();
        }
    }

    ThemedDialog dialog(Activity activity, String title, View body) {
        return new ThemedDialog(activity, title, body);
    }

    /** A yes/no question in the same style. */
    void confirm(Activity activity, String title, String message, String yes, Runnable onYes) {
        TextView m = text(new TextView(activity), 17, false);
        m.setText(message);
        m.setPadding(dp(4), dp(4), dp(4), dp(8));
        ThemedDialog d = dialog(activity, title, m);
        d.addButton("Cancel", false, v -> d.dialog.dismiss());
        Button y = d.addButton(yes, false, v -> {
            d.dialog.dismiss();
            onYes.run();
        });
        y.setTextColor(opaque(danger));
        d.show();
    }
}
