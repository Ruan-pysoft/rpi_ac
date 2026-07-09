package test_client

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

	buf: [1024]byte

	for {
		bytes_recvd, err_recv := net.recv_tcp(sock, buf[:])
		if err_recv != nil {
			fmt.println("got error reading from socket")
			return
		}
		received := buf[:bytes_recvd]
		if len(received) == 0 {
			fmt.println("Disconnecting client")
			break
		}

		fmt.printfln("Server received [ %d bytes ]: %s", len(received), received)
		bytes_sent, err_send := net.send_tcp(sock, received)
		if err_send != nil {
			fmt.println("failed sending to socket")
		}
		sent := received[:bytes_sent]
		fmt.printfln("Server sent [ %d bytes ]: %s", len(sent), sent)
	}
}

main :: proc() {
	fmt.println("Hello, world!")

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
		cli, source, err_accept := unix.unix_accept_tcp(sock)
		if err_accept != nil {
			fmt.println("Failed to accept TCP connection")
			continue
		}
		fmt.printfln("Accepted TCP connection from `{}`", cstring(transmute([^]u8) &source.sun_path))
		thread.create_and_start_with_poly_data(cli, handle_connection)
	}
}
