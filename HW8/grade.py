#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Bộ tự chấm IT4062 - Bài tập tuần 8: Chat TCP đa luồng + mã hóa AES-256-CBC.

Cách dùng:
    python3 grade.py --zip HotenSV_MSSV_HW8.zip     # chấm file zip nộp bài
    python3 grade.py --dir thu_muc_bai_lam          # chấm một thư mục có Makefile

Yêu cầu: python3 >= 3.8, gcc, make, libssl-dev (để biên dịch) và lệnh `openssl`
(bộ chấm dùng openssl để mã hóa/giải mã AES-256-CBC khi nói chuyện với server).
Bộ chấm chỉ dùng thư viện chuẩn của Python. Mọi thao tác đều có timeout.

Bộ chấm KHÔNG sửa bài làm: nó sao chép bài sang thư mục tạm, biên dịch và chạy ở đó.
"""
import argparse
import hashlib
import os
import queue
import random
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import zipfile

# ---------------------------------------------------------------------------
# Hằng số giao thức (phải khớp với common.h và crypto.c)
# ---------------------------------------------------------------------------
AES_KEY = b"IT4062_HW8_AES256_KEY_DEMO_2026!"
KEY_HEX = AES_KEY.hex()
MAX_FRAME = 12 * 1024 * 1024        # LEN tối đa của frame (byte)
MAX_FILE = 10 * 1024 * 1024         # kích thước file tối đa (byte)
ACCOUNT_FILE = "account.txt"
TIMEOUT = 5.0                       # giây, cho mỗi lần chờ
PW = "pw12345"


class Fail(Exception):
    """Test không đạt. Thông điệp mô tả lỗi để sinh viên tự sửa."""


class Closed(Fail):
    """Server đã đóng kết nối khi đang chờ phản hồi."""


# ---------------------------------------------------------------------------
# Mã hóa / giải mã bằng openssl (AES-256-CBC, IV ngẫu nhiên, PKCS#7)
# ---------------------------------------------------------------------------
def aes_encrypt(plain):
    iv = os.urandom(16)
    r = subprocess.run(
        ["openssl", "enc", "-aes-256-cbc", "-K", KEY_HEX, "-iv", iv.hex()],
        input=plain, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
    return iv + r.stdout


def aes_decrypt(payload):
    if len(payload) < 32 or (len(payload) - 16) % 16 != 0:
        raise ValueError("độ dài ciphertext không hợp lệ")
    r = subprocess.run(
        ["openssl", "enc", "-d", "-aes-256-cbc", "-K", KEY_HEX, "-iv", payload[:16].hex()],
        input=payload[16:], stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
    return r.stdout


def sha256_hex(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


# ---------------------------------------------------------------------------
# Frame và kết nối TCP
# ---------------------------------------------------------------------------
class Frame:
    """Một frame nhận từ server: bản mã thô, bản rõ, dòng lệnh (head) và thân (body)."""

    def __init__(self, raw, plain):
        self.raw = raw
        self.plain = plain
        if plain is None:
            self.head, self.body = "<khong giai ma duoc>", b""
            return
        nl = plain.find(b"\n")
        if nl < 0:
            self.head, self.body = plain.decode("utf-8", "replace"), b""
        else:
            self.head = plain[:nl].decode("utf-8", "replace")
            self.body = plain[nl + 1:]


def is_ok(head):
    return re.match(r"OK(\s|$)", head) is not None


def recv_exact(sock, n):
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(min(1 << 16, n - len(buf)))
        if not chunk:
            raise EOFError("đóng kết nối")
        buf += chunk
    return bytes(buf)


def take(q, timeout, what):
    try:
        item = q.get(timeout=timeout)
    except queue.Empty:
        raise Fail(f"quá {timeout:.0f} giây mà không nhận được {what}")
    if item is None:
        q.put(None)  # giữ trạng thái "đã đóng" cho các lần lấy sau
        raise Closed(f"server đã đóng kết nối khi đang chờ {what}")
    return item


class Conn:
    """Kết nối TCP tới server. Luồng đọc nền tách phản hồi (OK/ERR) và tin chuyển tiếp (MSG/FILE)."""

    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=TIMEOUT)
        self.sock.settimeout(None)
        self.resp = queue.Queue()
        self.relay = queue.Queue()
        self._wlock = threading.Lock()
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        try:
            while True:
                (n,) = struct.unpack(">I", recv_exact(self.sock, 4))
                raw = recv_exact(self.sock, n)
                try:
                    plain = aes_decrypt(raw)
                except Exception:
                    plain = None
                fr = Frame(raw, plain)
                if fr.head.split(" ", 1)[0] in ("MSG", "FILE"):
                    self.relay.put(fr)
                else:
                    self.resp.put(fr)
        except Exception:
            self.resp.put(None)
            self.relay.put(None)

    def send_bytes(self, data):
        with self._wlock:
            self.sock.sendall(data)

    def send_frame_raw(self, payload):
        """Gửi một frame có IV||CIPHERTEXT tùy ý (đã được đóng gói LEN)."""
        self.send_bytes(struct.pack(">I", len(payload)) + payload)

    def req_plain(self, plain):
        self.send_frame_raw(aes_encrypt(plain))
        return self.next_resp()

    def req(self, header, body=b""):
        return self.req_plain(header.encode("utf-8") + b"\n" + body)

    def next_resp(self, timeout=TIMEOUT):
        return take(self.resp, timeout, "phản hồi của server")

    def next_relay(self, timeout=TIMEOUT):
        return take(self.relay, timeout, "tin chuyển tiếp từ server")

    def assert_no_relay(self, wait=0.6):
        try:
            item = self.relay.get(timeout=wait)
        except queue.Empty:
            return
        if item is not None:
            raise Fail(f"không được chuyển tiếp nhưng người nhận nhận được '{item.head}'")

    def abort(self):
        """Ngắt kết nối đột ngột bằng RST (không gửi QUIT)."""
        try:
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
            self.sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        try:
            self.sock.close()
        except OSError:
            pass

    def close(self):
        try:
            self.sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        try:
            self.sock.close()
        except OSError:
            pass


# ---------------------------------------------------------------------------
# Tiến trình server / client
# ---------------------------------------------------------------------------
class ClientProc:
    """Chạy ./client với stdin/stdout là pipe. Đọc stdout ở luồng nền."""

    def __init__(self, binary, port, cwd):
        self.cwd = cwd
        self.p = subprocess.Popen(
            [binary, "127.0.0.1", str(port)], cwd=cwd,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.lines = []
        self._cv = threading.Condition()
        threading.Thread(target=self._pump, daemon=True).start()

    def _pump(self):
        for raw in iter(self.p.stdout.readline, b""):
            with self._cv:
                self.lines.append(raw.decode("utf-8", "replace").rstrip("\r\n"))
                self._cv.notify_all()
        with self._cv:
            self._cv.notify_all()

    def mark(self):
        with self._cv:
            return len(self.lines)

    def send(self, line):
        try:
            self.p.stdin.write((line + "\n").encode("utf-8"))
            self.p.stdin.flush()
        except OSError:
            raise Fail(f"client đã thoát, không nhận được lệnh '{line}'")

    def expect(self, pattern, after=0, timeout=TIMEOUT):
        """Chờ một dòng stdout khớp regex, tìm từ chỉ số `after` trở đi."""
        rx = re.compile(pattern)
        deadline = time.monotonic() + timeout
        idx = after
        with self._cv:
            while True:
                while idx < len(self.lines):
                    if rx.search(self.lines[idx]):
                        return self.lines[idx]
                    idx += 1
                left = deadline - time.monotonic()
                if left <= 0:
                    tail = "\n".join("      | " + x for x in self.lines[-6:])
                    raise Fail(f"client không in dòng khớp /{pattern}/ sau {timeout:.0f}s."
                               f" Các dòng gần nhất:\n{tail}")
                self._cv.wait(left)

    def wait_exit(self, timeout=5.0):
        try:
            return self.p.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            raise Fail("client không thoát sau lệnh /quit")

    def kill(self):
        if self.p.poll() is None:
            self.p.kill()
            self.p.wait()


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Env:
    """Môi trường riêng cho mỗi test: thư mục tạm, server, các kết nối và client."""

    def __init__(self, bindir):
        self.bindir = bindir
        self.root = tempfile.mkdtemp(prefix="hw8_test_")
        self.wd = os.path.join(self.root, "server")
        os.makedirs(self.wd)
        self.port = None
        self.proc = None
        self._logf = None
        self._conns = []
        self._clients = []

    def bin(self, name):
        return os.path.join(self.bindir, name)

    def start_server(self):
        self.port = free_port()
        self._logf = open(os.path.join(self.root, "server.log"), "wb")
        self.proc = subprocess.Popen([self.bin("server"), str(self.port)], cwd=self.wd,
                                     stdout=self._logf, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 3.0
        while True:
            if self.proc.poll() is not None:
                raise Fail(f"./server thoát ngay khi khởi động (mã {self.proc.returncode})")
            try:
                socket.create_connection(("127.0.0.1", self.port), timeout=0.3).close()
                return self.port
            except OSError:
                if time.monotonic() > deadline:
                    raise Fail(f"server không lắng nghe được trên cổng {self.port} sau 3 giây")
                time.sleep(0.05)

    def alive(self):
        return self.proc is not None and self.proc.poll() is None

    def conn(self):
        try:
            c = Conn(self.port)
        except OSError as e:
            raise Fail(f"không kết nối được tới server: {e}")
        self._conns.append(c)
        return c

    def client(self, name):
        d = os.path.join(self.root, name)
        os.makedirs(d, exist_ok=True)
        cl = ClientProc(self.bin("client"), self.port, d)
        self._clients.append(cl)
        return cl

    def accounts(self):
        path = os.path.join(self.wd, ACCOUNT_FILE)
        if not os.path.exists(path):
            return []
        with open(path, encoding="utf-8", errors="replace") as f:
            return [tuple(line.split()[:2]) for line in f if line.strip()]

    def server_log(self, n=8):
        try:
            with open(os.path.join(self.root, "server.log"), encoding="utf-8", errors="replace") as f:
                return "\n".join(f.read().splitlines()[-n:])
        except OSError:
            return ""

    def close(self):
        for c in self._conns:
            c.close()
        for cl in self._clients:
            cl.kill()
        if self.proc is not None and self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait()
        if self._logf is not None:
            self._logf.close()
        shutil.rmtree(self.root, ignore_errors=True)


def run_quick(env, argv, what, timeout=3.0):
    """Chạy chương trình, bắt buộc thoát với mã khác 0 và không được treo."""
    try:
        r = subprocess.run(argv, cwd=env.wd, stdin=subprocess.DEVNULL,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
    except subprocess.TimeoutExpired:
        raise Fail(f"`{what}` bị treo (không thoát sau {timeout:.0f} giây)")
    if r.returncode == 0:
        raise Fail(f"`{what}` phải thoát với mã lỗi khác 0 nhưng lại thoát với mã 0")


# ---------------------------------------------------------------------------
# Tiện ích cho test
# ---------------------------------------------------------------------------
def ok(fr, what):
    if not is_ok(fr.head):
        raise Fail(f"{what}: mong đợi 'OK ...', nhận '{fr.head}'")
    return fr


def err(fr, code, what):
    if not re.match(rf"ERR {code}(\s|$)", fr.head):
        raise Fail(f"{what}: mong đợi 'ERR {code} ...', nhận '{fr.head}'")
    return fr


def new_user(env, name, pw=PW):
    """Mở một kết nối mới, REGISTER rồi LOGIN bằng chính kết nối đó."""
    c = env.conn()
    ok(c.req(f"REGISTER {name} {pw}"), f"REGISTER {name}")
    ok(c.req(f"LOGIN {name} {pw}"), f"LOGIN {name}")
    return c


def users_in_list(fr):
    return set(fr.head.split()[2:])


def wait_offline(watcher, name, timeout=3.0):
    deadline = time.monotonic() + timeout
    while True:
        if name not in users_in_list(watcher.req("LIST")):
            return
        if time.monotonic() > deadline:
            raise Fail(f"server vẫn coi '{name}' đang online sau khi client của '{name}' ngắt kết nối")
        time.sleep(0.1)


def client_session(env, name, user):
    """Client dòng lệnh: /register rồi /login."""
    cl = env.client(name)
    m = cl.mark()
    cl.send(f"/register {user} {PW}")
    cl.expect(r"^\[OK\] ", after=m)
    m = cl.mark()
    cl.send(f"/login {user} {PW}")
    cl.expect(r"^\[OK\] ", after=m)
    return cl


def parallel(fn, items):
    """Chạy fn(i, x) song song cho mỗi phần tử; trả về danh sách kết quả theo thứ tự."""
    items = list(items)
    out = [None] * len(items)
    errs = []

    def run(i, x):
        try:
            out[i] = fn(i, x)
        except Exception as e:  # noqa: BLE001 - gom lỗi để báo lại
            errs.append(e)

    ths = [threading.Thread(target=run, args=(i, x), daemon=True) for i, x in enumerate(items)]
    for t in ths:
        t.start()
    for t in ths:
        t.join(60)
    if errs:
        raise errs[0]
    return out


# ---------------------------------------------------------------------------
# Danh sách test: (mã, điểm, mô tả, hàm)
# ---------------------------------------------------------------------------
TESTS = []


def test(tid, pts, title):
    def deco(fn):
        TESTS.append((tid, pts, title, fn))
        return fn
    return deco


@test("B1", 0.25, "./server không tham số, cổng sai (99999, abc) → thoát mã khác 0")
def t_b1(env):
    for args in ([], ["99999"], ["abc"]):
        run_quick(env, [env.bin("server"), *args], "./server " + " ".join(args))


@test("B2", 0.25, "./client thiếu tham số, hoặc cổng đóng → thoát mã khác 0, không treo")
def t_b2(env):
    run_quick(env, [env.bin("client")], "./client")
    run_quick(env, [env.bin("client"), "127.0.0.1", str(free_port())],
              "./client 127.0.0.1 <cổng không có server>", timeout=5)


@test("C1", 0.30, "REGISTER hợp lệ → OK; account.txt có dòng 'username sha256(password)'")
def t_c1(env):
    env.start_server()
    c = env.conn()
    ok(c.req("REGISTER alice secret123"), "REGISTER alice")
    acc = env.accounts()
    if ("alice", sha256_hex("secret123")) not in acc:
        raise Fail(f"account.txt không đúng định dạng 'alice <sha256(pass)>'. Nội dung hiện có: {acc}")


@test("C2", 0.25, "REGISTER trùng username → ERR 409 và không ghi thêm dòng")
def t_c2(env):
    env.start_server()
    ok(env.conn().req("REGISTER alice secret123"), "REGISTER alice lần 1")
    err(env.conn().req("REGISTER alice khac12345"), 409, "REGISTER alice lần 2")
    if len(env.accounts()) != 1:
        raise Fail(f"account.txt phải có đúng 1 dòng, hiện có {len(env.accounts())} dòng")


@test("C3", 0.30, "LOGIN đúng → OK; sai mật khẩu hoặc tài khoản không tồn tại → ERR 401")
def t_c3(env):
    env.start_server()
    ok(env.conn().req("REGISTER alice secret123"), "REGISTER alice")
    c = env.conn()
    err(c.req("LOGIN alice sai12345"), 401, "LOGIN sai mật khẩu")
    err(c.req("LOGIN ghost secret123"), 401, "LOGIN tài khoản không tồn tại")
    ok(c.req("LOGIN alice secret123"), "LOGIN đúng (sau khi đã thử sai)")


@test("C4", 0.25, "LIST / MSG / FILE khi chưa đăng nhập → ERR 403")
def t_c4(env):
    env.start_server()
    c = env.conn()
    err(c.req("LIST"), 403, "LIST")
    err(c.req("MSG bob xin chao"), 403, "MSG")
    err(c.req("FILE bob a.png 3", b"abc"), 403, "FILE")


@test("C5", 0.20, "Sai cú pháp / tham số (user ngắn, user có ký tự lạ, pass ngắn, thừa/thiếu tham số) → ERR 400")
def t_c5(env):
    env.start_server()
    c = env.conn()
    cases = [
        "REGISTER ab secret1",
        "REGISTER bad-name secret1",
        "REGISTER carol",
        "REGISTER carol pw",
        "REGISTER carol pw12345 extra",
        "REGISTER " + "a" * 25 + " pw12345",
        "LOGIN",
        "FOO bar",
    ]
    for line in cases:
        err(c.req(line), 400, line)
    if env.accounts():
        raise Fail("account.txt đã bị ghi dù các lệnh đều sai cú pháp")


@test("C6", 0.20, "Mỗi tài khoản chỉ đăng nhập một phiên; QUIT giải phóng phiên")
def t_c6(env):
    env.start_server()
    ok(env.conn().req("REGISTER alice secret123"), "REGISTER alice")
    c1 = env.conn()
    ok(c1.req("LOGIN alice secret123"), "LOGIN alice (kết nối 1)")
    c2 = env.conn()
    err(c2.req("LOGIN alice secret123"), 409, "LOGIN alice khi đang online (kết nối 2)")
    ok(c1.req("QUIT"), "QUIT")
    deadline = time.monotonic() + 3
    while True:
        fr = c2.req("LOGIN alice secret123")
        if is_ok(fr.head):
            return
        if not re.match(r"ERR 409", fr.head) or time.monotonic() > deadline:
            raise Fail(f"sau QUIT, LOGIN alice vẫn không được chấp nhận: '{fr.head}'")
        time.sleep(0.1)


@test("D1", 0.75, "50 vòng: 10 kết nối đồng thời cùng REGISTER một username → mỗi vòng đúng 1 OK + 9 ERR 409")
def t_d1(env):
    env.start_server()
    conns = [env.conn() for _ in range(10)]
    rounds = 50
    for r in range(rounds):
        name = f"racer{r}"
        barrier = threading.Barrier(10)

        def reg(i, _, name=name, barrier=barrier):
            barrier.wait(10)
            return conns[i].req(f"REGISTER {name} pw12345")

        frames = parallel(reg, range(10))
        oks = [f for f in frames if is_ok(f.head)]
        conflicts = [f for f in frames if re.match(r"ERR 409", f.head)]
        if len(oks) != 1 or len(conflicts) != 9:
            heads = sorted(set(f.head for f in frames))
            raise Fail(f"vòng {r + 1}/{rounds}: mong đợi 1 OK + 9 ERR 409 (mutex phải bảo vệ"
                       f" account.txt), nhận {len(oks)} OK + {len(conflicts)} ERR 409. Phản hồi: {heads}")
    acc = env.accounts()
    names = sorted(a[0] for a in acc)
    if names != sorted(f"racer{r}" for r in range(rounds)):
        raise Fail(f"account.txt phải có đúng {rounds} dòng, mỗi user một lần; hiện có {len(acc)} dòng")


@test("D2", 0.75, "5 client đồng thời: đăng ký/đăng nhập, LIST thấy đủ, mỗi người nhận đúng tin của người trước")
def t_d2(env):
    env.start_server()
    names = [f"user{i}" for i in range(5)]
    conns = parallel(lambda i, n: new_user(env, n), names)
    for i, c in enumerate(conns):
        fr = ok(c.req("LIST"), f"LIST của {names[i]}")
        missing = set(names) - users_in_list(fr)
        if missing:
            raise Fail(f"LIST của {names[i]} thiếu các user đang online: {sorted(missing)}")

    def send_ring(i, _):
        return ok(conns[i].req(f"MSG {names[(i + 1) % 5]} hello-{i}"), f"MSG của {names[i]}")

    parallel(send_ring, range(5))
    for i, c in enumerate(conns):
        src = (i - 1) % 5
        fr = c.next_relay()
        if fr.head != f"MSG {names[src]} hello-{src}":
            raise Fail(f"{names[i]} nhận '{fr.head}', mong đợi 'MSG {names[src]} hello-{src}'")
    if len(env.accounts()) != 5:
        raise Fail(f"account.txt phải có 5 dòng, hiện có {len(env.accounts())} dòng")


@test("E1", 1.00, "Chat văn bản: A gửi MSG (có khoảng trắng, UTF-8) → A nhận 'OK SENT', B nhận 'MSG <A> <nội dung>'")
def t_e1(env):
    env.start_server()
    alice = new_user(env, "alice")
    bob = new_user(env, "bob")
    text = "Xin chào Bob! 123 abc"
    ok(alice.req(f"MSG bob {text}"), "MSG alice → bob")
    fr = bob.next_relay()
    if fr.head != f"MSG alice {text}":
        raise Fail(f"bob nhận '{fr.head}', mong đợi 'MSG alice {text}'")


@test("E2", 0.50, "100 tin liên tiếp từ A tới B: không mất, không trùng, đúng thứ tự")
def t_e2(env):
    env.start_server()
    alice = new_user(env, "alice")
    bob = new_user(env, "bob")
    for i in range(100):
        ok(alice.req(f"MSG bob msg-{i:04d}"), f"MSG số {i}")
    for i in range(100):
        fr = bob.next_relay()
        if fr.head != f"MSG alice msg-{i:04d}":
            raise Fail(f"tin thứ {i}: bob nhận '{fr.head}', mong đợi 'MSG alice msg-{i:04d}'")


@test("F1", 0.60, "Mã hóa: bản rõ không xuất hiện trên đường truyền; gửi lại cùng nội dung → bản mã khác (IV ngẫu nhiên)")
def t_f1(env):
    env.start_server()
    alice = new_user(env, "alice")
    bob = new_user(env, "bob")
    secret = "BI_MAT_" + str(random.randint(10 ** 8, 10 ** 9))
    r = alice.req(f"MSG bob {secret}")
    ok(r, "MSG")
    if b"OK SENT" in r.raw:
        raise Fail("phản hồi gửi cho người gửi là bản rõ (không mã hóa)")
    f1 = bob.next_relay()
    if secret.encode() in f1.raw:
        raise Fail("frame gửi cho người nhận chứa nguyên văn nội dung: dữ liệu chưa được mã hóa")
    if f1.plain is None or f1.head != f"MSG alice {secret}":
        raise Fail(f"frame không giải mã được bằng khóa AES-256 chung. Dòng nhận: '{f1.head}'")
    alice.req(f"MSG bob {secret}")
    f2 = bob.next_relay()
    if f2.raw == f1.raw:
        raise Fail("hai lần gửi cùng nội dung cho ra cùng bản mã: IV phải được chọn ngẫu nhiên")


@test("F2", 0.40, "Frame sai: LEN > 12 MiB → ERR 413 hoặc đóng; ciphertext rác → ERR 400 hoặc đóng; server vẫn phục vụ")
def t_f2(env):
    env.start_server()
    c = env.conn()
    c.send_bytes(struct.pack(">I", MAX_FRAME + 1))
    try:
        err(c.next_resp(timeout=3), 413, "LEN quá lớn")
    except Closed:
        pass
    if not env.alive():
        raise Fail("server thoát khi nhận LEN quá lớn")

    c2 = env.conn()
    c2.send_frame_raw(os.urandom(48))
    try:
        fr = c2.next_resp(timeout=3)
        if not re.match(r"ERR 400(\s|$)", fr.head):
            raise Fail(f"ciphertext rác: mong đợi 'ERR 400' hoặc đóng kết nối, nhận '{fr.head}'")
    except Closed:
        pass
    if not env.alive():
        raise Fail("server thoát sau khi nhận frame rác")
    new_user(env, "erin")


@test("G1", 1.00, "FILE ảnh 300 KB (nhị phân) → A nhận OK; B nhận đúng header 'FILE <A> <tên> <size>' và đúng từng byte")
def t_g1(env):
    env.start_server()
    alice = new_user(env, "alice")
    bob = new_user(env, "bob")
    data = os.urandom(300 * 1024)
    ok(alice.req(f"FILE bob anh.png {len(data)}", data), "FILE alice → bob")
    fr = bob.next_relay(timeout=10)
    if fr.head != f"FILE alice anh.png {len(data)}":
        raise Fail(f"bob nhận header '{fr.head}', mong đợi 'FILE alice anh.png {len(data)}'")
    if fr.body != data:
        raise Fail(f"nội dung file bị sai: nhận {len(fr.body)} byte, mong đợi {len(data)} byte giống hệt")


@test("G2", 0.25, "FILE khai báo size khác số byte thực tế, hoặc size = 0 → ERR 400, không chuyển tiếp")
def t_g2(env):
    env.start_server()
    alice = new_user(env, "alice")
    bob = new_user(env, "bob")
    err(alice.req("FILE bob a.png 100", b"x" * 50), 400, "size khai báo 100, thực tế 50")
    err(alice.req("FILE bob a.png 0", b""), 400, "size = 0")
    bob.assert_no_relay()


@test("G3", 0.25, "FILE lớn hơn 10 MiB (frame vẫn ≤ 12 MiB) → ERR 413, không chuyển tiếp")
def t_g3(env):
    env.start_server()
    alice = new_user(env, "alice")
    bob = new_user(env, "bob")
    size = MAX_FILE + 1
    err(alice.req(f"FILE bob big.bin {size}", os.urandom(size)), 413, "FILE 10 MiB + 1 byte")
    bob.assert_no_relay()


@test("H1", 0.50, "Client ngắt đột ngột (nửa frame, RST) → server sống, xóa khỏi danh sách online, phục vụ tiếp")
def t_h1(env):
    env.start_server()
    alice = new_user(env, "alice")
    bob = new_user(env, "bob")
    bob.send_bytes(struct.pack(">I", 1000) + b"\x00" * 10)   # frame bị cắt giữa chừng
    bob.abort()
    wait_offline(alice, "bob")
    err(alice.req("MSG bob ban co khong"), 404, "MSG tới bob đã ngắt")

    bob2 = new_user(env, "bob2")
    bob2.abort()
    for i in range(5):
        fr = alice.req(f"MSG bob2 ping{i}")
        if not (is_ok(fr.head) or re.match(r"ERR 404(\s|$)", fr.head)):
            raise Fail(f"MSG tới bob2 vừa ngắt kết nối: nhận '{fr.head}'")
    if not env.alive():
        raise Fail("server đã thoát sau khi có client ngắt kết nối đột ngột")
    carol = new_user(env, "carol")
    if "carol" not in users_in_list(carol.req("LIST")):
        raise Fail("server không phục vụ đúng sau khi có client ngắt kết nối")


@test("H2", 0.50, "Dòng lệnh bất thường (không có '\\n', header rỗng, UTF-8 lỗi, MSG quá dài) → ERR 400, server vẫn chạy")
def t_h2(env):
    env.start_server()
    alice = new_user(env, "alice")
    err(alice.req_plain(b"LIST"), 400, "dòng không có '\\n'")
    err(alice.req_plain(b"\n"), 400, "header rỗng")
    err(alice.req_plain(b"\xff\xfe\xfd\n"), 400, "lệnh không phải UTF-8 hợp lệ")
    err(alice.req_plain(b"MSG bob " + b"a" * 3000 + b"\n"), 400, "dòng lệnh > 2048 byte")
    err(alice.req_plain(b"MSG bob " + b"a" * 1025 + b"\n"), 400, "nội dung > 1024 byte")
    if not env.alive():
        raise Fail("server thoát sau các lệnh bất thường")
    new_user(env, "dora")


@test("I1", 0.30, "Client: /register, /login, /list, lỗi 404 / 403 / lệnh lạ hiển thị đúng; /quit thoát mã 0")
def t_i1(env):
    env.start_server()
    cl = env.client("a")
    m = cl.mark()
    cl.send("/register alice pw12345")
    cl.expect(r"^\[OK\] ", after=m)
    m = cl.mark()
    cl.send("/login alice pw12345")
    cl.expect(r"^\[OK\] ", after=m)
    m = cl.mark()
    cl.send("/list")
    cl.expect(r"^\[OK\] USERS .*\balice\b", after=m)
    m = cl.mark()
    cl.send("/msg nobody hi")
    cl.expect(r"^\[ERR 404\] ", after=m)
    m = cl.mark()
    cl.send("/login alice pw12345")
    cl.expect(r"^\[ERR 403\] ", after=m)
    m = cl.mark()
    cl.send("/abc")
    cl.expect(r"^\[ERR\] ", after=m)
    cl.send("/quit")
    rc = cl.wait_exit()
    if rc != 0:
        raise Fail(f"client thoát với mã {rc} sau /quit (mong đợi 0)")


@test("I2", 0.35, "Client chat: alice /msg bob → bob in '[MSG] alice: <nội dung>' và ngược lại")
def t_i2(env):
    env.start_server()
    a = client_session(env, "a", "alice")
    b = client_session(env, "b", "bob")
    m = a.mark()
    a.send("/msg bob Xin chao bob 123")
    a.expect(r"^\[OK\] ", after=m)
    b.expect(r"^\[MSG\] alice: Xin chao bob 123$")
    m = b.mark()
    b.send("/msg alice hi back")
    b.expect(r"^\[OK\] ", after=m)
    a.expect(r"^\[MSG\] bob: hi back$")
    for cl in (a, b):
        cl.send("/quit")
        if cl.wait_exit() != 0:
            raise Fail("client không thoát mã 0 sau /quit")


@test("I3", 0.35, "Client gửi ảnh: /file → bob lưu downloads/alice_anh.png đúng từng byte; file không phải ảnh bị từ chối cục bộ")
def t_i3(env):
    env.start_server()
    a = client_session(env, "a", "alice")
    b = client_session(env, "b", "bob")
    data = os.urandom(200 * 1024)
    with open(os.path.join(a.cwd, "anh.png"), "wb") as f:
        f.write(data)
    with open(os.path.join(a.cwd, "notes.txt"), "w", encoding="utf-8") as f:
        f.write("khong phai anh")

    m = a.mark()
    a.send("/file bob anh.png")
    a.expect(r"^\[OK\] ", after=m)
    b.expect(rf"^\[FILE\] alice: anh\.png \({len(data)} bytes\) -> downloads/alice_anh\.png$")
    with open(os.path.join(b.cwd, "downloads", "alice_anh.png"), "rb") as f:
        got = f.read()
    if got != data:
        raise Fail(f"file nhận được khác file gốc ({len(got)} byte so với {len(data)} byte)")

    m = a.mark()
    a.send("/file bob notes.txt")
    a.expect(r"^\[ERR\] ", after=m)
    for cl in (a, b):
        cl.send("/quit")
        if cl.wait_exit() != 0:
            raise Fail("client không thoát mã 0 sau /quit")


# ---------------------------------------------------------------------------
# Điều phối chấm
# ---------------------------------------------------------------------------
def find_makefile_dir(root):
    """Tìm thư mục chứa Makefile ở gốc hoặc tối đa 1 cấp con (zip hay có thư mục bọc ngoài)."""
    for dirpath, dirnames, filenames in os.walk(root):
        depth = dirpath[len(root):].count(os.sep)
        if "Makefile" in filenames or "makefile" in filenames:
            return dirpath
        if depth >= 1:
            dirnames[:] = []
    return None


def build(mk):
    for name in ("server", "client"):
        p = os.path.join(mk, name)
        if os.path.exists(p):
            os.remove(p)
    try:
        r = subprocess.run(["make"], cwd=mk, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=180)
    except subprocess.TimeoutExpired:
        return False, "make chạy quá 180 giây"
    out = r.stdout.decode("utf-8", "replace")
    missing = [n for n in ("server", "client") if not os.access(os.path.join(mk, n), os.X_OK)]
    if r.returncode != 0 or missing:
        extra = f"\nKhông tạo được file thực thi: {', '.join(missing)}" if missing else ""
        return False, out + extra
    return True, out


def run_one(bindir, fn):
    env = Env(bindir)
    try:
        try:
            fn(env)
            return True, ""
        except Fail as e:
            msg = str(e)
        except Exception as e:  # noqa: BLE001
            msg = f"lỗi không mong đợi: {type(e).__name__}: {e}"
        log = env.server_log()
        if log:
            msg += "\n      [log server, 8 dòng cuối]\n" + "\n".join("      | " + x for x in log.splitlines())
        return False, msg
    finally:
        env.close()


def main():
    ap = argparse.ArgumentParser(description="Bộ tự chấm IT4062 HW8")
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--zip", metavar="FILE.zip", help="file zip bài nộp, ví dụ HotenSV_MSSV_HW8.zip")
    src.add_argument("--dir", metavar="THU_MUC", help="thư mục bài làm (có Makefile)")
    ap.add_argument("--only", metavar="ID,ID", default="",
                    help="chỉ chạy các test có mã này, ví dụ --only C1,F2 (mặc định: tất cả)")
    args = ap.parse_args()
    wanted = {x.strip().upper() for x in args.only.split(",") if x.strip()}

    for tool in ("make", "gcc", "openssl"):
        if not shutil.which(tool):
            print(f"Thiếu công cụ '{tool}' trong PATH. Hãy cài đặt rồi chạy lại.")
            sys.exit(2)

    if args.zip and not re.match(r"^.+_\d{6,12}_HW8\.zip$", os.path.basename(args.zip)):
        print("[!] Tên file nộp nên có dạng HotenSV_MSSV_HW8.zip (ví dụ NguyenVanA_20161234_HW8.zip)."
              " Không trừ điểm, nhưng GV sẽ kiểm tra.")

    work = tempfile.mkdtemp(prefix="hw8_src_")
    try:
        root = os.path.join(work, "src")
        if args.zip:
            with zipfile.ZipFile(args.zip) as z:
                z.extractall(root)
        else:
            shutil.copytree(args.dir, root)

        mk = find_makefile_dir(root)
        print("=" * 66)
        print(" CHẤM TỰ ĐỘNG - IT4062 BÀI TẬP TUẦN 8 (Chat TCP đa luồng + AES-256-CBC)")
        print("=" * 66)
        if mk is None:
            print(" [A1] KHÔNG ĐẠT: không tìm thấy Makefile trong bài nộp. Tổng điểm: 0.00 / 10")
            return

        built, log = build(mk)
        rows = []
        if not built:
            print(" [A1] (0.50) make tạo đủ ./server và ./client ............ KHÔNG ĐẠT")
            print("\n".join("      | " + x for x in log.splitlines()[-15:]))
            rows.append(("A1", 0.5, False))
            print(" Biên dịch thất bại: mọi test bên dưới được tính 0 điểm.")
            for tid, pts, title, _ in TESTS:
                print(f" [{tid}] ({pts:.2f}) {title} ........ KHÔNG ĐẠT (không có chương trình)")
                rows.append((tid, pts, False))
        else:
            print(" [A1] (0.50) make tạo đủ ./server và ./client ............ ĐẠT")
            rows.append(("A1", 0.5, True))
            for tid, pts, title, fn in TESTS:
                if wanted and tid not in wanted:
                    continue
                passed, msg = run_one(mk, fn)
                tag = "ĐẠT" if passed else "KHÔNG ĐẠT"
                print(f" [{tid}] ({pts:.2f}) {title} ........ {tag}")
                if not passed:
                    print(f"      -> {msg}")
                rows.append((tid, pts, passed))

        total = round(sum(p for _, p, ok_ in rows if ok_), 2)
        print("-" * 66)
        print(f" Tổng điểm tự động: {total:.2f} / 10.00")
        print(" Điểm chấm tay (luồng/mutex, giải phóng tài nguyên, chất lượng code)"
              " do GV đánh giá riêng.")
        print("=" * 66)
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
