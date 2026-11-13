package main

import (
	"fmt"
	"net/netip"
	"os"
	"path/filepath"
	"runtime"
	"strings"
)

func configDir() (string, error) {
	if runtime.GOOS == "windows" {
		return os.UserConfigDir() // %AppData%
	}

	if xdg := os.Getenv("XDG_CONFIG_HOME"); filepath.IsAbs(xdg) {
		return xdg, nil
	}

	home, err := os.UserHomeDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(home, ".config"), nil
}

type Light struct {
	Name    string
	Address string // "ip:port", as written, for the URL
	Host    string // the ip for the Host header, bracketed for IPv6
}

func parseConfig(text string) ([]Light, error) {
	var lights []Light

	lineNumber := 0
	for line := range strings.SplitSeq(text, "\n") {
		lineNumber++

		line = strings.TrimSpace(line)
		if line == "" || line[0] == '#' {
			continue
		}

		name, address, found := strings.Cut(line, "=")
		if !found {
			return nil, configError(lineNumber, "expected name = address")
		}
		name = strings.TrimSpace(name)
		address = strings.TrimSpace(address)

		if name == "" {
			return nil, configError(lineNumber, "empty name")
		}
		if strings.ContainsAny(name, " \t") {
			return nil, configError(lineNumber, "name \""+name+"\" contains whitespace")
		}
		if address == "" {
			return nil, configError(lineNumber, "empty address")
		}

		// Literals only, never a DNS lookup. Takes "[fe80::1]:9123" too.
		addrPort, err := netip.ParseAddrPort(address)
		if err != nil || addrPort.Port() == 0 {
			return nil, configError(lineNumber, "invalid address \""+address+"\"")
		}

		for _, existing := range lights {
			if existing.Name == name {
				return nil, configError(lineNumber, "duplicate name \""+name+"\"")
			}
		}

		host := addrPort.Addr().String()
		if addrPort.Addr().Is6() {
			host = "[" + host + "]"
		}
		lights = append(lights, Light{Name: name, Address: address, Host: host})
	}

	return lights, nil
}

func configError(lineNumber int, problem string) error {
	return fmt.Errorf("line %d: %s", lineNumber, problem)
}

func selectLights(lights []Light, name string, hasName bool) ([]Light, bool) {
	if !hasName {
		return lights, true
	}

	for i := range lights {
		if lights[i].Name == name {
			return lights[i : i+1], true
		}
	}

	return nil, false
}
