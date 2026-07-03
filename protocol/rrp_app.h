#ifndef RRP_APP_H
#define RRP_APP_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#define PROTOCOL_VERSION 0

enum RequestType {
	RQT_GET,
	RQT_POST,
	RQT_PING,

	RQT_MAX
};

extern const char *request_type_map[RQT_MAX];

struct Request {
	enum RequestType type;
	char *path;
	char *mimetype; // can be NULL
	size_t content_length;
	char *body; // can be NULL
	// NOTE: body is NOT null terminated
};

void request_delete(struct Request *req);

// TODO: move these two to a result type for threading later?
extern const char *protocol_error;
extern bool response_protocol_too_high;
int request_recv(int sock, struct Request *req);

enum ResponseType {
	RST_CONTENT,
	RST_PONG,
	RST_VERSION,
	RST_ERROR,

	RST_MAX,
};

extern const char *response_type_map[RST_MAX];

struct Response {
	enum ResponseType type;
	const char *status;
	const char *mimetype; // can be NULL
	size_t content_length;
	const char *body; // can be NULL
	// NOTE: body is NOT null terminated
};

struct Response response_from_protocol_error(const char *errstr);
struct Response response_lower_version(void);

// TODO: add some sort of response_validate to check if type, status, mimetype are conforming to spec

int response_send(int sock, struct Response res);

#endif // RRP_APP_H

#ifdef RRP_APP_IMPL

#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_HEADER_LINE_LEN 4096

/** REQUEST **/

const char *request_type_map[RQT_MAX] = {
	[RQT_GET] = "GET",
	[RQT_POST] = "POST",
	[RQT_PING] = "PING",
};

void request_delete(struct Request *req) {
	free(req->path);
	if (req->mimetype) free(req->mimetype);
	if (req->body) free(req->body);

	*req = (struct Request) {0};
}

const char *protocol_error = NULL;
bool response_protocol_too_high = false;

struct read_buf {
	size_t buf_cap;
	size_t buf_len;
	char *buf;
};

static ssize_t _read_buf_recv(int sock, struct read_buf *buf) {
	if (buf->buf_len == buf->buf_cap) {
		buf->buf_cap = buf->buf_cap ? buf->buf_cap*2 : 1024;
		buf->buf = realloc(buf->buf, buf->buf_cap);

		// assume OOM is catastrophic
		if (buf->buf == NULL) {
			perror("failed reallocating read buffer");
			exit(1);
		}
	}

	ssize_t read_bytes = read(sock, &buf->buf[buf->buf_len], buf->buf_cap - buf->buf_len);

	if (read_bytes > 0) buf->buf_len += read_bytes;

	return read_bytes;
}

static char *_header_line_recv(int sock, struct read_buf *buf) {
	bool found_newline = false;
	size_t line_len = 0;
	size_t newline_len = 1;

	for (
		; line_len < MAX_HEADER_LINE_LEN
		&& line_len < buf->buf_len
		; ++line_len
	) {
		if (buf->buf[line_len] == '\n') {
			found_newline = true;
			break;
		}
	}

	while (!found_newline && line_len < MAX_HEADER_LINE_LEN) {
		ssize_t bytes_recvd = _read_buf_recv(sock, buf);
		if (bytes_recvd <= 0) {
			protocol_error = "Socket closed while reading header line";
			return NULL;
		}

		for (
			; line_len < MAX_HEADER_LINE_LEN
			&& line_len < buf->buf_len
			; ++line_len
		) {
			if (buf->buf[line_len] == '\n') {
				found_newline = true;
				break;
			}
		}
	}

	if (!found_newline) {
		protocol_error = "Request header line too long";
		return NULL;
	}

	if (line_len > 0) {
		if (buf->buf[line_len-1] == '\r') {
			--line_len;
			newline_len = 2;
		}
	}

	char *header_line = strndup(buf->buf, line_len);

	buf->buf_len -= line_len+newline_len;
	memmove(
		buf->buf,
		&buf->buf[line_len+newline_len],
		buf->buf_len
	);

	return header_line;
}

int request_recv(int sock, struct Request *req) {
	struct read_buf buf = {0};

	char *magic_line;
	char *type_line;
	char *path_line;
	char *mimetype_line;
	char *content_length_line;

	magic_line = _header_line_recv(sock, &buf);
	if (magic_line == NULL) goto fail;
	type_line = _header_line_recv(sock, &buf);
	if (type_line == NULL) goto fail__after_magic;
	path_line = _header_line_recv(sock, &buf);
	if (path_line == NULL) goto fail__after_type;
	mimetype_line = _header_line_recv(sock, &buf);
	if (mimetype_line == NULL) goto fail__after_path;
	content_length_line = _header_line_recv(sock, &buf);
	if (content_length_line == NULL) goto fail__after_mimetype;

	if (strnlen(magic_line, 8) < 8 || strncmp(magic_line, "RRPROT/", 7) != 0) {
		protocol_error = "Invalid magic header";
		goto fail__after_content_length;
	}
	if (strnlen(magic_line, 9) != 8 || (magic_line[7]-'0') > PROTOCOL_VERSION) {
		protocol_error = "Protocol version too large";
		response_protocol_too_high = true;
		goto fail__after_content_length;
	}
	if ((magic_line[7]-'0') < PROTOCOL_VERSION) {
		protocol_error = "Protocol version too small";
		goto fail__after_content_length;
	}

	enum RequestType type;
	for (type = 0; type < RQT_MAX; ++type) {
		if (strcmp(request_type_map[type], type_line) == 0) break;
	}
	if (type == RQT_MAX) {
		protocol_error = "Invalid request type";
		goto fail__after_content_length;
	}

	// trust that path is formatted correctly;
	// TODO: validate

	// trust that mimetype is formatted correctly;
	// TODO: validate

	// trust that the content length is formatted correctly;
	// TODO: validate
	size_t content_length = strtoull(content_length_line, NULL, 10);

	if (!content_length) {
		if (mimetype_line[0] == '\0') {
			free(mimetype_line);
			mimetype_line = NULL;
		}

		free(buf.buf);

		*req = (struct Request) {
			.type = type,
			.path = path_line,
			.mimetype = mimetype_line,
			.content_length = 0,
			.body = NULL,
		};

		return 0;
	}

	if (buf.buf_len == 0 || (buf.buf_len == 1 && buf.buf[0] == '\r')) {
		if (_read_buf_recv(sock, &buf) <= 0) {
			protocol_error = "Expected blank line between header and body, but socket closed unexpectedly";
			goto fail__after_content_length;
		}
	}

	if (buf.buf[0] != '\n' && !(buf.buf_len > 1 && buf.buf[0] == '\r' && buf.buf[1] == '\n')) {
		protocol_error = "Expected blank line between header and body";
		goto fail__after_content_length;
	}

	if (buf.buf[0] == '\n') {
		buf.buf_len -= 1;
		memmove(
			buf.buf,
			&buf.buf[1],
			buf.buf_len
		);
	} else {
		buf.buf_len -= 2;
		memmove(
			buf.buf,
			&buf.buf[2],
			buf.buf_len-1
		);
	}

	while (buf.buf_len < content_length) {
		if (_read_buf_recv(sock, &buf) <= 0) {
			protocol_error = "Socket closed before whole body could be read";
			goto fail__after_content_length;
		}
	}

	if (mimetype_line[0] == '\0') {
		free(mimetype_line);
		mimetype_line = NULL;
	}

	*req = (struct Request) {
		.type = type,
		.path = path_line,
		.mimetype = mimetype_line,
		.content_length = content_length,
		.body = buf.buf,
	};

	return 0;

fail__after_content_length:
	free(content_length_line);
fail__after_mimetype:
	if (mimetype_line) free(mimetype_line);
fail__after_path:
	free(path_line);
fail__after_type:
	free(type_line);
fail__after_magic:
	free(magic_line);
fail:
	if (buf.buf) free(buf.buf);
	return 1;
}

/** RESPONSE **/

const char *response_type_map[RST_MAX] = {
	[RST_CONTENT] = "CONTENT",
	[RST_PONG] = "PONG",
	[RST_VERSION] = "VERSION",
	[RST_ERROR] = "ERROR",
};

struct Response response_from_protocol_error(const char *errstr) {
	return (struct Response) {
		.type = RST_ERROR,
		.status = "",
		.mimetype = "text/plain",
		.content_length = strlen(errstr),
		.body = errstr,
	};
}
struct Response response_lower_version(void) {
	static char version_header[9] = "RRPROT/";
	if (version_header[7] == '\0') {
		version_header[7] = '0'+PROTOCOL_VERSION;
		version_header[8] = '\0';
	}
	return (struct Response) {
		.type = RST_VERSION,
		.status = version_header,
		.mimetype = NULL,
		.content_length = 0,
		.body = NULL,
	};
}

static int _nwrite_all(int sock, const char *str, size_t len) {
	while (len) {
		ssize_t bytes_written = write(sock, str, len);
		if (bytes_written <= 0) return 1;
		assert(bytes_written <= len);
		len -= bytes_written;
	}

	return 0;
}
static int _write_all(int sock, const char *str) {
	return _nwrite_all(sock, str, strlen(str));
}

int response_send(int sock, struct Response res) {
	int had_err = 0;

	had_err = _write_all(sock, response_type_map[res.type]);
	if (had_err) return 1;
	had_err = _write_all(sock, "\n");
	if (had_err) return 1;

	had_err = _write_all(sock, res.status);
	if (had_err) return 1;
	had_err = _write_all(sock, "\n");
	if (had_err) return 1;

	if (res.mimetype) {
		had_err = _write_all(sock, res.mimetype);
		if (had_err) return 1;
	}
	had_err = _write_all(sock, "\n");
	if (had_err) return 1;

	static char clbuf[32];
	snprintf(clbuf, sizeof(clbuf), "%zu", res.content_length);
	had_err = _write_all(sock, clbuf);
	if (had_err) return 1;
	had_err = _write_all(sock, "\n");
	if (had_err) return 1;

	if (res.content_length) {
		had_err = _write_all(sock, "\n");
		if (had_err) return 1;
		had_err = _nwrite_all(sock, res.body, res.content_length);
		if (had_err) return 1;
	}

	return 0;
}

#endif // RRP_APP_IMPL
