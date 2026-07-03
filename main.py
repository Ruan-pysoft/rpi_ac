#!/bin/python3

import http.server as hs
from multiprocessing import Value
import time


request_no = Value("i", 0)


class RqH(hs.BaseHTTPRequestHandler):
    def do_GET(self):
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
