package com.oll.esp;

import android.content.Context;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;

final class Dbg {
    private static File logFile(Context c) {
        File dir = c.getExternalFilesDir(null);
        return new File(dir, "oll_esp.log");
    }

    static void log(Context c, String line) {
        try {
            File f = logFile(c);
            FileWriter w = new FileWriter(f, true);
            w.write(line + "\n");
            w.close();
        } catch (Exception ignored) { }
    }

    static String read(Context c) {
        try {
            File f = logFile(c);
            if (!f.exists()) return "no log yet — run auto-find or start ESP";
            StringBuilder b = new StringBuilder();
            BufferedReader r = new BufferedReader(new FileReader(f));
            String l;
            int n = 0;
            while ((l = r.readLine()) != null && n++ < 250) b.append(l).append('\n');
            r.close();
            return b.toString();
        } catch (Exception e) {
            return "log error: " + e.getMessage();
        }
    }
}
