package io.github.pellcorp.grumpyscreen;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URI;
import java.net.URISyntaxException;
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Map;

/** Downloads the selected printer's optional override. Never reuses a local copy. */
final class PrinterConfig {
    private static final int MAX_BYTES = 256 * 1024;

    private PrinterConfig() {}

    /** Call off the UI thread. A 404 means this printer uses the bundled defaults. */
    static byte[] download(String host, int port) throws IOException {
        HttpURLConnection connection;
        try {
            URI uri = new URI("http", null, host, port,
                    "/server/files/config/grumpyscreen.ini", "date=" + System.currentTimeMillis(), null);
            connection = (HttpURLConnection) uri.toURL().openConnection();
        } catch (URISyntaxException e) {
            throw new IOException("Invalid printer address", e);
        }
        connection.setConnectTimeout(3000);
        connection.setReadTimeout(5000);
        connection.setUseCaches(false);
        connection.setInstanceFollowRedirects(false);
        connection.setRequestProperty("Cache-Control", "no-cache");
        try {
            int status = connection.getResponseCode();
            if (status == HttpURLConnection.HTTP_NOT_FOUND) return null;
            if (status != HttpURLConnection.HTTP_OK) {
                throw new IOException("Moonraker returned HTTP " + status);
            }
            long length = connection.getContentLengthLong();
            if (length > MAX_BYTES) throw new IOException("grumpyscreen.ini exceeds 256 KiB");
            ByteArrayOutputStream body = new ByteArrayOutputStream();
            try (InputStream in = connection.getInputStream()) {
                byte[] buffer = new byte[8192];
                int count;
                while ((count = in.read(buffer)) != -1) {
                    if (body.size() + count > MAX_BYTES) {
                        throw new IOException("grumpyscreen.ini exceeds 256 KiB");
                    }
                    body.write(buffer, 0, count);
                }
            }
            if (length >= 0 && body.size() != length) {
                throw new IOException("Incomplete grumpyscreen.ini download");
            }
            byte[] bytes = body.toByteArray();
            validate(bytes);
            return bytes;
        } finally {
            connection.disconnect();
        }
    }

    // Catch error pages and malformed INI before handing it to the native
    // parser. In particular, mixing [section] and [section "id"] would give
    // the native JSON representation conflicting object/array types.
    private static void validate(byte[] bytes) throws IOException {
        final String content;
        try {
            content = StandardCharsets.UTF_8.newDecoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .decode(ByteBuffer.wrap(bytes)).toString();
        } catch (CharacterCodingException e) {
            throw new IOException("grumpyscreen.ini is not UTF-8", e);
        }
        Map<String, Boolean> sections = new HashMap<>();
        boolean inSection = false;
        int lineNumber = 0;
        for (String line : content.split("\n", -1)) {
            lineNumber++;
            int comment = line.indexOf('#');
            int semicolon = line.indexOf(';');
            if (semicolon >= 0 && (comment < 0 || semicolon < comment)) comment = semicolon;
            String text = (comment < 0 ? line : line.substring(0, comment)).trim();
            if (text.isEmpty()) continue;
            if (text.startsWith("[") && text.endsWith("]")) {
                String section = text.substring(1, text.length() - 1).trim();
                int space = firstSeparator(section, ' ', '\t');
                boolean grouped = space >= 0;
                String name = grouped ? section.substring(0, space) : section;
                String id = grouped ? section.substring(space + 1).trim() : "";
                Boolean previous = sections.put(name, grouped);
                if (name.isEmpty() || (grouped && (id.isEmpty() || id.equals("\"\"") || id.equals("''")))
                        || (previous != null && previous != grouped)) {
                    throw invalidLine(lineNumber);
                }
                inSection = true;
            } else {
                int separator = firstSeparator(text, ':', '=');
                if (!inSection || text.startsWith("[") || separator <= 0 || text.indexOf('\0') >= 0) {
                    throw invalidLine(lineNumber);
                }
            }
        }
    }

    private static int firstSeparator(String value, char a, char b) {
        int first = value.indexOf(a), second = value.indexOf(b);
        return first < 0 ? second : second < 0 ? first : Math.min(first, second);
    }

    private static IOException invalidLine(int line) {
        return new IOException("Invalid grumpyscreen.ini at line " + line);
    }
}
