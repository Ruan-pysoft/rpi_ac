#!/bin/python3

import http.server as hs
import os
import socket
# import time
from typing import Callable, Optional

from htmlserver.protocol import rrp_server as rrp


def get_runfile(filename: str) -> str:
    return f"/run/user/{os.getuid()}/{filename}"


def write_intro(self: hs.BaseHTTPRequestHandler, title: Optional[bytes] = None):
    self.wfile.writelines((
        b"<!DOCTYPES html>",
        b"<html lang=\"af-za\">",
        b"<head>",
        b"    <meta charset=\"utf-8\">",
        b"    <meta name=\"viewport\" content=\"width=device-width, intial-scale=1.0\">",
        #b"    <meta name=\"description\" content=\"\">",
        b"<title>" + (b"" if title is None else title + b" &en; ") + b"RPi Interface</title>",
        b"<link rel=\"stylesheet\" href=\"https://pysoft.co.za/site.css\">",
        b"</head>",
        b"<body>",
        b"    <header id=\"header\">",
        b"        <nav id=\"navbar\">",
        b"            <a href=\"/\" id=\"link-home-nav\">Home</a>",
        b"            <a href=\"/fan\">Fan Interface</a>",
        b"        </nav>",
        b"    </header>",
        b"    <main>",
    ))


def write_outro(self: hs.BaseHTTPRequestHandler):
    self.wfile.writelines((
        b"    </main>",
        b"    <footer id=\"footer\">",
        b"        <p>Created by <a href=\"https://pysoft.co.za\">Ruan</a> &copy; 2026</p>",
        b"    </footer>",
        b"</body>",
        b"</html>",
    ))


def default_get(self: hs.BaseHTTPRequestHandler, base: str, path: str):
    self.send_response(200)
    self.send_header('Content-type', 'text/html')
    self.end_headers()

    write_intro(self)
    self.wfile.writelines((
        f"<p>You connected from {self.client_address} and requested the path {path}!</p>".encode("utf-8"),
    ))
    write_outro(self)


def fan_get(self: hs.BaseHTTPRequestHandler, base: str, path: str):
    req = rrp.Request(
        rtype=rrp.RequestType.GET,
        path=rrp.RequestPath(path),
        mimetype=None,
        body=b"",
    )

    print(req)

    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM, 0) as app_sock:
        app_sock.connect(get_runfile("rrp_test_app.sock"))
        resp = rrp.send_request(app_sock, req)

    print(resp)

    if resp.mimetype == "text/html":
        code, *message = resp.status.split(maxsplit=1)
        message = None if not message else message[0]
        code = int(code) # TODO: handle non-int code?

        self.send_response(code, message)
        self.send_header('Content-type', 'text/html')
        self.end_headers()

        write_intro(self, title=b"Fan Interface")
        self.wfile.writelines((
            resp.body,
        ))
        write_outro(self)
    else:
        self.send_response(500, "Unknown MIMETYPE")
        self.send_header('Content-type', 'text-html')
        self.end_headers()

        write_intro(self, title=b"ERROR unknown mimetype")
        self.wfile.writelines((
            b"<h1>INTERNAL SERVER ERROR</h1>",
            f"<p>The fan app returned content of unknown mimetype {resp.mimetype!r}.</p>".encode("utf-8"),
        ))
        write_outro(self)


get_funcs: dict[str, Callable[[hs.BaseHTTPRequestHandler, str, str], None]] = {
    '': default_get,
    'fan': fan_get,
}


class RqH(hs.BaseHTTPRequestHandler):
    def do_GET(self):
        base, *rest = self.path.lstrip('/').split('/', 1)
        rest = rest[0] if rest else ''

        func = get_funcs.get(base, default_get)
        func(self, base, rest)

    '''
    def setup(self):
        with request_no.get_lock():
            self.rno = request_no.value
            request_no.value += 1
        print(f"Request {self.rno} started!")

        self.shutdown_server = False

    def handle(self):
        print(f"Client connected from {self.client_address}.")

        data = self.request.recv(512)
        print("Received data:", data)
        self.request.sendall(b"Hello, world!\n")

        if data.lower().startswith(b"quit"):
            self.shutdown_server = True

        time.sleep(10)

    def finish(self):
        print(f"Request {self.rno} finished!")
        self.request.close()
        if self.shutdown_server:
            self.server.shutdown()
    '''


with hs.ThreadingHTTPServer(("192.168.0.165", 4402), RqH) as server:
    server.serve_forever()
