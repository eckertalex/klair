package klair

import "core:fmt"
import "core:net"
import "core:os"
import "core:path/filepath"
import "core:strings"

Light :: struct {
	name:     string,
	address:  string, // as written, for messages
	host:     string, // the address without the port, for the Host header
	endpoint: net.Endpoint,
}

Config_Error :: struct {
	line:    int,
	message: string,
}

WHITESPACE :: " \t\r"

config_dir :: proc() -> (path: string, ok: bool) {
	when ODIN_OS == .Windows {
		appdata := os.get_env("APPDATA", context.allocator)
		if appdata == "" {
			return
		}
		return appdata, true
	} else {
		xdg := os.get_env("XDG_CONFIG_HOME", context.allocator)
		if filepath.is_abs(xdg) {
			return xdg, true
		}
		home := os.get_env("HOME", context.allocator)
		if home == "" {
			return
		}
		joined, err := filepath.join({home, ".config"})
		if err != nil {
			return
		}
		return joined, true
	}
}

lights_from_config :: proc(text: string) -> (lights: [dynamic]Light, err: Config_Error, ok: bool) {
	rest := text
	line_number := 0
	for line in strings.split_iterator(&rest, "\n") {
		line_number += 1

		trimmed := strings.trim(line, WHITESPACE)
		if trimmed == "" || trimmed[0] == '#' {
			continue
		}

		name, equals, address := strings.partition(trimmed, "=")
		if equals == "" {
			return lights, {line_number, "expected name = address"}, false
		}
		name = strings.trim(name, WHITESPACE)
		address = strings.trim(address, WHITESPACE)

		if name == "" {
			return lights, {line_number, "empty name"}, false
		}
		if strings.index_any(name, WHITESPACE) >= 0 {
			return lights, {line_number, fmt.aprintf(`name "%s" contains whitespace`, name)}, false
		}
		if address == "" {
			return lights, {line_number, "empty address"}, false
		}
		endpoint, host, valid := endpoint_from_address(address)
		if !valid {
			return lights, {line_number, fmt.aprintf(`invalid address "%s"`, address)}, false
		}
		for light in lights {
			if light.name == name {
				return lights, {line_number, fmt.aprintf(`duplicate name "%s"`, name)}, false
			}
		}

		append(&lights, Light{name = name, address = address, host = host, endpoint = endpoint})
	}
	return lights, {}, true
}

endpoint_from_address :: proc(address: string) -> (endpoint: net.Endpoint, host: string, ok: bool) {
	endpoint = net.parse_endpoint(address) or_return
	if endpoint.port < 1 || endpoint.port > 65535 {
		return
	}
	if address[0] == '[' {
		host = address[:strings.index_byte(address, ']') + 1]
	} else {
		host = address[:strings.last_index_byte(address, ':')]
	}
	return endpoint, host, true
}
