package dexpp

import (
	"errors"
	"io/fs"
	"os"
	"path/filepath"
	"regexp"
	"runtime"
	"slices"
	"strings"
	"sync"
	"testing"
)

// data resolves a fixture under the repository's tests/data. go test runs the
// tests in this folder.
func data(name string) string {
	return filepath.Join("..", "..", "..", "tests", "data", name)
}

func mustDEX(t *testing.T, name string) *Context {
	t.Helper()
	ctx, err := FromDEX(data(name))
	if err != nil {
		t.Fatalf("FromDEX(%s): %v", name, err)
	}
	t.Cleanup(ctx.Close)
	return ctx
}

func mustClass(t *testing.T, ctx *Context, descriptor string) *Class {
	t.Helper()
	cls := ctx.FindClass(descriptor)
	if cls == nil {
		t.Fatalf("FindClass(%s) = nil", descriptor)
	}
	return cls
}

func mustMethod(t *testing.T, cls *Class, name string) *Method {
	t.Helper()
	for _, m := range cls.Methods() {
		if m.Name() == name {
			return m
		}
	}
	t.Fatalf("%s has no method %s", cls.Name(), name)
	return nil
}

func mustField(t *testing.T, cls *Class, name string) *Field {
	t.Helper()
	for _, f := range cls.Fields() {
		if f.Name() == name {
			return f
		}
	}
	t.Fatalf("%s has no field %s", cls.Name(), name)
	return nil
}

func classNames(ctx *Context) []string {
	var names []string
	for _, c := range ctx.Classes() {
		names = append(names, c.Name())
	}
	return names
}

// ---- Loading --------------------------------------------------------------

func TestVersion(t *testing.T) {
	// src/version.hpp is the one place the version is written.
	header, err := os.ReadFile(filepath.Join("..", "..", "..", "src", "version.hpp"))
	if err != nil {
		t.Fatal(err)
	}
	want := regexp.MustCompile(`#define DEXPP_VERSION "([^"]+)"`).FindSubmatch(header)
	if want == nil || Version != string(want[1]) {
		t.Errorf("Version = %q, version.hpp says %q", Version, want)
	}
}

func TestLoadAndFindClasses(t *testing.T) {
	ctx := mustDEX(t, "classes.dex")
	if !slices.Contains(classNames(ctx), "LTestDex;") {
		t.Fatalf("classes = %v, want LTestDex;", classNames(ctx))
	}
	cls := mustClass(t, ctx, "LTestDex;")
	if !cls.IsPublic() {
		t.Error("LTestDex; is not public")
	}
	if got := ctx.FindClass("LNoSuch;"); got != nil {
		t.Errorf("FindClass(LNoSuch;) = %v, want nil", got)
	}
}

func TestFromAPK(t *testing.T) {
	ctx, err := FromAPK(data("app.apk"))
	if err != nil {
		t.Fatalf("FromAPK: %v", err)
	}
	defer ctx.Close()
	for _, d := range []string{"LTestDex;", "LFibonacci;"} {
		if ctx.FindClass(d) == nil {
			t.Errorf("app.apk has no %s", d)
		}
	}
}

func TestFromAPKWithAnyThreadCount(t *testing.T) {
	// multidex.apk has four deflated DEX entries, enough for the parallel path.
	load := func(opts ...LoadOption) []string {
		ctx, err := FromAPK(data("multidex.apk"), opts...)
		if err != nil {
			t.Fatalf("FromAPK: %v", err)
		}
		defer ctx.Close()
		return classNames(ctx)
	}
	serial := load(WithThreads(1))
	if len(serial) == 0 || serial[0] != "LTestDex;" {
		t.Fatalf("serial classes = %v, want LTestDex; first", serial)
	}
	for _, opts := range [][]LoadOption{{WithThreads(4)}, nil, {WithThreads(-3)}, {nil}} {
		if got := load(opts...); !slices.Equal(got, serial) {
			t.Errorf("classes with %d options = %v, want %v", len(opts), got, serial)
		}
	}
}

func TestFromDEXFiles(t *testing.T) {
	// The cross-DEX split of inherit.dex: the classes of both halves load.
	ctx, err := FromDEXFiles([]string{data("inherit_a.dex"), data("inherit_b.dex")})
	if err != nil {
		t.Fatalf("FromDEXFiles: %v", err)
	}
	defer ctx.Close()
	a, b := mustDEX(t, "inherit_a.dex"), mustDEX(t, "inherit_b.dex")
	want := append(classNames(a), classNames(b)...)
	if got := classNames(ctx); !slices.Equal(got, want) {
		t.Errorf("classes = %v, want %v", got, want)
	}

	empty, err := FromDEXFiles(nil)
	if err != nil {
		t.Fatalf("FromDEXFiles(nil): %v", err)
	}
	defer empty.Close()
	if n := len(empty.Classes()); n != 0 {
		t.Errorf("an empty context has %d classes", n)
	}
}

func TestStrings(t *testing.T) {
	ctx := mustDEX(t, "classes.dex")
	strs := ctx.Strings()
	for _, want := range []string{"Hello Dex++!!", "LTestDex;"} {
		if !slices.Contains(strs, want) {
			t.Errorf("Strings() has no %q", want)
		}
	}
	xrefs, err := ctx.Xrefs()
	if err != nil {
		t.Fatal(err)
	}
	for _, s := range xrefs.ReferencedStrings() {
		if !slices.Contains(strs, s) {
			t.Errorf("referenced string %q is not in Strings()", s)
		}
	}

	// A string with a NUL byte keeps it.
	multi := mustDEX(t, "multistring.dex")
	if !slices.Contains(multi.Strings(), "test\x00embedded") {
		t.Error(`multistring.dex: Strings() has no "test\x00embedded"`)
	}
}

// ---- Errors ---------------------------------------------------------------

func TestLoadErrors(t *testing.T) {
	dir := t.TempDir()
	cases := []struct {
		name     string
		load     func() error
		code     ErrorCode
		sentinel error
	}{
		{"missing dex", func() error { _, err := FromDEX(data("nonexistent.dex")); return err },
			CodeFileNotFound, ErrFileNotFound},
		{"not a dex", func() error { _, err := FromDEX(data("TestDex.java")); return err },
			CodeInvalidDex, ErrInvalidDex},
		{"directory as dex", func() error { _, err := FromDEX(dir); return err },
			CodeCannotRead, ErrCannotRead},
		{"one bad file of several", func() error {
			_, err := FromDEXFiles([]string{data("classes.dex"), data("TestDex.java")})
			return err
		}, CodeInvalidDex, ErrInvalidDex},
		{"missing apk", func() error { _, err := FromAPK(data("missing.apk")); return err },
			CodeFileNotFound, ErrFileNotFound},
		{"apk without classes.dex", func() error { _, err := FromAPK(data("nodex.apk")); return err },
			CodeInvalidAPK, ErrInvalidAPK},
		{"dex as apk", func() error { _, err := FromAPK(data("classes.dex")); return err },
			CodeInvalidAPK, ErrInvalidAPK},
		{"directory as apk", func() error { _, err := FromAPK(dir); return err },
			CodeCannotRead, ErrCannotRead},
		{"open missing apk", func() error { _, err := OpenAPK(data("missing.apk")); return err },
			CodeFileNotFound, ErrFileNotFound},
		{"open dex as apk", func() error { _, err := OpenAPK(data("classes.dex")); return err },
			CodeInvalidAPK, ErrInvalidAPK},
		{"open directory", func() error { _, err := OpenAPK(dir); return err },
			CodeCannotRead, ErrCannotRead},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			err := c.load()
			var e *Error
			if !errors.As(err, &e) {
				t.Fatalf("err = %v (%T), want an *Error", err, err)
			}
			if e.Code != c.code {
				t.Errorf("Code = %v, want %v", e.Code, c.code)
			}
			if e.Message == "" || err.Error() != e.Message {
				t.Errorf("Message = %q, Error() = %q", e.Message, err.Error())
			}
			if len(e.Problems) != 0 {
				t.Errorf("Problems = %v, want none", e.Problems)
			}
			if !errors.Is(err, c.sentinel) {
				t.Errorf("errors.Is(err, %v) = false", c.sentinel)
			}
			if errors.Is(err, ErrInternal) {
				t.Error("errors.Is(err, ErrInternal) = true")
			}
			if got, want := errors.Is(err, fs.ErrNotExist), c.code == CodeFileNotFound; got != want {
				t.Errorf("errors.Is(err, fs.ErrNotExist) = %v, want %v", got, want)
			}
		})
	}
}

func TestErrorCodesHaveNames(t *testing.T) {
	sentinels := map[ErrorCode]*Error{
		CodeFileNotFound: ErrFileNotFound, CodeCannotRead: ErrCannotRead,
		CodeCannotWrite: ErrCannotWrite, CodeInvalidAPK: ErrInvalidAPK,
		CodeInvalidDex: ErrInvalidDex, CodeUnsupportedDex: ErrUnsupportedDex,
		CodeInvalidModel: ErrInvalidModel, CodePoolOverflow: ErrPoolOverflow,
		CodeVersionTooLow: ErrVersionTooLow, CodeModel: ErrModel, CodeInternal: ErrInternal,
	}
	// The C API numbers its codes from 1 to 11; each one has a sentinel and a
	// name.
	for code := ErrorCode(1); code <= 11; code++ {
		s, ok := sentinels[code]
		if !ok || s.Code != code {
			t.Errorf("code %d has no sentinel", code)
		}
		if strings.HasPrefix(code.String(), "error code") {
			t.Errorf("code %d has no name", code)
		}
	}
	if got := ErrorCode(99).String(); got != "error code 99" {
		t.Errorf("ErrorCode(99).String() = %q", got)
	}
}

// ---- Classes, methods, fields ---------------------------------------------

func TestClass(t *testing.T) {
	ctx := mustDEX(t, "shapes.dex")
	circle := mustClass(t, ctx, "LCircle;")
	if got := circle.Name(); got != "LCircle;" {
		t.Errorf("Name = %q", got)
	}
	if got := circle.Package(); got != "" {
		t.Errorf("Package = %q, want empty for the default package", got)
	}
	sup := circle.Superclass()
	if sup == nil || sup.Descriptor() != "LAbstractShape;" || !sup.IsResolved() {
		t.Fatalf("Superclass = %v, want the resolved LAbstractShape;", sup)
	}
	if c := sup.Resolve(); c == nil || c.Name() != "LAbstractShape;" {
		t.Errorf("Resolve() = %v", c)
	}
	if file, ok := circle.SourceFile(); !ok || file != "Shapes.java" {
		t.Errorf("SourceFile = %q, %v", file, ok)
	}

	abstract := mustClass(t, ctx, "LAbstractShape;")
	if !abstract.IsAbstract() || abstract.IsInterface() {
		t.Errorf("LAbstractShape;: abstract %v, interface %v", abstract.IsAbstract(), abstract.IsInterface())
	}
	ifaces := abstract.Interfaces()
	if len(ifaces) != 1 || ifaces[0].Descriptor() != "LShape;" {
		t.Errorf("Interfaces = %v", ifaces)
	}
	shape := mustClass(t, ctx, "LShape;")
	if !shape.IsInterface() || !shape.IsAbstract() || !shape.AccessFlags().Has(AccessInterface) {
		t.Errorf("LShape; is not an interface: %v", shape.AccessFlags())
	}

	// The superclass of a class outside the loaded files does not resolve.
	object := shape.Superclass()
	if object == nil || object.Descriptor() != "Ljava/lang/Object;" {
		t.Fatalf("LShape; superclass = %v", object)
	}
	if object.IsResolved() || object.Resolve() != nil {
		t.Error("Ljava/lang/Object; resolved in shapes.dex")
	}
}

func TestMethods(t *testing.T) {
	ctx := mustDEX(t, "classes.dex")
	cls := mustClass(t, ctx, "LTestDex;")
	var names []string
	for _, m := range cls.Methods() {
		names = append(names, m.Name())
	}
	if !slices.Equal(names, []string{"<init>", "helloDex", "main"}) {
		t.Fatalf("methods = %v", names)
	}

	hello := mustMethod(t, cls, "helloDex")
	if !hello.IsStatic() || !hello.HasCode() || !hello.IsPublic() || hello.IsPrivate() {
		t.Errorf("helloDex flags = %v", hello.AccessFlags())
	}
	if got := hello.ReturnType(); got != "Ljava/lang/String;" {
		t.Errorf("ReturnType = %q", got)
	}
	if got := hello.Parameters(); len(got) != 0 {
		t.Errorf("Parameters = %v, want none", got)
	}
	if got := hello.Descriptor(); got != "()Ljava/lang/String;" {
		t.Errorf("Descriptor = %q", got)
	}
	if got := hello.DeclaringClass().Descriptor(); got != "LTestDex;" {
		t.Errorf("DeclaringClass = %q", got)
	}
	want := MethodID{"LTestDex;", "helloDex", "()Ljava/lang/String;"}
	if got := hello.ID(); got != want {
		t.Errorf("ID = %v, want %v", got, want)
	}
	if got := want.String(); got != "LTestDex;->helloDex()Ljava/lang/String;" {
		t.Errorf("MethodID.String = %q", got)
	}

	main := mustMethod(t, cls, "main")
	if got := main.Parameters(); !slices.Equal(got, []string{"[Ljava/lang/String;"}) {
		t.Errorf("main parameters = %v", got)
	}
	if main.IsConstructor() {
		t.Error("main is a constructor")
	}
	if init := mustMethod(t, cls, "<init>"); !init.IsConstructor() ||
		!init.AccessFlags().Has(AccessConstructor) {
		t.Error("<init> is not a constructor")
	}

	// An abstract method has no code.
	pets := mustDEX(t, "polymorphic.dex")
	sound := mustMethod(t, mustClass(t, pets, "LAnimal;"), "sound")
	if sound.HasCode() || !sound.IsAbstract() || sound.IsNative() || sound.RegisterCount() != 0 {
		t.Errorf("Animal.sound: has code %v, abstract %v", sound.HasCode(), sound.IsAbstract())
	}
}

func TestAccessFlags(t *testing.T) {
	ctx := mustDEX(t, "classes.dex")
	cls := mustClass(t, ctx, "LTestDex;")
	flags := cls.AccessFlags()
	if flags != AccessPublic || !flags.Has(AccessPublic) || flags.Has(AccessFinal) {
		t.Errorf("LTestDex; flags = %v", flags)
	}
	hello := mustMethod(t, cls, "helloDex").AccessFlags()
	if hello != AccessPublic|AccessStatic || hello.String() != "public|static" {
		t.Errorf("helloDex flags = %v", hello)
	}
	// The DEX format gives two pairs of flags the same bit.
	if AccessBridge != AccessVolatile || AccessVarargs != AccessTransient {
		t.Error("shared bits differ")
	}
	if got := AccessFlags(0).String(); got != "0" {
		t.Errorf("no flags = %q", got)
	}
	if got := (AccessVolatile | 0x80000000).String(); got != "volatile/bridge|0x80000000" {
		t.Errorf("unknown bit = %q", got)
	}
}

func TestFields(t *testing.T) {
	ctx := mustDEX(t, "annotated.dex")
	cls := mustClass(t, ctx, "LAnnotated;")
	var names []string
	for _, f := range cls.Fields() {
		names = append(names, f.Name())
	}
	slices.Sort(names)
	if !slices.Equal(names, []string{"BIG", "FLAG", "GREETING", "MAGIC", "PI", "counter"}) {
		t.Errorf("fields = %v", names)
	}
	magic := mustField(t, cls, "MAGIC")
	if magic.Type() != "I" || !magic.IsStatic() || !magic.IsFinal() || magic.IsPublic() {
		t.Errorf("MAGIC: type %q, flags %v", magic.Type(), magic.AccessFlags())
	}
	if got := magic.DeclaringClass().Descriptor(); got != "LAnnotated;" {
		t.Errorf("DeclaringClass = %q", got)
	}
	want := FieldID{"LAnnotated;", "MAGIC", "I"}
	if got := magic.ID(); got != want || got.String() != "LAnnotated;->MAGIC:I" {
		t.Errorf("ID = %v", got)
	}
}

func TestInitialValues(t *testing.T) {
	ctx := mustDEX(t, "annotated.dex")
	cls := mustClass(t, ctx, "LAnnotated;")
	for name, want := range map[string]AnnotationValue{
		"MAGIC": int64(42), "GREETING": "hello", "BIG": int64(1234567890123),
		"PI": 3.25, "FLAG": true,
	} {
		got, ok := mustField(t, cls, name).InitialValue()
		if !ok || got != want {
			t.Errorf("%s = %#v, %v; want %#v", name, got, ok, want)
		}
	}
	if got, ok := mustField(t, cls, "counter").InitialValue(); ok {
		t.Errorf("counter = %#v, want no recorded value", got)
	}
}

func TestAnnotations(t *testing.T) {
	ctx := mustDEX(t, "annotated.dex")
	cls := mustClass(t, ctx, "LAnnotated;")

	find := func(list []Annotation, descriptor string) Annotation {
		t.Helper()
		for _, a := range list {
			if a.TypeDescriptor == descriptor {
				return a
			}
		}
		t.Fatalf("no %s annotation in %v", descriptor, list)
		return Annotation{}
	}
	element := func(a Annotation, name string) AnnotationValue {
		t.Helper()
		v, ok := a.Element(name)
		if !ok {
			t.Fatalf("%s has no element %s", a.TypeDescriptor, name)
		}
		return v
	}

	marker := find(cls.Annotations(), "LMarker;")
	if marker.Visibility != VisibilityRuntime {
		t.Errorf("Visibility = %v", marker.Visibility)
	}
	if got := element(marker, "name"); got != "cls" {
		t.Errorf("name = %#v", got)
	}
	if got := element(marker, "level"); got != int64(3) {
		t.Errorf("level = %#v", got)
	}
	tags, ok := element(marker, "tags").([]AnnotationValue)
	if !ok || len(tags) != 2 || tags[0] != "a" || tags[1] != "b" {
		t.Errorf("tags = %#v", element(marker, "tags"))
	}
	if _, ok := marker.Element("target"); ok {
		t.Error("target is listed, but it is left at its default")
	}

	greet := find(mustMethod(t, cls, "greet").Annotations(), "LMarker;")
	if got := element(greet, "target"); got != (TypeRef{Descriptor: "Ljava/lang/String;"}) {
		t.Errorf("target = %#v", got)
	}
	find(mustMethod(t, cls, "old").Annotations(), "Ljava/lang/Deprecated;")
	if got := element(find(mustField(t, cls, "MAGIC").Annotations(), "LMarker;"), "name"); got != "fld" {
		t.Errorf("field marker name = %#v", got)
	}

	// The annotation class itself: an enum value, an array of enum values,
	// and the defaults as a nested annotation with an empty array.
	m := mustClass(t, ctx, "LMarker;")
	retention := find(m.Annotations(), "Ljava/lang/annotation/Retention;")
	want := EnumRef{Constant: FieldID{"Ljava/lang/annotation/RetentionPolicy;", "RUNTIME",
		"Ljava/lang/annotation/RetentionPolicy;"}}
	if got := element(retention, "value"); got != want {
		t.Errorf("Retention.value = %#v", got)
	}
	target := find(m.Annotations(), "Ljava/lang/annotation/Target;")
	kinds, ok := element(target, "value").([]AnnotationValue)
	if !ok || len(kinds) != 3 {
		t.Fatalf("Target.value = %#v", element(target, "value"))
	}
	if e, ok := kinds[1].(EnumRef); !ok || e.Constant.Name != "METHOD" {
		t.Errorf("Target.value[1] = %#v", kinds[1])
	}
	defaults := find(m.Annotations(), "Ldalvik/annotation/AnnotationDefault;")
	if defaults.Visibility != VisibilitySystem {
		t.Errorf("AnnotationDefault visibility = %v", defaults.Visibility)
	}
	nested, ok := element(defaults, "value").(Annotation)
	if !ok || nested.TypeDescriptor != "LMarker;" {
		t.Fatalf("AnnotationDefault.value = %#v", element(defaults, "value"))
	}
	if got := element(nested, "level"); got != int64(1) {
		t.Errorf("default level = %#v", got)
	}
	if got, ok := element(nested, "tags").([]AnnotationValue); !ok || got == nil || len(got) != 0 {
		t.Errorf("default tags = %#v, want an empty array", element(nested, "tags"))
	}
	if got := element(nested, "target"); got != (TypeRef{Descriptor: "Ljava/lang/Object;"}) {
		t.Errorf("default target = %#v", got)
	}
}

// Every annotation of every class of every fixture converts without a crash,
// and only into the documented Go types.
func TestEveryAnnotationValueHasADocumentedType(t *testing.T) {
	var check func(v AnnotationValue)
	check = func(v AnnotationValue) {
		switch v := v.(type) {
		case nil, bool, int64, float64, string, TypeRef, EnumRef, FieldRef, MethodRef,
			MethodTypeRef, MethodHandleRef:
		case []AnnotationValue:
			for _, e := range v {
				check(e)
			}
		case Annotation:
			for _, e := range v.Elements {
				check(e.Value)
			}
		default:
			t.Errorf("undocumented value type %T", v)
		}
	}
	for _, name := range []string{"annotated.dex", "inherit.dex", "polymorphic.dex", "shapes.dex",
		"classes.dex", "fibonacci.dex", "trycatch.dex", "multistring.dex"} {
		ctx := mustDEX(t, name)
		for _, cls := range ctx.Classes() {
			for _, a := range cls.Annotations() {
				check(a)
			}
			for _, m := range cls.Methods() {
				for _, a := range m.Annotations() {
					check(a)
				}
			}
			for _, f := range cls.Fields() {
				for _, a := range f.Annotations() {
					check(a)
				}
				if v, ok := f.InitialValue(); ok {
					check(v)
				}
			}
		}
	}
}

// ---- Handles --------------------------------------------------------------

func TestHandlesOutliveTheirContext(t *testing.T) {
	ctx, err := FromDEX(data("classes.dex"))
	if err != nil {
		t.Fatal(err)
	}
	cls := ctx.FindClass("LTestDex;")
	methods := cls.Methods()
	ctx.Close()
	ctx.Close() // a second Close does nothing
	runtime.GC()
	if got := cls.Name(); got != "LTestDex;" {
		t.Errorf("Name after Close = %q", got)
	}
	cls.Close()
	for _, m := range methods {
		if m.Name() == "" || len(m.Instructions()) == 0 {
			t.Errorf("method %q unusable after its class and context are closed", m.Name())
		}
	}
}

// A handle that nothing else refers to survives a garbage collection in the
// middle of a chain of calls (runtime.KeepAlive in every method).
func TestChainedCallsSurviveGC(t *testing.T) {
	for i := 0; i < 50; i++ {
		ctx, err := FromDEX(data("fibonacci.dex"))
		if err != nil {
			t.Fatal(err)
		}
		name := ctx.FindClass("LFibonacci;").Methods()[0].DeclaringClass().Resolve().Name()
		if name != "LFibonacci;" {
			t.Fatalf("name = %q", name)
		}
		runtime.GC()
	}
}

func expectPanic(t *testing.T, want string, f func()) {
	t.Helper()
	defer func() {
		t.Helper()
		got, _ := recover().(string)
		if !strings.Contains(got, want) {
			t.Errorf("panic = %q, want one that mentions %q", got, want)
		}
	}()
	f()
}

func TestClosedAndNilHandlesPanic(t *testing.T) {
	ctx := mustDEX(t, "polymorphic.dex")
	cls := mustClass(t, ctx, "LDog;")
	method := cls.Methods()[0]
	field := mustClass(t, ctx, "LPet;").Fields()[0]
	ref := cls.Superclass()
	graph, _ := ctx.CallGraph()
	hierarchy, _ := ctx.ClassHierarchy()
	xrefs, _ := ctx.Xrefs()
	apk, err := OpenAPK(data("sample.apk"))
	if err != nil {
		t.Fatal(err)
	}
	resources := apk.Resources()
	model := NewModel(35)

	for _, h := range []interface{ Close() }{cls, method, field, ref, graph, hierarchy, xrefs, apk, model} {
		h.Close()
		h.Close()
	}
	expectPanic(t, "*Class used after Close", func() { cls.Name() })
	expectPanic(t, "*Method used after Close", func() { method.Instructions() })
	expectPanic(t, "*Field used after Close", func() { field.Type() })
	expectPanic(t, "*ClassRef used after Close", func() { ref.Descriptor() })
	expectPanic(t, "*CallGraph used after Close", func() { graph.Nodes() })
	expectPanic(t, "*ClassHierarchy used after Close", func() { hierarchy.Empty() })
	expectPanic(t, "*Xrefs used after Close", func() { xrefs.Referrers() })
	expectPanic(t, "*Apk used after Close", func() { apk.Entries() })
	expectPanic(t, "*Apk used after Close", func() { resources.Empty() })
	expectPanic(t, "*Model used after Close", func() { model.Write(true) })

	var nilContext *Context
	var nilClass *Class
	var nilModel *Model
	var nilResources *ResourceTable
	nilContext.Close() // Close on nil does nothing
	nilClass.Close()
	nilModel.Close()
	expectPanic(t, "nil *Context", func() { nilContext.Classes() })
	expectPanic(t, "nil *Class", func() { nilClass.Methods() })
	expectPanic(t, "nil *Model", func() { NewModel(35).CopyClass(nilModel, "LA;") })
	expectPanic(t, "nil *ResourceTable", func() { nilResources.ResolveString(1) })
	expectPanic(t, "nil *Method", func() { (*Method)(nil).Cfg() })
}

// Reading from one analysis in several goroutines at once is safe. Run with
// -race to check the Go side; the C++ side has its own threaded tests.
func TestConcurrentReads(t *testing.T) {
	ctx := mustDEX(t, "polymorphic.dex")
	classes := ctx.Classes()
	var wg sync.WaitGroup
	for i := 0; i < 8; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for _, cls := range classes {
				for _, m := range cls.Methods() {
					for _, insn := range m.Instructions() {
						_ = ToString(insn)
					}
					_ = m.Cfg()
					_ = m.Calls()
				}
			}
			g, err := ctx.CallGraph()
			if err != nil {
				t.Error(err)
				return
			}
			if g.EdgeCount() == 0 {
				t.Error("empty call graph")
			}
			x, err := ctx.Xrefs()
			if err != nil {
				t.Error(err)
				return
			}
			_ = x.Referrers()
		}()
	}
	wg.Wait()
}
