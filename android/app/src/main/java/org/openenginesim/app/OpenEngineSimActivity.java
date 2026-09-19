package org.openenginesim.app;

import android.content.res.AssetManager;
import android.os.Bundle;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

public class OpenEngineSimActivity extends SDLActivity {
    private static final String TAG = "OpenEngineSim";
    private static final String ASSET_VERSION = "0.2.2-android-alpha1";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        try {
            syncAssets();
        }
        catch (IOException exception) {
            Log.e(TAG, "Failed to extract simulator assets", exception);
        }

        super.onCreate(savedInstanceState);
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
