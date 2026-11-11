package io.github.pellcorp.grumpyscreen;

import android.util.Log;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** Finds GitHub releases with a grumpyscreen.apk for the user to download. */
final class UpdateChecker {
    private static final String TAG = "grumpyscreen";
    private static final String LATEST_RELEASE_URL =
            "https://api.github.com/repos/pellcorp/grumpyscreen/releases/latest";
    private static final String APK_NAME = "grumpyscreen.apk";
    private static final int CONNECT_TIMEOUT_MS = 15000;
    private static final int READ_TIMEOUT_MS = 30000;
    private static final int MAX_JSON_BYTES = 1024 * 1024;
    private static final Pattern RELEASE_TAG = Pattern.compile("^v(\\d+)\\.(\\d+)\\.(\\d+)-(\\d+)$");

    /** A release with an APK and its page to open in the browser. */
    static final class Release {
        final String tag;
        final String pageUrl;

        Release(String tag) {
            this.tag = tag;
            // parse() only accepts numeric release tags, so no URL escaping is needed.
            this.pageUrl = "https://github.com/pellcorp/grumpyscreen/releases/tag/" + tag;
        }
    }

    interface CheckListener {
        /** Called on the checking thread; one of the arguments is null. */
        void onResult(Release release, String error);
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
        String tag = json.optString("tag_name", "").trim();
        JSONArray assets = json.optJSONArray("assets");
        if (!isReleaseVersion(tag) || assets == null) return null;
        for (int i = 0; i < assets.length(); i++) {
            JSONObject asset = assets.optJSONObject(i);
            if (asset == null || !APK_NAME.equals(asset.optString("name", ""))) continue;
            String url = asset.optString("browser_download_url", "");
            if (!url.startsWith("https://")) return null;
            return new Release(tag);
        }
        return null;
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
}
