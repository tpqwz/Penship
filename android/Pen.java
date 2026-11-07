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
