package io.github.pellcorp.grumpyscreen;

import android.content.SharedPreferences;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;

/**
 * The printers the user has connected to, most recently used first, and which
 * one is current. Kept in SharedPreferences as JSON.
 */
final class SavedPrinters {
    private static final String KEY_LIST = "printers";
    // the current printer; also what versions with a single printer stored
    static final String KEY_HOST = "host";
    static final String KEY_PORT = "port";

    private final SharedPreferences prefs;

    SavedPrinters(SharedPreferences prefs) {
        this.prefs = prefs;
        // carry over the single printer older builds saved
        if (!prefs.contains(KEY_LIST) && prefs.contains(KEY_HOST)) {
            String host = prefs.getString(KEY_HOST, "");
            int port = prefs.getInt(KEY_PORT, PrinterScanner.DEFAULT_PORT);
            save(listOf(new PrinterScanner.Printer(host, port, null)));
        }
    }

    boolean hasCurrent() {
        return prefs.contains(KEY_HOST);
    }

    String currentHost() {
        return prefs.getString(KEY_HOST, null);
    }

    int currentPort() {
        return prefs.getInt(KEY_PORT, PrinterScanner.DEFAULT_PORT);
    }

    boolean isCurrent(PrinterScanner.Printer p) {
        return p.host.equals(currentHost()) && p.port == currentPort();
    }

    List<PrinterScanner.Printer> all() {
        List<PrinterScanner.Printer> out = new ArrayList<>();
        try {
            JSONArray a = new JSONArray(prefs.getString(KEY_LIST, "[]"));
            for (int i = 0; i < a.length(); i++) {
                JSONObject o = a.getJSONObject(i);
                String name = o.optString("name", "");
                out.add(new PrinterScanner.Printer(o.getString("host"),
                        o.optInt("port", PrinterScanner.DEFAULT_PORT), name.isEmpty() ? null : name));
            }
        } catch (Exception ignored) {
            // corrupt entry: start over rather than fail to launch
        }
        return out;
    }

    PrinterScanner.Printer find(String host, int port) {
        for (PrinterScanner.Printer p : all()) {
            if (p.host.equals(host) && p.port == port) {
                return p;
            }
        }
        return null;
    }

    /** Makes host:port current and moves it to the top, keeping a known name. */
    void use(String host, int port, String name) {
        List<PrinterScanner.Printer> list = all();
        PrinterScanner.Printer old = find(host, port);
        String keep = name != null ? name : old != null ? old.name : null;
        List<PrinterScanner.Printer> out = new ArrayList<>();
        out.add(new PrinterScanner.Printer(host, port, keep));
        for (PrinterScanner.Printer p : list) {
            if (!(p.host.equals(host) && p.port == port)) {
                out.add(p);
            }
        }
        save(out);
        prefs.edit().putString(KEY_HOST, host).putInt(KEY_PORT, port).commit();
    }

    /** Fills in a name learnt later (from the printer's hostname). */
    void rename(String host, int port, String name) {
        List<PrinterScanner.Printer> out = new ArrayList<>();
        for (PrinterScanner.Printer p : all()) {
            out.add(p.host.equals(host) && p.port == port ? new PrinterScanner.Printer(host, port, name) : p);
        }
        save(out);
    }

    void remove(PrinterScanner.Printer gone) {
        List<PrinterScanner.Printer> out = new ArrayList<>();
        for (PrinterScanner.Printer p : all()) {
            if (!(p.host.equals(gone.host) && p.port == gone.port)) {
                out.add(p);
            }
        }
        save(out);
    }

    private static List<PrinterScanner.Printer> listOf(PrinterScanner.Printer p) {
        List<PrinterScanner.Printer> l = new ArrayList<>();
        l.add(p);
        return l;
    }

    private void save(List<PrinterScanner.Printer> list) {
        JSONArray a = new JSONArray();
        try {
            for (PrinterScanner.Printer p : list) {
                JSONObject o = new JSONObject();
                o.put("host", p.host);
                o.put("port", p.port);
                if (p.name != null) {
                    o.put("name", p.name);
                }
                a.put(o);
            }
        } catch (Exception ignored) {
            // JSONObject.put only throws for non-finite numbers
        }
        prefs.edit().putString(KEY_LIST, a.toString()).commit();
    }
}
