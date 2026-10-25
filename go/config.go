package main

import (
	"fmt"
	"net/netip"
	"os"
	"path/filepath"
	"runtime"
	"strings"
)

// Not os.UserConfigDir: on macOS that returns `~/Library/Application Support`,
// and it errors on a relative XDG_CONFIG_HOME instead of ignoring it.
func configDir() (string, error) {
	if runtime.GOOS == "windows" {
		return os.UserConfigDir() // %AppData%
	}

	// Linux, macOS, BSDs: honor XDG, fall back to ~/.config
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
	Address string // "ip:port", validated by parseConfig
}

// name = ip:port, one per line, blank lines ignored.
// Rules, checked in this order for every non blank line:
//  1. there is a '='
//  2. name is not empty and has no spaces or tabs
//  3. address is not empty
//  4. address is a valid ip:port with a nonzero port
//  5. name is not used by an earlier line
func parseConfig(text string) ([]Light, error) {
	var lights []Light

	lineNumber := 0
	for line := range strings.SplitSeq(text, "\n") {
		lineNumber++

		line = strings.TrimSpace(line)
		if line == "" {
			continue
		}

		name, address, found := strings.Cut(line, "=")
		if !found {
			return nil, fmt.Errorf("line %d: expected name = ip:port", lineNumber)
		}
		name = strings.TrimSpace(name)
		address = strings.TrimSpace(address)

		if name == "" {
			return nil, fmt.Errorf("line %d: empty name", lineNumber)
		}
		if strings.ContainsAny(name, " \t") {
			return nil, fmt.Errorf("line %d: name %q contains whitespace", lineNumber, name)
		}
		if address == "" {
			return nil, fmt.Errorf("line %d: empty address", lineNumber)
		}

		addrPort, err := netip.ParseAddrPort(address)
		if err != nil {
			return nil, fmt.Errorf("line %d: bad address %q: %v", lineNumber, address, err)
		}
		if addrPort.Port() == 0 {
			return nil, fmt.Errorf("line %d: port must not be 0", lineNumber)
		}

		for _, existing := range lights {
			if existing.Name == name {
				return nil, fmt.Errorf("line %d: duplicate name %q", lineNumber, name)
			}
		}

		lights = append(lights, Light{Name: name, Address: address})
	}

	return lights, nil
}

// Empty name selects every light
// func selectLights(lights []Light, name string) ([]Light, bool) {
// 	if name == "" {
// 		return lights, true
// 	}
//
// 	for _, light := range lights {
// 		if light.Name == name {
// 			return []Light{light}, true
// 		}
// 	}
//
// 	return nil, false
// }
