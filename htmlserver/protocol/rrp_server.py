from dataclasses import dataclass
from enum import Enum
from socket import socket
from typing import Optional

# TODO: validate request headers
# TODO: validate response headers


PROTOCOL_VERSION: int = 0
MAX_HEADER_LINE_LEN: int = 4096


class RequestType(Enum):
    GET  = b"GET"
    POST = b"POST"
    PING = b"PING"


class RequestPath:
    def __init__(self, path: str):
        self.path = path.strip().strip("/")

    def __str__(self) -> str:
        return self.path

    def __repr__(self) -> str:
        return f"RequestPath({self.path!r})"


@dataclass
class Request:
    rtype: RequestType
    path: RequestPath
    mimetype: Optional[str]
    body: bytes

    def to_bytes(self) -> bytes:
        header = f"RRPROT/{PROTOCOL_VERSION}".encode("ascii")
        rtype = self.rtype.value
        path = str(self.path).encode("ascii")
        mimetype = b"" if self.mimetype is None \
            else self.mimetype.encode("ascii")
        content_length = str(len(self.body)).encode("ascii")

        header = b"\n".join((
            header, rtype, path, mimetype, content_length
        )) + b"\n"

        return header + b"\n" + self.body

    # TODO: def validate(self)


def send_request_only(sock: socket, req: Request):
    # TODO: req.validate()
    sock.sendall(req.to_bytes())


class ResponseType(Enum):
    CONTENT = b"CONTENT"
    PONG    = b"PONG"
    VERSION = b"VERSION"
    ERROR   = b"ERROR"


@dataclass
class Response:
    rtype: ResponseType
    status: bytes
    mimetype: Optional[str]
    body: Optional[bytes]


def _recv_header_line(sock: socket, buffer: bytes) -> (bytes, bytes):
    while len(buffer) < MAX_HEADER_LINE_LEN+2 \
            and ord('\n') not in buffer:
        recvd = sock.recv(1024)
        if not recvd:
            raise ValueError(
                "Connection closed before whole header line received"
            )
        buffer += recvd

    if ord('\n') not in buffer:
        raise ValueError(
            f"Header line too long (>{MAX_HEADER_LINE_LEN} bytes)"
        )

    newline_pos = buffer.find(ord('\n'))
    newline_len = 1

    if newline_pos > 0 and buffer[newline_pos-1] == ord('\r'):
        newline_pos -= 1
        newline_len += 1

    if newline_pos > MAX_HEADER_LINE_LEN:
        raise ValueError(
            f"Header line too long (>{MAX_HEADER_LINE_LEN} bytes)"
        )

    return (buffer[newline_pos+newline_len:], buffer[:newline_pos])


def recv_response(sock: socket) -> Response:
    buffer = b""

    buffer, rtype_line = _recv_header_line(sock, buffer)
    buffer, status_line = _recv_header_line(sock, buffer)
    buffer, mimetype_line = _recv_header_line(sock, buffer)
    buffer, content_length_line = _recv_header_line(sock, buffer)

    # raises a ValueError upon invalid response type
    rtype = ResponseType(rtype_line)
    status = status_line
    mimetype = mimetype_line.decode("ascii")
    if not mimetype: mimetype = None
    content_length = int(content_length_line.decode("ascii"))

    if content_length == 0:
        body = b""
    else:
        if not buffer:
            buffer = sock.recv(1024)

        if not buffer:
            raise ValueError(
                "Expected a blank line between response header & body, got EOF instead"
            )

        if not buffer.startswith(b"\n") and not buffer.startswith(b"\r\n"):
            raise ValueError(
                "Expected a blank line between response header & body"
            )

        if buffer[0] == ord('\r'):
            buffer = buffer[2:]
        else:
            buffer = buffer[1:]

        body = buffer

        while len(body) < content_length:
            recvd = sock.recv(content_length - len(body))
            if not recvd:
                raise ValueError(
                    "Got EOF while fetching response body"
                )
            body += recvd

        if len(body) > content_length:
            body = body[:content_length]

    return Response(
        rtype=rtype,
        status=status,
        mimetype=mimetype,
        body=body,
    )


def send_request(sock: socket, req: Request) -> Response:
    send_request_only(sock, req)
    return recv_response(sock)
