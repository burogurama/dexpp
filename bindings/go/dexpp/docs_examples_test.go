package dexpp_test

// The guides of the documentation show the testable examples of the
// example_*_test.go files: the code between a "--8<-- [start:name]" and a
// "--8<-- [end:name]" marker, and the output from
// testdata/examples/TOPIC.NAME.txt, where TOPIC is the part of the file name
// between "example_" and "_test.go". go test checks that an example prints its
// "Output:" comment. This test checks that each output file holds exactly that
// comment, so the output on the page is the output go test checks.
//
// After changing an example, write the output files again and look at the diff:
//
//	go test ./dexpp -run TestDocsExampleOutputs -update

import (
	"flag"
	"go/ast"
	"go/doc"
	"go/parser"
	"go/token"
	"os"
	"path/filepath"
	"regexp"
	"slices"
	"strings"
	"testing"
)

var update = flag.Bool("update", false, "write testdata/examples from the Output comments of the examples")

var startMarker = regexp.MustCompile(`^// --8<-- \[start:([a-z_]+)\]$`)

// exampleOutputs returns the Output comment of every example, keyed by the
// name of its output file.
func exampleOutputs(t *testing.T) map[string]string {
	t.Helper()
	paths, err := filepath.Glob("example_*_test.go")
	if err != nil {
		t.Fatal(err)
	}
	if len(paths) == 0 {
		t.Fatal("no example_*_test.go files")
	}
	outputs := map[string]string{}
	fset := token.NewFileSet()
	for _, path := range paths {
		file, err := parser.ParseFile(fset, path, nil, parser.ParseComments)
		if err != nil {
			t.Fatal(err)
		}
		topic := strings.TrimSuffix(strings.TrimPrefix(path, "example_"), "_test.go")
		for _, example := range doc.Examples(file) {
			where := path + ": Example" + example.Name
			sections := sectionsIn(example.Code, file.Comments)
			if len(sections) != 1 {
				t.Errorf("%s: has %d start markers, want 1", where, len(sections))
				continue
			}
			if example.Output == "" || example.Unordered {
				t.Errorf("%s: needs an ordered, non-empty Output comment", where)
				continue
			}
			name := topic + "." + sections[0] + ".txt"
			if _, seen := outputs[name]; seen {
				t.Errorf("%s: a second example for %s", where, name)
			}
			outputs[name] = example.Output
		}
	}
	return outputs
}

// sectionsIn returns the names of the start markers inside a function body.
func sectionsIn(body ast.Node, comments []*ast.CommentGroup) []string {
	var names []string
	for _, group := range comments {
		if group.Pos() < body.Pos() || group.End() > body.End() {
			continue
		}
		for _, comment := range group.List {
			if m := startMarker.FindStringSubmatch(comment.Text); m != nil {
				names = append(names, m[1])
			}
		}
	}
	return names
}

func TestDocsExampleOutputs(t *testing.T) {
	outputs := exampleOutputs(t)
	dir := filepath.Join("testdata", "examples")
	if *update {
		if err := os.RemoveAll(dir); err != nil {
			t.Fatal(err)
		}
		if err := os.MkdirAll(dir, 0o755); err != nil {
			t.Fatal(err)
		}
		for name, output := range outputs {
			if err := os.WriteFile(filepath.Join(dir, name), []byte(output), 0o644); err != nil {
				t.Fatal(err)
			}
		}
		return
	}

	for name, output := range outputs {
		saved, err := os.ReadFile(filepath.Join(dir, name))
		if err != nil {
			t.Errorf("%s: %v (run with -update)", name, err)
			continue
		}
		// Git on Windows may check the file out with CRLF line ends.
		if text := strings.ReplaceAll(string(saved), "\r\n", "\n"); text != output {
			t.Errorf("%s differs from the Output comment of its example (run with -update):\nfile:\n%s\ncomment:\n%s",
				name, text, output)
		}
	}
	entries, err := os.ReadDir(dir)
	if err != nil {
		t.Fatal(err)
	}
	var leftover []string
	for _, entry := range entries {
		if _, ok := outputs[entry.Name()]; !ok {
			leftover = append(leftover, entry.Name())
		}
	}
	slices.Sort(leftover)
	if len(leftover) > 0 {
		t.Errorf("output files with no example behind them: %s", strings.Join(leftover, ", "))
	}
}
