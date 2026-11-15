package dexpp_test

// The testable examples of the guide "Editing DEX files". docs_examples_test.go
// says how the guide uses them. Code outside the markers is not shown: it sets
// up the variables that the earlier sections of the guide made.

import (
	"bytes"
	"errors"
	"fmt"
	"log"
	"os"
	"path/filepath"

	"github.com/burogurama/dexpp/bindings/go/dexpp"
)

// readModel reads a DEX file into a model, with debug information.
func readModel(path string) *dexpp.Model {
	model, err := dexpp.ReadModelFile(path, true)
	if err != nil {
		log.Fatal(err)
	}
	return model
}

// readRenamedShapes reads shapes.dex and renames Circle, as the guide's rename
// section does.
func readRenamedShapes() *dexpp.Model {
	model := readModel("../../../tests/data/shapes.dex")
	model.RenameTypes(map[string]string{"LCircle;": "Lcom/example/Round;"})
	return model
}

// Read a DEX file into a model, and look at its classes.
func ExampleReadModelFile() {
	// --8<-- [start:read]
	model, err := dexpp.ReadModelFile("../../../tests/data/shapes.dex", true)
	if err != nil {
		log.Fatal(err)
	}
	defer model.Close()
	fmt.Println("DEX version", model.Version(), "with", model.ClassCount(), "classes")
	fmt.Println(model.ClassDescriptors())

	info, ok := model.ClassInfo("LCircle;")
	if !ok {
		log.Fatal("no Circle class")
	}
	if info.Superclass != nil {
		fmt.Println(info.Descriptor, "extends", *info.Superclass, "-", info.MethodCount, "methods")
	}

	abstract, ok := model.ClassInfo("LAbstractShape;")
	if !ok {
		log.Fatal("no AbstractShape class")
	}
	fmt.Println(abstract.Descriptor, "implements", abstract.Interfaces)
	// --8<-- [end:read]

	// Output:
	// DEX version 35 with 4 classes
	// [LShape; LAbstractShape; LCircle; LRectangle;]
	// LCircle; extends LAbstractShape; - 4 methods
	// LAbstractShape; implements [LShape;]
}

// Split a DEX file into one file per class.
func ExampleModel_CopyClass() {
	model := readModel("../../../tests/data/shapes.dex")
	defer model.Close()

	// --8<-- [start:split]
	outDir, err := os.MkdirTemp("", "dexpp")
	if err != nil {
		log.Fatal(err)
	}
	defer os.RemoveAll(outDir)

	for index, descriptor := range model.ClassDescriptors() {
		one := dexpp.NewModel(model.Version())
		if err := one.CopyClass(model, descriptor); err != nil {
			log.Fatal(err)
		}
		path := filepath.Join(outDir, fmt.Sprintf("class%d.dex", index))
		if err := one.WriteFile(path, true); err != nil {
			log.Fatal(err)
		}
		one.Close()
	}

	entries, err := os.ReadDir(outDir) // sorted by name
	if err != nil {
		log.Fatal(err)
	}
	var fileNames []string
	for _, entry := range entries {
		fileNames = append(fileNames, entry.Name())
	}
	fmt.Println(fileNames)
	// --8<-- [end:split]

	// Output:
	// [class0.dex class1.dex class2.dex class3.dex]
}

// Merge several DEX files into one model.
func ExampleModel_MoveClass() {
	model := readModel("../../../tests/data/shapes.dex")
	defer model.Close()
	outDir, err := os.MkdirTemp("", "dexpp")
	if err != nil {
		log.Fatal(err)
	}
	defer os.RemoveAll(outDir)
	var fileNames []string
	for index, descriptor := range model.ClassDescriptors() {
		one := dexpp.NewModel(model.Version())
		if err := one.CopyClass(model, descriptor); err != nil {
			log.Fatal(err)
		}
		name := fmt.Sprintf("class%d.dex", index)
		if err := one.WriteFile(filepath.Join(outDir, name), true); err != nil {
			log.Fatal(err)
		}
		one.Close()
		fileNames = append(fileNames, name)
	}

	// --8<-- [start:merge]
	merged := dexpp.NewModel(model.Version())
	defer merged.Close()
	for _, fileName := range fileNames {
		part, err := dexpp.ReadModelFile(filepath.Join(outDir, fileName), true)
		if err != nil {
			log.Fatal(err)
		}
		for _, descriptor := range part.ClassDescriptors() {
			if err := merged.MoveClass(part, descriptor); err != nil {
				log.Fatal(err)
			}
		}
		part.Close()
	}

	fmt.Println(merged.ClassCount(), "classes")
	mergedBytes, err := merged.Write(true)
	if err != nil {
		log.Fatal(err)
	}
	originalBytes, err := model.Write(true)
	if err != nil {
		log.Fatal(err)
	}
	fmt.Println("same bytes as the original model:", bytes.Equal(mergedBytes, originalBytes))
	// --8<-- [end:merge]

	// Output:
	// 4 classes
	// same bytes as the original model: true
}

// Rename a class everywhere it is referenced.
func ExampleModel_RenameTypes() {
	model := readModel("../../../tests/data/shapes.dex")
	defer model.Close()

	// --8<-- [start:rename]
	renames := map[string]string{"LCircle;": "Lcom/example/Round;"}
	model.RenameTypes(renames)
	fmt.Println(model.ClassDescriptors())
	// --8<-- [end:rename]

	// Output:
	// [LShape; LAbstractShape; Lcom/example/Round; LRectangle;]
}

// Check a model before writing it.
func ExampleModel_Validate() {
	model := readRenamedShapes()
	defer model.Close()

	// --8<-- [start:validate]
	problems := model.Validate()
	fmt.Println("problems:", problems)

	stats := model.PoolStats()
	fmt.Println(stats.Strings, "strings,", stats.Types, "types,", stats.Methods, "methods")
	needed := model.RequiredVersion()
	fmt.Println("needs DEX version", needed)
	// --8<-- [end:validate]

	// Output:
	// problems: []
	// 22 strings, 8 types, 12 methods
	// needs DEX version 35
}

// Write a model, and check that writing is repeatable.
func ExampleModel_Write() {
	model := readRenamedShapes()
	defer model.Close()

	// --8<-- [start:write]
	data, err := model.Write(true)
	if err != nil {
		log.Fatal(err)
	}
	again, err := dexpp.ReadModel(data, true)
	if err != nil {
		log.Fatal(err)
	}
	defer again.Close()
	againData, err := again.Write(true)
	if err != nil {
		log.Fatal(err)
	}
	fmt.Println(len(data), "bytes; writing is repeatable:", bytes.Equal(againData, data))
	// --8<-- [end:write]

	// Output:
	// 1364 bytes; writing is repeatable: true
}

// Reading input that is not a DEX file fails with ErrInvalidDex.
func ExampleReadModel() {
	// --8<-- [start:errors]
	_, err := dexpp.ReadModel([]byte("not a dex file"), true)
	if errors.Is(err, dexpp.ErrInvalidDex) {
		fmt.Println("invalid DEX:", err)
	}
	// --8<-- [end:errors]

	// Output:
	// invalid DEX: not a DEX file (bad magic)
}

// Writing a model that breaks rules of the format fails with the list of
// broken rules.
func ExampleModel_Write_invalid() {
	// --8<-- [start:invalid]
	broken, err := dexpp.ReadModelFile("../../../tests/data/shapes.dex", true)
	if err != nil {
		log.Fatal(err)
	}
	defer broken.Close()
	broken.RenameTypes(map[string]string{"LCircle;": "not a descriptor"})

	_, err = broken.Write(true)
	var modelErr *dexpp.Error
	if errors.As(err, &modelErr) && modelErr.Code == dexpp.CodeInvalidModel {
		fmt.Println(len(modelErr.Problems), "problems")
		for _, problem := range modelErr.Problems {
			fmt.Println(" ", problem.Where, "-", problem.Message)
		}
	}
	// --8<-- [end:invalid]

	// Output:
	// 2 problems
	//   not a descriptor - invalid class descriptor "not a descriptor"
	//   not a descriptor-><init>(Ljava/lang/String;D)V, item 3 - invalid class descriptor "not a descriptor"
}

// Writing fails when the model needs a higher DEX version than it has.
func ExampleModel_SetVersion() {
	// --8<-- [start:version]
	defaults, err := dexpp.ReadModelFile("../../../tests/data/inherit.dex", true)
	if err != nil {
		log.Fatal(err)
	}
	defer defaults.Close()
	defaults.SetVersion(35)
	if _, err := defaults.Write(true); errors.Is(err, dexpp.ErrVersionTooLow) {
		fmt.Println("version too low:", err)
	}

	defaults.SetVersion(defaults.RequiredVersion())
	data, err := defaults.Write(true)
	if err != nil {
		log.Fatal(err)
	}
	fmt.Println("written with version", defaults.Version(), "-", len(data), "bytes")
	// --8<-- [end:version]

	// Output:
	// version too low: the model needs DEX 037 (LGreeter;->greet()V: a private or default interface method), but its version is 035
	// written with version 37 - 4024 bytes
}
