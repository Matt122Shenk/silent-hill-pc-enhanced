package com.silenthill.port;

import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.AssetManager;
import android.content.pm.PackageManager;
import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.zip.CRC32;

/**
 * The port's own files (fonts, language packs, title art, UI sounds), copied
 * out of the APK into the data root, where native code reads them.
 *
 * They used to be written only when missing, so an app update never reached a
 * player who already had them. The new code then read the old pl.lang and old
 * font art, and Polish letters drew as the wrong glyphs.
 *
 * The manifest records, per file, the bundled version last installed. A disk
 * copy that differs from the APK is then one of two things:
 *  - it matches the manifest: the bundle has not changed since, so the player
 *    edited it, and it is left alone;
 *  - it does not: the APK carries a newer version, and the player is asked.
 * An install from before the manifest has no records, so everything that
 * differs counts as out of date, which is right for exactly that upgrade.
 */
final class BundledAssets {

    private static final String TAG = "SilentHill";

    private static final String[] TREES = { "gamedata", "licenses" };
    private static final String MANIFEST = "gamedata/.bundled_assets";
    /** Outside gamedata/, so no loader ever scans a backed-up pack. */
    static final String BACKUP_DIR = "replaced_files";

    private static final String PREFS        = "bundled_assets";
    private static final String KEY_MODE     = "update_mode";
    private static final String KEY_SETTLED  = "settled";

    static final int MODE_ASK    = 0;
    static final int MODE_ALWAYS = 1;
    static final int MODE_NEVER  = 2;

    private BundledAssets() { }

    static int mode(Context ctx) {
        return prefs(ctx).getInt(KEY_MODE, MODE_ASK);
    }

    static void setMode(Context ctx, int mode) {
        prefs(ctx).edit().putInt(KEY_MODE, mode).apply();
    }

    /**
     * Writes whatever is missing, and returns the files whose bundled version is
     * newer than the copy on disk. Each one is a question for the player; none
     * of them has been touched.
     *
     * The full comparison reads every bundled file and its disk copy, so it runs
     * once per app version and data root. Once nothing is left to ask about,
     * later launches only fill in missing files.
     */
    static List<String> stage(Context ctx, File root) {
        List<String> outdated = new ArrayList<String>();
        AssetManager am = ctx.getAssets();

        if (stamp(ctx, root).equals(prefs(ctx).getString(KEY_SETTLED, ""))) {
            copyMissing(ctx, root);
            return outdated;
        }

        Map<String, Long> manifest = readManifest(root);
        List<String> files = new ArrayList<String>();
        for (String tree : TREES) {
            listTree(am, tree, files);
        }

        for (String path : files) {
            File dest = new File(root, path);
            try {
                long bundled = crcOfAsset(am, path);

                if (!dest.isFile()) {
                    copyAsset(am, path, dest);
                    manifest.put(path, bundled);
                    continue;
                }
                long disk = crcOfFile(dest);
                if (disk == bundled) {
                    manifest.put(path, bundled);
                    continue;
                }
                Long installed = manifest.get(path);
                if (installed == null || installed.longValue() != bundled) {
                    outdated.add(path);
                }
            } catch (IOException e) {
                Log.e(TAG, "bundled asset " + path + ": " + e.getMessage());
            }
        }

        writeManifest(root, manifest);
        if (outdated.isEmpty()) {
            settle(ctx, root);
        }
        return outdated;
    }

    /** Replaces each file with the APK's copy. The old one goes to replaced_files/. */
    static void update(Context ctx, File root, List<String> paths) {
        AssetManager am = ctx.getAssets();
        Map<String, Long> manifest = readManifest(root);

        for (String path : paths) {
            File dest = new File(root, path);
            try {
                if (dest.isFile()) {
                    File bak = new File(new File(root, BACKUP_DIR), path);
                    File parent = bak.getParentFile();
                    if (parent != null) parent.mkdirs();
                    if (bak.exists()) bak.delete();
                    if (!dest.renameTo(bak)) {
                        Log.w(TAG, "could not keep a copy of " + path);
                    }
                }
                copyAsset(am, path, dest);
                manifest.put(path, crcOfAsset(am, path));
                Log.i(TAG, "updated " + path);
            } catch (IOException e) {
                Log.e(TAG, "updating " + path + ": " + e.getMessage());
            }
        }
        writeManifest(root, manifest);
        settle(ctx, root);
    }

    /**
     * Keeps the player's copies for good: they are recorded as installed, so
     * they read as the player's own edits from now on. A later app version
     * that changes them again asks again (unless the mode says never).
     */
    static void keep(Context ctx, File root, List<String> paths) {
        AssetManager am = ctx.getAssets();
        Map<String, Long> manifest = readManifest(root);

        for (String path : paths) {
            try {
                manifest.put(path, crcOfAsset(am, path));
            } catch (IOException e) {
                Log.e(TAG, "bundled asset " + path + ": " + e.getMessage());
            }
        }
        writeManifest(root, manifest);
        settle(ctx, root);
    }

    /** The cheap path, for launches that skip setup (a USB pad starting the game). */
    static void copyMissing(Context ctx, File root) {
        AssetManager am = ctx.getAssets();
        List<String> files = new ArrayList<String>();

        for (String tree : TREES) {
            listTree(am, tree, files);
        }
        for (String path : files) {
            File dest = new File(root, path);
            if (dest.exists()) continue;
            try {
                copyAsset(am, path, dest);
            } catch (IOException e) {
                Log.e(TAG, "staging " + path + ": " + e.getMessage());
            }
        }
    }

    // ------------------------------------------------------------------------

    private static SharedPreferences prefs(Context ctx) {
        return ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    /** A new APK, or a different data root, is a new comparison. */
    private static String stamp(Context ctx, File root) {
        long updated = 0;
        try {
            updated = ctx.getPackageManager()
                         .getPackageInfo(ctx.getPackageName(), 0).lastUpdateTime;
        } catch (PackageManager.NameNotFoundException ignored) { }
        return updated + ":" + root.getAbsolutePath();
    }

    private static void settle(Context ctx, File root) {
        prefs(ctx).edit().putString(KEY_SETTLED, stamp(ctx, root)).apply();
    }

    private static void listTree(AssetManager am, String path, List<String> out) {
        String[] children;
        try {
            children = am.list(path);
        } catch (IOException e) {
            children = null;
        }
        if (children == null || children.length == 0) {
            out.add(path);
            return;
        }
        for (String child : children) {
            listTree(am, path + "/" + child, out);
        }
    }

    private static long crcOfAsset(AssetManager am, String path) throws IOException {
        try (InputStream in = am.open(path)) {
            return crcOf(in);
        }
    }

    private static long crcOfFile(File f) throws IOException {
        try (InputStream in = new FileInputStream(f)) {
            return crcOf(in);
        }
    }

    private static long crcOf(InputStream in) throws IOException {
        CRC32 crc = new CRC32();
        byte[] buf = new byte[16 * 1024];
        int n;
        while ((n = in.read(buf)) > 0) {
            crc.update(buf, 0, n);
        }
        return crc.getValue();
    }

    private static void copyAsset(AssetManager am, String path, File dest) throws IOException {
        File parent = dest.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IOException("mkdir failed: " + parent);
        }
        byte[] buf = new byte[16 * 1024];
        try (InputStream in = am.open(path);
             OutputStream out = new FileOutputStream(dest)) {
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
            }
        }
        Log.i(TAG, "staged " + path);
    }

    private static Map<String, Long> readManifest(File root) {
        Map<String, Long> m = new HashMap<String, Long>();
        File f = new File(root, MANIFEST);
        if (!f.isFile()) return m;

        try (BufferedReader r = new BufferedReader(new FileReader(f))) {
            String line;
            while ((line = r.readLine()) != null) {
                int sp = line.indexOf(' ');
                if (sp <= 0) continue;
                try {
                    m.put(line.substring(sp + 1), Long.parseLong(line.substring(0, sp), 16));
                } catch (NumberFormatException ignored) { }
            }
        } catch (IOException e) {
            Log.w(TAG, "reading " + MANIFEST + ": " + e.getMessage());
        }
        return m;
    }

    private static void writeManifest(File root, Map<String, Long> m) {
        File f = new File(root, MANIFEST);
        File parent = f.getParentFile();
        if (parent != null) parent.mkdirs();

        try (PrintWriter w = new PrintWriter(new FileOutputStream(f))) {
            for (Map.Entry<String, Long> e : m.entrySet()) {
                w.println(Long.toHexString(e.getValue()) + " " + e.getKey());
            }
        } catch (IOException e) {
            Log.w(TAG, "writing " + MANIFEST + ": " + e.getMessage());
        }
    }
}
