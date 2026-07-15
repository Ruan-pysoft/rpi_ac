package test_app

import "base:runtime"

import "core:fmt"
import "core:net"
import str "core:strings"
import "core:sys/linux"
import "core:thread"

import "protocol"
import unix "unix_sockets"

rundir: string

@(init)
@(private="file")
_find_rundir :: proc "contextless" () {
	context = runtime.default_context()
	rundir = fmt.aprintf("/run/user/{}/", linux.getuid())
}

get_runfile :: proc(filename: string) -> string {
	return fmt.aprint(rundir, filename, sep="")
}

// NOTE: based on github.com/odin-lang/examples/net/tcp_echo_server/server/server.odin

handle_connection :: proc(sock: net.TCP_Socket) {
	defer net.close(sock)

	req, req_err := protocol.request_recv(sock)
	if req_err != nil {
		fmt.printfln("got error receiving request: {}", req_err)

		switch err in req_err {
		case protocol.ProtocolError:
			fmt.printfln(
				"protocol error: {}",
				protocol.ProtocolErrorMap[err],
			)

			resp := protocol.response_from_protocol_error(err)
			defer {
				protocol.response_delete(&resp)
			}
			_ = protocol.response_send(sock, resp)
		case net.TCP_Recv_Error:
		}

		return
	}
	defer protocol.request_delete(&req)

	switch req.type {
	case .GET:
		body := fmt.aprintf(
			"<h1>Hello from the app!</h1>\n" +
			"<p>You're accessing the resource at <kbd>{}</kbd>.</p>\n",
			req.path
		)
		resp := protocol.Response {
			mimetype = str.clone("text/html"),
			status = str.clone("200 OK"),
			body = transmute([]u8) body,
		}
		defer protocol.response_delete(&resp)

		err := protocol.response_send(sock, resp)
		if err != nil {
			fmt.printfln("Error sending response: {}", err)
			return
		}
	case .POST: panic("not implemented")
	case .PING:
		resp := protocol.response_from_ping(req)
		defer protocol.response_delete(&resp)

		err := protocol.response_send(sock, resp)
		if err != nil {
			fmt.printfln("Error sending response: {}", err)
			return
		}
	}
}

main :: proc() {
	sockfname := get_runfile("rrp_test_app.sock")
	defer delete(sockfname)
	sockaddr := unix.make_unix_address(sockfname)

	sock, err := unix.unix_listen_tcp(sockaddr, 16)
	if err != nil {
		fmt.printfln("Failed to listen on TCP: {}", err)
		return
	}
	defer {
		net.close(sock)
		fmt.println("Closed socket")
	}
	fmt.printfln("Listening on TCP: {}", cstring(transmute([^]u8) &sockaddr.sun_path))

	for {
		cli, _, err_accept := unix.unix_accept_tcp(sock)
		if err_accept != nil {
			fmt.println("Failed to accept TCP connection")
			continue
		}
		fmt.println("Accepted TCP connection")
		thread.create_and_start_with_poly_data(cli, handle_connection)
	}
}
