package io.github.alondero.aerogaugerecomp;

import java.io.*;
import java.nio.file.*;
import java.security.MessageDigest;

/** A bounded, validated import. A failed import never replaces a working ROM. */
final class RomImport {
    static final String NAME = "AeroGauge (USA).z64";
    static final int SIZE = 8 * 1024 * 1024;
    static final String SHA256 = "2cc529109b11b00289d87f693a40591ef260d1dc7c1129113966ba6ddb1be4a5";

    static void install(InputStream input, File directory) throws Exception {
        if (input == null) throw new IOException("The selected file could not be opened. Try a local copy.");
        byte[] bytes = new byte[SIZE];
        int offset = 0, count;
        while (offset < SIZE && (count = input.read(bytes, offset, SIZE - offset)) != -1) offset += count;
        if (offset != SIZE || input.read() != -1)
            throw new IOException("Choose an uncompressed 8 MiB AeroGauge USA ROM (.z64, .v64 or .n64).");
        if ((bytes[0] & 255) == 0x37) {
            for (int i = 0; i < SIZE; i += 2) {
                byte a = bytes[i]; bytes[i] = bytes[i+1]; bytes[i+1] = a;
            }
        } else if ((bytes[0] & 255) == 0x40) {
            for (int i = 0; i < SIZE; i += 4) {
                byte a = bytes[i], b = bytes[i+1];
                bytes[i] = bytes[i+3]; bytes[i+1] = bytes[i+2]; bytes[i+2] = b; bytes[i+3] = a;
            }
        }
        StringBuilder hash = new StringBuilder();
        for (byte b : MessageDigest.getInstance("SHA-256").digest(bytes))
            hash.append(String.format(java.util.Locale.ROOT, "%02x", b & 255));
        if (!SHA256.equals(hash.toString()))
            throw new IOException("This ROM does not match AeroGauge USA. Other regions and modified ROMs are not supported.");
        File temporary = File.createTempFile("rom-import-", ".tmp", directory);
        try {
            try (FileOutputStream output = new FileOutputStream(temporary)) {
                output.write(bytes);
                output.getFD().sync();
            }
            Files.move(temporary.toPath(), new File(directory, NAME).toPath(),
                StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
        } finally { temporary.delete(); }
    }
}
