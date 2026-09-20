package io.github.alondero.aerogaugerecomp;

import android.content.Context;
import java.io.*;
import java.nio.file.*;
import java.util.HashSet;
import java.util.zip.*;

/** Locked, bounded export/import for the two player save images. */
final class SaveTransfer {
    private static final String EEPROM = "aerogauge.us.bin";
    private static final String EEPROM_BACKUP = "aerogauge.us.bin.bak";
    private static final String PAK = "aerogauge.us.mpk";

    private static File root(Context context) { return new File(context.getFilesDir(), "AeroGaugeRecomp/saves"); }
    private static boolean allowed(String name) { return EEPROM.equals(name) || EEPROM_BACKUP.equals(name) || PAK.equals(name); }
    private static int expectedSize(String name) { return PAK.equals(name) ? 32768 : 512; }

    static void export(Context context, OutputStream output) throws Exception {
        File directory = root(context);
        recover(directory);
        File[] files = directory.listFiles();
        if (files == null) throw new IOException("No saves yet. Play a race and save in the game first.");
        int count = 0;
        try (ZipOutputStream zip = new ZipOutputStream(output)) {
            for (File file : files) {
                if (!file.isFile() || !allowed(file.getName())) continue;
                if (file.length() != expectedSize(file.getName())) throw new IOException("A save image is incomplete; try again after leaving the game.");
                zip.putNextEntry(new ZipEntry(file.getName()));
                Files.copy(file.toPath(), zip);
                zip.closeEntry();
                count++;
            }
            if (count == 0) throw new IOException("No saves yet. Play a race and save in the game first.");
        }
    }

    static void restore(Context context, InputStream input) throws Exception {
        if (input == null) throw new IOException("The selected backup could not be opened.");
        File directory = root(context);
        File parent = directory.getParentFile();
        if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("Cannot prepare the save directory.");
        recover(directory);
        File staging = Files.createTempDirectory(parent.toPath(), "saves-restore-").toFile();
        boolean installed = false;
        try {
            int count = 0;
            HashSet<String> names = new HashSet<>();
            try (ZipInputStream zip = new ZipInputStream(input)) {
                ZipEntry entry;
                byte[] buffer = new byte[8192];
                while ((entry = zip.getNextEntry()) != null) {
                    String name = entry.getName();
                    if (entry.isDirectory() || !allowed(name) || !names.add(name) || ++count > 3)
                        throw new IOException("This is not an AeroGauge save backup.");
                    File target = new File(staging, name);
                    long bytes = 0;
                    try (OutputStream output = new BufferedOutputStream(new FileOutputStream(target))) {
                        int read;
                        while ((read = zip.read(buffer)) != -1) {
                            bytes += read;
                            if (bytes > expectedSize(name)) throw new IOException("A save image is too large.");
                            output.write(buffer, 0, read);
                        }
                    }
                    if (bytes != expectedSize(name)) throw new IOException("A save image is truncated.");
                }
            }
            if (count == 0) throw new IOException("This backup contains no AeroGauge saves.");
            File previous = new File(parent, "saves.before-restore");
            if (previous.exists()) throw new IOException("A previous restore needs attention; restart the launcher and try again.");
            if (directory.exists() && !directory.renameTo(previous)) throw new IOException("Could not protect the current saves.");
            try {
                if (!staging.renameTo(directory)) throw new IOException("Could not install the restored saves.");
                installed = true;
            } catch (Exception error) {
                if (!directory.exists() && previous.exists()) previous.renameTo(directory);
                throw error;
            } finally {
                if (installed) deleteTree(previous);
            }
        } finally {
            if (!installed) deleteTree(staging);
        }
    }

    /** Repairs the only two crash windows left by the directory swap. */
    static void recover(File directory) throws IOException {
        File parent = directory.getParentFile();
        if (parent == null) return;
        File previous = new File(parent, "saves.before-restore");
        if (!directory.exists() && previous.exists() && !previous.renameTo(directory))
            throw new IOException("Could not recover the existing saves.");
        if (directory.exists() && previous.exists()) deleteTree(previous);
        File[] temporary = parent.listFiles((file, name) -> name.startsWith("saves-restore-"));
        if (temporary != null) for (File file : temporary) deleteTree(file);
    }

    private static void deleteTree(File file) throws IOException {
        if (!file.exists()) return;
        File[] children = file.listFiles();
        if (children != null) for (File child : children) deleteTree(child);
        if (!file.delete()) throw new IOException("Could not clean up temporary save data.");
    }
}
