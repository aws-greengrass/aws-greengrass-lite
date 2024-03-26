#!/usr/bin/env python
import base64
import hashlib
import json
import os
import shutil
import struct
import tempfile
import glob
import pathlib


class narWriter:

    def __init__(self, write):
        self._write = write

    def __call__(self, path):
        self._item(b'nix-archive-1')
        self._serialize(path)

    def _serialize(self, path):
        self._item(b'(')
        if path.is_symlink():
            self._item(b'type')
            self._item(b'symlink')
            self._item(b'target')
            self._item(path.readlink())
        elif path.is_dir():
            self._item(b'type')
            self._item(b'directory')
            files = [f for f in os.listdir(path)]
            files.sort()
            for f in files:
                self._serializeEntry(f.encode(), path / f)
        else:
            self._item(b'type')
            self._item(b'regular')
            if os.access(path, os.X_OK):
                self._item(b'executable')
                self._item(b'')
            self._item(b'contents')
            with open(path, 'rb') as f:
                self._item(f.read())
        self._item(b')')

    def _serializeEntry(self, name, path):
        self._item(b'entry')
        self._item(b'(')
        self._item(b'name')
        self._item(name)
        self._item(b'node')
        self._serialize(path)
        self._item(b')')

    def _item(self, s):
        self._write(struct.pack('<Q', len(s)))
        self._write(s)
        extra_bytes = (8 - len(s) % 8) % 8
        self._write(b'\0' * extra_bytes)


def getHash(path):
    hasher = hashlib.sha256()
    if path.is_dir():
        narWriter(hasher.update)(path)
    else:
        with open(path, 'rb') as f:
            hasher.update(f.read())
    return (b'sha256-' + base64.b64encode(hasher.digest())).decode('utf-8')


def getHashForDep(url, rev):
    prevdir = os.getcwd()
    with tempfile.TemporaryDirectory() as tmpdir:
        os.chdir(tmpdir)
        os.system(f"git clone --no-checkout --filter=tree:0 {url} .")
        os.system(f"git checkout {rev}")
        os.system("git submodule update --init --recursive --filter=tree:0")
        shutil.rmtree(".git")
        for f in glob.glob("**/.git", recursive=True):
            os.remove(f)
        os.chdir(prevdir)
        return getHash(pathlib.Path(tmpdir))


with open("dependencies.json", "rb") as file:
    deps = json.loads(file.read())

for n, v in deps.items():
    v["hash"] = getHashForDep(v["url"], v["rev"])

with open("dependencies.json", "w") as file:
    json.dump(deps, file, indent=2)
    file.write("\n")
