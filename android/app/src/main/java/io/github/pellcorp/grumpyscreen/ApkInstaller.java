package io.github.pellcorp.grumpyscreen;

import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInstaller;
import android.os.Build;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/** Installs a downloaded APK through Android's package installer. */
final class ApkInstaller {
    static final String ACTION_INSTALL_RESULT =
            "io.github.pellcorp.grumpyscreen.INSTALL_RESULT";

    private ApkInstaller() {}

    /** Whether this app may start an install; before Android 8 it is a device setting. */
    static boolean allowed(Context context) {
        return Build.VERSION.SDK_INT < Build.VERSION_CODES.O
                || context.getPackageManager().canRequestPackageInstalls();
    }

    /**
     * Hands the APK to the system installer. It asks the user to confirm, then
     * replaces the app; the outcome arrives at {@link InstallReceiver}.
     */
    static void install(Context context, File apk) throws IOException {
        PackageInstaller installer = context.getPackageManager().getPackageInstaller();
        PackageInstaller.SessionParams params = new PackageInstaller.SessionParams(
                PackageInstaller.SessionParams.MODE_FULL_INSTALL);
        params.setAppPackageName(context.getPackageName());

        int sessionId = installer.createSession(params);
        PackageInstaller.Session session = null;
        boolean committed = false;
        try {
            session = installer.openSession(sessionId);
            try (InputStream in = new FileInputStream(apk);
                 OutputStream out = session.openWrite("grumpyscreen.apk", 0, apk.length())) {
                byte[] buf = new byte[64 * 1024];
                int n;
                while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
                session.fsync(out);
            }
            Intent result = new Intent(context, InstallReceiver.class)
                    .setAction(ACTION_INSTALL_RESULT);
            int flags = PendingIntent.FLAG_UPDATE_CURRENT;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                // the installer adds the user-action intent to the result
                flags |= PendingIntent.FLAG_MUTABLE;
            }
            PendingIntent pending = PendingIntent.getBroadcast(context, sessionId, result, flags);
            session.commit(pending.getIntentSender());
            committed = true;
        } finally {
            if (session != null) {
                if (!committed) {
                    try {
                        session.abandon();
                    } catch (RuntimeException ignored) {
                        // already gone
                    }
                }
                session.close();
            } else {
                try {
                    installer.abandonSession(sessionId);
                } catch (RuntimeException ignored) {
                    // already gone
                }
            }
        }
    }
}
