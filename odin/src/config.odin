package klair

import "core:os"
import "core:path/filepath"

// A relative XDG_CONFIG_HOME is ignored, per the XDG spec.
// Allocates from context.allocator (arena), so nothing to free.
config_dir :: proc() -> (path: string, ok: bool) {
	when ODIN_OS == .Windows {
		appdata := os.get_env("APPDATA", context.allocator) // %AppData%
		if appdata == "" {
			return
		}

		return appdata, true
	} else {
		// Linux, macOS, BSDs: honor XDG, fall back to ~/.config
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
