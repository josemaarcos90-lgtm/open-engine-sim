package org.openenginesim.app;

import android.content.res.AssetManager;
import android.graphics.Color;
import android.os.Bundle;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

public class OpenEngineSimActivity extends SDLActivity {
    private static final String TAG = "OpenEngineSim";
    private static final String ASSET_VERSION = "0.2.2-android-alpha4";
    private TextView diagnosticView;
    private String assetStatus = "ASSETS: AINDA NAO VERIFICADOS";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        try {
            syncAssets();
            assetStatus = validateAssets();
        } catch (IOException exception) {
            Log.e(TAG, "Failed to extract simulator assets", exception);
            assetStatus = "ASSETS: FALHOU\n" + exception;
        }
        super.onCreate(savedInstanceState);
        updateNativeStatus("Aguardando entrada no codigo C++...");
    }

    public void updateNativeStatus(final String nativeStatus) {
        runOnUiThread(() -> {
            if (diagnosticView == null) {
                diagnosticView = new TextView(this);
                diagnosticView.setTextColor(Color.GREEN);
                diagnosticView.setBackgroundColor(Color.BLACK);
                diagnosticView.setTextSize(16.0f);
                diagnosticView.setGravity(Gravity.TOP | Gravity.START);
                diagnosticView.setPadding(32, 48, 32, 32);
                addContentView(diagnosticView, new ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
            }
            diagnosticView.setVisibility(View.VISIBLE);
            diagnosticView.setText("OPEN ENGINE SIM - ANDROID DIAGNOSTICO V3\n\n" +
                "JAVA/SDL ACTIVITY: OK\n" + assetStatus + "\n\nNATIVO/GPU:\n" + nativeStatus);
        });
    }

    public void showRenderDiagnostics(final String renderStatus) {
        runOnUiThread(() -> {
            if (diagnosticView == null) return;
            diagnosticView.setBackgroundColor(0xCC000000);
            diagnosticView.setTextColor(Color.GREEN);
            diagnosticView.setTextSize(15.0f);
            diagnosticView.setVisibility(View.VISIBLE);
            diagnosticView.setText("OPEN ENGINE SIM - RENDER DIAGNOSTICO V5\n\n" + renderStatus);
            // Keep the diagnostic visible long enough to photograph, then reveal
            // the SDL/GPU surface automatically for the actual visual test.
            diagnosticView.removeCallbacks(hideDiagnosticsRunnable);
            diagnosticView.postDelayed(hideDiagnosticsRunnable, 2500);
        });
    }

    private final Runnable hideDiagnosticsRunnable = () -> {
        if (diagnosticView != null) diagnosticView.setVisibility(View.GONE);
    };

    public void hideNativeDiagnostics() {
        runOnUiThread(() -> {
            if (diagnosticView != null) diagnosticView.setVisibility(View.GONE);
        });
    }

    private String validateAssets() {
        final File root = new File(getFilesDir(), "assets");
        final File vertex = new File(root, "shaders/engine_sim.vertex.spv");
        final File fragment = new File(root, "shaders/engine_sim.fragment.spv");
        final File mainScript = new File(root, "main.mr");
        return "ASSETS: " + (root.isDirectory() ? "OK" : "FALHOU") +
            "\nVERTEX SPV: " + (vertex.isFile() ? "OK" : "FALHOU") +
            "\nFRAGMENT SPV: " + (fragment.isFile() ? "OK" : "FALHOU") +
            "\nMAIN.MR: " + (mainScript.isFile() ? "OK" : "FALHOU");
    }

    private void syncAssets() throws IOException {
        final File destinationRoot = new File(getFilesDir(), "assets");
        final File marker = new File(destinationRoot, ".asset-version");
        if (marker.isFile()) {
            final String installedVersion = new String(Files.readAllBytes(marker.toPath()), StandardCharsets.UTF_8).trim();
            if (ASSET_VERSION.equals(installedVersion)) return;
        }
        deleteRecursively(destinationRoot);
        if (!destinationRoot.mkdirs() && !destinationRoot.isDirectory()) throw new IOException("Could not create " + destinationRoot);
        copyAssetTree(getAssets(), "", destinationRoot);
        Files.write(marker.toPath(), ASSET_VERSION.getBytes(StandardCharsets.UTF_8));
    }

    private static void copyAssetTree(AssetManager manager, String assetPath, File destination) throws IOException {
        final String[] children = manager.list(assetPath);
        if (children != null && children.length > 0) {
            if (!destination.mkdirs() && !destination.isDirectory()) throw new IOException("Could not create " + destination);
            for (String child : children) copyAssetTree(manager, assetPath.isEmpty() ? child : assetPath + "/" + child, new File(destination, child));
            return;
        }
        final File parent = destination.getParentFile();
        if (parent != null && !parent.mkdirs() && !parent.isDirectory()) throw new IOException("Could not create " + parent);
        try (InputStream input = manager.open(assetPath); FileOutputStream output = new FileOutputStream(destination)) {
            final byte[] buffer = new byte[64 * 1024];
            int count;
            while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
        }
    }

    private static void deleteRecursively(File file) throws IOException {
        if (!file.exists()) return;
        final File[] children = file.listFiles();
        if (children != null) for (File child : children) deleteRecursively(child);
        if (!file.delete()) throw new IOException("Could not delete " + file);
    }
}
