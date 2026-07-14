#!/bin/python3

import http.server as hs
import os
import socket
# import time

from protocol import rrp_server as rrp


def get_runfile(filename: str) -> str:
    return f"/run/user/{os.getuid()}/{filename}"


class RqH(hs.BaseHTTPRequestHandler):
    def do_GET(self):
        base, *rest = self.path.lstrip('/').split('/', 1)
        rest = rest[0] if rest else ''

        if base == 'app':
            req = rrp.Request(
                rtype=rrp.RequestType.GET,
                path=rrp.RequestPath(rest),
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

                self.wfile.writelines((
                    b"<!DOCTYPE html>",
                    b"<html>",
                    b"<body>",
                    resp.body,
                    b"<body>",
                    b"</html>",
                ))

                return

        self.send_response(200)
        self.send_header('Content-type', 'text/html')
        self.end_headers()

        self.wfile.writelines((
            b"<!DOCTYPE html>",
            b"<html>",
            b"<body>",
            b"<h1>Hello, world!</h1>",
            f"<p>You connected from {self.client_address} and requested the path {self.path}!</p>".encode("utf-8"),
            b"<body>",
            b"</html>",
        ))

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


with hs.ThreadingHTTPServer(("", 4402), RqH) as server:
    server.serve_forever()
