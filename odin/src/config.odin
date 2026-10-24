package klair

import "core:os"
import "core:strings"

config_path :: proc() -> (path: string, ok: bool) {
    home := os.get_env("HOME", context.allocator)
    if home == "" {
        return "", false
    }

    return strings.concatenate({home, "/.config/klair"}), true
}
