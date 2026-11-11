package penship;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * NativeActivity plus the open-with document. Schemes are an allowlist:
 * content goes through the URI grant, and a file URI cannot overwrite the
 * app's private data directory.
 */
public class Pen extends android.app.NativeActivity {
    /** Same cap as DOC_MAX_BYTES. A longer stream is refused whole. */
    private static final int CAP = 1 << 22;

    /* NativeActivity dlopens the library. The VM only resolves these natives after loadLibrary. */
    static {
        System.loadLibrary("penship");
    }

    /** Document from the last successful read. Saves keep using it if a new intent arrives first. */
    private static Uri opened;

    /** The view or edit intent. A launcher intent must not replace it. */
    private static Intent documentIntent;

    private static final int REQ_OPEN = 1;
    private static final int REQ_SAVE = 2;
    private static final int REQ_EXPORT = 3;
    private static final int REQ_FONT = 4;
    private static final int FONT_MAX = 8 * 1024 * 1024;

    private static native void reload(int adopt);

    private static native void fontPicked();

    private static native void facesFetched(int ok);

    private static native void listsFetched(int ok);

    private static native void catalogFetched(int ok);

    /** Open is 0. Save-as is 1. Export is 2. A font file is 3. */
    public static void pick(Activity activity, int create) {
        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                int request = create == 0 ? REQ_OPEN : (create == 2 ? REQ_EXPORT : (create == 3 ? REQ_FONT : REQ_SAVE));
                Intent intent = new Intent(create == 1 || create == 2 ? Intent.ACTION_CREATE_DOCUMENT
                        : Intent.ACTION_OPEN_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                intent.setType(create == 2 ? "text/html" : (create == 1 ? "text/markdown" : "*/*"));
                intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                        | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
                if (create == 0) {
                    intent.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {"text/plain", "text/markdown", "text/*"});
                } else if (create == 3) {
                    intent.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {"font/ttf", "font/otf",
                            "application/x-font-ttf", "application/octet-stream"});
                } else {
                    intent.putExtra(Intent.EXTRA_TITLE, create == 2 ? "note.html" : "note.md");
                }
                try {
                    activity.startActivityForResult(intent, request);
                } catch (android.content.ActivityNotFoundException missing) {
                    if (create == 0 || create == 3) {
                        intent.setAction(Intent.ACTION_GET_CONTENT);
                        try {
                            activity.startActivityForResult(intent, request);
                        } catch (android.content.ActivityNotFoundException again) {
                            android.util.Log.e("penship", "no file picker", again);
                        }
                    } else {
                        android.util.Log.e("penship", "no file picker", missing);
                    }
                }
            }
        });
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request == REQ_EXPORT && result == RESULT_OK && data != null && data.getData() != null) {
            copyExport(data.getData());
            return;
        }
        if (request == REQ_FONT && result == RESULT_OK && data != null && data.getData() != null) {
            if (copyFont(data.getData())) {
                fontPicked();
            }
            return;
        }
        if ((request != REQ_OPEN && request != REQ_SAVE) || result != RESULT_OK || data == null) {
            return;
        }
        Uri uri = data.getData();
        if (uri == null) {
            return;
        }
        int flags = data.getFlags()
                & (Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        if (flags != 0) {
            try {
                getContentResolver().takePersistableUriPermission(uri, flags);
            } catch (SecurityException ignored) {
            }
        }
        Intent view = new Intent(Intent.ACTION_VIEW);
        view.setDataAndType(uri, "text/markdown");
        view.addFlags(flags);
        documentIntent = view;
        setIntent(view);
        if (request == REQ_SAVE) {
            opened = uri;
            reload(1);
        } else {
            opened = null;
            reload(0);
        }
    }

    /** Copy a chosen font into app storage. The note stays put. Returns false past 8 MiB or on a short read. */
    private boolean copyFont(Uri uri) {
        java.io.InputStream in = null;
        java.io.OutputStream out = null;
        java.io.File dest = new java.io.File(getFilesDir(), "custom.ttf");
        try {
            in = getContentResolver().openInputStream(uri);
            if (in == null) {
                return false;
            }
            out = new java.io.FileOutputStream(dest);
            byte[] buf = new byte[8192];
            int total = 0;
            int n;
            while ((n = in.read(buf)) > 0) {
                total += n;
                if (total > FONT_MAX) {
                    out.close();
                    out = null;
                    dest.delete();
                    return false;
                }
                out.write(buf, 0, n);
            }
            return total > 0;
        } catch (java.io.IOException ignored) {
            dest.delete();
            return false;
        } finally {
            if (in != null) {
                try {
                    in.close();
                } catch (java.io.IOException ignored) {
                }
            }
            if (out != null) {
                try {
                    out.close();
                } catch (java.io.IOException ignored) {
                }
            }
        }
    }

    /** Copy the prepared HTML page into the file the user picked. The note itself stays put. */
    private void copyExport(Uri uri) {
        java.io.InputStream in = null;
        java.io.OutputStream out = null;
        try {
            in = new java.io.FileInputStream(new java.io.File(getFilesDir(), "export.html"));
            out = getContentResolver().openOutputStream(uri);
            if (out == null) {
                return;
            }
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
            }
        } catch (java.io.IOException ignored) {
        } finally {
            if (in != null) {
                try {
                    in.close();
                } catch (java.io.IOException ignored) {
                }
            }
            if (out != null) {
                try {
                    out.close();
                } catch (java.io.IOException ignored) {
                }
            }
        }
    }

    private static final String FACE_PACK =
            "https://github.com/liberationfonts/liberation-fonts/files/7261482/liberation-fonts-ttf-2.1.5.tar.gz";
    private static final String[] FACE_FILE = {"LiberationSans-Regular.ttf", "LiberationSerif-Regular.ttf",
            "LiberationMono-Regular.ttf"};
    private static volatile boolean fetching;

    /** Download the three regular Liberation faces into app storage. The note stays put. */
    public static void fetchFaces(Activity activity) {
        if (fetching) {
            return;
        }
        fetching = true;
        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                final Activity host = activity;
                new Thread(new Runnable() {
                    @Override
                    public void run() {
                        boolean ok = downloadFaces(host);
                        fetching = false;
                        final int code = ok ? 1 : 0;
                        host.runOnUiThread(new Runnable() {
                            @Override
                            public void run() {
                                if (!host.isFinishing()) {
                                    facesFetched(code);
                                }
                            }
                        });
                    }
                }, "penship-faces").start();
            }
        });
    }

    private static boolean hostOk(String host) {
        return "github.com".equals(host) || "www.github.com".equals(host) || "objects.githubusercontent.com".equals(host);
    }

    private static boolean downloadFaces(Activity activity) {
        java.net.HttpURLConnection conn = null;
        java.io.InputStream in = null;
        try {
            java.net.URL url = new java.net.URL(FACE_PACK);
            for (int hop = 0; hop < 5; hop++) {
                if (!hostOk(url.getHost())) {
                    return false;
                }
                conn = (java.net.HttpURLConnection) url.openConnection();
                conn.setInstanceFollowRedirects(false);
                conn.setConnectTimeout(15000);
                conn.setReadTimeout(30000);
                int code = conn.getResponseCode();
                if (code >= 300 && code < 400) {
                    String loc = conn.getHeaderField("Location");
                    conn.disconnect();
                    conn = null;
                    if (loc == null) {
                        return false;
                    }
                    url = new java.net.URL(url, loc);
                    continue;
                }
                if (code != 200) {
                    return false;
                }
                in = conn.getInputStream();
                return extractFaces(in, activity.getFilesDir());
            }
            return false;
        } catch (java.io.IOException ignored) {
            return false;
        } finally {
            if (in != null) {
                try {
                    in.close();
                } catch (java.io.IOException ignored) {
                }
            }
            if (conn != null) {
                conn.disconnect();
            }
        }
    }

    private static int readFull(java.io.InputStream in, byte[] buf, int n) throws java.io.IOException {
        int got = 0;
        while (got < n) {
            int r = in.read(buf, got, n - got);
            if (r < 0) {
                return got;
            }
            got += r;
        }
        return got;
    }

    private static void skipFull(java.io.InputStream in, long n) throws java.io.IOException {
        byte[] buf = new byte[8192];
        while (n > 0) {
            int chunk = n > buf.length ? buf.length : (int) n;
            int r = in.read(buf, 0, chunk);
            if (r < 0) {
                return;
            }
            n -= r;
        }
    }

    private static long octal(byte[] header, int at, int n) {
        long value = 0;
        for (int i = at; i < at + n; i++) {
            byte c = header[i];
            if (c < '0' || c > '7') {
                break;
            }
            value = value * 8 + (c - '0');
        }
        return value;
    }

    private static boolean extractFaces(java.io.InputStream gzip, java.io.File dir) throws java.io.IOException {
        java.util.zip.GZIPInputStream in = new java.util.zip.GZIPInputStream(gzip);
        byte[] header = new byte[512];
        boolean[] got = new boolean[FACE_FILE.length];
        while (readFull(in, header, 512) == 512) {
            boolean empty = true;
            for (int i = 0; i < 512; i++) {
                if (header[i] != 0) {
                    empty = false;
                    break;
                }
            }
            if (empty) {
                break;
            }
            int nameEnd = 0;
            while (nameEnd < 100 && header[nameEnd] != 0) {
                nameEnd++;
            }
            String name = new String(header, 0, nameEnd, java.nio.charset.StandardCharsets.US_ASCII);
            int slash = name.lastIndexOf('/');
            String base = slash >= 0 ? name.substring(slash + 1) : name;
            long size = octal(header, 124, 12);
            int type = header[156] & 0xff;
            int which = -1;
            for (int i = 0; i < FACE_FILE.length; i++) {
                if (FACE_FILE[i].equals(base)) {
                    which = i;
                }
            }
            boolean regular = type == '0' || type == 0;
            if (regular && which >= 0 && size > 0 && size <= FONT_MAX) {
                java.io.File dest = new java.io.File(dir, FACE_FILE[which]);
                java.io.File part = new java.io.File(dir, FACE_FILE[which] + ".part");
                java.io.OutputStream out = new java.io.FileOutputStream(part);
                byte[] buf = new byte[8192];
                long left = size;
                boolean shortRead = false;
                while (left > 0) {
                    int chunk = left > buf.length ? buf.length : (int) left;
                    int r = in.read(buf, 0, chunk);
                    if (r < 0) {
                        shortRead = true;
                        break;
                    }
                    out.write(buf, 0, r);
                    left -= r;
                }
                out.close();
                if (!shortRead && part.renameTo(dest)) {
                    got[which] = true;
                } else {
                    part.delete();
                }
            } else if (size > 0) {
                skipFull(in, size);
            }
            long pad = (512 - (size % 512)) % 512;
            if (pad > 0) {
                skipFull(in, pad);
            }
        }
        return got[0] && got[1] && got[2];
    }

    /* Stylesheets on these hosts. rect and grove publish the same kind of list. */
    private static final String[] LIST_HOST = {"acimwb.grok.me", "tlnotes.grok.me", "hyperbase.grok.me", "rect.grok.me",
            "grove.grok.me"};
    private static volatile boolean listing;
    private static volatile boolean facing;

    /** Fetch each host's face list into files/fontlists.css. Names only; a tap downloads one TrueType. */
    public static void fetchLists(Activity activity) {
        if (listing) {
            return;
        }
        listing = true;
        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                final Activity host = activity;
                new Thread(new Runnable() {
                    @Override
                    public void run() {
                        boolean ok = downloadLists(host);
                        listing = false;
                        final int code = ok ? 1 : 0;
                        host.runOnUiThread(new Runnable() {
                            @Override
                            public void run() {
                                if (!host.isFinishing()) {
                                    listsFetched(code);
                                }
                            }
                        });
                    }
                }, "penship-lists").start();
            }
        });
    }

    /** Download one TrueType for a family named by a list. The sites ship WOFF2, which the painter cannot read. */
    public static void fetchFace(Activity activity, String name) {
        if (facing) {
            return;
        }
        if (!familyOk(name)) {
            activity.runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    if (!activity.isFinishing()) {
                        catalogFetched(0);
                    }
                }
            });
            return;
        }
        facing = true;
        final String family = name;
        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                final Activity host = activity;
                new Thread(new Runnable() {
                    @Override
                    public void run() {
                        boolean ok = downloadFace(host, family);
                        facing = false;
                        final int code = ok ? 1 : 0;
                        host.runOnUiThread(new Runnable() {
                            @Override
                            public void run() {
                                if (!host.isFinishing()) {
                                    catalogFetched(code);
                                }
                            }
                        });
                    }
                }, "penship-face").start();
            }
        });
    }

    private static boolean familyOk(String name) {
        int i = 0;
        if (name == null || name.length() < 1 || name.length() >= 64) {
            return false;
        }
        if (name.charAt(0) == ' ' || name.charAt(0) == '-') {
            return false;
        }
        if (name.charAt(name.length() - 1) == ' ' || name.charAt(name.length() - 1) == '-') {
            return false;
        }
        for (i = 0; i < name.length(); i++) {
            char c = name.charAt(i);
            if (c >= 'a' && c <= 'z') {
                continue;
            }
            if (c >= 'A' && c <= 'Z') {
                continue;
            }
            if (c >= '0' && c <= '9') {
                continue;
            }
            if (c == ' ' || c == '-') {
                continue;
            }
            return false;
        }
        return true;
    }

    private static byte[] httpsGet(String urlString, String host, int max) {
        java.net.HttpURLConnection conn = null;
        InputStream in = null;
        try {
            java.net.URL url = new java.net.URL(urlString);
            for (int hop = 0; hop < 5; hop++) {
                if (!"https".equals(url.getProtocol()) || url.getUserInfo() != null) {
                    return null;
                }
                int port = url.getPort();
                if ((port != -1 && port != 443) || host == null || !host.equals(url.getHost())) {
                    return null;
                }
                conn = (java.net.HttpURLConnection) url.openConnection();
                conn.setInstanceFollowRedirects(false);
                conn.setConnectTimeout(8000);
                conn.setReadTimeout(15000);
                conn.setRequestProperty("User-Agent", "penship/0.1");
                int code = conn.getResponseCode();
                if (code >= 300 && code < 400) {
                    String loc = conn.getHeaderField("Location");
                    conn.disconnect();
                    conn = null;
                    if (loc == null) {
                        return null;
                    }
                    url = new java.net.URL(url, loc);
                    continue;
                }
                if (code != 200) {
                    return null;
                }
                in = conn.getInputStream();
                return readCap(in, max);
            }
            return null;
        } catch (IOException ignored) {
            return null;
        } finally {
            if (in != null) {
                try {
                    in.close();
                } catch (IOException ignored) {
                }
            }
            if (conn != null) {
                conn.disconnect();
            }
        }
    }

    private static byte[] readCap(InputStream in, int max) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buf = new byte[8192];
        int total = 0;
        int n;
        while (total < max && (n = in.read(buf)) > 0) {
            int take = n;
            if (total > max - take) {
                take = max - total;
            }
            if (take > 0) {
                out.write(buf, 0, take);
            }
            total += n;
        }
        return out.toByteArray();
    }

    private static void appendFamilies(StringBuilder out, String href) {
        int p = 0;
        while (p >= 0 && p < href.length()) {
            int at = href.indexOf("family=", p);
            int end = 0;
            String raw;
            String name;
            if (at < 0) {
                break;
            }
            at += 7;
            end = at;
            while (end < href.length()) {
                char c = href.charAt(end);
                if (c == '&' || c == ':' || c == '#') {
                    break;
                }
                end++;
            }
            raw = href.substring(at, end).replace('+', ' ');
            name = raw.trim();
            if (familyOk(name)) {
                out.append("@font-face{font-family:\"").append(name).append("\";}\n");
            }
            p = end + 1;
        }
    }

    private static boolean downloadLists(Activity activity) {
        StringBuilder css = new StringBuilder();
        File dir = activity.getFilesDir();
        File part = new File(dir, "fontlists.css.part");
        File dest = new File(dir, "fontlists.css");
        int h = 0;
        for (h = 0; h < LIST_HOST.length && css.length() < 700000; h++) {
            String host = LIST_HOST[h];
            byte[] html = httpsGet("https://" + host + "/", host, 200000);
            String text;
            int from = 0;
            int sheets = 0;
            if (html == null || html.length == 0) {
                continue;
            }
            text = new String(html, java.nio.charset.StandardCharsets.UTF_8);
            css.append("/* penship-host: ").append(host, 0, host.indexOf('.')).append(" */\n");
            while (sheets < 4 && css.length() < 700000 && (from = text.indexOf("href=\"", from)) >= 0) {
                int start = from + 6;
                int end = text.indexOf('"', start);
                String href;
                String abs = null;
                if (end < 0) {
                    break;
                }
                href = text.substring(start, end).replace("&amp;", "&");
                from = end + 1;
                if (href.contains("fonts.googleapis.com/css")) {
                    appendFamilies(css, href);
                    continue;
                }
                if (!href.contains(".css")) {
                    continue;
                }
                if (href.startsWith("https://" + host + "/")) {
                    abs = href;
                } else if (href.startsWith("/") && !href.startsWith("//")) {
                    abs = "https://" + host + href;
                }
                if (abs == null || abs.indexOf(' ') >= 0) {
                    continue;
                }
                byte[] body = httpsGet(abs, host, 200000);
                sheets++;
                if (body == null || body.length == 0) {
                    continue;
                }
                css.append(new String(body, java.nio.charset.StandardCharsets.UTF_8));
                css.append('\n');
            }
        }
        if (css.length() == 0) {
            return false;
        }
        try {
            OutputStream raw = new FileOutputStream(part);
            raw.write(css.toString().getBytes(java.nio.charset.StandardCharsets.UTF_8));
            raw.close();
        } catch (IOException ignored) {
            part.delete();
            return false;
        }
        if (!part.renameTo(dest)) {
            part.delete();
            return false;
        }
        return true;
    }

    private static String cssUrl(String family) {
        StringBuilder q = new StringBuilder();
        int i = 0;
        for (i = 0; i < family.length(); i++) {
            char c = family.charAt(i);
            q.append(c == ' ' ? '+' : c);
        }
        return "https://fonts.googleapis.com/css2?family=" + q + ":wght@400&display=swap";
    }

    private static boolean ttfUrlOk(String url) {
        int i = 0;
        if (url == null || !url.startsWith("https://fonts.gstatic.com/") || !url.contains(".ttf") || url.length() > 400) {
            return false;
        }
        for (i = 0; i < url.length(); i++) {
            char c = url.charAt(i);
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
                continue;
            }
            if (c == '/' || c == ':' || c == '.' || c == '-' || c == '_' || c == '%') {
                continue;
            }
            return false;
        }
        return url.indexOf("..") < 0;
    }

    private static String urlBefore(String css, int fmt) {
        int url = css.lastIndexOf("url(", fmt);
        int start = 0;
        int end = 0;
        if (url < 0 || fmt - url > 500) {
            return null;
        }
        start = url + 4;
        end = start;
        while (end < css.length() && end < fmt) {
            char c = css.charAt(end);
            if (c == ')' || c == '\'' || c == '"' || c == ' ' || c == '\n') {
                break;
            }
            end++;
        }
        if (end <= start) {
            return null;
        }
        String found = css.substring(start, end);
        return ttfUrlOk(found) ? found : null;
    }

    private static String ttfFromCss(String css) {
        String fallback = null;
        int from = 0;
        if (css == null) {
            return null;
        }
        while (from < css.length()) {
            int fmt = css.indexOf("truetype", from);
            String url;
            if (fmt < 0) {
                break;
            }
            url = urlBefore(css, fmt);
            if (url != null) {
                int win = fmt - 240;
                String prev;
                if (win < 0) {
                    win = 0;
                }
                prev = css.substring(win, fmt);
                if (prev.contains("latin") || prev.contains("U+0000-00FF")) {
                    return url;
                }
                if (fallback == null) {
                    fallback = url;
                }
            }
            from = fmt + 8;
        }
        return fallback;
    }

    private static String trueTypeFor(String family) {
        byte[] css = httpsGet(cssUrl(family), "fonts.googleapis.com", 100000);
        String url = css == null ? null : ttfFromCss(new String(css, java.nio.charset.StandardCharsets.UTF_8));
        String shorter = null;
        if (url != null) {
            return url;
        }
        if (family.endsWith(" Variable") && family.length() > 9) {
            shorter = family.substring(0, family.length() - 9);
        } else if (family.endsWith(" Var") && family.length() > 4) {
            shorter = family.substring(0, family.length() - 4);
        }
        if (shorter == null || shorter.endsWith(" ") || !familyOk(shorter)) {
            return null;
        }
        css = httpsGet(cssUrl(shorter), "fonts.googleapis.com", 100000);
        return css == null ? null : ttfFromCss(new String(css, java.nio.charset.StandardCharsets.UTF_8));
    }

    private static boolean downloadFace(Activity activity, String family) {
        String url = trueTypeFor(family);
        byte[] body;
        File dir = activity.getFilesDir();
        File part = new File(dir, "catalog.ttf.part");
        File namePart = new File(dir, "catalog.name.part");
        if (url == null) {
            return false;
        }
        body = httpsGet(url, "fonts.gstatic.com", FONT_MAX);
        if (body == null || body.length < 12) {
            return false;
        }
        boolean ttf = (body[0] == 0 && body[1] == 1 && body[2] == 0 && body[3] == 0)
                || (body[0] == 't' && body[1] == 'r' && body[2] == 'u' && body[3] == 'e')
                || (body[0] == 'O' && body[1] == 'T' && body[2] == 'T' && body[3] == 'O');
        if (!ttf) {
            return false;
        }
        try {
            OutputStream raw = new FileOutputStream(part);
            raw.write(body);
            raw.close();
            OutputStream name = new FileOutputStream(namePart);
            name.write(family.getBytes(java.nio.charset.StandardCharsets.US_ASCII));
            name.write('\n');
            name.close();
        } catch (IOException ignored) {
            part.delete();
            namePart.delete();
            return false;
        }
        return true;
    }

    static final String IME_TAG = "penship-ime";

    static native void backPressed();

    private android.window.OnBackInvokedCallback backCallback;

    /** Stay in the note. Register again so this sits ahead of the keyboard. */
    void keepBack() {
        if (android.os.Build.VERSION.SDK_INT < 33) {
            return;
        }
        if (backCallback == null) {
            backCallback = new android.window.OnBackInvokedCallback() {
                @Override
                public void onBackInvoked() {
                    android.util.Log.i("penship", "back invoked");
                    backPressed();
                }
            };
        }
        android.window.OnBackInvokedDispatcher dispatcher = getOnBackInvokedDispatcher();
        dispatcher.unregisterOnBackInvokedCallback(backCallback);
        dispatcher.registerOnBackInvokedCallback(android.window.OnBackInvokedDispatcher.PRIORITY_OVERLAY,
                backCallback);
    }

    public static void claimBack(android.app.Activity activity) {
        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (activity instanceof Pen) {
                    ((Pen) activity).keepBack();
                }
            }
        });
    }

    @Override
    public void onBackPressed() {
        backPressed();
    }

    @Override
    public boolean dispatchKeyEvent(android.view.KeyEvent event) {
        if (event.getKeyCode() == android.view.KeyEvent.KEYCODE_BACK
                && event.getAction() == android.view.KeyEvent.ACTION_DOWN) {
            android.util.Log.i("penship", "back key");
            backPressed();
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    private boolean imeOpen() {
        if (android.os.Build.VERSION.SDK_INT < 30) {
            return false;
        }
        android.view.View decor = getWindow().getDecorView();
        android.view.WindowInsets insets = decor.getRootWindowInsets();
        return insets != null && insets.isVisible(android.view.WindowInsets.Type.ime());
    }

    @Override
    protected void onCreate(android.os.Bundle savedInstanceState) {
        /* Rewrite a share before NativeActivity starts, so the first read sees the file. */
        if (!adoptShare(getIntent()) && isDocument(getIntent())) {
            documentIntent = new Intent(getIntent());
        }
        super.onCreate(savedInstanceState);
        keepBack();
        android.view.View content = findViewById(android.R.id.content);
        if (!(content instanceof android.view.ViewGroup)) {
            return;
        }
        android.view.ViewGroup group = (android.view.ViewGroup) content;
        if (group.findViewWithTag(IME_TAG) != null) {
            return;
        }
        group.addView(new ImeView(this), new android.view.ViewGroup.LayoutParams(1, 1));
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        /* A restore often arrives as a bare component intent. Replacing the
           open document with that would drop the file on the next resume. */
        if (adoptShare(intent)) {
            return;
        }
        if (isDocument(intent)) {
            documentIntent = new Intent(intent);
            setIntent(intent);
        } else if (documentIntent != null) {
            setIntent(documentIntent);
        }
    }

    /** Turn a share-sheet intent into a view of one text file. Returns true when it did. */
    private boolean adoptShare(Intent intent) {
        if (intent == null) {
            return false;
        }
        String action = intent.getAction();
        if (!Intent.ACTION_SEND.equals(action) && !Intent.ACTION_SEND_MULTIPLE.equals(action)) {
            return false;
        }
        Uri uri = shareStream(intent);
        if (uri == null) {
            uri = shareText(intent);
        }
        if (uri == null) {
            return false;
        }
        Intent view = new Intent(Intent.ACTION_VIEW);
        String type = intent.getType();
        if (type == null || "*/*".equals(type)) {
            type = "text/plain";
        }
        view.setDataAndType(uri, type);
        int flags = intent.getFlags()
                & (Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        view.addFlags(flags);
        if ("content".equals(uri.getScheme()) && flags != 0) {
            try {
                getContentResolver().takePersistableUriPermission(uri, flags);
            } catch (SecurityException ignored) {
            }
        }
        documentIntent = view;
        setIntent(view);
        opened = null;
        return true;
    }

    @SuppressWarnings("deprecation")
    private static Uri shareStream(Intent intent) {
        Object one = intent.getParcelableExtra(Intent.EXTRA_STREAM);
        if (one instanceof Uri && openable((Uri) one)) {
            return (Uri) one;
        }
        Object many = intent.getParcelableArrayListExtra(Intent.EXTRA_STREAM);
        if (many instanceof java.util.ArrayList) {
            for (Object item : (java.util.ArrayList<?>) many) {
                if (item instanceof Uri && openable((Uri) item)) {
                    return (Uri) item;
                }
            }
        }
        android.content.ClipData clip = intent.getClipData();
        if (clip != null) {
            for (int i = 0; i < clip.getItemCount(); i++) {
                Uri uri = clip.getItemAt(i).getUri();
                if (openable(uri)) {
                    return uri;
                }
            }
        }
        return null;
    }

    private Uri shareText(Intent intent) {
        String text = intent.getStringExtra(Intent.EXTRA_TEXT);
        if (text == null) {
            android.content.ClipData clip = intent.getClipData();
            if (clip != null && clip.getItemCount() > 0 && clip.getItemAt(0).getText() != null) {
                text = clip.getItemAt(0).getText().toString();
            }
        }
        if (text == null) {
            return null;
        }
        java.io.File dir = getFilesDir();
        java.io.File[] old = dir.listFiles();
        if (old != null) {
            for (java.io.File file : old) {
                String name = file.getName();
                if (name.startsWith("shared-") && name.endsWith(".md")) {
                    file.delete();
                }
            }
        }
        java.io.File outFile = new java.io.File(dir, "shared-" + System.currentTimeMillis() + ".md");
        java.io.FileOutputStream out = null;
        try {
            out = new java.io.FileOutputStream(outFile);
            out.write(text.getBytes(java.nio.charset.StandardCharsets.UTF_8));
            out.close();
            out = null;
            return Uri.fromFile(outFile);
        } catch (java.io.IOException ignored) {
            if (out != null) {
                try {
                    out.close();
                } catch (java.io.IOException ignoredClose) {
                }
            }
            outFile.delete();
            return null;
        }
    }

    private static boolean openable(Uri uri) {
        if (uri == null) {
            return false;
        }
        String scheme = uri.getScheme();
        return "content".equals(scheme) || "file".equals(scheme);
    }

    private static boolean isDocument(Intent intent) {
        if (intent == null) {
            return false;
        }
        String action = intent.getAction();
        if (!Intent.ACTION_VIEW.equals(action) && !Intent.ACTION_EDIT.equals(action)) {
            return false;
        }
        Uri uri = intent.getData();
        if (uri == null && intent.getClipData() != null && intent.getClipData().getItemCount() > 0) {
            uri = intent.getClipData().getItemAt(0).getUri();
        }
        if (uri == null) {
            return false;
        }
        String scheme = uri.getScheme();
        return "content".equals(scheme) || "file".equals(scheme);
    }

    /** The focusable editor, or null when the content view is not up yet. */
    static android.view.View editor(Activity activity) {
        android.view.View content = activity.findViewById(android.R.id.content);
        if (content == null) {
            return null;
        }
        return content.findViewWithTag(IME_TAG);
    }

    static void commit(String text, int composing) {
        if (text == null) {
            text = "";
        }
        /* shortcut: longer keyboard commits are dropped. The native queue also rejects past 4096 UTF-8 bytes. */
        if (text.length() > 4096) {
            return;
        }
        byte[] utf8;
        try {
            utf8 = text.getBytes("UTF-8");
        } catch (java.io.UnsupportedEncodingException e) {
            return;
        }
        commitBytes(utf8, composing);
    }

    private static native void commitBytes(byte[] utf8, int composing);

    static native void erase(int count);

    static native void eraseForward(int count);

    static native void motion(int dir, int rows, int extend);

    /** Matches IME_SELECT, IME_COPY, IME_CUT, and IME_PASTE in src/ime.h. */
    static final int CLIP_SELECT = 9;
    static final int CLIP_COPY = 10;
    static final int CLIP_CUT = 11;
    static final int CLIP_PASTE = 12;

    static void setClipboard(Activity activity, byte[] utf8) {
        Clip.put(activity, utf8);
    }

    static byte[] getClipboard(Activity activity) {
        return Clip.get(activity);
    }

    static native void clip(int kind);

    static native void heading(int dir);

    static native void tableTab(int dir);

    static native void finishComposing();

    static native void undo();

    static native void shortcut(int keyCode);

    /** Identity of the opened document, or null when this launch is the scratch note. */
    public static String id(Activity activity) {
        Uri uri = document(activity);
        if (uri == null) {
            return null;
        }
        return uri.toString();
    }

    /** Bytes of the opened document. Null if this launch has none or the read is refused. */
    public static byte[] read(Activity activity) {
        Uri uri = document(activity);
        if (uri == null) {
            return null;
        }
        keep(activity, uri);
        InputStream in = null;
        try {
            in = activity.getContentResolver().openInputStream(uri);
            if (in == null) {
                return null;
            }
            byte[] got = drain(in);
            if (got != null) {
                opened = uri;
            }
            return got;
        } catch (Exception ignored) {
            return null;
        } finally {
            if (in != null) {
                try {
                    in.close();
                } catch (IOException ignored) {
                }
            }
        }
    }

    /**
     * Write bytes back to the opened document. Returns 0, or -1 when there is
     * nothing writable or the private data directory would be the target.
     */
    public static int write(Activity activity, byte[] bytes) {
        if (bytes == null || bytes.length > CAP) {
            return -1;
        }
        Uri uri = opened;
        if (uri == null) {
            return -1;
        }
        if ("file".equals(uri.getScheme())) {
            if (insidePrivate(activity, uri)) {
                return -1;
            }
            return writeFile(uri, bytes);
        }
        OutputStream out = null;
        try {
            out = activity.getContentResolver().openOutputStream(uri);
            if (out == null) {
                return -1;
            }
            out.write(bytes);
            return 0;
        } catch (Exception ignored) {
            return -1;
        } finally {
            if (out != null) {
                try {
                    out.close();
                } catch (IOException ignored) {
                }
            }
        }
    }

    /** Replace a file only after the new bytes are on disk. A failed write leaves the file. */
    private static int writeFile(Uri uri, byte[] bytes) {
        String path = uri.getPath();
        if (path == null) {
            return -1;
        }
        File tmp = new File(path + ".tmp");
        FileOutputStream out = null;
        try {
            out = new FileOutputStream(tmp);
            out.write(bytes);
            out.close();
            out = null;
            if (!tmp.renameTo(new File(path))) {
                tmp.delete();
                return -1;
            }
            return 0;
        } catch (Exception ignored) {
            if (out != null) {
                try {
                    out.close();
                } catch (IOException ignoredClose) {
                }
            }
            tmp.delete();
            return -1;
        }
    }

    private static Uri document(Activity activity) {
        Intent intent = activity.getIntent();
        if (intent == null) {
            return null;
        }
        String action = intent.getAction();
        if (!Intent.ACTION_VIEW.equals(action) && !Intent.ACTION_EDIT.equals(action)) {
            return null;
        }
        Uri uri = intent.getData();
        if (uri == null && intent.getClipData() != null && intent.getClipData().getItemCount() > 0) {
            uri = intent.getClipData().getItemAt(0).getUri();
        }
        if (uri == null) {
            return null;
        }
        String scheme = uri.getScheme();
        if (!"content".equals(scheme) && !"file".equals(scheme)) {
            return null;
        }
        return uri;
    }

    private static void keep(Activity activity, Uri uri) {
        if (!"content".equals(uri.getScheme())) {
            return;
        }
        int flags = activity.getIntent().getFlags()
                & (Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        if (flags == 0) {
            return;
        }
        try {
            activity.getContentResolver().takePersistableUriPermission(uri, flags);
        } catch (SecurityException ignored) {
        }
    }

    private static byte[] drain(InputStream in) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buf = new byte[8192];
        int total = 0;
        while (true) {
            int n = in.read(buf);
            if (n < 0) {
                break;
            }
            if (n == 0) {
                continue;
            }
            if (total > CAP - n) {
                return null;
            }
            out.write(buf, 0, n);
            total += n;
        }
        return out.toByteArray();
    }

    /** True when a file URI resolves inside the app's private data directory. */
    private static boolean insidePrivate(Activity activity, Uri uri) {
        String path = uri.getPath();
        if (path == null) {
            return true;
        }
        try {
            String real = new File(path).getCanonicalPath();
            String data = activity.getDataDir().getCanonicalPath();
            return real.equals(data) || real.startsWith(data + "/");
        } catch (IOException ignored) {
            return true;
        }
    }
}

/**
 * One-pixel focus target. NativeActivity's surface is not a text editor, so the
 * keyboard has nothing to commit into unless this view is focused.
 */
class ImeView extends android.view.View {
    ImeView(android.content.Context context) {
        super(context);
        setTag(Pen.IME_TAG);
        setFocusable(true);
        setFocusableInTouchMode(true);
    }

    @Override
    public boolean onCheckIsTextEditor() {
        return true;
    }

    /* Arrows move the caret. A software keyboard that fails to move it also sends a newline,
       which would push the text after the caret down. PenInput drops that newline. */
    static boolean claimKey(android.view.KeyEvent event) {
        int keyCode = event.getKeyCode();
        if (isArrow(keyCode)) {
            if (event.getAction() != android.view.KeyEvent.ACTION_DOWN) {
                return true;
            }
            if (event.isCtrlPressed() && !event.isShiftPressed() && isVertical(keyCode)) {
                Pen.heading(isDown(keyCode) ? 1 : -1);
                return true;
            }
            if (!event.isCtrlPressed()) {
                int dir = isDown(keyCode) || keyCode == android.view.KeyEvent.KEYCODE_DPAD_RIGHT
                        || keyCode == android.view.KeyEvent.KEYCODE_SYSTEM_NAVIGATION_RIGHT ? 1 : -1;
                int rows = isVertical(keyCode) ? 1 : 0;
                Pen.motion(dir, rows, event.isShiftPressed() ? 1 : 0);
                return true;
            }
            return false;
        }
        if (event.getAction() != android.view.KeyEvent.ACTION_DOWN) {
            return false;
        }
        if (keyCode == android.view.KeyEvent.KEYCODE_TAB) {
            Pen.tableTab(event.isShiftPressed() ? -1 : 1);
            return true;
        }
        if (!event.isCtrlPressed() && keyCode == android.view.KeyEvent.KEYCODE_FORWARD_DEL) {
            Pen.eraseForward(1);
            return true;
        }
        if (!event.isCtrlPressed()) {
            return false;
        }
        if (event.getRepeatCount() != 0) {
            return false;
        }
        if (!event.isShiftPressed()) {
            if (keyCode == android.view.KeyEvent.KEYCODE_A) {
                Pen.clip(Pen.CLIP_SELECT);
                return true;
            }
            if (keyCode == android.view.KeyEvent.KEYCODE_C) {
                Pen.clip(Pen.CLIP_COPY);
                return true;
            }
            if (keyCode == android.view.KeyEvent.KEYCODE_X) {
                Pen.clip(Pen.CLIP_CUT);
                return true;
            }
            if (keyCode == android.view.KeyEvent.KEYCODE_V) {
                Pen.clip(Pen.CLIP_PASTE);
                return true;
            }
        }
        if (!event.isShiftPressed() && keyCode == android.view.KeyEvent.KEYCODE_Z) {
            android.util.Log.i("penship", "ime undo");
            Pen.undo();
            return true;
        }
        if (event.isShiftPressed() && (keyCode == android.view.KeyEvent.KEYCODE_T
                || keyCode == android.view.KeyEvent.KEYCODE_F
                || keyCode == android.view.KeyEvent.KEYCODE_P
                || keyCode == android.view.KeyEvent.KEYCODE_S)) {
            android.util.Log.i("penship", "ime shortcut " + keyCode);
            Pen.shortcut(keyCode);
            return true;
        }
        return false;
    }

    private static boolean isArrow(int keyCode) {
        return keyCode == android.view.KeyEvent.KEYCODE_DPAD_LEFT
                || keyCode == android.view.KeyEvent.KEYCODE_DPAD_RIGHT
                || keyCode == android.view.KeyEvent.KEYCODE_DPAD_UP
                || keyCode == android.view.KeyEvent.KEYCODE_DPAD_DOWN
                || keyCode == android.view.KeyEvent.KEYCODE_SYSTEM_NAVIGATION_LEFT
                || keyCode == android.view.KeyEvent.KEYCODE_SYSTEM_NAVIGATION_RIGHT
                || keyCode == android.view.KeyEvent.KEYCODE_SYSTEM_NAVIGATION_UP
                || keyCode == android.view.KeyEvent.KEYCODE_SYSTEM_NAVIGATION_DOWN;
    }

    private static boolean isVertical(int keyCode) {
        return keyCode == android.view.KeyEvent.KEYCODE_DPAD_UP
                || keyCode == android.view.KeyEvent.KEYCODE_DPAD_DOWN
                || keyCode == android.view.KeyEvent.KEYCODE_SYSTEM_NAVIGATION_UP
                || keyCode == android.view.KeyEvent.KEYCODE_SYSTEM_NAVIGATION_DOWN;
    }

    private static boolean isDown(int keyCode) {
        return keyCode == android.view.KeyEvent.KEYCODE_DPAD_DOWN
                || keyCode == android.view.KeyEvent.KEYCODE_SYSTEM_NAVIGATION_DOWN;
    }

    @Override
    public boolean onKeyPreIme(int keyCode, android.view.KeyEvent event) {
        if (keyCode == android.view.KeyEvent.KEYCODE_BACK
                && event.getAction() == android.view.KeyEvent.ACTION_DOWN) {
            Pen.backPressed();
            return true;
        }
        /* Gboard takes hardware keys once this view is the editor. Claim the editing keys here. */
        return claimKey(event);
    }

    @Override
    public boolean onKeyDown(int keyCode, android.view.KeyEvent event) {
        if (keyCode == android.view.KeyEvent.KEYCODE_ENTER && !event.isCtrlPressed()) {
            PenInput.newline();
            post(() -> PenInput.newlineSent = false);
            return true;
        }
        if (claimKey(event)) {
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    @Override
    public android.view.inputmethod.InputConnection onCreateInputConnection(android.view.inputmethod.EditorInfo outAttrs) {
        outAttrs.inputType = android.text.InputType.TYPE_CLASS_TEXT
                | android.text.InputType.TYPE_TEXT_FLAG_MULTI_LINE;
        /* NO_ENTER_ACTION: Enter inserts a newline. ACTION_NONE alone makes Gboard swallow it. */
        outAttrs.imeOptions = android.view.inputmethod.EditorInfo.IME_FLAG_NO_EXTRACT_UI
                | android.view.inputmethod.EditorInfo.IME_FLAG_NO_FULLSCREEN
                | android.view.inputmethod.EditorInfo.IME_FLAG_NO_ENTER_ACTION;
        return new PenInput(this);
    }
}

/** Keyboard commits land here and are queued for the native thread. */
class PenInput extends android.view.inputmethod.BaseInputConnection {
    private final android.view.View view;
    /* Set when Down was just handled. Cleared on the next loop turn, so a fallback newline
       from that same key is dropped and a later Enter still starts a line. */
    static boolean dropNewline;
    /* Enter and a same-turn commit of "\\n" are one key. The flag drops the second. */
    static boolean newlineSent;

    PenInput(android.view.View view) {
        super(view, true);
        this.view = view;
    }

    static void newline() {
        if (newlineSent) {
            return;
        }
        newlineSent = true;
        Pen.commit("\n", 0);
    }

    private void clearNewlineSoon() {
        view.post(() -> newlineSent = false);
    }

    @Override
    public boolean sendKeyEvent(android.view.KeyEvent event) {
        boolean enter = event.getKeyCode() == android.view.KeyEvent.KEYCODE_ENTER && !event.isCtrlPressed();
        if (enter && event.getAction() == android.view.KeyEvent.ACTION_DOWN) {
            newline();
            clearNewlineSoon();
            return true;
        }
        if (enter) {
            return true;
        }
        boolean down = event.getAction() == android.view.KeyEvent.ACTION_DOWN
                && (event.getKeyCode() == android.view.KeyEvent.KEYCODE_DPAD_DOWN
                        || event.getKeyCode() == android.view.KeyEvent.KEYCODE_SYSTEM_NAVIGATION_DOWN);
        if (ImeView.claimKey(event)) {
            if (down) {
                dropNewline = true;
                view.post(() -> dropNewline = false);
            }
            return true;
        }
        return super.sendKeyEvent(event);
    }

    @Override
    public boolean performEditorAction(int editorAction) {
        newline();
        clearNewlineSoon();
        return true;
    }

    @Override
    public boolean commitText(CharSequence text, int newCursorPosition) {
        String s = text == null ? "" : text.toString();
        if (dropNewline && "\n".equals(s)) {
            dropNewline = false;
            android.util.Log.i("penship", "drop newline from down arrow");
            return true;
        }
        dropNewline = false;
        if ("\n".equals(s)) {
            newline();
            clearNewlineSoon();
            return true;
        }
        newlineSent = false;
        Pen.commit(s, 0);
        return true;
    }

    @Override
    public boolean setComposingText(CharSequence text, int newCursorPosition) {
        Pen.commit(text == null ? "" : text.toString(), 1);
        return true;
    }

    @Override
    public boolean finishComposingText() {
        Pen.finishComposing();
        return super.finishComposingText();
    }

    @Override
    public boolean deleteSurroundingText(int beforeLength, int afterLength) {
        /* shortcut: the count is UTF-16 units and is applied as code points. */
        eraseAround(beforeLength, afterLength);
        return true;
    }

    @Override
    public boolean deleteSurroundingTextInCodePoints(int beforeLength, int afterLength) {
        eraseAround(beforeLength, afterLength);
        return true;
    }

    private static void eraseAround(int beforeLength, int afterLength) {
        if (beforeLength > 0) {
            Pen.erase(beforeLength);
        }
        if (afterLength > 0) {
            Pen.eraseForward(afterLength);
        }
    }
}

/** Clipboard hops. A clip longer than 1 MiB is ignored. */
final class Clip implements Runnable {
    private static final int PUT = 1;
    private static final int GET = 2;
    private static final int CAP = 1 << 20;

    private final Activity activity;
    private final int mode;
    private final byte[] in;
    private final byte[][] out;
    private final java.util.concurrent.CountDownLatch latch;

    private Clip(Activity activity, int mode, byte[] in, byte[][] out, java.util.concurrent.CountDownLatch latch) {
        this.activity = activity;
        this.mode = mode;
        this.in = in;
        this.out = out;
        this.latch = latch;
    }

    static void put(Activity activity, byte[] utf8) {
        if (activity == null || utf8 == null || utf8.length == 0 || utf8.length > CAP) {
            return;
        }
        java.util.concurrent.CountDownLatch latch = new java.util.concurrent.CountDownLatch(1);
        activity.runOnUiThread(new Clip(activity, PUT, utf8, null, latch));
        await(latch);
    }

    static byte[] get(Activity activity) {
        if (activity == null) {
            return null;
        }
        byte[][] out = new byte[1][];
        java.util.concurrent.CountDownLatch latch = new java.util.concurrent.CountDownLatch(1);
        activity.runOnUiThread(new Clip(activity, GET, null, out, latch));
        await(latch);
        return out[0];
    }

    private static void await(java.util.concurrent.CountDownLatch latch) {
        try {
            latch.await(200, java.util.concurrent.TimeUnit.MILLISECONDS);
        } catch (InterruptedException ignored) {
            Thread.currentThread().interrupt();
        }
    }

    @Override
    public void run() {
        try {
            android.content.ClipboardManager clips =
                    (android.content.ClipboardManager) activity.getSystemService(Activity.CLIPBOARD_SERVICE);
            if (clips == null) {
                return;
            }
            if (mode == PUT) {
                String text = new String(in, "UTF-8");
                clips.setPrimaryClip(android.content.ClipData.newPlainText("penship", text));
                return;
            }
            if (!clips.hasPrimaryClip()) {
                return;
            }
            android.content.ClipData data = clips.getPrimaryClip();
            if (data == null || data.getItemCount() < 1) {
                return;
            }
            CharSequence raw = data.getItemAt(0).coerceToText(activity);
            if (raw == null) {
                return;
            }
            String text = raw.toString();
            if (text.length() == 0 || text.length() > CAP) {
                return;
            }
            byte[] utf8 = text.getBytes("UTF-8");
            if (utf8.length <= CAP) {
                out[0] = utf8;
            }
        } catch (Exception ignored) {
        } finally {
            latch.countDown();
        }
    }
}
