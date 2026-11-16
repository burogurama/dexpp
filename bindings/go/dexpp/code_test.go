package dexpp

import (
	"fmt"
	"slices"
	"strings"
	"testing"
)

func TestInstructions(t *testing.T) {
	ctx := mustDEX(t, "classes.dex")
	cls := mustClass(t, ctx, "LTestDex;")

	hello := mustMethod(t, cls, "helloDex").Instructions()
	if len(hello) != 2 {
		t.Fatalf("helloDex has %d instructions", len(hello))
	}
	cs, ok := hello[0].(ConstStringInstruction)
	if !ok || cs.Base().Offset != 0 || cs.Size != 2 {
		t.Errorf("first instruction = %#v", hello[0])
	}
	if ret, ok := hello[1].(ReturnInstruction); !ok || !ret.HasReg || ret.Reg != cs.Dest {
		t.Errorf("second instruction = %#v", hello[1])
	}

	// sget-object v1, field@0; invoke-static {}, method@1; move-result-object v0;
	// invoke-virtual {v1, v0}, method@3; return-void
	main := mustMethod(t, cls, "main").Instructions()
	if len(main) != 5 {
		t.Fatalf("main has %d instructions", len(main))
	}
	field, ok := main[0].(FieldInstruction)
	if !ok || !field.IsStatic || field.IsWrite || field.HasObjectReg || field.ValueReg != 1 {
		t.Errorf("main[0] = %#v", main[0])
	}
	if call, ok := main[1].(InvokeInstruction); !ok || len(call.ArgRegs) != 0 {
		t.Errorf("main[1] = %#v", main[1])
	}
	if move, ok := main[2].(MoveInstruction); !ok || move.Dest != 0 || move.HasSrc ||
		move.Width != WidthSingle {
		t.Errorf("main[2] = %#v", main[2])
	}
	call, ok := main[3].(InvokeInstruction)
	if !ok || !slices.Equal(call.ArgRegs, []uint16{1, 0}) {
		t.Errorf("main[3] = %#v", main[3])
	}
	if ret, ok := main[4].(ReturnInstruction); !ok || ret.HasReg {
		t.Errorf("main[4] = %#v", main[4])
	}

	var text []string
	for _, insn := range main {
		text = append(text, ToString(insn))
	}
	if text[0] != "sget-object v1, field@0" || text[3] != "invoke-virtual method@3 {v1, v0}" ||
		text[4] != "return-void" {
		t.Errorf("ToString = %q", text)
	}
	if got := OpcodeName(call.Opcode); got != "invoke-virtual" {
		t.Errorf("OpcodeName = %q", got)
	}

	// Each instruction starts where the one before it ends.
	offset := uint32(0)
	for _, insn := range main {
		if insn.Base().Offset != offset {
			t.Errorf("instruction at %d, want %d", insn.Base().Offset, offset)
		}
		offset += uint32(insn.Base().Size)
	}

	// A method without code has no instructions.
	pets := mustDEX(t, "polymorphic.dex")
	if got := mustMethod(t, mustClass(t, pets, "LAnimal;"), "sound").Instructions(); got != nil {
		t.Errorf("an abstract method has instructions: %v", got)
	}
}

func TestHandlesAndPolymorphicInstructions(t *testing.T) {
	// handles.dex, Handles.run: invoke-polymorphic {v4, v5}; move-result-object v0;
	// array-length v1, v5; const-method-handle v2; const-method-type v3;
	// invoke-custom {v1}; return-object v0. Handles.spread: invoke-polymorphic/range.
	ctx := mustDEX(t, "handles.dex")
	cls := mustClass(t, ctx, "LHandles;")
	run := mustMethod(t, cls, "run").Instructions()
	if len(run) != 7 {
		t.Fatalf("run has %d instructions", len(run))
	}
	poly, ok := run[0].(InvokePolymorphicInstruction)
	if !ok || !slices.Equal(poly.ArgRegs, []uint16{4, 5}) {
		t.Errorf("run[0] = %#v", run[0])
	}
	if move, ok := run[1].(MoveInstruction); !ok || move.HasSrc {
		t.Errorf("run[1] = %#v", run[1])
	}
	if length, ok := run[2].(ArrayLengthInstruction); !ok || length.Dest != 1 || length.ArrayReg != 5 {
		t.Errorf("run[2] = %#v", run[2])
	}
	if handle, ok := run[3].(ConstMethodHandleInstruction); !ok || handle.Dest != 2 {
		t.Errorf("run[3] = %#v", run[3])
	}
	if mt, ok := run[4].(ConstMethodTypeInstruction); !ok || mt.Dest != 3 {
		t.Errorf("run[4] = %#v", run[4])
	}
	spread := mustMethod(t, cls, "spread").Instructions()
	if r, ok := spread[0].(InvokePolymorphicInstruction); !ok ||
		!slices.Equal(r.ArgRegs, []uint16{0, 1, 2, 3, 4, 5}) || r.ProtoIndex == poly.ProtoIndex {
		t.Errorf("spread[0] = %#v", spread[0])
	}
	calls := mustMethod(t, cls, "run").Calls()
	if len(calls) != 1 || calls[0].Kind != InvokePolymorphic || calls[0].Target.Name != "invokeExact" {
		t.Errorf("Calls = %+v", calls)
	}
}

func TestOpcodeNames(t *testing.T) {
	for op, want := range map[uint8]string{0x00: "nop", 0x1a: "const-string", 0x6e: "invoke-virtual",
		0x73: "unused-73"} {
		if got := OpcodeName(op); got != want {
			t.Errorf("OpcodeName(%#x) = %q, want %q", op, got, want)
		}
	}
	ctx := mustDEX(t, "classes.dex")
	var rendered []string
	for _, insn := range mustMethod(t, mustClass(t, ctx, "LTestDex;"), "helloDex").Instructions() {
		rendered = append(rendered, ToString(insn))
	}
	if !strings.HasPrefix(rendered[0], "const-string v") || !strings.Contains(rendered[0], "string@") ||
		!strings.HasPrefix(rendered[1], "return-object v") {
		t.Errorf("rendered = %q", rendered)
	}
}

// ToString converts the Go instruction back to C. Rendering every
// instruction of every fixture checks the conversion in both directions for
// every kind the fixtures hold.
func TestToStringOfEveryInstruction(t *testing.T) {
	kinds := map[string]bool{}
	for _, name := range []string{"classes.dex", "fibonacci.dex", "trycatch.dex", "shapes.dex",
		"polymorphic.dex", "annotated.dex", "inherit.dex", "multistring.dex", "casts.dex",
		"handles.dex", "switches.dex"} {
		ctx := mustDEX(t, name)
		for _, cls := range ctx.Classes() {
			for _, m := range cls.Methods() {
				for _, insn := range m.Instructions() {
					kinds[fmt.Sprintf("%T", insn)] = true
					name := OpcodeName(insn.Base().Opcode)
					if text := ToString(insn); !strings.HasPrefix(text, name) {
						t.Errorf("%q does not start with %q", text, name)
					}
				}
			}
		}
	}
	if len(kinds) < 15 {
		t.Errorf("the fixtures hold only %d instruction kinds: %v", len(kinds), kinds)
	}
}

func TestDisassemble(t *testing.T) {
	ctx := mustDEX(t, "classes.dex")
	main := mustMethod(t, mustClass(t, ctx, "LTestDex;"), "main")
	want := []DisassemblyLine{
		{0, "sget-object v1, Ljava/lang/System;->out:Ljava/io/PrintStream;"},
		{2, "invoke-static {}, LTestDex;->helloDex()Ljava/lang/String;"},
		{5, "move-result-object v0"},
		{6, "invoke-virtual {v1, v0}, Ljava/io/PrintStream;->println(Ljava/lang/String;)V"},
		{9, "return-void"},
	}
	if got := main.Disassemble(); !slices.Equal(got, want) {
		t.Errorf("Disassemble() = %q, want %q", got, want)
	}
	insns := main.Instructions()
	for i, line := range main.Disassemble() {
		if line.Offset != insns[i].Base().Offset {
			t.Errorf("line %d is at %d, its instruction at %d", i, line.Offset, insns[i].Base().Offset)
		}
	}

	shapes := mustDEX(t, "shapes.dex")
	if lines := mustMethod(t, mustClass(t, shapes, "LShape;"), "area").Disassemble(); lines != nil {
		t.Errorf("an abstract method has lines: %q", lines)
	}

	handles := mustDEX(t, "handles.dex")
	run := mustMethod(t, mustClass(t, handles, "LHandles;"), "run").Disassemble()
	if len(run) != 7 || run[3].Text != "const-method-handle v2, invoke-static@LHandles;->target(I)I" ||
		!strings.HasPrefix(run[5].Text, `invoke-custom {v1}, call_site@0("apply", (I)V)@`) {
		t.Errorf("handles.dex run() = %q", run)
	}
	strs := mustDEX(t, "multistring.dex")
	clinit := mustMethod(t, mustClass(t, strs, "LMultiStringTest;"), "<clinit>")
	escaped := false
	for _, line := range clinit.Disassemble() {
		escaped = escaped || strings.HasSuffix(line.Text, `"test\u0000embedded"`)
	}
	if !escaped {
		t.Errorf("the NUL in a string is not escaped: %q", clinit.Disassemble())
	}
}

// otherInstruction is an Instruction type from outside the package.
type otherInstruction struct{ InstructionBase }

func TestToStringOfAnUnknownInstructionType(t *testing.T) {
	insn := otherInstruction{InstructionBase{Opcode: 0x0e, Size: 1}}
	if got := ToString(insn); got != "return-void" {
		t.Errorf("ToString = %q, want the opcode name alone", got)
	}
}

// Instructions built in Go render like the decoded ones, including the
// operands kept in C memory for the call (switch cases, argument registers).
func TestToStringOfHandMadeInstructions(t *testing.T) {
	for _, c := range []struct {
		insn Instruction
		want string
	}{
		{GotoInstruction{InstructionBase{Opcode: 0x28, Size: 1}, -4}, "goto -4"},
		{IfZeroInstruction{InstructionBase{Opcode: 0x38, Size: 2}, 6, 3}, "if-eqz v3, +6"},
		{InvokeInstruction{InstructionBase{Opcode: 0x71, Size: 3}, 9, []uint16{2, 3}},
			"invoke-static method@9 {v2, v3}"},
		{SwitchInstruction{InstructionBase{Opcode: 0x2b, Size: 3}, 8, 1, true,
			[]SwitchCase{{1, 4}, {2, 6}}}, "packed-switch v1, +8"},
		// A width outside the two values counts as single.
		{MoveInstruction{InstructionBase{Opcode: 0x01, Size: 1}, 2, 3, true, OperandWidth(7)},
			"move v2, v3"},
		{MoveInstruction{InstructionBase{Opcode: 0x0d, Size: 1}, 4, 0, false, WidthSingle},
			"move-exception v4"},
		{InvalidInstruction{InstructionBase{Opcode: 0x3e, Size: 1}}, "unused-3e"},
		{ArrayLengthInstruction{InstructionBase{Opcode: 0x21, Size: 1}, 1, 5}, "array-length v1, v5"},
		{InvokePolymorphicInstruction{InstructionBase{Opcode: 0xfa, Size: 4}, 5, 2, []uint16{4, 5}},
			"invoke-polymorphic method@5 proto@2 {v4, v5}"},
		{ConstMethodHandleInstruction{InstructionBase{Opcode: 0xfe, Size: 2}, 2, 7},
			"const-method-handle v2, method_handle@7"},
		{ConstMethodTypeInstruction{InstructionBase{Opcode: 0xff, Size: 2}, 3, 1},
			"const-method-type v3, proto@1"},
	} {
		if got := ToString(c.insn); got != c.want {
			t.Errorf("ToString(%#v) = %q, want %q", c.insn, got, c.want)
		}
	}
}

func TestCfg(t *testing.T) {
	ctx := mustDEX(t, "fibonacci.dex")
	compute := mustMethod(t, mustClass(t, ctx, "LFibonacci;"), "compute")
	cfg := compute.Cfg()
	if cfg.Empty() || cfg.Len() < 2 || cfg.Len() != len(cfg.Blocks) {
		t.Fatalf("Len = %d", cfg.Len())
	}
	if entry, ok := cfg.Entry(); !ok || entry.Index != 0 || entry.StartOffset != 0 {
		t.Errorf("Entry = %+v, %v", entry, ok)
	}
	if b, ok := cfg.BlockAtOffset(0); !ok || b.Index != 0 {
		t.Errorf("BlockAtOffset(0) = %+v, %v", b, ok)
	}
	if _, ok := cfg.BlockAtOffset(99999); ok {
		t.Error("BlockAtOffset(99999) found a block")
	}
	kinds := map[CfgEdgeKind]bool{}
	insns := compute.Instructions()
	for i, b := range cfg.Blocks {
		if b.Index != uint32(i) {
			t.Errorf("block %d has index %d", i, b.Index)
		}
		if b.InsnCount == 0 || insns[b.FirstInsn].Base().Offset != b.StartOffset {
			t.Errorf("block %d does not start at its first instruction", i)
		}
		if got, ok := cfg.BlockAtOffset(b.StartOffset); !ok || got.Index != b.Index {
			t.Errorf("BlockAtOffset(%d) = %d", b.StartOffset, got.Index)
		}
		for _, e := range b.Successors {
			kinds[e.Kind] = true
			if int(e.TargetBlock) >= cfg.Len() {
				t.Fatalf("edge to block %d of %d", e.TargetBlock, cfg.Len())
			}
			if !slices.Contains(cfg.Blocks[e.TargetBlock].Predecessors, uint32(i)) {
				t.Errorf("block %d is not a predecessor of block %d", i, e.TargetBlock)
			}
		}
	}
	if !kinds[CfgBranchTaken] || !kinds[CfgGoto] {
		t.Errorf("edge kinds = %v", kinds)
	}

	// A method without code has an empty graph.
	pets := mustDEX(t, "polymorphic.dex")
	empty := mustMethod(t, mustClass(t, pets, "LAnimal;"), "sound").Cfg()
	if !empty.Empty() || empty.Len() != 0 {
		t.Errorf("abstract method has %d blocks", empty.Len())
	}
	if _, ok := empty.Entry(); ok {
		t.Error("an empty graph has an entry")
	}
}

func TestTryBlocksAndExceptionEdges(t *testing.T) {
	ctx := mustDEX(t, "trycatch.dex")
	cls := mustClass(t, ctx, "LTryCatch;")
	divide := mustMethod(t, cls, "divide")
	tries := divide.TryBlocks()
	if len(tries) != 1 || tries[0].StartOffset >= tries[0].EndOffset || tries[0].HasCatchAll {
		t.Fatalf("TryBlocks = %+v", tries)
	}
	if h := tries[0].Handlers; len(h) != 1 || h[0].TypeDescriptor != "Ljava/lang/ArithmeticException;" {
		t.Errorf("Handlers = %+v", h)
	}
	exception := false
	cfg := divide.Cfg()
	for _, b := range cfg.Blocks {
		for _, e := range b.Successors {
			if e.Kind == CfgException {
				exception = true
				if e.CatchType != "Ljava/lang/ArithmeticException;" ||
					!cfg.Blocks[e.TargetBlock].IsHandlerEntry {
					t.Errorf("exception edge %+v", e)
				}
				if target, ok := cfg.BlockAtOffset(tries[0].Handlers[0].HandlerOffset); !ok ||
					target.Index != e.TargetBlock {
					t.Errorf("the edge does not lead to the handler")
				}
			}
		}
	}
	if !exception {
		t.Error("divide has no exception edge")
	}

	catchAll := false
	safeParse := mustMethod(t, cls, "safeParse")
	for _, try := range safeParse.TryBlocks() {
		catchAll = catchAll || try.HasCatchAll
	}
	if !catchAll {
		t.Error("safeParse has no catch-all handler")
	}
	// The catch-all handler has edges of its own kind, with no catch type.
	catchAllEdge := false
	for _, b := range safeParse.Cfg().Blocks {
		for _, e := range b.Successors {
			if e.Kind == CfgExceptionCatchAll {
				catchAllEdge = true
				if e.CatchType != "" {
					t.Errorf("catch-all edge %+v has a catch type", e)
				}
			}
		}
	}
	if !catchAllEdge {
		t.Error("safeParse has no catch-all edge")
	}
	if got := mustMethod(t, cls, "<init>").TryBlocks(); got != nil {
		t.Errorf("<init> has try blocks: %+v", got)
	}
}

func TestSwitchCaseEdges(t *testing.T) {
	ctx := mustDEX(t, "switches.dex")
	packed := mustMethod(t, mustClass(t, ctx, "LSwitches;"), "packed")
	entry, ok := packed.Cfg().Entry()
	if !ok {
		t.Fatal("packed has no entry block")
	}
	var keys []int32
	defaults := 0
	for _, e := range entry.Successors {
		switch e.Kind {
		case CfgSwitchCase:
			keys = append(keys, e.CaseKey)
		case CfgSwitchDefault:
			defaults++
		}
	}
	if !slices.Equal(keys, []int32{10, 11, 12, 13}) || defaults != 1 {
		t.Errorf("case keys %v, %d default edges", keys, defaults)
	}
}

func TestParameterRegisters(t *testing.T) {
	ctx := mustDEX(t, "fibonacci.dex")
	cls := mustClass(t, ctx, "LFibonacci;")

	compute := mustMethod(t, cls, "compute") // an instance method with an int parameter
	if got := compute.RegisterCount(); got != 11 {
		t.Errorf("RegisterCount = %d", got)
	}
	want := []ParameterRegister{{9, "LFibonacci;", true, false}, {10, "I", false, false}}
	if got := compute.ParameterRegisters(); !slices.Equal(got, want) {
		t.Errorf("compute = %+v, want %+v", got, want)
	}
	classify := mustMethod(t, cls, "classify") // a static method with a long parameter
	want = []ParameterRegister{{3, "J", false, true}}
	if got := classify.ParameterRegisters(); !slices.Equal(got, want) {
		t.Errorf("classify = %+v, want %+v", got, want)
	}
}

func TestCallsAndOtherReferences(t *testing.T) {
	ctx := mustDEX(t, "classes.dex")
	cls := mustClass(t, ctx, "LTestDex;")
	main := mustMethod(t, cls, "main")

	calls := main.Calls()
	want := []MethodCall{
		{2, MethodID{"LTestDex;", "helloDex", "()Ljava/lang/String;"}, InvokeStatic},
		{6, MethodID{"Ljava/io/PrintStream;", "println", "(Ljava/lang/String;)V"}, InvokeVirtual},
	}
	if !slices.Equal(calls, want) {
		t.Errorf("Calls = %+v, want %+v", calls, want)
	}
	accesses := main.FieldAccesses()
	wantField := FieldID{"Ljava/lang/System;", "out", "Ljava/io/PrintStream;"}
	if len(accesses) != 1 || accesses[0].Field != wantField || !accesses[0].IsStatic ||
		accesses[0].IsWrite || accesses[0].CodeOffset != 0 {
		t.Errorf("FieldAccesses = %+v", accesses)
	}
	if got := mustMethod(t, cls, "helloDex").StringLoads(); len(got) != 1 || got[0].Value != "Hello Dex++!!" {
		t.Errorf("StringLoads = %+v", got)
	}
	if got := main.StringLoads(); got != nil {
		t.Errorf("main loads strings: %+v", got)
	}

	// A string constant with a NUL byte keeps it.
	multi := mustDEX(t, "multistring.dex")
	found := false
	for _, s := range mustMethod(t, mustClass(t, multi, "LMultiStringTest;"), "<clinit>").StringLoads() {
		found = found || s.Value == "test\x00embedded"
	}
	if !found {
		t.Error(`no load of "test\x00embedded"`)
	}

	pets := mustDEX(t, "polymorphic.dex")
	uses := mustMethod(t, mustClass(t, pets, "LDog;"), "describe").TypeUses()
	if len(uses) == 0 || uses[0].Descriptor != "Ljava/lang/StringBuilder;" || uses[0].Kind != TypeUseNewInstance {
		t.Errorf("TypeUses = %+v", uses)
	}
}

func TestEnumNames(t *testing.T) {
	for _, c := range []struct {
		got, want string
	}{
		{InvokeInterface.String(), "interface"}, {AnyInvokeKind.String(), "any"},
		{EdgeInheritedResolution.String(), "inherited-resolution"}, {AnyEdgeOrigin.String(), "any"},
		{CfgSwitchDefault.String(), "switch-default"}, {TypeUseFilledNewArray.String(), "filled-new-array"},
		{WidthPair.String(), "pair"}, {VisibilitySystem.String(), "system"},
		{ComponentProvider.String(), "provider"}, {ComponentKind(9).String(), "unknown(9)"},
	} {
		if c.got != c.want {
			t.Errorf("got %q, want %q", c.got, c.want)
		}
	}
}
