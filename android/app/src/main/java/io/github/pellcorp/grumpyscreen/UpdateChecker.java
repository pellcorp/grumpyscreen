package io.github.pellcorp.grumpyscreen;

import android.content.Context;
import android.content.pm.PackageInfo;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Finds and downloads the latest signed grumpyscreen.apk from this project's
 * GitHub releases. Android still asks the user to confirm the install; see
 * {@link ApkInstaller}.
 */
final class UpdateChecker {
    private static final String TAG = "grumpyscreen";
    private static final String LATEST_RELEASE_URL =
            "https://api.github.com/repos/pellcorp/grumpyscreen/releases/latest";
    private static final String APK_NAME = "grumpyscreen.apk";
    private static final int CONNECT_TIMEOUT_MS = 15000;
    private static final int READ_TIMEOUT_MS = 30000;
    private static final int MAX_JSON_BYTES = 1024 * 1024;
    private static final Pattern RELEASE_TAG = Pattern.compile("^v(\\d+)\\.(\\d+)\\.(\\d+)-(\\d+)$");

    /** The APK attached to the latest release. */
    static final class Release {
        final String tag;
        final String url;
        final String sha256;  // null when GitHub did not report a digest
        final long size;

        Release(String tag, String url, String sha256, long size) {
            this.tag = tag;
            this.url = url;
            this.sha256 = sha256;
            this.size = size;
        }
    }

    interface CheckListener {
        /** Called on the checking thread; one of the arguments is null. */
        void onResult(Release release, String error);
    }

    interface ProgressListener {
        /** Percent downloaded, or -1 when the size is not known. */
        void onProgress(int percent);
    }

    private UpdateChecker() {}

    /** Asks GitHub for the latest release. Always runs off the calling thread. */
    static void check(CheckListener listener) {
        new Thread(() -> {
            Release release = null;
            String error = null;
            try {
                release = parse(new JSONObject(httpGet(LATEST_RELEASE_URL)));
                if (release == null) {
                    error = "The latest release has no " + APK_NAME + ".";
                }
            } catch (Exception e) {
                Log.w(TAG, "unable to check for updates", e);
                error = e.getMessage() == null ? e.toString() : e.getMessage();
            }
            listener.onResult(release, error);
        }, "update-check").start();
    }

    /**
     * Whether the release tag is newer than this build. A development build
     * (its version is a commit hash, not a release tag) counts as older so the
     * update can still be installed by hand.
     */
    static boolean isNewer(String candidate, String current) {
        int[] newer = parseTag(candidate);
        if (newer == null) return false;
        int[] older = parseTag(current);
        if (older == null) return true;
        for (int i = 0; i < newer.length; i++) {
            if (newer[i] != older[i]) return newer[i] > older[i];
        }
        return false;
    }

    /** Whether the version looks like one of our release tags. */
    static boolean isReleaseVersion(String version) {
        return parseTag(version) != null;
    }

    private static int[] parseTag(String version) {
        if (version == null) return null;
        Matcher m = RELEASE_TAG.matcher(version.trim());
        if (!m.matches()) return null;
        try {
            return new int[]{Integer.parseInt(m.group(1)), Integer.parseInt(m.group(2)),
                    Integer.parseInt(m.group(3)), Integer.parseInt(m.group(4))};
        } catch (NumberFormatException e) {
            return null;
        }
    }

    private static Release parse(JSONObject json) {
        String tag = json.optString("tag_name", "");
        JSONArray assets = json.optJSONArray("assets");
        if (tag.isEmpty() || assets == null) return null;
        for (int i = 0; i < assets.length(); i++) {
            JSONObject asset = assets.optJSONObject(i);
            if (asset == null || !APK_NAME.equals(asset.optString("name", ""))) continue;
            String url = asset.optString("browser_download_url", "");
            if (!url.startsWith("https://")) return null;
            String digest = asset.optString("digest", "");
            String sha256 = digest.startsWith("sha256:") ? digest.substring("sha256:".length()) : null;
            return new Release(tag, url, sha256, asset.optLong("size", -1));
        }
        return null;
    }

    /**
     * Downloads the APK into the cache, checks GitHub's SHA-256 and that it is
     * this app, and returns it. Call from a background thread.
     */
    static File download(Context context, Release release, ProgressListener listener) throws Exception {
        File dir = new File(context.getCacheDir(), "updates");
        if (!dir.isDirectory() && !dir.mkdirs()) {
            throw new IOException("Cannot create " + dir);
        }
        File[] old = dir.listFiles();
        if (old != null) {
            for (File f : old) {
                if (!f.delete()) Log.w(TAG, "unable to delete " + f);
            }
        }
        File part = new File(dir, "grumpyscreen-" + safeName(release.tag) + ".part");
        File apk = new File(dir, "grumpyscreen-" + safeName(release.tag) + ".apk");

        HttpURLConnection connection =
                (HttpURLConnection) new URL(release.url).openConnection();
        connection.setConnectTimeout(CONNECT_TIMEOUT_MS);
        connection.setReadTimeout(READ_TIMEOUT_MS);
        connection.setInstanceFollowRedirects(true);
        connection.setRequestProperty("Accept", "application/octet-stream");
        connection.setRequestProperty("User-Agent", userAgent());
        try {
            int code = connection.getResponseCode();
            if (code != 200) {
                throw new IOException("GitHub answered HTTP " + code + " for the APK.");
            }
            long total = connection.getContentLengthLong() > 0
                    ? connection.getContentLengthLong() : release.size;
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            long copied = 0;
            int lastPercent = -1;
            try (InputStream in = connection.getInputStream();
                 OutputStream out = new FileOutputStream(part)) {
                byte[] buf = new byte[64 * 1024];
                int n;
                while ((n = in.read(buf)) > 0) {
                    out.write(buf, 0, n);
                    digest.update(buf, 0, n);
                    copied += n;
                    if (total > 0) {
                        int percent = (int) Math.min(100, copied * 100 / total);
                        if (percent != lastPercent) {
                            lastPercent = percent;
                            listener.onProgress(percent);
                        }
                    }
                }
            }
            if (release.size > 0 && copied != release.size) {
                throw new IOException("The APK download was incomplete.");
            }
            String actual = hex(digest.digest());
            if (release.sha256 != null && !release.sha256.equalsIgnoreCase(actual)) {
                throw new IOException("The downloaded APK does not match GitHub's checksum.");
            }
            PackageInfo info = context.getPackageManager()
                    .getPackageArchiveInfo(part.getAbsolutePath(), 0);
            if (info == null || !context.getPackageName().equals(info.packageName)) {
                throw new IOException("The downloaded file is not a GrumpyScreen APK.");
            }
            if (!part.renameTo(apk)) {
                // the previous attempt with this tag may still be there
                if (!apk.delete() || !part.renameTo(apk)) {
                    throw new IOException("Cannot keep the downloaded APK.");
                }
            }
            listener.onProgress(100);
            return apk;
        } catch (Exception e) {
            if (part.exists() && !part.delete()) Log.w(TAG, "unable to delete " + part);
            throw e;
        } finally {
            connection.disconnect();
        }
    }

    private static String httpGet(String url) throws IOException {
        HttpURLConnection connection = (HttpURLConnection) new URL(url).openConnection();
        connection.setConnectTimeout(CONNECT_TIMEOUT_MS);
        connection.setReadTimeout(READ_TIMEOUT_MS);
        connection.setRequestProperty("Accept", "application/vnd.github+json");
        connection.setRequestProperty("X-GitHub-Api-Version", "2022-11-28");
        connection.setRequestProperty("User-Agent", userAgent());
        try {
            int code = connection.getResponseCode();
            if (code != 200) {
                String detail = "";
                InputStream error = connection.getErrorStream();
                if (error != null) {
                    try (InputStream in = error) {
                        // GitHub explains failures as {"message": "..."}
                        detail = new JSONObject(read(in)).optString("message", "");
                    } catch (Exception ignored) {
                        // no body to explain the status
                    }
                }
                if (code == 403 && detail.toLowerCase(Locale.US).contains("rate limit")) {
                    throw new IOException("GitHub is limiting update checks. Try again later.");
                }
                throw new IOException("GitHub answered HTTP " + code
                        + (detail.isEmpty() ? "" : ": " + detail));
            }
            try (InputStream in = connection.getInputStream()) {
                return read(in);
            }
        } finally {
            connection.disconnect();
        }
    }

    private static String read(InputStream in) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buf = new byte[8192];
        int n;
        while ((n = in.read(buf)) > 0) {
            out.write(buf, 0, n);
            if (out.size() > MAX_JSON_BYTES) throw new IOException("GitHub's answer is too large.");
        }
        return out.toString(StandardCharsets.UTF_8.name());
    }

    private static String userAgent() {
        return "GrumpyScreen/" + BuildConfig.VERSION_NAME;
    }

    private static String safeName(String tag) {
        return tag.replaceAll("[^A-Za-z0-9._-]", "_");
    }

    private static String hex(byte[] bytes) {
        StringBuilder out = new StringBuilder(bytes.length * 2);
        for (byte b : bytes) out.append(String.format(Locale.US, "%02x", b));
        return out.toString();
    }
}
