package test_client

import "base:runtime"

import "core:c/libc"
import "core:fmt"
import "core:net"
import str "core:strings"
import "core:sys/linux"
import "core:sys/posix"
import "core:thread"

import "protocol"
/** BEGIN: UNIX SOCKETS **/

make_unix_address_cstring :: proc(address: cstring) -> linux.Sock_Addr_Un {
	res: linux.Sock_Addr_Un
	address_len := len(address)
	assert(address_len < size_of(res.sun_path))

	res.sun_family = .UNIX
	copy_slice(res.sun_path[:], (cast([^]u8)address)[:address_len+1])

	return res
}
make_unix_address_string :: proc(address: string) -> linux.Sock_Addr_Un {
	res: linux.Sock_Addr_Un
	assert(len(address) < size_of(res.sun_path))

	res.sun_family = .UNIX
	copy_slice(res.sun_path[:], transmute([]u8) address)
	res.sun_path[len(address)] = 0

	return res
}
make_unix_address :: proc{
	make_unix_address_cstring, make_unix_address_string,
}

unix_listen_tcp :: proc(endpoint: linux.Sock_Addr_Un, backlog := 1000) -> (socket: net.TCP_Socket, err: net.Network_Error) {
	errno: linux.Errno
	assert(backlog > 0 && backlog < int(max(i32)))

	os_sock: linux.Fd
	sock_type := linux.Socket_Type.STREAM
	sock_flags: linux.Socket_FD_Flags = {.CLOEXEC}
	sock_type_flags: i32 = cast(i32) sock_type | transmute(i32) sock_flags
	os_sock = transmute(linux.Fd) posix.socket(.UNIX, transmute(posix.Sock) sock_type_flags, nil)
	if os_sock < 0 {
		libc.perror("socket create")
		errno := libc.errno()^
		err = net._create_socket_error(transmute(linux.Errno) errno)
		return
	}

	socket = cast(net.TCP_Socket)os_sock
	defer if err != nil { net.close(socket) }

	endpoint := endpoint
	endpoint_raw := transmute([^]u8) &endpoint.sun_path
	cendpoint := cstring(endpoint_raw)
	posix.unlink(cendpoint)
	bindres := posix.bind(
		transmute(posix.FD)os_sock,
		(^posix.sockaddr)(&endpoint),
		size_of(endpoint),
	)
	if bindres != .OK {
		errno := libc.errno()^
		err = net._bind_error(transmute(linux.Errno) errno)
		return
	}

	if posix.listen(
		transmute(posix.FD)os_sock,
		cast(i32) backlog,
	) != .OK {
		errno := libc.errno()^
		err = net._listen_error(transmute(linux.Errno) errno)
		return
	}

	return
}

unix_accept_tcp :: proc(socket: net.TCP_Socket, options := net.DEFAULT_TCP_OPTIONS) -> (client: net.TCP_Socket, source: linux.Sock_Addr_Un, err: net.Accept_Error) {
	client_sock, errno := linux.accept(linux.Fd(socket), &source)
	if errno != .NONE {
		return {}, {}, net._accept_error(errno)
	}

	val: b32 = cast(b32) options.no_delay
	_ = linux.setsockopt(client_sock, linux.SOL_TCP, linux.Socket_TCP_Option.NODELAY, &val)
	return net.TCP_Socket(client_sock), source, nil
}

/** END: UNIX SOCKETS **/

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
	sockaddr := make_unix_address(sockfname)

	sock, err := unix_listen_tcp(sockaddr, 16)
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
		cli, source, err_accept := unix_accept_tcp(sock)
		if err_accept != nil {
			fmt.println("Failed to accept TCP connection")
			continue
		}
		fmt.printfln("Accepted TCP connection from `{}`", cstring(transmute([^]u8) &source.sun_path))
		thread.create_and_start_with_poly_data(cli, handle_connection)
	}
}
