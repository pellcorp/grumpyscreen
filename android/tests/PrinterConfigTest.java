package io.github.pellcorp.grumpyscreen;

import com.sun.net.httpserver.HttpServer;

import java.io.IOException;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

/** Exercises actual HTTP responses without Android or a printer. */
public final class PrinterConfigTest {
    private static final String PATH = "/server/files/config/grumpyscreen.ini";

    public static void main(String[] args) throws Exception {
        AtomicInteger status = new AtomicInteger(200);
        AtomicReference<byte[]> body = new AtomicReference<>();
        AtomicReference<String> request = new AtomicReference<>();
        AtomicReference<String> cacheControl = new AtomicReference<>();
        AtomicInteger transfer = new AtomicInteger(0); // fixed length, chunked, truncated
        Path cache = Files.createTempDirectory("grumpyscreen-config-test");
        Path session = cache.resolve("grumpyscreen.ini");
        HttpServer printer = HttpServer.create(new InetSocketAddress("127.0.0.1", 0), 0);
        printer.createContext("/", exchange -> {
            request.set(exchange.getRequestURI().toString());
            cacheControl.set(exchange.getRequestHeaders().getFirst("Cache-Control"));
            byte[] bytes = body.get();
            exchange.getResponseHeaders().set("Location", "/login");
            int mode = transfer.get();
            long length = mode == 1 ? 0 : mode == 2 ? bytes.length + 100
                    : bytes.length == 0 ? -1 : bytes.length;
            exchange.sendResponseHeaders(status.get(), length);
            try (java.io.OutputStream out = exchange.getResponseBody()) {
                out.write(bytes);
            } catch (IOException ignored) {
                // The client rejects oversized responses without reading them
                // all, and the truncation case deliberately closes early.
            } finally {
                exchange.close();
            }
        });
        printer.start();
        int port = printer.getAddress().getPort();
        try {
            // This is a normal printer override, including macros introduced
            // by the override and a printer-local Moonraker address.
            String ini = "# My printer\r\n[ui]\r\nprompt_emergency_stop: false\r\n"
                    + "[theme]\r\nprimary_colour = 0x00FF00 ; green\r\n"
                    + "[macro \"G28\"]\r\ndisplay_name: Home\r\n"
                    + "[moonraker]\r\nhost: 127.0.0.1\r\n";
            body.set(ini.getBytes(StandardCharsets.UTF_8));
            byte[] downloaded = PrinterConfig.download("127.0.0.1", port);
            require(Arrays.equals(body.get(), downloaded),
                    "INI contents and formatting survive the download");
            require(request.get().startsWith(PATH + "?date="), "Uses the Moonraker config root");
            require("no-cache".equals(cacheControl.get()), "Asks proxies for current configuration");
            // Connect prepares these bytes before the native UI opens. A new
            // process installs the same download when switching printers.
            PrinterConfig.install(session.toFile(), downloaded);
            require(Arrays.equals(downloaded, Files.readAllBytes(session)),
                    "Installs the prepared configuration without changing its contents");

            // A reconnect must observe edits, and removal must not reuse the
            // previous successful response.
            body.set("[ui]\nprompt_emergency_stop: true\n".getBytes(StandardCharsets.UTF_8));
            downloaded = PrinterConfig.download("127.0.0.1", port);
            require(Arrays.equals(body.get(), downloaded),
                    "Reconnecting fetches edited configuration");
            PrinterConfig.install(session.toFile(), downloaded);
            require(Arrays.equals(downloaded, Files.readAllBytes(session)),
                    "A shorter configuration completely replaces the previous session");
            status.set(404);
            downloaded = PrinterConfig.download("127.0.0.1", port);
            require(downloaded == null, "Missing INI uses defaults");
            PrinterConfig.install(session.toFile(), downloaded);
            require(!Files.exists(session), "Using defaults removes the previous printer's override");
            PrinterConfig.install(session.toFile(), downloaded);
            require(!Files.exists(session), "Defaults also work in a fresh process without a local file");

            for (int code : new int[]{401, 403, 500, 503, 302}) {
                status.set(code);
                expectFailure(port, "HTTP " + code);
            }
            status.set(200);
            for (String bad : new String[]{"<html>Login required</html>", "{\"error\":404}",
                    "[ui]\nprompt_emergency_stop: true\n[ui \"invalid\"]\nkey: value",
                    "[ui \"invalid\"]\nkey: value\n[ui]\nkey: value", "[ui", "[]=value"}) {
                body.set(bad.getBytes(StandardCharsets.UTF_8));
                expectFailure(port, "Invalid grumpyscreen.ini");
            }
            body.set(new byte[]{(byte) 0xFF});
            expectFailure(port, "UTF-8");
            body.set(new byte[256 * 1024 + 1]);
            expectFailure(port, "exceeds 256 KiB");
            transfer.set(1);
            expectFailure(port, "exceeds 256 KiB");
            transfer.set(2);
            body.set("[ui]\nprompt_emergency_stop: false\n".getBytes(StandardCharsets.UTF_8));
            // A server that stops before its declared length can close or
            // leave the socket open. Both EOF and read timeout must fail.
            expectFailure(port, null);
            transfer.set(0);
            body.set("# An empty override uses defaults\n".getBytes(StandardCharsets.UTF_8));
            require(Arrays.equals(body.get(), PrinterConfig.download("127.0.0.1", port)),
                    "An intentionally empty override is allowed");
            body.set(new byte[0]);
            require(PrinterConfig.download("127.0.0.1", port).length == 0, "Empty file is allowed");

            // The APK's bundled defaults must also be valid input, allowing
            // an existing full config to be used as the printer's override.
            body.set(java.nio.file.Files.readAllBytes(java.nio.file.Paths.get(args[0])));
            require(Arrays.equals(body.get(), PrinterConfig.download("127.0.0.1", port)),
                    "Accepts the repository's full configuration");
        } finally {
            printer.stop(0);
            Files.deleteIfExists(session);
            Files.delete(cache);
        }
        System.out.println("Printer configuration HTTP and installation tests passed");
    }

    private static void expectFailure(int port, String message) throws Exception {
        try {
            PrinterConfig.download("127.0.0.1", port);
        } catch (IOException e) {
            if (message != null) {
                require(e.getMessage().contains(message), "Expected " + message + ", got " + e);
            }
            return;
        }
        throw new AssertionError("Expected failure: " + message);
    }

    private static void require(boolean ok, String message) {
        if (!ok) {
            throw new AssertionError(message);
        }
    }
}
