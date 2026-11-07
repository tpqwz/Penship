package penship;

import android.app.Activity;
import android.util.Log;
import android.view.View;
import android.view.WindowInsets;
import android.view.inputmethod.InputMethodManager;

import java.util.concurrent.CountDownLatch;

/** Runs window work on the activity's main thread. */
public final class Keyboard implements Runnable {
    private static final int SHOW = 1;
    private static final int READ = 2;
    private static final int SHOW_AGAIN = 3;
    private static final int HIDE = 4;
    private static final int WAKE = 5;
    private static final int CLAIM = 6;

    private final Activity activity;
    private final int mode;
    private final int[] box;
    private final CountDownLatch latch;

    private Keyboard(Activity activity, int mode, int[] box, CountDownLatch latch) {
        this.activity = activity;
        this.mode = mode;
        this.box = box;
        this.latch = latch;
    }

    public static void show(Activity activity) {
        activity.runOnUiThread(new Keyboard(activity, SHOW, null, null));
    }

    public static void hide(Activity activity) {
        Keyboard job = new Keyboard(activity, HIDE, null, null);
        if (android.os.Looper.myLooper() == activity.getMainLooper()) {
            job.run();
        } else {
            activity.runOnUiThread(job);
        }
    }

    /** System font scale times 1000. 1300 is the 1.3 setting. */
    public static int fontScaleMilli(Activity activity) {
        float scale = activity.getResources().getConfiguration().fontScale;
        if (scale < 0.5f || scale > 3f) {
            return 1000;
        }
        return Math.round(scale * 1000f);
    }

    static native void wake();

    /** Returns {system-bar-and-keyboard top, system-bar-and-keyboard bottom}. */
    public static int[] insets(Activity activity) {
        int[] box = new int[] {0, 0};
        CountDownLatch latch = new CountDownLatch(1);
        activity.runOnUiThread(new Keyboard(activity, READ, box, latch));
        try {
            latch.await(200, java.util.concurrent.TimeUnit.MILLISECONDS);
        } catch (InterruptedException ignored) {
            Thread.currentThread().interrupt();
        }
        return box;
    }

    @Override
    public void run() {
        if (mode == READ) {
            View decor = activity.getWindow().getDecorView();
            WindowInsets insets = decor.getRootWindowInsets();
            if (insets != null) {
                if (android.os.Build.VERSION.SDK_INT >= 30) {
                    android.graphics.Insets bars = insets.getInsets(
                            WindowInsets.Type.systemBars() | WindowInsets.Type.ime());
                    box[0] = bars.top;
                    box[1] = bars.bottom;
                } else {
                    box[0] = insets.getSystemWindowInsetTop();
                    box[1] = insets.getSystemWindowInsetBottom();
                }
            }
            latch.countDown();
            return;
        }
        if (mode == WAKE) {
            wake();
            return;
        }
        if (mode == CLAIM) {
            if (activity instanceof Pen) {
                ((Pen) activity).keepBack();
            }
            return;
        }
        View ime = Pen.editor(activity);
        if (mode == HIDE) {
            /* 16 is ADJUST_RESIZE, 2 is STATE_HIDDEN. Leave focus alone so a focus
               gain does not open the keyboard again. */
            activity.getWindow().setSoftInputMode(16 | 2);
            android.view.View decor = activity.getWindow().getDecorView();
            if (android.os.Build.VERSION.SDK_INT >= 30) {
                android.view.WindowInsetsController controller = decor.getWindowInsetsController();
                if (controller != null) {
                    controller.hide(android.view.WindowInsets.Type.ime());
                }
            }
            InputMethodManager imm =
                    (InputMethodManager) activity.getSystemService(Activity.INPUT_METHOD_SERVICE);
            if (imm != null) {
                imm.hideSoftInputFromWindow(decor.getWindowToken(), 0);
                if (ime != null && ime.getWindowToken() != null) {
                    imm.hideSoftInputFromWindow(ime.getWindowToken(), 0);
                }
            }
            wakeSoon();
            return;
        }
        if (ime == null) {
            Log.i("penship", "ime view missing");
            return;
        }
        InputMethodManager imm =
                (InputMethodManager) activity.getSystemService(Activity.INPUT_METHOD_SERVICE);
        if (imm == null) {
            Log.i("penship", "ime manager missing");
            return;
        }
        if (mode == SHOW_AGAIN) {
            Log.i("penship", "ime shown=" + imm.showSoftInput(ime, InputMethodManager.SHOW_IMPLICIT));
            wakeSoon();
            return;
        }
        /* SOFT_INPUT_ADJUST_RESIZE | SOFT_INPUT_STATE_VISIBLE */
        activity.getWindow().setSoftInputMode(16 | 4);
        ime.requestFocus();
        imm.restartInput(ime);
        boolean shown = imm.showSoftInput(ime, InputMethodManager.SHOW_IMPLICIT);
        Log.i("penship", "ime focus=" + ime.isFocused() + " shown=" + shown);
        if (!shown) {
            ime.post(new Keyboard(activity, SHOW_AGAIN, null, null));
        }
        if (activity instanceof Pen) {
            ((Pen) activity).keepBack();
        }
        wakeSoon();
    }

    /** The keyboard inset arrives after show or hide. Redraw once it has settled.
        Claim back again after the keyboard has registered, so Back dismisses it. */
    private void wakeSoon() {
        android.view.View decor = activity.getWindow().getDecorView();
        decor.postDelayed(new Keyboard(activity, WAKE, null, null), 80);
        decor.postDelayed(new Keyboard(activity, WAKE, null, null), 320);
        decor.postDelayed(new Keyboard(activity, CLAIM, null, null), 80);
        decor.postDelayed(new Keyboard(activity, CLAIM, null, null), 320);
        decor.postDelayed(new Keyboard(activity, CLAIM, null, null), 700);
    }
}
