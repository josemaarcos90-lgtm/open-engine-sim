package org.openenginesim.app;

import android.content.res.AssetManager;
import android.graphics.Color;
import android.os.Bundle;
import android.util.Log;
import android.view.Gravity;
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
    private static final String ASSET_VERSION = "0.2.2-android-alpha2";
    private TextView diagnosticView;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        String assetStatus;
        try {
            syncAssets();
            assetStatus = validateAssets();
        }
        catch (IOException exception) {
            Log.e(TAG, "Failed to extract simulator assets", exception);
            assetStatus = "ASSETS: FALHOU\n" + exception;
        }

        super.onCreate(savedInstanceState);
        showDiagnostics(
            "OPEN ENGINE SIM - ANDROID DIAGNOSTICO\n\n" +
            "JAVA/SDL ACTIVITY: OK\n" +
            assetStatus + "\n\n" +
            "Se esta tela continuar visivel, envie um print.\n" +
            "O proximo estagio e a inicializacao nativa/GPU.");
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

    private void showDiagnostics(final String message) {
        runOnUiThread(() -> {
            diagnosticView = new TextView(this);
            diagnosticView.setText(message);
            diagnosticView.setTextColor(Color.GREEN);
            diagnosticView.setBackgroundColor(Color.BLACK);
            diagnosticView.setTextSize(16.0f);
            diagnosticView.setGravity(Gravity.TOP | Gravity.START);
            diagnosticView.setPadding(32, 48, 32, 32);
            addContentView(diagnosticView, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
        });
    }

    private void syncAssets() throws IOException {
        final File destinationRoot = new File(getFilesDir(), "assets");
        final File marker = new File(destinationRoot, ".asset-version");

        if (marker.isFile()) {
            final String installedVersion = new String(
                Files.readAllBytes(marker.toPath()), StandardCharsets.UTF_8).trim();
            if (ASSET_VERSION.equals(installedVersion)) {
                return;
            }
        }

        deleteRecursively(destinationRoot);
        if (!destinationRoot.mkdirs() && !destinationRoot.isDirectory()) {
            throw new IOException("Could not create " + destinationRoot);
        }

        copyAssetTree(getAssets(), "", destinationRoot);
        Files.write(marker.toPath(), ASSET_VERSION.getBytes(StandardCharsets.UTF_8));
        Log.i(TAG, "Simulator assets extracted to " + destinationRoot);
    }

    private static void copyAssetTree(
        AssetManager manager,
        String assetPath,
        File destination) throws IOException
    {
        final String[] children = manager.list(assetPath);
        if (children != null && children.length > 0) {
            if (!destination.mkdirs() && !destination.isDirectory()) {
                throw new IOException("Could not create " + destination);
            }

            for (String child : children) {
                final String childAssetPath = assetPath.isEmpty()
                    ? child
                    : assetPath + "/" + child;
                copyAssetTree(manager, childAssetPath, new File(destination, child));
            }
            return;
        }

        final File parent = destination.getParentFile();
        if (parent != null && !parent.mkdirs() && !parent.isDirectory()) {
            throw new IOException("Could not create " + parent);
        }

        try (InputStream input = manager.open(assetPath);
             FileOutputStream output = new FileOutputStream(destination)) {
            final byte[] buffer = new byte[64 * 1024];
            int count;
            while ((count = input.read(buffer)) != -1) {
                output.write(buffer, 0, count);
            }
        }
    }

    private static void deleteRecursively(File file) throws IOException {
        if (!file.exists()) return;

        final File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) {
                deleteRecursively(child);
            }
        }

        if (!file.delete()) {
            throw new IOException("Could not delete " + file);
        }
    }
}
