package io.github.pellcorp.grumpyscreen;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInstaller;
import android.os.Build;
import android.util.Log;
import android.widget.Toast;

/** The outcome of an update handed to Android's package installer. */
public class InstallReceiver extends BroadcastReceiver {
    private static final String TAG = "grumpyscreen";

    @Override
    @SuppressWarnings("deprecation")
    public void onReceive(Context context, Intent intent) {
        int status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS,
                PackageInstaller.STATUS_FAILURE);
        if (status == PackageInstaller.STATUS_PENDING_USER_ACTION) {
            // Android wants the app to show its install confirmation
            Intent confirm = Build.VERSION.SDK_INT >= 33
                    ? intent.getParcelableExtra(Intent.EXTRA_INTENT, Intent.class)
                    : (Intent) intent.getParcelableExtra(Intent.EXTRA_INTENT);
            if (confirm != null) {
                confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                try {
                    context.startActivity(confirm);
                } catch (RuntimeException e) {
                    Log.e(TAG, "unable to show the installer", e);
                }
            }
            return;
        }

        String message = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE);
        Log.i(TAG, "update install status " + status + ": " + message);
        if (status != PackageInstaller.STATUS_SUCCESS) {
            Toast.makeText(context, failure(status, message), Toast.LENGTH_LONG).show();
        }
    }

    private static String failure(int status, String message) {
        switch (status) {
            case PackageInstaller.STATUS_FAILURE_ABORTED:
                return "Update cancelled.";
            case PackageInstaller.STATUS_FAILURE_BLOCKED:
                return "Android blocked the update. Allow installs from GrumpyScreen in Settings.";
            case PackageInstaller.STATUS_FAILURE_CONFLICT:
                return "The update conflicts with the installed app; it may be signed with another key.";
            case PackageInstaller.STATUS_FAILURE_INCOMPATIBLE:
                return "This update is not compatible with the device.";
            case PackageInstaller.STATUS_FAILURE_STORAGE:
                return "There is not enough storage to install the update.";
            case PackageInstaller.STATUS_FAILURE_INVALID:
                return "The downloaded update is not a valid APK.";
            default:
                return message == null || message.isEmpty()
                        ? "The update could not be installed." : "Update failed: " + message;
        }
    }
}
