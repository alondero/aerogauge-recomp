package io.github.alondero.aerogaugerecomp;

import android.app.AlertDialog;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.os.Looper;
import android.system.Os;
import android.view.*;
import android.widget.*;
import org.libsdl.app.SDLActivity;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

public final class GameActivity extends SDLActivity {
    static native void nativeTouch(int buttons, int x, int y);
    static native boolean nativeMenuOpen();
    static native void nativeBackground(boolean background);
    static native void nativeRequestQuit();
    static native void nativeMotionRotation(int rotation);
    private TouchControls controls;
    private boolean librariesReady;
    private boolean resumed;
    private DisplayManager displayManager;
    private final DisplayManager.DisplayListener displayListener = new DisplayManager.DisplayListener() {
        @Override public void onDisplayAdded(int id) { }
        @Override public void onDisplayRemoved(int id) { }
        @Override public void onDisplayChanged(int id) { publishMotionRotation(); }
    };

    private void publishMotionRotation() {
        if (librariesReady) nativeMotionRotation(getWindowManager().getDefaultDisplay().getRotation());
    }

    @Override protected String[] getLibraries() { return new String[]{"c++_shared", "SDL2", "main"}; }
    @Override public void loadLibraries() {
        try {
            String nativeDir = getApplicationInfo().nativeLibraryDir;
            Os.setenv("AERO_ANDROID_NATIVE_LIB_DIR", nativeDir + "/", true);
            Os.setenv("AERO_ANDROID_CACHE_DIR", getCacheDir().getAbsolutePath() + "/", true);
            Os.setenv("SDL_VULKAN_LIBRARY", nativeDir + "/libaero_vulkan.so", true);
            DriverImport.configure(this);
            super.loadLibraries();
            librariesReady = true;
        } catch (Exception error) {
            DriverImport.releaseGameLock();
            try {
                Files.write(new File(getFilesDir(), "startup-error.txt").toPath(),
                    ("Cannot start graphics: " + error.getMessage()).getBytes(StandardCharsets.UTF_8));
            } catch (Exception ignored) { }
            throw new IllegalStateException("Cannot start graphics: " + error.getMessage(), error);
        }
    }
    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        if (!librariesReady) { finish(); return; }
        // A 180-degree landscape flip need not change Configuration.orientation.
        // Observe display changes as well as resume/configuration callbacks.
        displayManager = (DisplayManager) getSystemService(DISPLAY_SERVICE);
        displayManager.registerDisplayListener(displayListener, new android.os.Handler(getMainLooper()));
        publishMotionRotation();
        if (mLayout == null) return;
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        controls = new TouchControls(this, getSharedPreferences("player", 0).getInt("touch", 0));
        mLayout.addView(controls, new android.widget.RelativeLayout.LayoutParams(-1, -1));
        immersive();
    }
    private void immersive() {
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
    }
    @Override public void onWindowFocusChanged(boolean focus) {
        super.onWindowFocusChanged(focus);
        if (focus) immersive();
    }
    @Override public void onConfigurationChanged(android.content.res.Configuration configuration) {
        super.onConfigurationChanged(configuration);
        publishMotionRotation();
    }
    @Override protected void onResume() {
        super.onResume();
        resumed = true;
        if (librariesReady) {
            publishMotionRotation();
            nativeBackground(false);
        }
        immersive();
    }
    @Override protected void onPause() {
        resumed = false;
        if (controls != null) controls.release();
        if (librariesReady) nativeBackground(true);
        super.onPause();
    }
    @Override protected void onDestroy() {
        if (displayManager != null) displayManager.unregisterDisplayListener(displayListener);
        super.onDestroy();
    }
    @Override public void onBackPressed() { showActions(); }
    public void showActions() {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            runOnUiThread(this::showActions);
            return;
        }
        if (controls != null) controls.release();
        nativeBackground(true);
        new AlertDialog.Builder(this).setTitle("AeroGauge")
            .setOnDismissListener(dialog -> { if (resumed) nativeBackground(false); })
            .setItems(new String[]{"Resume", "Graphics & enhancements", "Exit to launcher"}, (dialog, which) -> {
                if (which == 1) {
                    onNativeKeyDown(KeyEvent.KEYCODE_ESCAPE);
                    new android.os.Handler(getMainLooper()).postDelayed(() -> onNativeKeyUp(KeyEvent.KEYCODE_ESCAPE), 80);
                } else if (which == 2) nativeRequestQuit();
            }).show();
    }
}
