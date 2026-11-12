package io.github.openstarbound.mobile;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.util.ArrayList;
import java.util.Arrays;

// Offline launcher: imports the user's own packed.pak and mods into the app's
// external files directory (the same directory the native game reads), then
// starts the game. Nothing here touches the network.
public class LauncherActivity extends Activity {
    private static final int REQ_PAK = 1;
    private static final int REQ_MODS = 2;
    private static final byte[] PAK_MAGIC = "SBAsset6".getBytes();

    private TextView status;
    private TextView progress;
    private Button playButton;
    private Button[] actionButtons;

    private File dataDir() {
        File dir = getExternalFilesDir(null);
        if (dir == null) dir = getFilesDir();
        return dir;
    }

    private File assetsDir() { File d = new File(dataDir(), "assets"); d.mkdirs(); return d; }
    private File modsDir() { File d = new File(dataDir(), "mods"); d.mkdirs(); return d; }
    private File packedPak() { return new File(assetsDir(), "packed.pak"); }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER_HORIZONTAL);
        int pad = (int) (16 * getResources().getDisplayMetrics().density);
        root.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText("OpenStarbound");
        title.setTextSize(28);
        title.setGravity(Gravity.CENTER);
        root.addView(title);

        status = new TextView(this);
        status.setTextSize(16);
        status.setPadding(0, pad, 0, pad);
        root.addView(status);

        progress = new TextView(this);
        progress.setTextSize(14);
        root.addView(progress);

        playButton = button("Play", v -> startGame());
        Button importPak = button("Import packed.pak", v -> pick(REQ_PAK, false));
        Button addMods = button("Add mods (.pak)", v -> pick(REQ_MODS, true));
        Button removeMods = button("Remove mods", v -> removeModsDialog());
        Button viewLog = button("View last log", v -> showLog());
        actionButtons = new Button[] { playButton, importPak, addMods, removeMods, viewLog };
        for (Button b : actionButtons) root.addView(b);

        TextView note = new TextView(this);
        note.setTextSize(12);
        note.setPadding(0, pad, 0, 0);
        note.setText("You must supply your own packed.pak from a legitimate Starbound copy.\n"
            + "Folder mods can be copied by hand to:\n" + modsDir().getAbsolutePath());
        root.addView(note);

        ScrollView scroll = new ScrollView(this);
        scroll.addView(root);
        setContentView(scroll);
        refresh();
    }

    private Button button(String text, View.OnClickListener l) {
        Button b = new Button(this);
        b.setText(text);
        b.setOnClickListener(l);
        return b;
    }

    private void refresh() {
        StringBuilder sb = new StringBuilder();
        File pak = packedPak();
        boolean havePak = pak.isFile() && pak.length() > 0;
        if (havePak)
            sb.append("packed.pak: ").append(pak.length() / (1024 * 1024)).append(" MB\n");
        else
            sb.append("packed.pak: MISSING - import it before playing\n");
        String[] mods = modsDir().list();
        int count = 0;
        if (mods != null) for (String m : mods) if (!m.startsWith(".")) count++;
        sb.append("Mods: ").append(count);
        status.setText(sb.toString());
        playButton.setEnabled(havePak);
    }

    private void setBusy(boolean busy) {
        for (Button b : actionButtons) b.setEnabled(!busy);
        if (!busy) refresh();
    }

    private void pick(int request, boolean multiple) {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, multiple);
        startActivityForResult(intent, request);
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (result != RESULT_OK || data == null) return;
        ArrayList<Uri> uris = new ArrayList<>();
        if (data.getClipData() != null) {
            for (int i = 0; i < data.getClipData().getItemCount(); i++)
                uris.add(data.getClipData().getItemAt(i).getUri());
        } else if (data.getData() != null) {
            uris.add(data.getData());
        }
        if (uris.isEmpty()) return;
        setBusy(true);
        new Thread(() -> importFiles(request, uris)).start();
    }

    private void importFiles(int request, ArrayList<Uri> uris) {
        String message = null;
        try {
            for (Uri uri : uris) {
                String name = displayName(uri);
                if (request == REQ_PAK) {
                    File tmp = new File(assetsDir(), "packed.pak.part");
                    copy(uri, tmp, name);
                    if (!hasMagic(tmp)) {
                        tmp.delete();
                        throw new Exception(name + " is not a Starbound .pak (bad header)");
                    }
                    File dest = packedPak();
                    dest.delete();
                    if (!tmp.renameTo(dest)) throw new Exception("could not move into place");
                } else {
                    if (!name.toLowerCase().endsWith(".pak"))
                        throw new Exception(name + " is not a .pak file");
                    String safe = new File(name).getName();
                    File tmp = new File(modsDir(), safe + ".part");
                    copy(uri, tmp, name);
                    File dest = new File(modsDir(), safe);
                    dest.delete();
                    if (!tmp.renameTo(dest)) throw new Exception("could not move " + safe + " into place");
                }
            }
        } catch (Exception e) {
            message = "Import failed: " + e.getMessage();
        }
        final String finalMessage = message;
        runOnUiThread(() -> {
            progress.setText(finalMessage == null ? "Import finished." : finalMessage);
            setBusy(false);
        });
    }

    private String displayName(Uri uri) {
        String name = null;
        try (Cursor c = getContentResolver().query(uri, null, null, null, null)) {
            if (c != null && c.moveToFirst()) {
                int idx = c.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (idx >= 0) name = c.getString(idx);
            }
        }
        if (name == null) name = uri.getLastPathSegment();
        return name == null ? "file.pak" : name;
    }

    private void copy(Uri uri, File dest, String name) throws Exception {
        byte[] buf = new byte[1 << 20];
        long total = 0;
        long lastReport = 0;
        try (InputStream in = getContentResolver().openInputStream(uri);
             OutputStream out = new FileOutputStream(dest)) {
            if (in == null) throw new Exception("cannot open " + name);
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
                total += n;
                if (total - lastReport > 32L * 1024 * 1024) {
                    lastReport = total;
                    final String text = "Copying " + name + ": " + total / (1024 * 1024) + " MB";
                    runOnUiThread(() -> progress.setText(text));
                }
            }
        } catch (Exception e) {
            dest.delete();
            throw e;
        }
    }

    private boolean hasMagic(File file) throws Exception {
        byte[] head = new byte[PAK_MAGIC.length];
        try (InputStream in = new FileInputStream(file)) {
            return in.read(head) == head.length && Arrays.equals(head, PAK_MAGIC);
        }
    }

    private void removeModsDialog() {
        File[] files = modsDir().listFiles();
        if (files == null || files.length == 0) {
            progress.setText("No mods installed.");
            return;
        }
        final ArrayList<File> mods = new ArrayList<>();
        for (File f : files) if (!f.getName().startsWith(".")) mods.add(f);
        String[] names = new String[mods.size()];
        final boolean[] checked = new boolean[mods.size()];
        for (int i = 0; i < names.length; i++) names[i] = mods.get(i).getName();
        new AlertDialog.Builder(this)
            .setTitle("Select mods to remove")
            .setMultiChoiceItems(names, checked, (d, which, isChecked) -> checked[which] = isChecked)
            .setPositiveButton("Remove", (d, w) -> {
                for (int i = 0; i < checked.length; i++)
                    if (checked[i]) deleteRecursive(mods.get(i));
                refresh();
            })
            .setNegativeButton("Cancel", null)
            .show();
    }

    private void deleteRecursive(File f) {
        File[] children = f.listFiles();
        if (children != null) for (File c : children) deleteRecursive(c);
        f.delete();
    }

    private void showLog() {
        File log = new File(new File(dataDir(), "logs"), "starbound.log");
        String text;
        if (!log.isFile()) {
            text = "No log yet: " + log.getAbsolutePath();
        } else {
            try (RandomAccessFile f = new RandomAccessFile(log, "r")) {
                int len = (int) Math.min(f.length(), 24 * 1024);
                byte[] buf = new byte[len];
                f.seek(f.length() - len);
                f.readFully(buf);
                text = new String(buf, "UTF-8");
            } catch (Exception e) {
                text = "Could not read log: " + e;
            }
        }
        final String shown = text;
        TextView tv = new TextView(this);
        tv.setText(shown);
        tv.setTextSize(10);
        tv.setTypeface(android.graphics.Typeface.MONOSPACE);
        tv.setTextIsSelectable(true);
        ScrollView sv = new ScrollView(this);
        sv.addView(tv);
        new AlertDialog.Builder(this)
            .setTitle("starbound.log (end)")
            .setView(sv)
            .setPositiveButton("Copy", (d, w) -> {
                ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
                cm.setPrimaryClip(ClipData.newPlainText("starbound.log", shown));
                progress.setText("Log copied to clipboard.");
            })
            .setNegativeButton("Close", null)
            .show();
    }

    private void startGame() {
        startActivity(new Intent(this, GameActivity.class));
    }
}
