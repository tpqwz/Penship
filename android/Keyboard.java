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
        View ime = Pen.editor(activity);
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
    }
}
