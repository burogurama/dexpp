package dexpp

import (
	"bytes"
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"testing"
)

// shapes.dex holds an interface (LShape;), an abstract class implementing it
// (LAbstractShape;), and two concrete subclasses (LCircle;, LRectangle;).
func readModel(t *testing.T, name string) *Model {
	t.Helper()
	m, err := ReadModelFile(data(name), true)
	if err != nil {
		t.Fatalf("ReadModelFile(%s): %v", name, err)
	}
	t.Cleanup(m.Close)
	return m
}

// wantCode checks that err is an *Error with this code.
func wantCode(t *testing.T, err error, code ErrorCode) *Error {
	t.Helper()
	var e *Error
	if !errors.As(err, &e) {
		t.Fatalf("err = %v (%T), want an *Error with %v", err, err, code)
	}
	if e.Code != code {
		t.Errorf("Code = %v (%q), want %v", e.Code, e.Message, code)
	}
	return e
}

func TestModelReadAndClasses(t *testing.T) {
	m := readModel(t, "shapes.dex")
	want := []string{"LShape;", "LAbstractShape;", "LCircle;", "LRectangle;"}
	if got := m.ClassDescriptors(); !slices.Equal(got, want) {
		t.Errorf("ClassDescriptors = %v, want %v", got, want)
	}
	if m.ClassCount() != len(want) {
		t.Errorf("ClassCount = %d", m.ClassCount())
	}
	if !m.HasClass("LCircle;") || m.HasClass("Lno/such/Class;") {
		t.Error("HasClass is wrong")
	}
}

func TestModelClassInfo(t *testing.T) {
	m := readModel(t, "shapes.dex")
	circle, ok := m.ClassInfo("LCircle;")
	if !ok || circle.Descriptor != "LCircle;" || circle.MethodCount == 0 {
		t.Fatalf("ClassInfo(LCircle;) = %+v, %v", circle, ok)
	}
	if circle.Superclass == nil || *circle.Superclass != "LAbstractShape;" {
		t.Errorf("Superclass = %v", circle.Superclass)
	}
	if circle.SourceFile == nil || *circle.SourceFile != "Shapes.java" {
		t.Errorf("SourceFile = %v", circle.SourceFile)
	}
	if got := m.ClassInterfaces("LAbstractShape;"); !slices.Equal(got, []string{"LShape;"}) {
		t.Errorf("ClassInterfaces(LAbstractShape;) = %v", got)
	}
	// An interface's own superclass is java.lang.Object.
	shape, ok := m.ClassInfo("LShape;")
	if !ok || shape.Superclass == nil || *shape.Superclass != "Ljava/lang/Object;" {
		t.Errorf("ClassInfo(LShape;) = %+v", shape)
	}
	if !shape.AccessFlags.Has(AccessInterface) || shape.InterfaceCount != 0 {
		t.Errorf("LShape; flags = %v", shape.AccessFlags)
	}
	if _, ok := m.ClassInfo("Lno/such/Class;"); ok {
		t.Error("ClassInfo of an absent class found it")
	}
	if got := m.ClassInterfaces("Lno/such/Class;"); got != nil {
		t.Errorf("ClassInterfaces of an absent class = %v", got)
	}
}

func TestModelWriteRoundTrip(t *testing.T) {
	m := readModel(t, "shapes.dex")
	out, err := m.Write(true)
	if err != nil || len(out) == 0 {
		t.Fatalf("Write: %d bytes, %v", len(out), err)
	}
	again, err := ReadModel(out, true)
	if err != nil {
		t.Fatalf("ReadModel: %v", err)
	}
	defer again.Close()
	if again.ClassCount() != m.ClassCount() {
		t.Errorf("round trip has %d classes, want %d", again.ClassCount(), m.ClassCount())
	}
	// write(read(write(m))) gives the same bytes.
	if second, err := again.Write(true); err != nil || !bytes.Equal(second, out) {
		t.Errorf("the second write differs (%v)", err)
	}
	// Without debug information the file is smaller and still reads back.
	stripped, err := m.Write(false)
	if err != nil || len(stripped) >= len(out) {
		t.Errorf("Write(false) = %d bytes (with debug info: %d), %v", len(stripped), len(out), err)
	}
}

func TestModelSplitOnePerClass(t *testing.T) {
	src := readModel(t, "shapes.dex")
	descriptors := src.ClassDescriptors()
	work := src.Clone()
	defer work.Close()
	for _, d := range descriptors {
		one := NewModel(src.Version())
		if err := one.MoveClass(work, d); err != nil {
			t.Fatalf("MoveClass(%s): %v", d, err)
		}
		out, err := one.Write(true)
		one.Close()
		if err != nil {
			t.Fatalf("Write %s: %v", d, err)
		}
		part, err := ReadModel(out, true)
		if err != nil {
			t.Fatalf("ReadModel %s: %v", d, err)
		}
		if got := part.ClassDescriptors(); !slices.Equal(got, []string{d}) {
			t.Errorf("one-class file holds %v, want [%s]", got, d)
		}
		part.Close()
	}
	if work.ClassCount() != 0 {
		t.Errorf("the clone has %d classes left", work.ClassCount())
	}
	// The clone was drained; the original is untouched.
	if src.ClassCount() != len(descriptors) {
		t.Errorf("the original has %d classes, want %d", src.ClassCount(), len(descriptors))
	}
}

func TestModelCopyRemoveAndErrors(t *testing.T) {
	src := readModel(t, "shapes.dex")
	dst := NewModel(src.Version())
	defer dst.Close()
	if err := dst.CopyClass(src, "LCircle;"); err != nil {
		t.Fatalf("CopyClass: %v", err)
	}
	if !src.HasClass("LCircle;") || !dst.HasClass("LCircle;") {
		t.Error("CopyClass did not copy")
	}
	for name, err := range map[string]error{
		"copy into a model that has it":    dst.CopyClass(src, "LCircle;"),
		"move into a model that has it":    dst.MoveClass(src, "LCircle;"),
		"copy a class the source lacks":    dst.CopyClass(src, "Lno/such/Class;"),
		"move a class the source lacks":    dst.MoveClass(src, "Lno/such/Class;"),
		"move a class within one model":    dst.MoveClass(dst, "LCircle;"),
		"copy a class from an empty model": dst.CopyClass(NewModel(35), "LShape;"),
	} {
		if e := wantCode(t, err, CodeModel); !errors.Is(err, ErrModel) || e.Message == "" {
			t.Errorf("%s: %v", name, err)
		}
	}
	if !dst.RemoveClass("LCircle;") || dst.RemoveClass("LCircle;") || dst.ClassCount() != 0 {
		t.Error("RemoveClass is wrong")
	}
}

func TestModelRenameTypes(t *testing.T) {
	m := readModel(t, "shapes.dex")
	m.RenameTypes(map[string]string{"LCircle;": "Lcom/x/Round;"})
	m.RenameTypes(nil) // an empty mapping changes nothing
	if m.HasClass("LCircle;") || !m.HasClass("Lcom/x/Round;") {
		t.Fatalf("classes after rename = %v", m.ClassDescriptors())
	}
	// The rename reaches references too, and the model still writes.
	out, err := m.Write(true)
	if err != nil {
		t.Fatalf("Write after rename: %v", err)
	}
	ctx := writeAndLoad(t, out)
	if ctx.FindClass("Lcom/x/Round;") == nil || ctx.FindClass("LCircle;") != nil {
		t.Error("the written file does not have the renamed class")
	}
}

// writeAndLoad loads DEX bytes into an analysis context.
func writeAndLoad(t *testing.T, dex []byte) *Context {
	t.Helper()
	path := filepath.Join(t.TempDir(), "out.dex")
	if err := os.WriteFile(path, dex, 0o644); err != nil {
		t.Fatal(err)
	}
	ctx, err := FromDEX(path)
	if err != nil {
		t.Fatalf("FromDEX of written bytes: %v", err)
	}
	t.Cleanup(ctx.Close)
	return ctx
}

func TestModelValidateAndStats(t *testing.T) {
	m := readModel(t, "shapes.dex")
	if problems := m.Validate(); problems != nil {
		t.Errorf("a model read from a file has problems: %+v", problems)
	}
	stats := m.PoolStats()
	if stats.Strings == 0 || stats.Types == 0 || stats.Protos == 0 || stats.Methods == 0 ||
		stats.MethodHandles != 0 || stats.CallSites != 0 {
		t.Errorf("PoolStats = %+v", stats)
	}
	if v := m.RequiredVersion(); v != 35 {
		t.Errorf("RequiredVersion = %d, want 35", v)
	}
}

func TestModelNewAndVersion(t *testing.T) {
	m := NewModel(39)
	defer m.Close()
	if m.Version() != 39 || m.ClassCount() != 0 || m.ClassDescriptors() != nil {
		t.Errorf("new model: version %d, %d classes", m.Version(), m.ClassCount())
	}
	m.SetVersion(35)
	if m.Version() != 35 {
		t.Errorf("Version after SetVersion = %d", m.Version())
	}
	// An empty model writes a valid DEX file that reads back.
	out, err := m.Write(true)
	if err != nil {
		t.Fatalf("Write: %v", err)
	}
	again, err := ReadModel(out, true)
	if err != nil {
		t.Fatalf("ReadModel: %v", err)
	}
	again.Close()
}

func TestModelWriteFile(t *testing.T) {
	m := readModel(t, "shapes.dex")
	path := filepath.Join(t.TempDir(), "out.dex")
	if err := m.WriteFile(path, true); err != nil {
		t.Fatalf("WriteFile: %v", err)
	}
	written, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	if want, _ := m.Write(true); !bytes.Equal(written, want) {
		t.Error("WriteFile wrote other bytes than Write returns")
	}
}

func TestModelReadErrors(t *testing.T) {
	_, err := ReadModel([]byte("not a dex"), true)
	wantCode(t, err, CodeInvalidDex)
	_, err = ReadModel(nil, true)
	wantCode(t, err, CodeInvalidDex)
	_, err = ReadModelFile(data("TestDex.java"), true)
	wantCode(t, err, CodeInvalidDex)

	_, err = ReadModelFile(data("does_not_exist.dex"), true)
	wantCode(t, err, CodeFileNotFound)
	if !errors.Is(err, fs.ErrNotExist) || !errors.Is(err, ErrFileNotFound) {
		t.Errorf("a missing file: %v", err)
	}
	_, err = ReadModelFile(t.TempDir(), true)
	if e := wantCode(t, err, CodeCannotRead); !strings.Contains(e.Message, "directory") {
		t.Errorf("message = %q", e.Message)
	}

	// The model does not read the DEX version 41 container format.
	dex, err := os.ReadFile(data("shapes.dex"))
	if err != nil {
		t.Fatal(err)
	}
	copy(dex[4:7], "041")
	_, err = ReadModel(dex, true)
	wantCode(t, err, CodeUnsupportedDex)
	if !errors.Is(err, ErrUnsupportedDex) {
		t.Errorf("errors.Is(err, ErrUnsupportedDex) = false for %v", err)
	}
}

func TestInvalidModelErrorListsProblems(t *testing.T) {
	m := readModel(t, "shapes.dex")
	m.RenameTypes(map[string]string{"LCircle;": "not a descriptor"})
	want := m.Validate()
	if len(want) == 0 {
		t.Fatal("Validate found no problem")
	}
	for name, write := range map[string]func() error{
		"Write":     func() error { _, err := m.Write(true); return err },
		"WriteFile": func() error { return m.WriteFile(os.DevNull, true) },
	} {
		err := write()
		e := wantCode(t, err, CodeInvalidModel)
		if !slices.Equal(e.Problems, want) {
			t.Errorf("%s: Problems = %+v, want %+v", name, e.Problems, want)
		}
		if !strings.Contains(err.Error(), "not a descriptor") || !errors.Is(err, ErrInvalidModel) {
			t.Errorf("%s: %v", name, err)
		}
	}
}

func TestVersionTooLowError(t *testing.T) {
	// inherit.dex has default interface methods, which need DEX version 37.
	m := readModel(t, "inherit.dex")
	if m.RequiredVersion() != 37 {
		t.Fatalf("RequiredVersion = %d, want 37", m.RequiredVersion())
	}
	m.SetVersion(35)
	_, err := m.Write(true)
	wantCode(t, err, CodeVersionTooLow)
	m.SetVersion(m.RequiredVersion())
	if _, err := m.Write(true); err != nil {
		t.Errorf("Write at the required version: %v", err)
	}
}

func TestPoolOverflowError(t *testing.T) {
	if testing.Short() {
		t.Skip("builds a model with more than 65536 fields")
	}
	// A DEX file holds at most 65536 fields. The class in this fixture has 7
	// fields of its own, so 9400 copies of it, each renamed into its own
	// package, need more than that.
	source, err := ReadModelFile(data("multistring.dex"), false)
	if err != nil {
		t.Fatal(err)
	}
	defer source.Close()
	merged := NewModel(source.Version())
	defer merged.Close()
	for i := 0; i < 9400; i++ {
		part := source.Clone()
		mapping := map[string]string{}
		for _, d := range part.ClassDescriptors() {
			mapping[d] = fmt.Sprintf("Lcopy%d/%s", i, d[1:])
		}
		part.RenameTypes(mapping)
		for _, d := range part.ClassDescriptors() {
			if err := merged.MoveClass(part, d); err != nil {
				t.Fatal(err)
			}
		}
		part.Close()
	}
	if merged.PoolStats().Fields <= 65536 {
		t.Fatalf("Fields = %d", merged.PoolStats().Fields)
	}
	_, err = merged.Write(true)
	if e := wantCode(t, err, CodePoolOverflow); !strings.Contains(e.Message, "65536") {
		t.Errorf("message = %q", e.Message)
	}
}

func TestCannotWriteError(t *testing.T) {
	m := readModel(t, "shapes.dex")
	dir := t.TempDir()
	for _, path := range []string{filepath.Join(dir, "no", "such", "folder", "out.dex"), dir} {
		err := m.WriteFile(path, true)
		wantCode(t, err, CodeCannotWrite)
		if !errors.Is(err, ErrCannotWrite) || errors.Is(err, fs.ErrNotExist) {
			t.Errorf("WriteFile(%s): %v", path, err)
		}
	}
	// A full disk: on Linux every write to /dev/full fails.
	if _, err := os.Stat("/dev/full"); err == nil {
		wantCode(t, m.WriteFile("/dev/full", true), CodeCannotWrite)
	}
}
