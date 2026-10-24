"""The companion screen's server (src/web.c), end to end on the host: the page, the icon, the manifest, the WebSocket
handshake and its frames (30 a second, the shape once), and an upload that lands in the Shapes folder.
    python3 test/web_test.py   (after test/build_host.sh; run by test/run_tests.sh)
"""
import socket, struct, time, os, sys, base64, hashlib, subprocess, shutil
# start the engine with the web server (skin_dump plays a note for a while)
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
tmp = os.path.join(root, "build", "host", "webshapes")
shutil.rmtree(tmp, ignore_errors=True); os.makedirs(tmp)
shutil.copy(os.path.join(root, "shapes", "crescent.png"), os.path.join(tmp, "crescent.png"))
env = dict(os.environ, VADER_WEB="1", VADER_BLOCKS="60000", VADER_DATA=tmp)
p = subprocess.Popen([os.path.join(root, "build/host/skin_dump"), "1", "2"], env=env, stdout=subprocess.DEVNULL, cwd=root)
time.sleep(0.8)
# the page
s = socket.create_connection(("127.0.0.1", 6731)); s.sendall(b"GET / HTTP/1.1\r\nHost: x\r\n\r\n"); page = b""
while True:
    d = s.recv(65536)
    if not d: break
    page += d
s.close()
assert b"200 OK" in page and b"<canvas" in page, page[:100]
print("page ok", len(page))
# the icon and manifest
for path in ("/icon.png", "/manifest.json"):
    s = socket.create_connection(("127.0.0.1", 6731)); s.sendall(b"GET " + path.encode() + b" HTTP/1.1\r\n\r\n"); d = b""
    while True:
        x = s.recv(65536)
        if not x: break
        d += x
    s.close(); assert b"200 OK" in d, path; print(path, "ok", len(d))
# the websocket: handshake, then frames
s = socket.create_connection(("127.0.0.1", 6731))
key = base64.b64encode(os.urandom(16)).decode()
s.sendall(("GET /ws HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n" % key).encode())
resp = b""
while b"\r\n\r\n" not in resp: resp += s.recv(4096)
head, rest = resp.split(b"\r\n\r\n", 1)
acc = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
assert b"101" in head and acc.encode() in head, head
print("handshake ok")
buf = rest; frames = {1: 0, 2: 0}; t0 = time.time()
while time.time() - t0 < 1.5:
    buf += s.recv(65536)
    while len(buf) >= 2:
        b1 = buf[1] & 0x7f; hl = 2
        if b1 == 126: ln = struct.unpack(">H", buf[2:4])[0]; hl = 4
        elif b1 == 127: ln = struct.unpack(">Q", buf[2:10])[0]; hl = 10
        else: ln = b1
        if len(buf) < hl + ln: break
        pay = buf[hl:hl + ln]; buf = buf[hl + ln:]
        frames[pay[0]] = frames.get(pay[0], 0) + 1
        if pay[0] == 2:
            n = struct.unpack("<I", pay[4:8])[0]; assert n == 256
        if pay[0] == 1:
            vals = struct.unpack("<14f", pay[4:4 + 56]); assert all(abs(v) < 1e6 for v in vals)
s.close()
print("frames in 1.5 s:", frames)
assert frames[2] >= 1 and 30 <= frames[1] <= 60, frames
# an upload: the picture back as a new shape, which the engine then loads
data = open(os.path.join(root, "shapes", "gear.png"), "rb").read()
s = socket.create_connection(("127.0.0.1", 6731))
s.sendall(("POST /upload?name=from%%20phone.png HTTP/1.1\r\nHost: x\r\nContent-Length: %d\r\n\r\n" % len(data)).encode() + data)
d = b""
while True:
    x = s.recv(4096)
    if not x: break
    d += x
s.close()
assert b"200 OK" in d and b"loaded" in d, d
time.sleep(1.0)
assert os.path.exists(os.path.join(tmp, "from_phone.png")), os.listdir(tmp)
print("upload ok:", os.listdir(tmp))
p.wait(timeout=60)
print("engine exit", p.returncode)
