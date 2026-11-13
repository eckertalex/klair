package main

import "testing"

func TestConfigParses(t *testing.T) {
	text := "# desk\r\n" +
		"left  = 192.168.2.164:9123\r\n" +
		"\r\n" +
		"  right=192.168.2.165:9123  \n" +
		"\tbehind = [fe80::1]:9123\n"

	lights, err := parseConfig(text)
	if err != nil {
		t.Fatalf("parseConfig: %v", err)
	}

	want := []Light{
		{Name: "left", Address: "192.168.2.164:9123", Host: "192.168.2.164"},
		{Name: "right", Address: "192.168.2.165:9123", Host: "192.168.2.165"},
		{Name: "behind", Address: "[fe80::1]:9123", Host: "[fe80::1]"},
	}
	if len(lights) != len(want) {
		t.Fatalf("parseConfig = %v, want %v", lights, want)
	}
	for i := range want {
		if lights[i] != want[i] {
			t.Errorf("light %d = %+v, want %+v", i, lights[i], want[i])
		}
	}
}

func TestConfigWithoutLightsIsNotAnError(t *testing.T) {
	for _, text := range []string{"", "\n  \n\t\n", "# only a comment\n"} {
		lights, err := parseConfig(text)
		if err != nil || len(lights) != 0 {
			t.Errorf("parseConfig(%q) = %v, %v", text, lights, err)
		}
	}
}

func TestConfigErrors(t *testing.T) {
	tests := []struct {
		text string
		want string
	}{
		{"left 127.0.0.1:19123", "line 1: expected name = address"},
		{" = 127.0.0.1:19123", "line 1: empty name"},
		{"=", "line 1: empty name"},
		{"my light = 127.0.0.1:19123", "line 1: name \"my light\" contains whitespace"},
		{"left =", "line 1: empty address"},
		{"left = lights.local:9123", "line 1: invalid address \"lights.local:9123\""},
		{"left = 127.0.0.1:0", "line 1: invalid address \"127.0.0.1:0\""},
		{"left = 127.0.0.1", "line 1: invalid address \"127.0.0.1\""},
		{"left = 127.0.0.1:99999", "line 1: invalid address \"127.0.0.1:99999\""},
		{"left = 127.0.0.1:+80", "line 1: invalid address \"127.0.0.1:+80\""},
		{"left = fe80::1:9123", "line 1: invalid address \"fe80::1:9123\""},
		{"left = 127.0.0.1:1\n# x\n\nleft = 127.0.0.1:2", "line 4: duplicate name \"left\""},
		{"bad\nalso bad", "line 1: expected name = address"},
	}

	for _, tt := range tests {
		_, err := parseConfig(tt.text)
		if err == nil || err.Error() != tt.want {
			t.Errorf("parseConfig(%q) error = %v, want %q", tt.text, err, tt.want)
		}
	}
}

func TestSelectLights(t *testing.T) {
	lights := []Light{{Name: "left"}, {Name: "right"}}

	all, found := selectLights(lights, "", false)
	if !found || len(all) != 2 {
		t.Errorf("selectLights(\"\") = %v, %v", all, found)
	}

	one, found := selectLights(lights, "right", true)
	if !found || len(one) != 1 || one[0].Name != "right" {
		t.Errorf("selectLights(right) = %v, %v", one, found)
	}

	if _, found := selectLights(lights, "Left", true); found {
		t.Error("selectLights matched Left to left")
	}
	if _, found := selectLights(lights, "", true); found {
		t.Error("selectLights matched an explicitly empty name")
	}
}
