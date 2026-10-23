#!/usr/bin/env python3
"""Rewrite every symbol a map overlay DEFINES to <mapname>_<symbol>.

Why this exists: on PSX only one map overlay is resident at a time, so every map
carries its own copy of the shared AI/particle/player code that it #includes.
Statically linking all 43 into one binary therefore collides on 500+ globals
(see pc_port/CMakeLists.txt, MAP_SOURCES). The DLL build sidesteps this by
keeping each map in its own module, but iOS refuses to load dylibs that are not
part of the signed bundle, so the static path needs the collisions resolved at
link time instead. Prefixing per map reproduces PSX overlay semantics exactly:
each overlay keeps a private copy of the shared code and data.

Only DEFINED, EXTERNAL symbols are renamed. Undefined references are left alone
so map code still binds to the one real engine copy, and local symbols are
already per-object.

Mach-O prefixes C symbols with an underscore and ELF/COFF-x86_64 do not, so the
new name has to be built around that prefix rather than pasted in front of the
raw symbol: _Foo must become _map0_s01_Foo, not map0_s01__Foo.

WebAssembly: llvm-objcopy refuses every symbol operation on wasm objects ("only
flags for section dumping, removal, and addition are supported"), so --ar
selects an in-process rewrite instead. A wasm object keeps symbol names in two
places only: the "linking" section's symbol table, and the import section,
which is where an undefined function/global/table/tag gets its name unless the
symbol carries WASM_SYM_EXPLICIT_NAME. Relocations refer to symbols by index,
so renaming both and re-encoding the two section sizes is the whole job.
"""
import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def run(cmd, cwd=None):
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=cwd)
    if r.returncode != 0:
        sys.exit("%s failed (%d):\n%s" % (Path(cmd[0]).name, r.returncode, r.stderr.strip()))
    return r.stdout


def _uleb(buf, pos):
    result = shift = 0
    while True:
        b = buf[pos]
        pos += 1
        result |= (b & 0x7F) << shift
        shift += 7
        if not b & 0x80:
            return result, pos


def _enc_uleb(v):
    out = bytearray()
    while True:
        b = v & 0x7F
        v >>= 7
        if v:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def _name(buf, pos):
    n, pos = _uleb(buf, pos)
    return bytes(buf[pos:pos + n]), pos + n


def _enc_name(s):
    return _enc_uleb(len(s)) + s


_SYM_FUNCTION, _SYM_DATA, _SYM_GLOBAL, _SYM_SECTION, _SYM_TAG, _SYM_TABLE = range(6)
_SYM_UNDEFINED = 0x10
_SYM_EXPLICIT_NAME = 0x40


def _rewrite_symtab(buf, renames):
    pos = 0
    count, pos = _uleb(buf, pos)
    out = bytearray(_enc_uleb(count))
    for _ in range(count):
        kind = buf[pos]
        pos += 1
        flags, pos = _uleb(buf, pos)
        out.append(kind)
        out += _enc_uleb(flags)
        if kind in (_SYM_FUNCTION, _SYM_GLOBAL, _SYM_TAG, _SYM_TABLE):
            index, pos = _uleb(buf, pos)
            out += _enc_uleb(index)
            if not (flags & _SYM_UNDEFINED) or (flags & _SYM_EXPLICIT_NAME):
                nm, pos = _name(buf, pos)
                out += _enc_name(renames.get(nm, nm))
        elif kind == _SYM_DATA:
            nm, pos = _name(buf, pos)
            out += _enc_name(renames.get(nm, nm))
            if not (flags & _SYM_UNDEFINED):
                start = pos
                for _f in range(3):  # segment index, offset, size
                    _v, pos = _uleb(buf, pos)
                out += buf[start:pos]
        elif kind == _SYM_SECTION:
            start = pos
            _v, pos = _uleb(buf, pos)
            out += buf[start:pos]
        else:
            sys.exit("unknown wasm symbol kind %d" % kind)
    return bytes(out)


def _rewrite_linking(payload, renames):
    pos = 0
    version, pos = _uleb(payload, pos)
    out = bytearray(_enc_uleb(version))
    while pos < len(payload):
        sub = payload[pos]
        pos += 1
        size, pos = _uleb(payload, pos)
        body = payload[pos:pos + size]
        pos += size
        if sub == 8:  # WASM_SYMBOL_TABLE
            body = _rewrite_symtab(body, renames)
        out.append(sub)
        out += _enc_uleb(len(body)) + body
    return bytes(out)


def _rewrite_imports(payload, renames):
    pos = 0
    count, pos = _uleb(payload, pos)
    out = bytearray(_enc_uleb(count))
    for _ in range(count):
        module, pos = _name(payload, pos)
        field, pos = _name(payload, pos)
        kind = payload[pos]
        start = pos
        pos += 1
        if kind == 0:      # func: type index
            _v, pos = _uleb(payload, pos)
        elif kind == 1:    # table: reftype + limits
            pos += 1
            lflags, pos = _uleb(payload, pos)
            _v, pos = _uleb(payload, pos)
            if lflags & 1:
                _v, pos = _uleb(payload, pos)
        elif kind == 2:    # memory: limits (flags may carry shared/64-bit bits)
            lflags, pos = _uleb(payload, pos)
            _v, pos = _uleb(payload, pos)
            if lflags & 1:
                _v, pos = _uleb(payload, pos)
        elif kind == 3:    # global: valtype + mutability
            pos += 2
        elif kind == 4:    # tag: attribute + type index
            pos += 1
            _v, pos = _uleb(payload, pos)
        else:
            sys.exit("unknown wasm import kind %d" % kind)
        if module == b"env":
            field = renames.get(field, field)
        out += _enc_name(module) + _enc_name(field) + payload[start:pos]
    return bytes(out)


def rewrite_wasm_object(path, renames):
    data = path.read_bytes()
    if data[:4] != b"\0asm":
        return False
    out = bytearray(data[:8])
    pos = 8
    while pos < len(data):
        sid = data[pos]
        size, body = _uleb(data, pos + 1)
        payload = data[body:body + size]
        pos = body + size
        if sid == 2:
            payload = _rewrite_imports(payload, renames)
        elif sid == 0:
            nm, npos = _name(payload, 0)
            if nm == b"linking":
                payload = payload[:npos] + _rewrite_linking(payload[npos:], renames)
        out.append(sid)
        out += _enc_uleb(len(payload)) + payload
    path.write_bytes(bytes(out))
    return True


def rewrite_wasm_archive(ar, archive, renames):
    members = [m for m in run([ar, "t", str(archive)]).splitlines() if m.strip()]
    if len(set(members)) != len(members):
        sys.exit("%s: duplicate archive member names; extraction would clobber" % archive)
    with tempfile.TemporaryDirectory() as tmp:
        run([ar, "x", str(archive.resolve())], cwd=tmp)
        for m in members:
            rewrite_wasm_object(Path(tmp) / m, renames)
        rebuilt = Path(tmp) / "rebuilt.a"
        run([ar, "rcs", str(rebuilt)] + [str(Path(tmp) / m) for m in members])
        shutil.copyfile(rebuilt, archive)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nm", required=True, help="llvm-nm executable")
    ap.add_argument("--objcopy", help="llvm-objcopy executable")
    ap.add_argument("--ar", help="llvm-ar executable; selects the wasm rewrite instead of objcopy")
    ap.add_argument("--archive", required=True, help="static library to rewrite in place")
    ap.add_argument("--map-name", required=True, help="overlay name, e.g. map0_s01")
    ap.add_argument("--sym-prefix", default="", help="platform C symbol prefix ('_' on Mach-O)")
    ap.add_argument("--stamp", help="file to touch on success, for the build graph")
    args = ap.parse_args()

    archive = Path(args.archive)
    if not archive.is_file():
        sys.exit("archive not found: %s" % archive)

    listing = run([args.nm, "--defined-only", "--extern-only",
                   "--format=just-symbols", str(archive)])

    p = args.sym_prefix
    renames = {}
    for line in listing.splitlines():
        sym = line.strip()
        # llvm-nm interleaves "member.o:" headers and blank lines when reading an
        # archive; neither is a symbol.
        if not sym or sym.endswith(":"):
            continue
        if p and not sym.startswith(p):
            # Not an ordinary C symbol under this ABI (assembler-local, etc.).
            continue
        bare = sym[len(p):] if p else sym
        # Skip compiler-generated symbols, which are not the overlay's own
        # globals. MinGW emits a `.refptr.<name>` COMDAT thunk holding the
        # address of each symbol it imports from the exe; renaming those
        # desyncs the thunk from the COMDAT the linker merges it by, and every
        # engine reference (g_SysWork, g_DeltaTime, ...) comes back undefined.
        # Each overlay keeping its own copy is correct and already how the DLL
        # build works. No ABI names a real C global starting with '.'.
        if not (bare[:1].isalpha() or bare[:1] == "_"):
            continue
        # Idempotent: re-running over an already-prefixed archive must be a no-op,
        # otherwise an incremental rebuild double-prefixes and the registry's
        # extern stops resolving.
        if bare.startswith(args.map_name + "_"):
            continue
        renames[sym] = "%s%s_%s" % (p, args.map_name, bare)

    if not renames:
        print("[prefix_map_syms] %s: nothing to rename (already prefixed?)" % args.map_name)
    else:
        listfile = archive.with_suffix(archive.suffix + ".renames")
        listfile.write_text(
            "".join("%s %s\n" % kv for kv in sorted(renames.items())), encoding="utf-8")
        if args.ar:
            rewrite_wasm_archive(args.ar, archive,
                                 {k.encode(): v.encode() for k, v in renames.items()})
        else:
            run([args.objcopy, "--redefine-syms=%s" % listfile, str(archive)])
        print("[prefix_map_syms] %s: renamed %d symbols" % (args.map_name, len(renames)))

    if args.stamp:
        Path(args.stamp).write_text("ok\n", encoding="utf-8")


if __name__ == "__main__":
    main()
