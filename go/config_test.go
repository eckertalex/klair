package main

import (
	"strings"
	"testing"
)

func TestConfigSkipsBlankLinesAndTrims(t *testing.T) {
	lights, err := parseConfig("left  = 192.168.2.164:9123\n\n  right=192.168.2.165:9123  \n")
	if err != nil || len(lights) != 2 {
		t.Fatalf("parseConfig = %v, %v", lights, err)
	}

	if lights[0].Name != "left" || lights[1].Address != "192.168.2.165:9123" {
		t.Errorf("parseConfig = %v", lights)
	}
}

func TestConfigEmptyIsNotAnError(t *testing.T) {
	lights, err := parseConfig("")
	if err != nil || len(lights) != 0 {
		t.Errorf("parseConfig(\"\") = %v, %v", lights, err)
	}

	lights, err = parseConfig("\n  \n\t\n")
	if err != nil || len(lights) != 0 {
		t.Errorf("parseConfig(blank lines) = %v, %v", lights, err)
	}
}

func TestConfigHandlesCRLF(t *testing.T) {
	lights, err := parseConfig("left=192.168.2.164:9123\r\nright=192.168.2.165:9123\r\n")
	if err != nil || len(lights) != 2 {
		t.Fatalf("parseConfig = %v, %v", lights, err)
	}
	if lights[0].Address != "192.168.2.164:9123" {
		t.Errorf("address kept a \\r: %q", lights[0].Address)
	}
}

func TestConfigRejectsBadLines(t *testing.T) {
	bad := []string{
		"left 192.168.2.164:9123\n",              // no '='
		"=192.168.2.164:9123\n",                  // empty name
		"=\n",                                    // both empty
		"left=\n",                                // empty address
		"le ft=192.168.2.164:9123\n",             // whitespace in name
		"left=192.168.2.164\n",                   // no port
		"left=192.168.2.164:0\n",                 // port 0
		"left=192.168.2.164:99999\n",             // port out of range
		"left=localhost:9123\n",                  // hostname, not an ip
		"left=1.1.1.1:9123\nleft=1.1.1.2:9123\n", // duplicate name
	}

	for _, text := range bad {
		if lights, err := parseConfig(text); err == nil {
			t.Errorf("parseConfig(%q) accepted: %v", text, lights)
		}
	}
}

func TestConfigErrorHasLineNumber(t *testing.T) {
	_, err := parseConfig("left=192.168.2.164:9123\n\nbad\n")
	if err == nil || !strings.Contains(err.Error(), "line 3") {
		t.Errorf("parseConfig error = %v, want it to mention line 3", err)
	}
}

// func TestSelectLights(t *testing.T) {
// 	lights := []Light{{"left", "1.1.1.1:1"}, {"right", "1.1.1.2:1"}}
//
// 	all, ok := selectLights(lights, "")
// 	if !ok || len(all) != 2 {
// 		t.Errorf("selectLights(\"\") = %v, %v", all, ok)
// 	}
//
// 	one, ok := selectLights(lights, "right")
// 	if !ok || len(one) != 1 || one[0].Name != "right" {
// 		t.Errorf("selectLights(right) = %v, %v", one, ok)
// 	}
//
// 	if _, ok := selectLights(lights, "middle"); ok {
// 		t.Error("selectLights accepted an unknown name")
// 	}
// }
