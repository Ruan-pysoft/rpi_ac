package test_client

import "base:runtime"

import "core:fmt"
import "core:net"
import str "core:strings"
import "core:sys/linux"
import "core:sys/posix"

import "protocol"

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

main :: proc() {
	fmt.println("Hello, world!")

	sockfd := posix.socket(.UNIX, .STREAM, nil)
	if sockfd == -1 do panic("couldn't create socket")
	defer linux.close(linux.Fd(sockfd))
	fmt.printf("{}\n", sockfd)

	sockfname := get_runfile("rrp_test_app.sock")
	defer delete(sockfname)
	c_sockfname := str.clone_to_cstring(sockfname)
	defer delete(c_sockfname)

	addr: posix.sockaddr_un
	addr.sun_family = .UNIX
	copy(addr.sun_path[:], sockfname)
	addr.sun_path[len(sockfname)] = 0
	posix.unlink(c_sockfname)
	bindres := posix.bind(sockfd, (^posix.sockaddr)(&addr), size_of(addr))
	if  bindres != .OK {
		fmt.println(bindres)
		panic("failed to bind socket")
	}

	if posix.listen(sockfd, 1024) != .OK {
		panic("failed to listen on socket")
	}

	serveraddr: posix.sockaddr
	serveraddrlen: posix.socklen_t = size_of(serveraddr)
	serverfd := posix.accept(sockfd, &serveraddr, &serveraddrlen)
	if serverfd == -1 do panic("failed accepting connection")

	serversock := net.TCP_Socket(serverfd)
	defer net.close(serversock)

	buf: [1024]byte

	for {
		recvd, err := net.recv_tcp(serversock, buf[:])
		if err != nil do panic("got error reading from socket")
		if recvd == 0 do break

		net.send_tcp(serversock, transmute([]u8) string("Got: "))
		net.send_tcp(serversock, buf[:recvd])
	}
}
