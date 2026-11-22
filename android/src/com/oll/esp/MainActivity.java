package com.oll.esp;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.view.Gravity;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;

public final class MainActivity extends Activity {
    private EditText pkgField;
    private TextView status;
    private final Handler main = new Handler(Looper.getMainLooper());

    @Override
    protected void onCreate(Bundle bundle) {
        super.onCreate(bundle);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER_HORIZONTAL);
        root.setPadding(40, 80, 40, 40);

        TextView title = new TextView(this);
        title.setText("OLL ESP  —  rooted arm64");
        title.setTextSize(22f);
        title.setGravity(Gravity.CENTER);
        root.addView(title);

        status = new TextView(this);
        status.setText("installing assets...");
        status.setGravity(Gravity.CENTER);
        status.setPadding(0, 24, 0, 24);
        root.addView(status);

        pkgField = new EditText(this);
        pkgField.setHint("target package");
        pkgField.setText("com.tencent.ig");
        pkgField.setSingleLine(true);
        root.addView(pkgField);

        Button grant = new Button(this);
        grant.setText("1. Grant overlay permission");
        grant.setOnClickListener(new android.view.View.OnClickListener() {
            @Override public void onClick(android.view.View v) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                    startActivity(new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                            Uri.parse("package:" + getPackageName())));
                }
            }
        });
        root.addView(grant);

        Button discover = new Button(this);
        discover.setText("Auto-find offsets (game must be open)");
        discover.setOnClickListener(new android.view.View.OnClickListener() {
            @Override public void onClick(android.view.View v) {
                runDiscovery(pkgField.getText().toString().trim());
            }
        });
        root.addView(discover);

        Button start = new Button(this);
        start.setText("2. Start ESP");
        start.setOnClickListener(new android.view.View.OnClickListener() {
            @Override public void onClick(android.view.View v) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M
                        && !Settings.canDrawOverlays(MainActivity.this)) {
                    status.setText("grant overlay permission first");
                    return;
                }
                Intent i = new Intent(MainActivity.this, EspService.class);
                i.putExtra(EspService.EXTRA_PKG, pkgField.getText().toString().trim());
                startForegroundService(i);
                status.setText("running — switch to the game");
            }
        });
        root.addView(start);

        Button stop = new Button(this);
        stop.setText("3. Stop ESP");
        stop.setOnClickListener(new android.view.View.OnClickListener() {
            @Override public void onClick(android.view.View v) {
                stopService(new Intent(MainActivity.this, EspService.class));
                status.setText("stopped");
            }
        });
        root.addView(stop);

        setContentView(root);
        installAssets();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            status.setText(Settings.canDrawOverlays(this)
                    ? "overlay ready — fill config.txt on device, then start"
                    : "overlay permission missing — tap button 1");
        }
    }

    private void runDiscovery(final String pkg) {
        status.setText("scanning " + pkg + " ...");
        new Thread(new Runnable() {
            @Override public void run() {
                try {
                    File tool = new File(getFilesDir(), "espdiscover");
                    File cfg = new File(getFilesDir(), "config.txt");
                    String cmd = tool.getAbsolutePath() + " " + pkg + " "
                            + cfg.getAbsolutePath();
                    Process p = Runtime.getRuntime().exec(new String[]{"su", "-c", cmd});
                    StringBuilder log = new StringBuilder();
                    BufferedReader err = new BufferedReader(
                            new InputStreamReader(p.getErrorStream()));
                    String line;
                    while ((line = err.readLine()) != null) log.append(line).append("\n");
                    final int rc = p.waitFor();
                    final String out = "exit=" + rc + "\n" + log.toString();
                    main.post(new Runnable() {
                        @Override public void run() { status.setText(out); }
                    });
                } catch (final Exception e) {
                    main.post(new Runnable() {
                        @Override public void run() {
                            status.setText("discovery error: " + e.getMessage());
                        }
                    });
                }
            }
        }).start();
    }

    private void installAssets() {
        try {
            copy("esphelper", true);
            copy("espdiscover", true);
            copy("config.txt", false);
            File h = new File(getFilesDir(), "esphelper");
            File d = new File(getFilesDir(), "espdiscover");
            if (!h.setExecutable(true) || !d.setExecutable(true)) status.setText("chmod failed");
        } catch (Exception e) {
            status.setText("asset error: " + e.getMessage());
        }
    }

    private void copy(String name, boolean overwrite) throws Exception {
        File out = new File(getFilesDir(), name);
        if (out.exists() && !overwrite) return;
        try (InputStream in = getAssets().open(name);
             OutputStream os = new FileOutputStream(out)) {
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0) os.write(buf, 0, n);
        }
    }
}
