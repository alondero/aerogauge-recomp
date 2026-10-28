"""Run the native mod-manager test with disposable, real ZIP packages."""

from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile


def main():
    with tempfile.TemporaryDirectory(prefix="aero-texture-packs-") as temporary:
        root = Path(temporary)
        packs = root / "packs-\u65e5\u672c with spaces"
        packs.mkdir()
        for name in ("high.rtz", "low.rtz", "missing-manifest.nrm"):
            with zipfile.ZipFile(packs / name, "w", zipfile.ZIP_DEFLATED) as archive:
                archive.writestr("rt64.json", '{"textures": [], "config": {"hashVersion": 3}}')
        (packs / "broken.rtz").write_bytes(b"not a zip archive")
        return subprocess.run([sys.argv[1], str(root), *sys.argv[2:]], check=False).returncode


if __name__ == "__main__":
    sys.exit(main())
