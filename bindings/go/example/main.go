// Command example is a small demo of the dexpp Go binding: it prints an APK's
// package, its reachable components, and a class/method summary.
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
	path := os.Args[1]

	apk, err := dexpp.OpenAPK(path)
	if err != nil {
		fmt.Fprintln(os.Stderr, "open:", err)
		os.Exit(1)
	}
	defer apk.Close()

	fmt.Println("package:", apk.Package())

	fmt.Println("components:")
	for _, c := range apk.Components() {
		mark := ""
		if c.Reachable {
			mark = "  [reachable]"
		} else if c.Exported {
			mark = "  [exported, signature-gated]"
		}
		fmt.Printf("  %-9s %s%s\n", c.Kind, c.Name, mark)
	}

	ctx, err := apk.Analysis()
	if err != nil {
		fmt.Fprintln(os.Stderr, "analysis:", err)
		os.Exit(1)
	}
	defer ctx.Close()

	classes := ctx.Classes()
	methods := 0
	for _, c := range classes {
		methods += len(c.Methods())
	}
	fmt.Printf("code: %d classes, %d methods, %d strings\n",
		len(classes), methods, len(ctx.Strings()))
}
