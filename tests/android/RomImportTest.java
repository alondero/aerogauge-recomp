package io.github.alondero.aerogaugerecomp;

import java.io.*;
import java.nio.file.*;
import java.util.Arrays;

/** Runs on a host JDK using the developer's ROM; never embeds game bytes. */
public final class RomImportTest {
    public static void main(String[] args) throws Exception {
        for (String path : args) checkRom(path);
    }

    private static void checkRom(String path) throws Exception {
        byte[] original = Files.readAllBytes(Path.of(path));
        Path temporary = Files.createTempDirectory("aero-rom-test-");
        try {
            for (int order : new int[]{1, 2, 4}) {
                byte[] converted = original.clone();
                for (int offset = 0; offset < converted.length; offset += order)
                    for (int i = 0; i < order/2; i++) {
                        byte b = converted[offset+i];
                        converted[offset+i] = converted[offset+order-1-i];
                        converted[offset+order-1-i] = b;
                    }
                RomImport.install(new ByteArrayInputStream(converted), temporary.toFile(), true);
                verify(original, temporary);
            }
            byte[] corrupt = original.clone(); corrupt[300] ^= 1;
            reject(new ByteArrayInputStream(corrupt), original, temporary);
            reject(new ByteArrayInputStream(new byte[64]), original, temporary);
            reject(new SequenceInputStream(new ByteArrayInputStream(original), new ByteArrayInputStream(new byte[]{1})), original, temporary);
            reject(new InputStream() { public int read() throws IOException { throw new IOException("interrupted provider"); } }, original, temporary);
            if (original[0x3e] == 'J') {
                try {
                    RomImport.install(new ByteArrayInputStream(original), temporary.toFile(), false);
                    throw new AssertionError("USA-only build accepted Japanese ROM");
                } catch (IOException correct) { verify(original, temporary); }
            }
            try (var entries = Files.list(temporary)) {
                if (entries.count() != 1) throw new AssertionError("Temporary import files leaked");
            }
            System.out.println("PASS " + path + ": all three byte orders; invalid imports preserve the installed ROM.");
        } finally {
            Files.deleteIfExists(temporary.resolve(RomImport.NAME));
            Files.delete(temporary);
        }
    }
    private static void verify(byte[] expected, Path directory) throws Exception {
        if (!Arrays.equals(expected, Files.readAllBytes(directory.resolve(RomImport.NAME)))) throw new AssertionError("Installed ROM differs");
    }
    private static void reject(InputStream input, byte[] expected, Path directory) throws Exception {
        try { RomImport.install(input, directory.toFile(), true); throw new AssertionError("Accepted invalid import"); }
        catch (IOException correct) { verify(expected, directory); }
    }
}
