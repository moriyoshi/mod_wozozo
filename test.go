package main

import (
	"encoding/binary"
	"fmt"
	"net"
	"os"
	"time"
)

const text = "おめでとうございます！"

func main() {
	l, err := net.ListenTCP("tcp", &net.TCPAddr{net.IPv4(0, 0, 0, 0), 20408, ""})
	if err != nil {
		fmt.Fprintf(os.Stderr, "%s", err.Error())
		os.Exit(1)
	}
	for {
		o, err := l.AcceptTCP()
		if err != nil {
			fmt.Fprintf(os.Stderr, "%s", err.Error())
			os.Exit(1)
		}
		go func(o *net.TCPConn) {
			defer o.Close()
			for _, c := range text {
				binary.Write(o, binary.LittleEndian, c)
				time.Sleep(1 * time.Second)
			}
		}(o)
	}
}
