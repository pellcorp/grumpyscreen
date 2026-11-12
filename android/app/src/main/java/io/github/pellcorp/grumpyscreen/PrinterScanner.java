package io.github.pellcorp.grumpyscreen;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.LinkAddress;
import android.net.LinkProperties;
import android.net.Network;
import android.net.nsd.NsdManager;
import android.net.nsd.NsdServiceInfo;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Looks for Moonraker on the local network:
 * <ul>
 *   <li>every address of the device's IPv4 /24 on the default port,</li>
 *   <li>mDNS {@code _moonraker._tcp} (Moonraker's optional [zeroconf]), and</li>
 *   <li>this device itself (Moonraker in Termux, or an adb reverse tunnel).</li>
 * </ul>
 * A host only counts once it answers {@code /printer/info} like Moonraker.
 * Callbacks arrive on the main thread.
 */
final class PrinterScanner {
    static final int DEFAULT_PORT = 7125;
    private static final String TAG = "grumpyscreen";
    private static final String MDNS_TYPE = "_moonraker._tcp.";
    private static final int CONNECT_TIMEOUT_MS = 400;
    private static final int READ_TIMEOUT_MS = 1500;

    static final class Printer {
        final String host;
        final int port;
        final String name;

        Printer(String host, int port, String name) {
            this.host = host;
            this.port = port;
            this.name = name;
        }

        @Override
        public String toString() {
            String where = host + (port == DEFAULT_PORT ? "" : ":" + port);
            return name == null || name.isEmpty() ? where : name + "  (" + where + ")";
        }
    }

    interface Listener {
        void onFound(Printer printer);

        /** The sweep is over; mDNS stays up until {@link #stop()}. */
        void onFinished(int probed);
    }

    private final Context context;
    private final Listener listener;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final Set<String> seen = new HashSet<>();
    private ExecutorService pool;
    private NsdManager nsd;
    private NsdManager.DiscoveryListener discovery;
    private volatile boolean stopped;

    PrinterScanner(Context context, Listener listener) {
        this.context = context.getApplicationContext();
        this.listener = listener;
    }

    void start() {
        stopped = false;
        pool = Executors.newFixedThreadPool(48);
        startMdns();

        Set<String> targets = new HashSet<>();
        targets.add("127.0.0.1");
        Inet4Address self = localIpv4();
        if (self != null) {
            byte[] a = self.getAddress();
            String prefix = (a[0] & 0xff) + "." + (a[1] & 0xff) + "." + (a[2] & 0xff) + ".";
            for (int i = 1; i < 255; i++) {
                targets.add(prefix + i);
            }
            Log.i(TAG, "scanning " + prefix + "0/24 for Moonraker on port " + DEFAULT_PORT);
        } else {
            Log.i(TAG, "no IPv4 network; only checking this device and mDNS");
        }

        AtomicInteger left = new AtomicInteger(targets.size());
        final int total = targets.size();
        for (String host : targets) {
            pool.execute(() -> {
                if (!stopped) {
                    probe(host, DEFAULT_PORT, null);
                }
                if (left.decrementAndGet() == 0) {
                    main.post(() -> {
                        if (!stopped) {
                            listener.onFinished(total);
                        }
                    });
                }
            });
        }
    }

    void stop() {
        stopped = true;
        if (pool != null) {
            pool.shutdownNow();
        }
        if (nsd != null && discovery != null) {
            try {
                nsd.stopServiceDiscovery(discovery);
            } catch (IllegalArgumentException ignored) {
                // discovery never started
            }
        }
        discovery = null;
    }

    /** The device's IPv4 address on the active network (wifi or ethernet). */
    private Inet4Address localIpv4() {
        ConnectivityManager cm = context.getSystemService(ConnectivityManager.class);
        if (cm == null) {
            return null;
        }
        Network net = cm.getActiveNetwork();
        LinkProperties lp = net == null ? null : cm.getLinkProperties(net);
        if (lp == null) {
            return null;
        }
        for (LinkAddress la : lp.getLinkAddresses()) {
            InetAddress addr = la.getAddress();
            if (addr instanceof Inet4Address && !addr.isLoopbackAddress()) {
                return (Inet4Address) addr;
            }
        }
        return null;
    }

    /** Confirms host:port is Moonraker and reports it, named if possible. */
    private void probe(String host, int port, String mdnsName) {
        String name = mdnsName;
        // /printer/info carries the hostname; it errors (as JSON) while
        // Klippy is down, so fall back to /server/info, which Moonraker
        // always answers.
        JSONObject info = jsonResult(httpGet(host, port, "/printer/info"));
        if (info != null && !info.optString("hostname", "").isEmpty()) {
            name = info.optString("hostname");
        } else {
            JSONObject server = jsonResult(httpGet(host, port, "/server/info"));
            if (server == null || !(server.has("klippy_state") || server.has("moonraker_version"))) {
                if (info == null) {
                    return;
                }
            }
        }
        report(new Printer(host, port, name));
    }

    private static JSONObject jsonResult(String body) {
        if (body == null) {
            return null;
        }
        try {
            return new JSONObject(body).optJSONObject("result");
        } catch (Exception e) {
            return null;
        }
    }

    /** Reachable Moonraker counts as online even while Klipper is restarting. */
    static boolean isOnline(String host, int port) {
        JSONObject server = jsonResult(httpGet(host, port, "/server/info", 2000));
        return server != null && (server.has("klippy_state") || server.has("moonraker_version"));
    }

    private void report(Printer p) {
        main.post(() -> {
            if (stopped || !seen.add(p.host + ":" + p.port)) {
                return;
            }
            listener.onFound(p);
        });
    }

    /**
     * A bare HTTP/1.0 GET over a socket: Android refuses cleartext
     * HttpURLConnection by default, and printers on a LAN are plain HTTP.
     */
    static String httpGet(String host, int port, String path) {
        return httpGet(host, port, path, CONNECT_TIMEOUT_MS);
    }

    /** As above, with a longer wait for one known host than a sweep can afford. */
    static String httpGet(String host, int port, String path, int connectTimeoutMs) {
        try (Socket s = new Socket()) {
            s.connect(new InetSocketAddress(host, port), connectTimeoutMs);
            s.setSoTimeout(READ_TIMEOUT_MS);
            OutputStream out = s.getOutputStream();
            out.write(("GET " + path + " HTTP/1.0\r\nHost: " + host + ":" + port
                    + "\r\nAccept: application/json\r\n\r\n").getBytes(StandardCharsets.US_ASCII));
            out.flush();
            InputStream in = s.getInputStream();
            ByteArrayOutputStream buf = new ByteArrayOutputStream();
            byte[] chunk = new byte[4096];
            int n;
            while ((n = in.read(chunk)) > 0 && buf.size() < 256 * 1024) {
                buf.write(chunk, 0, n);
            }
            String resp = buf.toString("UTF-8");
            int split = resp.indexOf("\r\n\r\n");
            if (!resp.startsWith("HTTP/") || split < 0) {
                return null;
            }
            String[] status = resp.substring(0, resp.indexOf("\r\n")).split(" ", 3);
            if (status.length < 2 || !"200".equals(status[1])) {
                return null;
            }
            return resp.substring(split + 4);
        } catch (Exception e) {
            return null;
        }
    }

    @SuppressWarnings("deprecation")
    private void startMdns() {
        nsd = context.getSystemService(NsdManager.class);
        if (nsd == null) {
            return;
        }
        discovery = new NsdManager.DiscoveryListener() {
            @Override public void onStartDiscoveryFailed(String type, int code) {
                Log.i(TAG, "mDNS discovery unavailable (" + code + ")");
            }
            @Override public void onStopDiscoveryFailed(String type, int code) {}
            @Override public void onDiscoveryStarted(String type) {}
            @Override public void onDiscoveryStopped(String type) {}
            @Override public void onServiceLost(NsdServiceInfo info) {}

            @Override
            public void onServiceFound(NsdServiceInfo info) {
                if (stopped) {
                    return;
                }
                nsd.resolveService(info, new NsdManager.ResolveListener() {
                    @Override public void onResolveFailed(NsdServiceInfo i, int code) {}

                    @Override
                    public void onServiceResolved(NsdServiceInfo i) {
                        InetAddress addr = i.getHost();
                        if (addr == null || stopped || pool.isShutdown()) {
                            return;
                        }
                        String host = addr.getHostAddress();
                        int port = i.getPort() > 0 ? i.getPort() : DEFAULT_PORT;
                        String name = i.getServiceName();
                        try {
                            pool.execute(() -> probe(host, port, name));
                        } catch (Exception ignored) {
                            // scan already stopped
                        }
                    }
                });
            }
        };
        try {
            nsd.discoverServices(MDNS_TYPE, NsdManager.PROTOCOL_DNS_SD, discovery);
        } catch (Exception e) {
            discovery = null;
        }
    }
}
