package dexpp

import (
	"path/filepath"
	"strings"
	"testing"
)

// data resolves a fixture under the repo's tests/data (tests run from this dir).
func data(name string) string {
	return filepath.Join("..", "..", "..", "tests", "data", name)
}

func TestOpenAPKAndManifest(t *testing.T) {
	apk, err := OpenAPK(data("sample.apk"))
	if err != nil {
		t.Fatalf("OpenAPK: %v", err)
	}
	defer apk.Close()

	if got := apk.Package(); got != "com.example.dexpp" {
		t.Errorf("package = %q, want com.example.dexpp", got)
	}

	comps := apk.Components()
	if len(comps) == 0 {
		t.Fatal("no components")
	}
	var act *Component
	for i := range comps {
		if strings.HasSuffix(comps[i].Name, "MainActivity") {
			act = &comps[i]
		}
	}
	if act == nil {
		t.Fatal("MainActivity not found")
	}
	if act.Kind != Activity || !act.Exported || !act.Reachable {
		t.Errorf("MainActivity = %+v, want activity/exported/reachable", *act)
	}
}

func TestAnalysisClassesAndMethods(t *testing.T) {
	apk, err := OpenAPK(data("sample.apk"))
	if err != nil {
		t.Fatalf("OpenAPK: %v", err)
	}
	defer apk.Close()

	ctx, err := apk.Analysis()
	if err != nil {
		t.Fatalf("Analysis: %v", err)
	}
	defer ctx.Close()

	classes := ctx.Classes()
	if len(classes) == 0 {
		t.Fatal("no classes")
	}

	cls := ctx.FindClass("LTestDex;")
	if cls == nil {
		t.Fatal("FindClass(LTestDex;) returned nil")
	}
	if cls.Name() != "LTestDex;" {
		t.Errorf("name = %q, want LTestDex;", cls.Name())
	}
	methods := cls.Methods()
	if len(methods) == 0 {
		t.Fatal("no methods on LTestDex;")
	}
	// Every method has a name and a well-formed descriptor.
	for _, m := range methods {
		if m.Name() == "" {
			t.Error("empty method name")
		}
		if d := m.Descriptor(); !strings.HasPrefix(d, "(") || !strings.Contains(d, ")") {
			t.Errorf("malformed descriptor %q", d)
		}
	}

	if missing := ctx.FindClass("Lno/such/Class;"); missing != nil {
		t.Error("FindClass of absent class should be nil")
	}
}

func TestStrings(t *testing.T) {
	ctx, err := FromAPK(data("sample.apk"))
	if err != nil {
		t.Fatalf("FromAPK: %v", err)
	}
	defer ctx.Close()
	if len(ctx.Strings()) == 0 {
		t.Error("empty string pool")
	}
}

func TestErrors(t *testing.T) {
	if _, err := OpenAPK(data("does_not_exist.apk")); err == nil {
		t.Error("expected error opening missing APK")
	}
}
