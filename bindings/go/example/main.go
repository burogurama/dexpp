// Command example is a small demo of the dexpp Go binding. It prints an
// APK's package, its components and which of them other apps can reach, a
// summary of its code, and the size of its call graph.
//
//	go run ./example <path/to.apk>
package main

import (
	"fmt"
	"os"

	"github.com/burogurama/dexpp/bindings/go/dexpp"
)

func main() {
	if len(os.Args) != 2 {
		fmt.Fprintln(os.Stderr, "usage: example <path/to.apk>")
		os.Exit(2)
	}
	if err := run(os.Args[1]); err != nil {
		fmt.Fprintln(os.Stderr, "example:", err)
		os.Exit(1)
	}
}

func run(path string) error {
	apk, err := dexpp.OpenAPK(path)
	if err != nil {
		return err
	}
	defer apk.Close()

	if m, ok := apk.Manifest(); ok {
		fmt.Println("package:", m.Package)
		fmt.Println("components:")
		for _, c := range m.Components {
			mark := ""
			if c.IsReachable {
				mark = "  [reachable]"
			} else if c.IsExported {
				mark = "  [exported, signature-gated]"
			}
			fmt.Printf("  %-9s %s%s\n", c.Kind, c.Name, mark)
		}
	} else {
		fmt.Println("no manifest")
	}

	ctx, err := apk.Analysis()
	if err != nil {
		return err
	}
	defer ctx.Close()

	classes := ctx.Classes()
	methods := 0
	for _, c := range classes {
		methods += len(c.Methods())
	}
	fmt.Printf("code: %d classes, %d methods, %d strings\n",
		len(classes), methods, len(ctx.Strings()))

	g, err := ctx.CallGraph()
	if err != nil {
		return err
	}
	fmt.Printf("call graph: %d methods, %d call edges\n", g.NodeCount(), g.EdgeCount())
	return nil
}
