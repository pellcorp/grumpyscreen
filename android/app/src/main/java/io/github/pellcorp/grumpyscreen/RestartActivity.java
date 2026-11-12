package io.github.pellcorp.grumpyscreen;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Process;

/**
 * Restarts the app in a fresh process. grumpyscreen is started once per
 * process, so a new printer address only takes effect after a restart. Runs
 * in its own process (see the manifest) so it outlives the one it kills.
 */
public class RestartActivity extends Activity {
    static final String EXTRA_PID = "pid";

    static void restart(Activity from) {
        Intent intent = new Intent(from, RestartActivity.class);
        intent.putExtra(EXTRA_PID, Process.myPid());
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        from.startActivity(intent);
        from.finishAffinity();
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Process.killProcess(getIntent().getIntExtra(EXTRA_PID, -1));
        Intent main = new Intent(this, MainActivity.class);
        main.putExtra(MainActivity.EXTRA_CONNECT, true);
        main.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        startActivity(main);
        finish();
        Runtime.getRuntime().exit(0);
    }
}
