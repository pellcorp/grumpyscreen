package io.github.pellcorp.grumpyscreen;

import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.os.Process;

import java.io.IOException;
import java.util.UUID;

/**
 * Restarts the app in a fresh process. grumpyscreen is started once per
 * process, so a new printer address only takes effect after a restart. Runs
 * in its own process (see the manifest) so it outlives the one it kills.
 */
public class RestartActivity extends Activity {
    static final String EXTRA_PID = "pid";
    private static final String EXTRA_TOKEN = "restart_token";
    private static final String PREFS = "printer_restart";

    static void restart(Activity from, byte[] config) throws IOException {
        String token = UUID.randomUUID().toString();
        if (!from.getSharedPreferences(PREFS, MODE_PRIVATE).edit().putString(EXTRA_TOKEN, token).commit()) {
            throw new IOException("Unable to prepare the printer restart");
        }
        Intent intent = new Intent(from, RestartActivity.class);
        intent.putExtra(EXTRA_PID, Process.myPid());
        intent.putExtra(MainActivity.EXTRA_CONNECT, true);
        intent.putExtra(MainActivity.EXTRA_CONFIG, config);
        intent.putExtra(EXTRA_TOKEN, token);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        from.startActivity(intent);
        from.finishAffinity();
    }

    /** Only an internal restart may supply configuration to the exported launcher Activity. */
    static boolean consumePreparedRestart(Activity activity) {
        SharedPreferences prefs = activity.getSharedPreferences(PREFS, MODE_PRIVATE);
        String expected = prefs.getString(EXTRA_TOKEN, null);
        String token = activity.getIntent().getStringExtra(EXTRA_TOKEN);
        prefs.edit().remove(EXTRA_TOKEN).apply();
        activity.getIntent().removeExtra(EXTRA_TOKEN);
        return expected != null && expected.equals(token);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Process.killProcess(getIntent().getIntExtra(EXTRA_PID, -1));
        Intent main = new Intent(this, MainActivity.class);
        main.putExtra(MainActivity.EXTRA_CONNECT,
                getIntent().getBooleanExtra(MainActivity.EXTRA_CONNECT, false));
        main.putExtra(MainActivity.EXTRA_CONFIG, getIntent().getByteArrayExtra(MainActivity.EXTRA_CONFIG));
        main.putExtra(EXTRA_TOKEN, getIntent().getStringExtra(EXTRA_TOKEN));
        main.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        startActivity(main);
        finish();
        Runtime.getRuntime().exit(0);
    }
}
