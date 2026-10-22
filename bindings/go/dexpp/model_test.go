package dexpp

import (
	"os"
	"path/filepath"
	"slices"
	"testing"
)

// shapes.dex holds an interface (LShape;), an abstract class implementing it
// (LAbstractShape;), and concrete subclasses (LCircle;, LRectangle;, ...).
func readShapes(t *testing.T) *Model {
	t.Helper()
	m, err := ReadModelFile(data("shapes.dex"), true)
	if err != nil {
		t.Fatalf("ReadModelFile: %v", err)
	}
	return m
}

func TestModelReadAndClasses(t *testing.T) {
	m := readShapes(t)
	defer m.Close()

	if m.ClassCount() == 0 {
		t.Fatal("no classes")
	}
	descriptors := m.ClassDescriptors()
	if len(descriptors) != m.ClassCount() {
		t.Fatalf("ClassDescriptors len %d != ClassCount %d", len(descriptors), m.ClassCount())
	}
	for _, want := range []string{"LShape;", "LAbstractShape;", "LCircle;"} {
		if !slices.Contains(descriptors, want) {
			t.Errorf("descriptors missing %s", want)
		}
		if !m.HasClass(want) {
			t.Errorf("HasClass(%s) = false", want)
		}
	}
	if m.HasClass("Lno/such/Class;") {
		t.Error("HasClass of absent class = true")
	}
}

func TestModelClassInfo(t *testing.T) {
	m := readShapes(t)
	defer m.Close()

	circle, ok := m.ClassInfo("LCircle;")
	if !ok {
		t.Fatal("ClassInfo(LCircle;) not found")
	}
	if circle.Superclass == nil || *circle.Superclass != "LAbstractShape;" {
		t.Errorf("Circle superclass = %v, want LAbstractShape;", circle.Superclass)
	}
	if circle.MethodCount == 0 {
		t.Error("Circle has no methods")
	}

	ifaces := m.ClassInterfaces("LAbstractShape;")
	if !slices.Contains(ifaces, "LShape;") {
		t.Errorf("AbstractShape interfaces = %v, want to contain LShape;", ifaces)
	}

	if _, ok := m.ClassInfo("Lno/such/Class;"); ok {
		t.Error("ClassInfo of absent class reported found")
	}
}

func TestModelWriteRoundTrip(t *testing.T) {
	m := readShapes(t)
	defer m.Close()

	bytes, err := m.Write(true)
	if err != nil {
		t.Fatalf("Write: %v", err)
	}
	if len(bytes) == 0 {
		t.Fatal("Write returned no bytes")
	}

	reread, err := ReadModel(bytes, true)
	if err != nil {
		t.Fatalf("ReadModel of written bytes: %v", err)
	}
	defer reread.Close()
	if reread.ClassCount() != m.ClassCount() {
		t.Errorf("round-trip class count %d != %d", reread.ClassCount(), m.ClassCount())
	}

	// Writing the re-read model again gives the exact same bytes (a fixpoint).
	again, err := reread.Write(true)
	if err != nil {
		t.Fatalf("second Write: %v", err)
	}
	if !slices.Equal(bytes, again) {
		t.Error("write(read(write(m))) is not a byte fixpoint")
	}
}

func TestModelSplitOnePerClass(t *testing.T) {
	src := readShapes(t)
	defer src.Close()
	descriptors := src.ClassDescriptors()

	// Move every class out of a clone into its own one-class model, write it,
	// and check it reads back as exactly that one class.
	work := src.Clone()
	defer work.Close()
	for _, d := range descriptors {
		one := NewModel(src.Version())
		if err := one.MoveClass(work, d); err != nil {
			one.Close()
			t.Fatalf("MoveClass(%s): %v", d, err)
		}
		bytes, err := one.Write(true)
		one.Close()
		if err != nil {
			t.Fatalf("Write one-class dex for %s: %v", d, err)
		}
		part, err := ReadModel(bytes, true)
		if err != nil {
			t.Fatalf("ReadModel one-class dex for %s: %v", d, err)
		}
		got := part.ClassDescriptors()
		part.Close()
		if len(got) != 1 || got[0] != d {
			t.Errorf("one-class dex holds %v, want [%s]", got, d)
		}
	}
	if work.ClassCount() != 0 {
		t.Errorf("after moving every class, source has %d left", work.ClassCount())
	}
}

func TestModelCopyAndRemove(t *testing.T) {
	src := readShapes(t)
	defer src.Close()

	dst := NewModel(src.Version())
	defer dst.Close()
	if err := dst.CopyClass(src, "LCircle;"); err != nil {
		t.Fatalf("CopyClass: %v", err)
	}
	if !src.HasClass("LCircle;") {
		t.Error("CopyClass removed the class from the source")
	}
	if !dst.HasClass("LCircle;") {
		t.Error("CopyClass did not add the class to the destination")
	}
	// Copying a class the destination already has fails.
	if err := dst.CopyClass(src, "LCircle;"); err == nil {
		t.Error("duplicate CopyClass should fail")
	}
	// Moving a class the source does not have fails.
	if err := dst.MoveClass(src, "Lno/such/Class;"); err == nil {
		t.Error("MoveClass of absent class should fail")
	}

	if !dst.RemoveClass("LCircle;") {
		t.Error("RemoveClass returned false for a present class")
	}
	if dst.RemoveClass("LCircle;") {
		t.Error("RemoveClass returned true for an absent class")
	}
	if dst.ClassCount() != 0 {
		t.Errorf("destination not empty after removal: %d", dst.ClassCount())
	}
}

func TestModelRenameTypes(t *testing.T) {
	m := readShapes(t)
	defer m.Close()

	m.RenameTypes(map[string]string{"LCircle;": "Lcom/x/Round;"})

	if m.HasClass("LCircle;") {
		t.Error("old descriptor still present after rename")
	}
	if !m.HasClass("Lcom/x/Round;") {
		t.Error("new descriptor absent after rename")
	}
	// The rename reaches references too, and the model still writes.
	if _, err := m.Write(true); err != nil {
		t.Fatalf("Write after rename: %v", err)
	}
}

func TestModelValidateAndStats(t *testing.T) {
	m := readShapes(t)
	defer m.Close()

	if problems := m.Validate(); len(problems) != 0 {
		t.Errorf("a read model should be valid, got %d problems: %+v", len(problems), problems)
	}

	stats := m.PoolStats()
	if stats.Strings == 0 || stats.Types == 0 || stats.Methods == 0 {
		t.Errorf("pool stats look empty: %+v", stats)
	}
	if v := m.RequiredVersion(); v < 35 {
		t.Errorf("RequiredVersion = %d, want >= 35", v)
	}
}

func TestModelNewAndVersion(t *testing.T) {
	m := NewModel(39)
	defer m.Close()
	if m.Version() != 39 {
		t.Errorf("Version = %d, want 39", m.Version())
	}
	if m.ClassCount() != 0 {
		t.Error("new model is not empty")
	}
	m.SetVersion(35)
	if m.Version() != 35 {
		t.Errorf("Version after SetVersion = %d, want 35", m.Version())
	}
	// An empty model writes to a valid, re-readable DEX.
	bytes, err := m.Write(true)
	if err != nil {
		t.Fatalf("Write empty model: %v", err)
	}
	reread, err := ReadModel(bytes, true)
	if err != nil {
		t.Fatalf("ReadModel empty dex: %v", err)
	}
	reread.Close()
}

func TestModelWriteFile(t *testing.T) {
	m := readShapes(t)
	defer m.Close()

	path := filepath.Join(t.TempDir(), "out.dex")
	if err := m.WriteFile(path, true); err != nil {
		t.Fatalf("WriteFile: %v", err)
	}
	info, err := os.Stat(path)
	if err != nil || info.Size() == 0 {
		t.Fatalf("WriteFile produced no file: %v", err)
	}
	reread, err := ReadModelFile(path, true)
	if err != nil {
		t.Fatalf("ReadModelFile of written file: %v", err)
	}
	defer reread.Close()
	if reread.ClassCount() != m.ClassCount() {
		t.Errorf("file round-trip class count %d != %d", reread.ClassCount(), m.ClassCount())
	}
}

func TestModelErrors(t *testing.T) {
	if _, err := ReadModel([]byte("not a dex"), true); err == nil {
		t.Error("expected error reading garbage")
	}
	if _, err := ReadModelFile(data("does_not_exist.dex"), true); err == nil {
		t.Error("expected error reading missing file")
	}
}
