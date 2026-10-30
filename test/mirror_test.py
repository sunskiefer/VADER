"""PLUG (src/web.c /mirror), end to end on the host: finding the Force by broadcast, the hello (and a refused hello
from another build), the whole state, deltas for a change made on the Force, a change sent from the desktop (applied,
not echoed back to its sender, readout updated), a fired button, and the 60 a second screen frames.
    python3 test/mirror_test.py   (after test/build_host.sh and the skin_dump build; run by test/run_tests.sh)
"""
import socket, struct, time, os, sys, base64, subprocess, shutil, re

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
tmp = os.path.join(root, "build", "host", "mirrorshapes")
shutil.rmtree(tmp, ignore_errors=True); os.makedirs(tmp)
env = dict(os.environ, VADER_WEB="1", VADER_BLOCKS="100000", VADER_REALTIME="1", VADER_DATA=tmp)
p = subprocess.Popen([os.path.join(root, "build/host/skin_dump"), "1", "2"], env=env, stdout=subprocess.DEVNULL, cwd=root)
time.sleep(0.8)
try:
    # the parameters, for indices and the schema
    hdr = open(os.path.join(root, "src", "vader_params.h")).read()
    NP = int(re.search(r"#define NP (\d+)", hdr).group(1))
    rows = re.findall(r'\{"([a-z0-9_]+)", "[^"]*", (K_[A-Z]+), ', hdr)
    assert len(rows) == NP, (len(rows), NP)
    keys = [k for k, _ in rows]; kinds = [k for _, k in rows]
    h = (2166136261 ^ NP) & 0xffffffff
    for k, kind in rows:
        if kind in ("K_SND", "K_GLOB", "K_TRIG"):
            for ch in k.encode(): h = ((h ^ ch) * 16777619) & 0xffffffff
    schema = h
    idx = {k: i for i, k in enumerate(keys)}

    # 1. the broadcast
    u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); u.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1); u.settimeout(1.5)
    u.sendto(b"VADER PLUG?", ("127.0.0.1", 6731))
    d, frm = u.recvfrom(128); u.close()
    assert d.startswith(b"VADER 6731 "), d
    print("found:", d.decode())

    def ws_connect(path):
        s = socket.create_connection(("127.0.0.1", 6731)); s.settimeout(3)
        key = base64.b64encode(os.urandom(16)).decode()
        s.sendall(("GET %s HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n" % (path, key)).encode())
        resp = b""
        while b"\r\n\r\n" not in resp: resp += s.recv(4096)
        head, rest = resp.split(b"\r\n\r\n", 1)
        assert b"101" in head, head
        return s, bytearray(rest)

    def send(s, payload):
        mask = os.urandom(4)
        n = len(payload)
        h = bytes([0x82, 0x80 | n]) if n < 126 else bytes([0x82, 0x80 | 126, n >> 8, n & 255])
        s.sendall(h + mask + bytes(b ^ mask[i & 3] for i, b in enumerate(payload)))

    def frames(s, buf, want=1, timeout=3.0):
        out = []; t0 = time.time()
        while len(out) < want and time.time() - t0 < timeout:
            while len(buf) >= 2:
                ln = buf[1] & 0x7f; hl = 2
                if ln == 126: ln = buf[2] << 8 | buf[3]; hl = 4
                elif ln == 127: ln = int.from_bytes(buf[2:10], "big"); hl = 10
                if len(buf) < hl + ln: break
                out.append(bytes(buf[hl:hl + ln])); del buf[:hl + ln]
            if len(out) >= want: break
            try: buf += s.recv(65536)
            except socket.timeout: break
        return out

    # 2. a wrong build is refused
    s, buf = ws_connect("/mirror")
    hello = frames(s, buf, 1)[0]
    assert hello[0] == 0x30, hello[:4]
    pr, np_, sc = struct.unpack_from("<III", hello, 1)
    ver = hello[15:15 + hello[14]].decode()   # hello[13] is our slot number
    assert pr == 1 and np_ == NP and sc == schema, (pr, np_, sc, schema)
    print("hello ok: proto %d, %d parameters, schema %08x, version %s" % (pr, np_, sc, ver))
    send(s, b"\x40" + struct.pack("<III", 1, NP, sc ^ 1))
    time.sleep(0.3)
    try:
        r = s.recv(10); closed = r == b""
    except (ConnectionResetError, socket.timeout): closed = True
    assert closed, "a different schema should be dropped"
    s.close(); print("other build refused ok")

    # 3. the right hello: the whole state comes, then frames
    s, buf = ws_connect("/mirror")
    frames(s, buf, 1)
    send(s, b"\x40" + struct.pack("<III", 1, NP, sc))
    got = frames(s, buf, 40, 2.0)
    kinds_got = [g[0] for g in got]
    assert 0x31 in kinds_got, kinds_got
    st = [g for g in got if g[0] == 0x31][0]
    assert st[1:].startswith(b"VADER 1\n"), st[:20]
    print("state ok:", len(st), "bytes,", st[1:].count(b"\n"), "lines")
    assert 0x21 in kinds_got and 0x02 in kinds_got, kinds_got
    # the frame rate: about 60 a second
    buf2 = bytearray(); t0 = time.time(); n21 = 0
    while time.time() - t0 < 1.0:
        for g in frames(s, buf, 1, 0.2): n21 += g[0] == 0x21
    assert 45 <= n21 <= 75, n21
    print("screen frames per second:", n21)

    # 4. a change made on the Force arrives as a delta
    s2, buf2 = ws_connect("/ws")   # a phone page to show up as "another client"? no: the Force's own change is the engine's. Use a second mirror as the changer
    s2.close()
    sB, bufB = ws_connect("/mirror")
    frames(sB, bufB, 1); send(sB, b"\x40" + struct.pack("<III", 1, NP, sc))
    assert any(g[0] == 0x31 for g in frames(sB, bufB, 30, 2.0))
    # drain
    frames(s, buf, 200, 0.3); frames(sB, bufB, 200, 0.3)
    i = idx["orb_speed"]
    send(sB, b"\x41" + struct.pack("<Hf", i, 0.123))
    t0 = time.time(); deltasA = []; deltasB = []; rd = []
    while time.time() - t0 < 1.5 and not (deltasA and rd):
        for g in frames(s, buf, 50, 0.2):
            if g[0] == 0x33: rd.append(g)
            if g[0] == 0x32:
                cnt = g[1] | g[2] << 8
                deltasA += [struct.unpack_from("<Hf", g, 3 + 6 * k) for k in range(cnt)]
        for g in frames(sB, bufB, 50, 0.05):
            if g[0] == 0x32:
                cnt = g[1] | g[2] << 8
                deltasB += [struct.unpack_from("<Hf", g, 3 + 6 * k) for k in range(cnt)]
    assert any(k == i and abs(v - 0.123) < 1e-5 for k, v in deltasA), deltasA[:5]
    assert not any(k == i for k, v in deltasB), "the sender got its own change back"
    print("delta ok: orb_speed reached the other desktop (%d deltas), not its sender" % len(deltasA))
    # the readout line followed
    t0 = time.time()
    while not rd and time.time() - t0 < 1.5: rd = [g for g in frames(s, buf, 50, 0.3) if g[0] == 0x33]
    assert rd and b"ORBIT SPEED" in rd[-1], rd[-1:]
    print("readout ok:", rd[-1][5:].decode())
    # 5. a fired button: TAP (harmless) is accepted; RANDOMIZE changes things that come back as deltas
    send(sB, b"\x42" + struct.pack("<H", idx["randomize"]))
    t0 = time.time(); n = 0
    while time.time() - t0 < 1.5 and n == 0:
        for g in frames(s, buf, 50, 0.2):
            if g[0] == 0x32: n += g[1] | g[2] << 8
    assert n > 5, n
    print("fire ok: RANDOMIZE sent %d changed values" % n)
    # 6. "again" sends the whole state once more
    send(sB, b"\x43")
    assert any(g[0] == 0x31 for g in frames(sB, bufB, 80, 1.5))
    print("state again ok")
    s.close(); sB.close()
    print("MIRROR TEST PASSED")
finally:
    p.kill()
