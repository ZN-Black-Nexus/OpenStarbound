package io.github.openstarbound.mobile;

import android.content.Context;
import android.os.Environment;
import android.os.StatFs;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;

// Chooses which app-specific storage volume (phone memory or SD card) holds
// assets/mods/saves. The choice is kept in <internal files dir>/datadir.txt,
// which the native game reads at startup (see StarAndroidSetup.cpp).
final class DataLocation {
    static final String[] DATA_DIRS = { "assets", "mods", "storage", "logs" };
    private static final String CHOICE_FILE = "datadir.txt";

    static final class Volume {
        final File dir;
        final String label;
        Volume(File dir, String label) { this.dir = dir; this.label = label; }
    }

    private DataLocation() {}

    static List<Volume> volumes(Context ctx) {
        List<Volume> result = new ArrayList<>();
        File[] dirs = ctx.getExternalFilesDirs(null);
        for (int i = 0; i < dirs.length; i++) {
            File d = dirs[i];
            if (d == null) continue;
            d.mkdirs();
            if (!d.isDirectory()) continue;
            boolean removable = i > 0 && Environment.isExternalStorageRemovable(d);
            String label = i == 0 ? "Phone storage" : (removable ? "SD card" : "Secondary storage");
            result.add(new Volume(d, label));
        }
        if (result.isEmpty()) result.add(new Volume(ctx.getFilesDir(), "Phone storage (internal)"));
        return result;
    }

    static File resolve(Context ctx) {
        File choice = new File(ctx.getFilesDir(), CHOICE_FILE);
        if (choice.isFile()) {
            try {
                String path = new String(Files.readAllBytes(choice.toPath()), StandardCharsets.UTF_8).trim();
                File dir = new File(path);
                for (Volume v : volumes(ctx))
                    if (v.dir.equals(dir)) return dir;
            } catch (IOException ignored) {
            }
        }
        return volumes(ctx).get(0).dir;
    }

    // Also called on every launch so the native side always has the file.
    static void save(Context ctx, File dir) throws IOException {
        try (FileOutputStream out = new FileOutputStream(new File(ctx.getFilesDir(), CHOICE_FILE))) {
            out.write(dir.getAbsolutePath().getBytes(StandardCharsets.UTF_8));
        }
    }

    static long size(File f) {
        File[] children = f.listFiles();
        if (children == null) return f.length();
        long total = 0;
        for (File c : children) total += size(c);
        return total;
    }

    static long freeSpace(File dir) {
        try {
            return new StatFs(dir.getAbsolutePath()).getAvailableBytes();
        } catch (Exception e) {
            return dir.getUsableSpace();
        }
    }

    interface Progress { void update(String text); }

    // Copies every data directory to dest, then removes the originals. The
    // originals are only deleted after all copies succeeded.
    static void move(File from, File to, Progress progress) throws IOException {
        if (from.equals(to)) return;
        long needed = 0;
        for (String name : DATA_DIRS) needed += size(new File(from, name));
        if (freeSpace(to) < needed + (64L << 20))
            throw new IOException("Not enough free space at the destination (" + (needed >> 20) + " MB needed)");
        long[] done = { 0 };
        byte[] buf = new byte[1 << 20];
        for (String name : DATA_DIRS) {
            File src = new File(from, name);
            if (src.exists()) copyTree(src, new File(to, name), buf, done, needed, progress);
        }
        for (String name : DATA_DIRS) deleteTree(new File(from, name));
    }

    private static void copyTree(File src, File dst, byte[] buf, long[] done, long total, Progress progress)
            throws IOException {
        if (src.isDirectory()) {
            dst.mkdirs();
            File[] children = src.listFiles();
            if (children != null)
                for (File c : children) copyTree(c, new File(dst, c.getName()), buf, done, total, progress);
            return;
        }
        File parent = dst.getParentFile();
        if (parent != null) parent.mkdirs();
        try (java.io.FileInputStream in = new java.io.FileInputStream(src);
             FileOutputStream out = new FileOutputStream(dst)) {
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
                done[0] += n;
            }
        }
        progress.update("Moving data: " + (done[0] >> 20) + " / " + (total >> 20) + " MB");
    }

    static void deleteTree(File f) {
        File[] children = f.listFiles();
        if (children != null) for (File c : children) deleteTree(c);
        f.delete();
    }
}
