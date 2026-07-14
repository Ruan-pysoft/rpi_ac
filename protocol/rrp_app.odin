package rrp_app

import "core:mem"
import "core:net"
import str "core:strings"
import "core:strconv"

PROTOCOL_VERSION :: 0

RequestType :: enum {
	GET,
	POST,
	PING,
}

RequestTypeMap := [RequestType]string {
	.GET = "GET",
	.POST = "POST",
	.PING = "PING",
}

Request :: struct {
	type: RequestType,
	path: string,
	mimetype: Maybe(string),
	body: [dynamic]u8,
}

request_delete :: proc(req: ^Request) {
	delete(req.path)
	mimetype, has := req.mimetype.?
	if has do delete(mimetype)
	delete(req.body)

	req^ = {}
}

ProtocolError :: enum {
	NoError = 0,
	PartialLine,
	LineTooLong,
	InvalidMagic,
	VersionTooHigh,
	VersionTooLow,
	InvalidRequestType,
	ClosedOnBlankLine,
	NoBlankLine,
	PartialBody,
}

ProtocolErrorMap := [ProtocolError]string {
	.NoError = "no error",
	.PartialLine = "socket closed while reading header line",
	.LineTooLong = "header line exceeds maximum length",
	.InvalidMagic = "invalid magic header",
	.VersionTooHigh = "protocol version is too high",
	.VersionTooLow = "protocol version is too low",
	.InvalidRequestType = "invalid request type",
	.ClosedOnBlankLine = "expected a blank line between header and body, but socked closed unexpectedly",
	.NoBlankLine = "expected a blank line between header and body",
	.PartialBody = "socket closed before whole body could be read",
}

RecvError :: union { net.TCP_Recv_Error, ProtocolError }

@(private="file")
_buf_recv :: proc(sock: net.TCP_Socket, buf: ^[dynamic]u8) -> (bytes_read: int, err: net.TCP_Recv_Error) {
	old_len := len(buf)
	if cap(buf) == 0 do reserve(buf, 1024)
	resize(buf, cap(buf) if cap(buf) > old_len else 2*cap(buf))

	bytes_read = net.recv_tcp(
		sock,
		buf[old_len:]
	) or_return
	resize(buf, old_len+bytes_read)

	return bytes_read, nil
}

MAX_HEADER_LINE_LEN :: 4096

@(private="file")
_header_line_recv :: proc(sock: net.TCP_Socket, buf: ^[dynamic]u8) -> (line: string, err: RecvError) {
	found_newline := false
	line_len := 0
	newline_len := 1

	for ;
		line_len < MAX_HEADER_LINE_LEN &&
		line_len < len(buf);
		line_len += 1
	{
		if buf[line_len] == '\n' {
			found_newline = true
			break
		}
	}

	for !found_newline && line_len < MAX_HEADER_LINE_LEN {
		bytes_recvd := _buf_recv(sock, buf) or_return
		if bytes_recvd == 0 {
			return {}, .PartialLine
		}

		for ;
			line_len < MAX_HEADER_LINE_LEN &&
			line_len < len(buf);
			line_len += 1
		{
			if buf[line_len] == '\n' {
				found_newline = true
				break
			}
		}
	}

	if !found_newline {
		return {}, .LineTooLong
	}

	if line_len > 0 {
		if buf[line_len-1] == '\r' {
			line_len -= 1
			newline_len = 2
		}
	}

	line = str.clone(cast(string) buf[:line_len])

	new_len := len(buf) - (line_len+newline_len)
	if new_len > 0 do mem.copy(
		&buf[0], &buf[line_len + newline_len],
		new_len,
	)
	resize(buf, new_len)

	return line, nil
}

request_recv :: proc(sock: net.TCP_Socket) -> (req: Request, err: RecvError) {
	buf: [dynamic]u8

	magic_line: string
	type_line: string
	path_line: string
	mimetype_line: string
	content_length_line: string

	magic_line = _header_line_recv(sock, &buf) or_return
	defer delete(magic_line)
	type_line = _header_line_recv(sock, &buf) or_return
	defer delete(type_line)
	path_line = _header_line_recv(sock, &buf) or_return
	defer delete(path_line)
	mimetype_line = _header_line_recv(sock, &buf) or_return
	defer delete(mimetype_line)
	content_length_line = _header_line_recv(sock, &buf) or_return
	defer delete(content_length_line)

	if !str.starts_with(magic_line, "RRPROT/") {
		return {}, .InvalidMagic
	}

	if len(magic_line) > 8 || (magic_line[7]-'0') > PROTOCOL_VERSION {
		return {}, .VersionTooHigh
	}
	if (magic_line[7]-'0') < PROTOCOL_VERSION {
		return {}, .VersionTooLow
	}

	ok: bool

	type: RequestType
	ok = false
	for rt in RequestType {
		if str.compare(RequestTypeMap[rt], type_line) == 0 {
			type, ok = rt, true
			break
		}
	}
	if !ok do return {}, .InvalidRequestType

	// trust that the path is formatted correctly
	// TODO: validate

	// trust that mimetype is formatted correctly
	// TODO: validate

	// trust that the content length is formatted correctly
	// TODO: validate
	content_length := strconv.parse_int(content_length_line, 10) or_else 0
	assert(content_length >= 0)

	if content_length == 0 {
		delete(buf)

		req = Request {
			type = type,
			path = path_line,
			mimetype = len(mimetype_line) == 0 ? {} : mimetype_line,
			body = {},
		}

		// dodge double frees
		path_line = {}
		if len(mimetype_line) != 0 do mimetype_line = {}

		return req, nil
	}

	if len(buf) == 0 || (len(buf) == 1 && buf[0] == '\r') {
		if (_buf_recv(sock, &buf) or_return) == 0 {
			return {}, .ClosedOnBlankLine
		}
	}

	if buf[0] != '\n' && !(len(buf) > 1 && buf[0] == '\r' && buf[1] == '\n') {
		return {}, .NoBlankLine
	}

	if buf[0] == '\n' {
		if len(buf) > 1 do mem.copy(
			&buf[0], &buf[1],
			len(buf)-1,
		)
		resize(&buf, len(buf)-1)
	} else {
		if len(buf) > 2 do mem.copy(
			&buf[0], &buf[2],
			len(buf)-2,
		)
		resize(&buf, len(buf)-1)
	}

	for len(buf) < content_length {
		if (_buf_recv(sock, &buf) or_return) == 0 {
			return {}, .PartialBody
		}
	}

	resize(&buf, content_length)
	req = Request {
		type = type,
		path = path_line,
		mimetype = len(mimetype_line) == 0 ? {} : mimetype_line,
		body = buf,
	}

	// dodge double frees
	path_line = {}
	if len(mimetype_line) != 0 do mimetype_line = {}

	return req, nil
}

ResponseType :: enum {
	CONTENT,
	PONG,
	VERSION,
	ERROR,
}

ResponseTypeMap := [ResponseType]string {
	.CONTENT = "CONTENT",
	.PONG = "PONG",
	.VERSION = "VERSION",
	.ERROR = "ERROR",
}

Response :: struct {
	type: ResponseType,
	status: string,
	mimetype: Maybe(string),
	body: []u8,
}

response_delete :: proc(response: ^Response) {
	delete(response.status)
	mimetype, ok := response.mimetype.?
	if ok do delete(mimetype)
	delete(response.body)

	response^ = {}
}

response_from_protocol_error_str :: proc(error_str: string) -> Response {
	body := make([]u8, len(error_str))
	mem.copy_non_overlapping(&body[0], raw_data(error_str), len(error_str))
	return {
		type = .ERROR,
		status = "",
		mimetype = str.clone("text/plain"),
		body = body,
	}
}
response_from_protocol_error_enum :: proc(error_enum: ProtocolError) -> Response {
	return response_from_protocol_error_str(ProtocolErrorMap[error_enum])
}
response_from_protocol_error :: proc{
	response_from_protocol_error_str,
	response_from_protocol_error_enum,
}
response_lower_version :: proc() -> Response {
	version_header_prefix :: "RRPROT/"
	#assert(PROTOCOL_VERSION < 10)
	version_header := str.concatenate([]string {
		version_header_prefix,
		transmute(string) []u8{'0'+PROTOCOL_VERSION}
	})

	return {
		type = .VERSION,
		status = version_header,
		mimetype = {},
		body = {},
	}
}
response_from_ping :: proc(req: Request) -> Response {
	assert(req.type == .PING)

	mimetype, ok := req.mimetype.?
	body := make([]u8, len(req.body))
	copy(body, req.body[:])

	return {
		type = .PONG,
		status = "",
		mimetype = ok ? str.clone(mimetype) : nil,
		body = body,
	}
}

// TODO: add some sort of response_validate to check if type, status, mimetype are conforming to spec

@(private="file")
_write_all_str :: proc(sock: net.TCP_Socket, data: string) -> (bytes_written: int, err: net.TCP_Send_Error) {
	return net.send_tcp(sock, transmute([]u8) data)
}

@(private="file")
_write_char :: proc(sock: net.TCP_Socket, data: u8) -> (bytes_written: int, err: net.TCP_Send_Error) {
	return net.send_tcp(sock, []u8{ data })
}

response_send :: proc(sock: net.TCP_Socket, res: Response) -> (err: net.TCP_Send_Error) {
	_write_all_str(sock, ResponseTypeMap[res.type]) or_return
	_write_char(sock, '\n') or_return

	_write_all_str(sock, res.status) or_return
	_write_char(sock, '\n') or_return

	mimetype, ok := res.mimetype.?
	if ok do _write_all_str(sock, mimetype) or_return
	_write_char(sock, '\n') or_return

	clenbuf: [32]u8
	content_length := strconv.write_int(clenbuf[:], i64(len(res.body)), 10)
	_write_all_str(sock, content_length) or_return
	_write_char(sock, '\n') or_return

	if len(res.body) != 0 {
		_write_char(sock, '\n') or_return
		bytes := net.send_tcp(sock, res.body[:]) or_return
	}

	return nil
}

@(private="file")
IncludeMain :: #config(RRP_APP_TEST_COMPILES, false)

when IncludeMain {
	main :: proc() {}
}
