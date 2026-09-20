package io.github.alondero.aerogaugerecomp;

import android.app.*;
import android.content.*;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.*;
import android.view.*;
import android.widget.*;
import java.io.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.*;
import java.util.zip.*;

public final class LauncherActivity extends Activity {
    private static final int ROM = 1, DRIVER = 2, BACKUP = 3, LOG = 4, BLUETOOTH = 5, RESTORE = 6;
    private static final ExecutorService WORK = Executors.newSingleThreadExecutor();
    private static volatile boolean busy;
    private static volatile String message = "";
    private static volatile LauncherActivity current;
    private TextView status, driverStatus;
    private Button play, importRom, importDriver, systemDriver, backup, restore, diagnostics;
    private ProgressBar progress;
    private boolean resumed;
    private boolean pendingPlay;
    private final int ink = Color.rgb(230, 242, 249), muted = Color.rgb(151, 176, 194);

    private int dp(float value) { return Math.round(value * getResources().getDisplayMetrics().density); }

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().setStatusBarColor(Color.rgb(9, 18, 31));
        getWindow().setNavigationBarColor(Color.rgb(9, 18, 31));
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackground(new GradientDrawable(GradientDrawable.Orientation.TL_BR,
            new int[]{0xff09121f, 0xff102b3d, 0xff09121f}));
        LinearLayout page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setPadding(dp(24), dp(24), dp(24), dp(32));
        scroll.addView(page);
        TextView eyebrow = text("NATIVE N64 RECOMPILATION", 12, 0xff59e2d0);
        eyebrow.setLetterSpacing(.18f); page.addView(eyebrow);
        TextView title = text("AeroGauge", 44, ink);
        title.setTypeface(Typeface.create("sans-serif-condensed", Typeface.BOLD));
        page.addView(title);
        page.addView(text("RECOMPILED  /  ANDROID", 13, muted));
        TextView tagline = text("Leave the road behind.", 23, ink);
        tagline.setPadding(0, dp(24), 0, dp(20)); page.addView(tagline);

        LinearLayout game = card(page);
        game.addView(text("YOUR GAME", 12, 0xff59e2d0));
        status = text("", 16, ink); game.addView(status);
        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setIndeterminate(true); game.addView(progress);
        play = button(game, "Play AeroGauge", true, () -> prepareGame());
        importRom = button(game, "Import ROM", false, () -> choose(ROM));
        game.addView(text("Bring your own USA cartridge dump. Import once; your game and saves stay on this device. No game data is included.", 13, muted));

        LinearLayout controls = card(page);
        controls.addView(text("READY TO RACE", 12, 0xff59e2d0));
        controls.addView(text("Steer with the left thumbstick. Hold A to accelerate, B to brake and Z to drift. Start pauses the game.", 15, ink));
        controls.addView(text("Prefer a controller? Connect a Bluetooth or USB gamepad. Touch controls can hide automatically.", 13, muted));
        Spinner touch = new Spinner(this);
        String[] modes = {"Touch controls: automatic", "Touch controls: always", "Touch controls: hidden"};
        ArrayAdapter<String> adapter = new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, modes);
        touch.setAdapter(adapter);
        touch.setSelection(getSharedPreferences("player", 0).getInt("touch", 0));
        touch.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            public void onNothingSelected(AdapterView<?> parent) { }
            public void onItemSelected(AdapterView<?> parent, View view, int pos, long id) {
                getSharedPreferences("player", 0).edit().putInt("touch", pos).apply();
            }
        });
        controls.addView(touch);

        LinearLayout advanced = card(page);
        advanced.addView(text("GRAPHICS & STORAGE", 12, 0xff59e2d0));
        driverStatus = text("", 15, ink); advanced.addView(driverStatus);
        advanced.addView(text("Start with your system driver. Older Adreno GPUs, including Pixel 5, may need a compatible Mesa Turnip driver. Import only a driver you trust.", 13, muted));
        importDriver = button(advanced, "Import GPU driver ZIP", false, () -> choose(DRIVER));
        systemDriver = button(advanced, "Use system driver", false, () -> work(() -> {
            DriverImport.useSystem(this); return "System GPU driver selected.";
        }));
        backup = button(advanced, "Back up saves", false, () -> create(BACKUP, "application/zip", "AeroGauge-saves.zip"));
        restore = button(advanced, "Restore saves", false, () -> choose(RESTORE));
        diagnostics = button(advanced, "Export diagnostics", false, () -> create(LOG, "text/plain", "AeroGauge-diagnostics.txt"));
        advanced.addView(text("Updates keep your saves. Back them up before uninstalling or clearing app data. Graphics and enhancements are available from Menu while playing.", 13, muted));
        setContentView(scroll);
        refresh();
    }

    private TextView text(String value, int size, int color) {
        TextView label = new TextView(this);
        label.setText(value); label.setTextSize(size); label.setTextColor(color);
        label.setPadding(0, dp(5), 0, dp(7)); label.setLineSpacing(dp(2), 1);
        return label;
    }
    private LinearLayout card(LinearLayout page) {
        LinearLayout card = new LinearLayout(this); card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(dp(18), dp(14), dp(18), dp(14));
        GradientDrawable background = new GradientDrawable(); background.setColor(0xcc142638);
        background.setCornerRadius(dp(20)); background.setStroke(dp(1), 0xff294456); card.setBackground(background);
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(-1, -2); p.bottomMargin = dp(16);
        page.addView(card, p); return card;
    }
    private Button button(LinearLayout parent, String label, boolean primary, Runnable action) {
        Button b = new Button(this); b.setText(label); b.setAllCaps(false); b.setTextSize(16);
        b.setTextColor(primary ? 0xff09242b : ink);
        b.setBackgroundTintList(android.content.res.ColorStateList.valueOf(primary ? 0xff59e2d0 : 0xff294456));
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(-1, dp(54)); p.topMargin = dp(6);
        parent.addView(b, p); b.setOnClickListener(v -> action.run()); return b;
    }
    @Override protected void onResume() {
        super.onResume(); resumed = true;
        current = this;
        File error = new File(getFilesDir(), "startup-error.txt");
        if (error.isFile()) {
            try {
                message = new String(Files.readAllBytes(error.toPath()), StandardCharsets.UTF_8);
                error.delete();
            } catch (IOException ignored) { }
        }
        refresh();
    }
    @Override protected void onPause() {
        resumed = false;
        if (current == this) current = null;
        super.onPause();
    }
    private void refresh() {
        boolean ready = new File(getFilesDir(), RomImport.NAME).isFile();
        status.setText(message.isEmpty() ? (ready ? "Your game is ready. See you on the starting grid." : "One quick setup, then take flight.") : message);
        progress.setVisibility(busy ? View.VISIBLE : View.GONE);
        play.setEnabled(ready && !busy);
        importRom.setText(ready ? "Replace imported ROM" : "Import ROM");
        for (Button b : new Button[]{importRom, importDriver, systemDriver, backup, restore, diagnostics}) b.setEnabled(!busy);
        driverStatus.setText("GPU driver: " + DriverImport.description(this));
    }
    private void choose(int request) {
        startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*"), request);
    }
    private void create(int request, String type, String name) {
        startActivityForResult(new Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE)
            .setType(type).putExtra(Intent.EXTRA_TITLE, name), request);
    }
    interface Operation { String run() throws Exception; }
    private void work(Operation operation) {
        if (busy) return;
        busy = true; message = "Working…"; refresh();
        WORK.execute(() -> {
            try { message = operation.run(); }
            catch (Exception error) { message = error.getMessage() == null ? "Could not complete the operation. Please try again." : error.getMessage(); }
            finally { busy = false; }
            LauncherActivity activity = current;
            if (activity != null) activity.runOnUiThread(() -> {
                if (!activity.isFinishing() && !activity.isDestroyed()) activity.refresh();
            });
        });
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (result != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        if (request == RESTORE) {
            new AlertDialog.Builder(this).setTitle("Restore saves?")
                .setMessage("This replaces the current AeroGauge EEPROM and Controller Pak files. Continue only if this backup is yours.")
                .setNegativeButton("Cancel", null)
                .setPositiveButton("Restore", (dialog, which) -> work(() -> {
                    try (InputStream input = getContentResolver().openInputStream(uri);
                         AutoCloseable lock = DriverImport.storageLock(this)) {
                        SaveTransfer.restore(this, input);
                    }
                    return "Saves restored. Launch the game to continue.";
                })).show();
            return;
        }
        work(() -> {
            if (request == ROM) {
                try (InputStream input = getContentResolver().openInputStream(uri)) { RomImport.install(input, getFilesDir()); }
                return "USA ROM verified. Ready to race!";
            }
            if (request == DRIVER) return "Driver ready: " + DriverImport.install(this, uri);
            if (request == BACKUP) { exportSaves(uri); return "Save backup exported. Keep it somewhere safe."; }
            if (request == LOG) {
                File log = new File(getFilesDir(), "native.log");
                if (!log.isFile()) return "No game diagnostics yet. Launch the game first.";
                try (OutputStream out = getContentResolver().openOutputStream(uri, "wt")) {
                    if (out == null) throw new IOException("Cannot write to the selected location.");
                    Files.copy(log.toPath(), out);
                }
                return "Diagnostics exported.";
            }
            return "";
        });
    }
    private void prepareGame() {
        if (Build.VERSION.SDK_INT >= 31 && checkSelfPermission(android.Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED) {
            pendingPlay = true;
            requestPermissions(new String[]{android.Manifest.permission.BLUETOOTH_CONNECT, android.Manifest.permission.BLUETOOTH_SCAN}, BLUETOOTH);
            return;
        }
        prepareGameAfterPermissions();
    }
    @Override public void onRequestPermissionsResult(int request, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(request, permissions, results);
        if (request == BLUETOOTH && pendingPlay) {
            pendingPlay = false;
            prepareGameAfterPermissions();
        }
    }
    private void prepareGameAfterPermissions() {
        work(() -> {
            copyGameAssetsIfNeeded();
            LauncherActivity activity = current;
            if (activity != null) activity.runOnUiThread(() -> {
                if (activity.resumed && !activity.isFinishing() && !activity.isDestroyed())
                    activity.startActivity(new Intent(activity, GameActivity.class));
            });
            return "Your game is ready.";
        });
    }
    private void copyGameAssetsIfNeeded() throws Exception {
        long packageStamp = getPackageManager().getPackageInfo(getPackageName(), 0).lastUpdateTime;
        android.content.SharedPreferences preferences = getSharedPreferences("player", 0);
        File symbols = new File(getFilesDir(), "aerogauge.syms.toml");
        File assets = new File(getFilesDir(), "assets");
        if (preferences.getLong("assets_stamp", 0) == packageStamp && assets.isDirectory() && symbols.isFile()) return;
        copyAsset("assets");
        copyAsset("aerogauge.syms.toml");
        preferences.edit().putLong("assets_stamp", packageStamp).apply();
    }
    private void copyAsset(String path) throws Exception {
        String[] children = getAssets().list(path);
        File destination = new File(getFilesDir(), path);
        if (children != null && children.length > 0) {
            if (!destination.isDirectory() && !destination.mkdirs()) throw new IOException("Cannot prepare game files.");
            for (String child : children) copyAsset(path + "/" + child);
        } else {
            try (InputStream input = getAssets().open(path)) { Files.copy(input, destination.toPath(), StandardCopyOption.REPLACE_EXISTING); }
        }
    }
    private void exportSaves(Uri uri) throws Exception {
        // The launcher only exports once the game process releases its driver lock.
        try (AutoCloseable lock = DriverImport.storageLock(this)) {
            try (OutputStream output = getContentResolver().openOutputStream(uri, "wt")) {
                if (output == null) throw new IOException("Cannot write to the selected location.");
                SaveTransfer.export(this, output);
            }
        }
    }
}
