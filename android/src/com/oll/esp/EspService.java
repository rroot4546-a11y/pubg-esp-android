package com.oll.esp;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Intent;
import android.graphics.PixelFormat;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.view.WindowManager;
import android.util.DisplayMetrics;
import android.widget.Toast;

import java.io.BufferedReader;
import java.io.File;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.List;

public final class EspService extends Service {
    public static final String EXTRA_PKG = "pkg";
    private static final String CHANNEL = "oll_esp";

    private WindowManager wm;
    private OverlayView overlay;
    private Handler main;
    private Process proc;
    private Thread reader;
    private volatile boolean running;

    @Override
    public void onCreate() {
        super.onCreate();
        wm = (WindowManager) getSystemService(WINDOW_SERVICE);
        main = new Handler(Looper.getMainLooper());
        createChannel();

        overlay = new OverlayView(this);
        WindowManager.LayoutParams lp = new WindowManager.LayoutParams(
                WindowManager.LayoutParams.MATCH_PARENT,
                WindowManager.LayoutParams.MATCH_PARENT,
                WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                        | WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE
                        | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN
                        | WindowManager.LayoutParams.FLAG_FULLSCREEN,
                PixelFormat.TRANSPARENT);
        wm.addView(overlay, lp);
    }

    private void createChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
            if (nm != null && nm.getNotificationChannel(CHANNEL) == null) {
                nm.createNotificationChannel(new NotificationChannel(
                        CHANNEL, "OLL ESP", NotificationManager.IMPORTANCE_LOW));
            }
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String pkg = intent != null ? intent.getStringExtra(EXTRA_PKG) : null;
        if (pkg == null || pkg.trim().isEmpty()) pkg = "com.tencent.ig";

        Notification note;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            note = new Notification.Builder(this, CHANNEL)
                    .setContentTitle("OLL ESP")
                    .setContentText("Reading " + pkg)
                    .setSmallIcon(android.R.drawable.ic_menu_view)
                    .build();
        } else {
            note = new Notification.Builder(this)
                    .setContentTitle("OLL ESP")
                    .setContentText("Reading " + pkg)
                    .setSmallIcon(android.R.drawable.ic_menu_view)
                    .build();
        }
        startForeground(1, note);

        if (reader != null && reader.isAlive()) return START_STICKY;
        running = true;
        final String target = pkg;
        reader = new Thread(new Runnable() {
            @Override public void run() { runHelper(target); }
        }, "esp-reader");
        reader.start();
        return START_STICKY;
    }

    private void runHelper(String pkg) {
        try {
            File helper = new File(getFilesDir(), "esphelper");
            File config = new File(getFilesDir(), "config.txt");
            DisplayMetrics dm = new DisplayMetrics();
            wm.getDefaultDisplay().getRealMetrics(dm);

            String cmd = helper.getAbsolutePath() + " " + pkg + " "
                    + dm.widthPixels + " " + dm.heightPixels + " "
                    + config.getAbsolutePath();
            proc = Runtime.getRuntime().exec(new String[]{"su", "-c", cmd});

            BufferedReader out = new BufferedReader(new InputStreamReader(proc.getInputStream()));
            BufferedReader err = new BufferedReader(new InputStreamReader(proc.getErrorStream()));
            new Thread(new Runnable() {
                @Override public void run() {
                    try {
                        String l;
                        while ((l = err.readLine()) != null) post(l);
                    } catch (Exception ignored) { }
                }
            }).start();

            final List<Target> batch = new ArrayList<>();
            String line;
            while (running && (line = out.readLine()) != null) {
                final String s = line;
                if (s.startsWith("#") || s.startsWith("!")) { post(s); continue; }
                if (s.startsWith("T ")) {
                    Target t = parseTarget(s);
                    if (t != null) batch.add(t);
                } else if (s.equals("F")) {
                    final List<Target> copy = new ArrayList<>(batch);
                    batch.clear();
                    main.post(new Runnable() {
                        @Override public void run() { overlay.setTargets(copy); }
                    });
                }
            }
        } catch (Exception e) {
            post("! " + e.getMessage());
        }
    }

    private Target parseTarget(String s) {
        try {
            String[] p = s.trim().split("\\s+");
            if (p.length < 10) return null;
            float l = Float.parseFloat(p[1]), t = Float.parseFloat(p[2]);
            float r = Float.parseFloat(p[3]), b = Float.parseFloat(p[4]);
            boolean enemy = p[5].equals("1");
            int dist = Integer.parseInt(p[6]);
            float hx = Float.parseFloat(p[7]), hy = Float.parseFloat(p[8]);
            int n = Integer.parseInt(p[9]);
            if (n < 0 || n > 26) return null;
            float[] lines = new float[n * 4];
            if (p.length < 10 + n * 4) return null;
            for (int i = 0; i < n * 4; i++) lines[i] = Float.parseFloat(p[10 + i]);
            return new Target(l, t, r, b, enemy, dist, hx, hy, lines);
        } catch (Exception e) {
            return null;
        }
    }

    private void post(final String msg) {
        main.post(new Runnable() {
            @Override public void run() {
                Toast.makeText(getApplicationContext(), msg, Toast.LENGTH_SHORT).show();
            }
        });
    }

    @Override
    public void onDestroy() {
        running = false;
        if (proc != null) proc.destroy();
        if (overlay != null) wm.removeView(overlay);
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) { return null; }
}
