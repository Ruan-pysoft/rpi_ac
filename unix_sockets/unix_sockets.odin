package unix_sockets

import "core:c/libc"
import "core:net"
import "core:sys/linux"
import "core:sys/posix"

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
