package dexpp

// A method's code: decoded instructions, the control-flow graph, try blocks,
// parameter registers, and what the code refers to.

/*
#include <stdlib.h>
#include "dexpp.h"
*/
import "C"

import (
	"runtime"
	"unsafe"
)

// ---- Instructions -------------------------------------------------------- //

// InstructionBase holds the fields every instruction has.
type InstructionBase struct {
	// Offset is the position of the instruction in the method, in 16-bit
	// code units from the start of the code.
	Offset uint32
	// Opcode is the raw opcode byte. OpcodeName gives its name.
	Opcode uint8
	// Size is the length of the instruction in code units, from 1 to 5.
	Size uint8
}

// Base returns the fields every instruction has. It makes every instruction
// type an Instruction.
func (b InstructionBase) Base() InstructionBase { return b }

// Instruction is one decoded Dalvik instruction. Its dynamic type is one of
// the 24 *Instruction struct types of this package, one per family of
// opcodes, the same families as the C++ and Python APIs. Use a type switch to
// tell them apart:
//
//	switch insn := insn.(type) {
//	case dexpp.InvokeInstruction:
//		fmt.Println("call through method index", insn.MethodIndex)
//	case dexpp.ReturnInstruction:
//		fmt.Println("return")
//	}
//
// Pool indices (StringIndex, TypeIndex, FieldIndex, MethodIndex, ...) are raw
// indices into the tables of the DEX file. An instruction alone cannot turn
// them into names: Method.Calls, Method.FieldAccesses, Method.StringLoads and
// Method.TypeUses give the names.
type Instruction interface {
	Base() InstructionBase
}

// NopInstruction is nop, which does nothing. Unused opcodes decode to it too.
type NopInstruction struct{ InstructionBase }

// GotoInstruction is goto, goto/16 and goto/32: an unconditional jump.
type GotoInstruction struct {
	InstructionBase
	// BranchOffset is where the jump goes, in code units, counted from this
	// instruction's own offset. It is negative for a jump backwards.
	BranchOffset int32
}

// IfInstruction is if-eq, if-ne, if-lt, if-ge, if-gt and if-le: compare two
// registers and jump when the condition holds.
type IfInstruction struct {
	InstructionBase
	BranchOffset int32 // where the jump goes, relative to this instruction
	RegA         uint8 // the first register compared
	RegB         uint8 // the second register compared
}

// IfZeroInstruction is if-eqz, if-nez, if-ltz, if-gez, if-gtz and if-lez:
// compare one register with zero and jump when the condition holds.
type IfZeroInstruction struct {
	InstructionBase
	BranchOffset int32 // where the jump goes, relative to this instruction
	Reg          uint8 // the register compared with zero
}

// SwitchCase is one case of a switch instruction.
type SwitchCase struct {
	Key    int32 // the value the case matches
	Target int32 // where the case jumps, relative to the switch instruction
}

// SwitchInstruction is packed-switch and sparse-switch: jump to the case that
// matches a register.
type SwitchInstruction struct {
	InstructionBase
	// PayloadOffset is where the case table is stored, in code units,
	// counted from this instruction's own offset.
	PayloadOffset int32
	Reg           uint8        // the register that is tested
	IsPacked      bool         // true for packed-switch, false for sparse-switch
	Cases         []SwitchCase // the decoded cases; empty when the case table is damaged
}

// ReturnInstruction is return-void, return, return-wide and return-object.
type ReturnInstruction struct {
	InstructionBase
	Reg    uint8        // the register that holds the returned value, when HasReg
	HasReg bool         // false for return-void
	Width  OperandWidth // the width of the returned value
}

// ThrowInstruction is throw: it throws the exception held in a register.
type ThrowInstruction struct {
	InstructionBase
	Reg uint8 // the register that holds the exception
}

// MoveInstruction is move, move-wide, move-object and their /from16 and /16
// forms, the move-result instructions, and move-exception.
type MoveInstruction struct {
	InstructionBase
	Dest uint16 // the register written
	// Src is the register read. It is 0 for the move-result instructions and
	// move-exception, which take their value from the last call or the caught
	// exception and have no source register.
	Src   uint16
	Width OperandWidth // the width of the value moved
}

// ConstInstruction is const/4, const/16, const, const/high16, const-wide/16,
// const-wide/32, const-wide and const-wide/high16: load a number into a
// register.
type ConstInstruction struct {
	InstructionBase
	Dest uint8 // the register written
	// Value is the number, as a 64-bit integer for every form. The bytecode
	// does not say whether a constant is an integer or a floating-point
	// number, so a float or double constant appears here as its bit pattern.
	Value int64
	Width OperandWidth // WidthPair for the const-wide forms
}

// ConstStringInstruction is const-string and const-string/jumbo: load a
// string into a register.
type ConstStringInstruction struct {
	InstructionBase
	Dest        uint8  // the register written
	StringIndex uint32 // index in the DEX file's string table
}

// ConstClassInstruction is const-class: load a class object into a register.
type ConstClassInstruction struct {
	InstructionBase
	Dest      uint8  // the register written
	TypeIndex uint32 // index in the DEX file's type table
}

// ConstHandleInstruction is const-method-handle and const-method-type.
type ConstHandleInstruction struct {
	InstructionBase
	Dest uint8 // the register written
	// Index is an index into the method handle table for
	// const-method-handle, or into the prototype table for
	// const-method-type.
	Index uint32
}

// TypeInstruction is check-cast, instance-of, new-instance and new-array.
type TypeInstruction struct {
	InstructionBase
	TypeIndex uint32 // index in the DEX file's type table
	// Dest is the register the instruction works on: the register that is
	// cast for check-cast, the result for instance-of and new-array, and the
	// new object for new-instance.
	Dest uint8
	// Src is the object register for instance-of, and the size register for
	// new-array, when HasSrc is true.
	Src    uint8
	HasSrc bool // false for check-cast and new-instance, which use one register
}

// FilledNewArrayInstruction is filled-new-array and filled-new-array/range:
// create an array and fill it from registers.
type FilledNewArrayInstruction struct {
	InstructionBase
	TypeIndex uint32   // index of the array type in the DEX file's type table
	ArgRegs   []uint16 // the registers that hold the elements, in order
}

// MonitorInstruction is monitor-enter and monitor-exit: take and release the
// lock of an object, at the start and end of a synchronized block.
type MonitorInstruction struct {
	InstructionBase
	Reg     uint8 // the register that holds the object
	IsEnter bool  // true for monitor-enter, false for monitor-exit
}

// FillArrayInstruction is fill-array-data: fill an array from a table of
// constants stored in the code.
type FillArrayInstruction struct {
	InstructionBase
	Reg uint8 // the register that holds the array
	// PayloadOffset is where the table is stored, in code units, counted from
	// this instruction's own offset.
	PayloadOffset int32
}

// CompareInstruction is cmpl-float, cmpg-float, cmpl-double, cmpg-double and
// cmp-long: compare two values and store -1, 0 or 1.
type CompareInstruction struct {
	InstructionBase
	Dest     uint8        // the register that gets the result, always an int
	SrcA     uint8        // the first register compared
	SrcB     uint8        // the second register compared
	SrcWidth OperandWidth // WidthPair when the operands are long or double
}

// ArrayOpInstruction is the aget and aput family: read or write one element
// of an array.
type ArrayOpInstruction struct {
	InstructionBase
	ValueReg uint8        // the register the element is read into, or written from
	ArrayReg uint8        // the register that holds the array
	IndexReg uint8        // the register that holds the index
	IsWrite  bool         // true for the aput instructions
	Width    OperandWidth // the width of the element
}

// FieldInstruction is the iget, iput, sget and sput family: read or write a
// field.
type FieldInstruction struct {
	InstructionBase
	FieldIndex   uint32       // index in the DEX file's field table
	ValueReg     uint8        // the register the field is read into, or written from
	ObjectReg    uint8        // the register that holds the object, when HasObjectReg
	HasObjectReg bool         // false for a static field
	IsStatic     bool         // true for sget and sput
	IsWrite      bool         // true for iput and sput
	Width        OperandWidth // the width of the field's value
}

// InvokeInstruction is invoke-virtual, invoke-super, invoke-direct,
// invoke-static, invoke-interface and their /range forms, and
// invoke-polymorphic: call a method.
type InvokeInstruction struct {
	InstructionBase
	MethodIndex uint32 // index in the DEX file's method table
	// ArgRegs are the argument registers, in order. For a call on an object
	// the first one holds the object. A long or double argument takes two
	// entries.
	ArgRegs []uint16
}

// InvokeCustomInstruction is invoke-custom and invoke-custom/range: call
// through a call site that is bound at runtime.
type InvokeCustomInstruction struct {
	InstructionBase
	CallSiteIndex uint32   // index in the DEX file's call site table
	ArgRegs       []uint16 // the argument registers, in order
}

// UnaryOpInstruction is one-operand arithmetic and conversions: neg-int,
// not-long, int-to-long, double-to-float and the like.
type UnaryOpInstruction struct {
	InstructionBase
	Dest      uint8        // the register written
	Src       uint8        // the register read
	DestWidth OperandWidth // the width of the result
	SrcWidth  OperandWidth // the width of the operand
}

// BinaryOpInstruction is two-operand arithmetic on registers: add-int,
// sub-long, mul-double, shl-int and the like, and their /2addr forms.
type BinaryOpInstruction struct {
	InstructionBase
	Dest  uint8        // the register written
	SrcA  uint8        // the first operand; for a /2addr form, the same register as Dest
	SrcB  uint8        // the second operand
	Width OperandWidth // the width of the operation
}

// BinaryLitInstruction is arithmetic on a register and a constant:
// add-int/lit16, rsub-int, mul-int/lit8, ushr-int/lit8 and the like.
type BinaryLitInstruction struct {
	InstructionBase
	Dest    uint8 // the register written
	Src     uint8 // the register read
	Literal int16 // the constant operand
}

func goRegs(p *C.uint16_t, n C.size_t) []uint16 {
	if p == nil || n == 0 {
		return nil
	}
	slice := unsafe.Slice(p, int(n))
	out := make([]uint16, int(n))
	for i := range out {
		out[i] = uint16(slice[i])
	}
	return out
}

// goInstruction converts one C instruction to its Go type.
func goInstruction(in *C.DexppInstruction) Instruction {
	b := InstructionBase{Offset: uint32(in.offset), Opcode: uint8(in.opcode), Size: uint8(in.size)}
	switch in.kind {
	case C.DEXPP_INSN_NOP:
		return NopInstruction{b}
	case C.DEXPP_INSN_GOTO:
		return GotoInstruction{b, int32(in.branch_offset)}
	case C.DEXPP_INSN_IF:
		return IfInstruction{b, int32(in.branch_offset), uint8(in.reg_a), uint8(in.reg_b)}
	case C.DEXPP_INSN_IF_ZERO:
		return IfZeroInstruction{b, int32(in.branch_offset), uint8(in.reg)}
	case C.DEXPP_INSN_SWITCH:
		var cases []SwitchCase
		if in.cases != nil && in.case_count > 0 {
			raw := unsafe.Slice(in.cases, int(in.case_count))
			cases = make([]SwitchCase, len(raw))
			for i, c := range raw {
				cases[i] = SwitchCase{int32(c.key), int32(c.target)}
			}
		}
		return SwitchInstruction{b, int32(in.payload_offset), uint8(in.reg), in.is_packed != 0, cases}
	case C.DEXPP_INSN_RETURN:
		return ReturnInstruction{b, uint8(in.reg), in.has_reg != 0, OperandWidth(in.width)}
	case C.DEXPP_INSN_THROW:
		return ThrowInstruction{b, uint8(in.reg)}
	case C.DEXPP_INSN_MOVE:
		return MoveInstruction{b, uint16(in.dest), uint16(in.src), OperandWidth(in.width)}
	case C.DEXPP_INSN_CONST:
		return ConstInstruction{b, uint8(in.dest), int64(in.value), OperandWidth(in.width)}
	case C.DEXPP_INSN_CONST_STRING:
		return ConstStringInstruction{b, uint8(in.dest), uint32(in.string_index)}
	case C.DEXPP_INSN_CONST_CLASS:
		return ConstClassInstruction{b, uint8(in.dest), uint32(in.type_index)}
	case C.DEXPP_INSN_CONST_HANDLE:
		return ConstHandleInstruction{b, uint8(in.dest), uint32(in.index)}
	case C.DEXPP_INSN_TYPE:
		return TypeInstruction{b, uint32(in.type_index), uint8(in.dest), uint8(in.src), in.has_src != 0}
	case C.DEXPP_INSN_FILLED_NEW_ARRAY:
		return FilledNewArrayInstruction{b, uint32(in.type_index), goRegs(in.arg_regs, in.arg_count)}
	case C.DEXPP_INSN_MONITOR:
		return MonitorInstruction{b, uint8(in.reg), in.is_enter != 0}
	case C.DEXPP_INSN_FILL_ARRAY:
		return FillArrayInstruction{b, uint8(in.reg), int32(in.payload_offset)}
	case C.DEXPP_INSN_COMPARE:
		return CompareInstruction{b, uint8(in.dest), uint8(in.src_a), uint8(in.src_b),
			OperandWidth(in.src_width)}
	case C.DEXPP_INSN_ARRAY_OP:
		return ArrayOpInstruction{b, uint8(in.value_reg), uint8(in.array_reg), uint8(in.index_reg),
			in.is_write != 0, OperandWidth(in.width)}
	case C.DEXPP_INSN_FIELD:
		return FieldInstruction{b, uint32(in.field_index), uint8(in.value_reg), uint8(in.object_reg),
			in.has_object_reg != 0, in.is_static != 0, in.is_write != 0, OperandWidth(in.width)}
	case C.DEXPP_INSN_INVOKE:
		return InvokeInstruction{b, uint32(in.method_index), goRegs(in.arg_regs, in.arg_count)}
	case C.DEXPP_INSN_INVOKE_CUSTOM:
		return InvokeCustomInstruction{b, uint32(in.call_site_index), goRegs(in.arg_regs, in.arg_count)}
	case C.DEXPP_INSN_UNARY_OP:
		return UnaryOpInstruction{b, uint8(in.dest), uint8(in.src), OperandWidth(in.dest_width),
			OperandWidth(in.src_width)}
	case C.DEXPP_INSN_BINARY_OP:
		return BinaryOpInstruction{b, uint8(in.dest), uint8(in.src_a), uint8(in.src_b),
			OperandWidth(in.width)}
	case C.DEXPP_INSN_BINARY_LIT:
		return BinaryLitInstruction{b, uint8(in.dest), uint8(in.src), int16(in.literal)}
	}
	return NopInstruction{b} // a kind this package does not know yet
}

// cRegs copies registers into C memory. The caller frees the result.
func cRegs(regs []uint16) (*C.uint16_t, C.size_t) {
	if len(regs) == 0 {
		return nil, 0
	}
	p := (*C.uint16_t)(C.malloc(C.size_t(len(regs)) * C.size_t(unsafe.Sizeof(C.uint16_t(0)))))
	slice := unsafe.Slice(p, len(regs))
	for i, r := range regs {
		slice[i] = C.uint16_t(r)
	}
	return p, C.size_t(len(regs))
}

// cWidth converts a width to C. A value other than WidthPair, such as an
// OperandWidth built by hand, becomes WidthSingle, so C never gets a value
// outside its enum.
func cWidth(w OperandWidth) C.DexppOperandWidth {
	if w == WidthPair {
		return C.DEXPP_WIDTH_PAIR
	}
	return C.DEXPP_WIDTH_SINGLE
}

// cInstruction converts a Go instruction to the C struct, for ToString. The
// struct is passed to C through a Go pointer, which cgo allows only when the
// struct holds no pointers to Go memory. So the arrays it points to are in C
// memory, and the caller frees them.
func cInstruction(insn Instruction) C.DexppInstruction {
	var out C.DexppInstruction
	b := insn.Base()
	out.offset, out.opcode, out.size = C.uint32_t(b.Offset), C.uint8_t(b.Opcode), C.uint8_t(b.Size)
	switch i := insn.(type) {
	case NopInstruction:
		out.kind = C.DEXPP_INSN_NOP
	case GotoInstruction:
		out.kind = C.DEXPP_INSN_GOTO
		out.branch_offset = C.int32_t(i.BranchOffset)
	case IfInstruction:
		out.kind = C.DEXPP_INSN_IF
		out.branch_offset = C.int32_t(i.BranchOffset)
		out.reg_a, out.reg_b = C.uint8_t(i.RegA), C.uint8_t(i.RegB)
	case IfZeroInstruction:
		out.kind = C.DEXPP_INSN_IF_ZERO
		out.branch_offset, out.reg = C.int32_t(i.BranchOffset), C.uint8_t(i.Reg)
	case SwitchInstruction:
		out.kind = C.DEXPP_INSN_SWITCH
		out.payload_offset, out.reg = C.int32_t(i.PayloadOffset), C.uint8_t(i.Reg)
		out.is_packed = cbool(i.IsPacked)
		if len(i.Cases) > 0 {
			size := C.size_t(len(i.Cases)) * C.size_t(unsafe.Sizeof(C.DexppSwitchCase{}))
			out.cases = (*C.DexppSwitchCase)(C.malloc(size))
			cases := unsafe.Slice(out.cases, len(i.Cases))
			for k, c := range i.Cases {
				cases[k] = C.DexppSwitchCase{key: C.int32_t(c.Key), target: C.int32_t(c.Target)}
			}
			out.case_count = C.size_t(len(i.Cases))
		}
	case ReturnInstruction:
		out.kind = C.DEXPP_INSN_RETURN
		out.reg, out.has_reg = C.uint8_t(i.Reg), cbool(i.HasReg)
		out.width = cWidth(i.Width)
	case ThrowInstruction:
		out.kind = C.DEXPP_INSN_THROW
		out.reg = C.uint8_t(i.Reg)
	case MoveInstruction:
		out.kind = C.DEXPP_INSN_MOVE
		out.dest, out.src = C.uint16_t(i.Dest), C.uint16_t(i.Src)
		out.width = cWidth(i.Width)
	case ConstInstruction:
		out.kind = C.DEXPP_INSN_CONST
		out.dest, out.value = C.uint16_t(i.Dest), C.int64_t(i.Value)
		out.width = cWidth(i.Width)
	case ConstStringInstruction:
		out.kind = C.DEXPP_INSN_CONST_STRING
		out.dest, out.string_index = C.uint16_t(i.Dest), C.uint32_t(i.StringIndex)
	case ConstClassInstruction:
		out.kind = C.DEXPP_INSN_CONST_CLASS
		out.dest, out.type_index = C.uint16_t(i.Dest), C.uint32_t(i.TypeIndex)
	case ConstHandleInstruction:
		out.kind = C.DEXPP_INSN_CONST_HANDLE
		out.dest, out.index = C.uint16_t(i.Dest), C.uint32_t(i.Index)
	case TypeInstruction:
		out.kind = C.DEXPP_INSN_TYPE
		out.type_index, out.dest = C.uint32_t(i.TypeIndex), C.uint16_t(i.Dest)
		out.src, out.has_src = C.uint16_t(i.Src), cbool(i.HasSrc)
	case FilledNewArrayInstruction:
		out.kind = C.DEXPP_INSN_FILLED_NEW_ARRAY
		out.type_index = C.uint32_t(i.TypeIndex)
		out.arg_regs, out.arg_count = cRegs(i.ArgRegs)
	case MonitorInstruction:
		out.kind = C.DEXPP_INSN_MONITOR
		out.reg, out.is_enter = C.uint8_t(i.Reg), cbool(i.IsEnter)
	case FillArrayInstruction:
		out.kind = C.DEXPP_INSN_FILL_ARRAY
		out.reg, out.payload_offset = C.uint8_t(i.Reg), C.int32_t(i.PayloadOffset)
	case CompareInstruction:
		out.kind = C.DEXPP_INSN_COMPARE
		out.dest, out.src_a, out.src_b = C.uint16_t(i.Dest), C.uint8_t(i.SrcA), C.uint8_t(i.SrcB)
		out.src_width = cWidth(i.SrcWidth)
	case ArrayOpInstruction:
		out.kind = C.DEXPP_INSN_ARRAY_OP
		out.value_reg, out.array_reg = C.uint8_t(i.ValueReg), C.uint8_t(i.ArrayReg)
		out.index_reg, out.is_write = C.uint8_t(i.IndexReg), cbool(i.IsWrite)
		out.width = cWidth(i.Width)
	case FieldInstruction:
		out.kind = C.DEXPP_INSN_FIELD
		out.field_index, out.value_reg = C.uint32_t(i.FieldIndex), C.uint8_t(i.ValueReg)
		out.object_reg, out.has_object_reg = C.uint8_t(i.ObjectReg), cbool(i.HasObjectReg)
		out.is_static, out.is_write = cbool(i.IsStatic), cbool(i.IsWrite)
		out.width = cWidth(i.Width)
	case InvokeInstruction:
		out.kind = C.DEXPP_INSN_INVOKE
		out.method_index = C.uint32_t(i.MethodIndex)
		out.arg_regs, out.arg_count = cRegs(i.ArgRegs)
	case InvokeCustomInstruction:
		out.kind = C.DEXPP_INSN_INVOKE_CUSTOM
		out.call_site_index = C.uint32_t(i.CallSiteIndex)
		out.arg_regs, out.arg_count = cRegs(i.ArgRegs)
	case UnaryOpInstruction:
		out.kind = C.DEXPP_INSN_UNARY_OP
		out.dest, out.src = C.uint16_t(i.Dest), C.uint16_t(i.Src)
		out.dest_width, out.src_width = cWidth(i.DestWidth), cWidth(i.SrcWidth)
	case BinaryOpInstruction:
		out.kind = C.DEXPP_INSN_BINARY_OP
		out.dest, out.src_a, out.src_b = C.uint16_t(i.Dest), C.uint8_t(i.SrcA), C.uint8_t(i.SrcB)
		out.width = cWidth(i.Width)
	case BinaryLitInstruction:
		out.kind = C.DEXPP_INSN_BINARY_LIT
		out.dest, out.src, out.literal = C.uint16_t(i.Dest), C.uint16_t(i.Src), C.int32_t(i.Literal)
	default:
		// A type from outside this package: the C side renders a kind it does
		// not know as the opcode name alone.
		out.kind = C.DexppInstructionKind(999)
	}
	return out
}

// Instructions returns the decoded bytecode of the method, in order. It is
// nil for a method without code. The library decodes the bytecode once and
// keeps it; each call copies it into new Go values.
func (m *Method) Instructions() []Instruction {
	defer runtime.KeepAlive(m)
	var n C.size_t
	arr := C.dexpp_method_instructions(m.p(), &n)
	defer C.dexpp_instruction_array_free(arr, n)
	if arr == nil || n == 0 {
		return nil
	}
	raw := unsafe.Slice(arr, int(n))
	out := make([]Instruction, len(raw))
	for i := range raw {
		out[i] = goInstruction(&raw[i])
	}
	return out
}

// OpcodeName returns the name of a Dalvik opcode byte, such as
// "invoke-virtual" for 0x6e. An opcode the format does not use gives
// "unused-XX", with the byte in hex.
func OpcodeName(opcode uint8) string {
	return C.GoString(C.dexpp_opcode_name(C.uint8_t(opcode)))
}

// ToString writes one instruction as text: the opcode name, then its
// operands, such as "const-string v0, string@12". Operands that point into a
// table of the DEX file are shown as raw indices (string@12, method@4); an
// instruction alone cannot turn them into names.
func ToString(insn Instruction) string {
	c := cInstruction(insn)
	defer func() {
		C.free(unsafe.Pointer(c.cases))
		C.free(unsafe.Pointer(c.arg_regs))
	}()
	return takeString(C.dexpp_instruction_to_string(&c))
}

// ---- Control-flow graph -------------------------------------------------- //

// CfgEdge is an edge from one basic block to another.
type CfgEdge struct {
	TargetBlock uint32      // index in Cfg.Blocks of the block the edge leads to
	Kind        CfgEdgeKind // why execution can go along the edge
	// Label is extra data for two kinds of edge, when HasLabel is true. For
	// CfgSwitchCase it is the case key. For CfgException it is the index of
	// the caught type in the DEX file's type table, or 0xFFFFFFFF for a
	// handler that catches everything.
	Label    int64
	HasLabel bool
}

// BasicBlock is a run of instructions that is always entered at its first
// instruction and left at its last one.
type BasicBlock struct {
	Index          uint32    // position of the block in Cfg.Blocks
	StartOffset    uint32    // offset of the block's first instruction, in code units
	EndOffset      uint32    // offset just past the block's last instruction
	FirstInsn      uint32    // index in Method.Instructions of the block's first instruction
	InsnCount      uint32    // number of instructions in the block
	Successors     []CfgEdge // the edges that leave the block; none when it returns or throws
	Predecessors   []uint32  // indices in Cfg.Blocks of the blocks with an edge to this one
	IsHandlerEntry bool      // true when the block is the start of a catch handler
}

// Cfg is the control-flow graph of one method. The blocks are ordered by
// offset, and block 0 is where the method starts.
type Cfg struct {
	Blocks []BasicBlock
}

// Cfg returns the method's control-flow graph. The library builds it once and
// keeps it; each call copies it into a new Cfg. It is empty for a method
// without code.
func (m *Method) Cfg() *Cfg {
	defer runtime.KeepAlive(m)
	var n C.size_t
	arr := C.dexpp_method_cfg(m.p(), &n)
	defer C.dexpp_basic_block_array_free(arr, n)
	g := &Cfg{}
	if arr == nil || n == 0 {
		return g
	}
	raw := unsafe.Slice(arr, int(n))
	g.Blocks = make([]BasicBlock, len(raw))
	for i := range raw {
		b := &raw[i]
		block := BasicBlock{
			Index:          uint32(b.index),
			StartOffset:    uint32(b.start_offset),
			EndOffset:      uint32(b.end_offset),
			FirstInsn:      uint32(b.first_insn),
			InsnCount:      uint32(b.insn_count),
			Predecessors:   goIndices(b.predecessors, b.predecessor_count),
			IsHandlerEntry: b.is_handler_entry != 0,
		}
		if b.successors != nil && b.successor_count > 0 {
			edges := unsafe.Slice(b.successors, int(b.successor_count))
			block.Successors = make([]CfgEdge, len(edges))
			for k, e := range edges {
				block.Successors[k] = CfgEdge{
					TargetBlock: uint32(e.target_block),
					Kind:        CfgEdgeKind(e.kind),
					Label:       int64(e.label),
					HasLabel:    e.has_label != 0,
				}
			}
		}
		g.Blocks[i] = block
	}
	return g
}

// Entry returns the block where the method starts. The bool is false when the
// graph is empty.
func (g *Cfg) Entry() (BasicBlock, bool) {
	if len(g.Blocks) == 0 {
		return BasicBlock{}, false
	}
	return g.Blocks[0], true
}

// BlockAtOffset returns the block that starts at this code-unit offset. The
// bool is false when no block starts there. Use it to follow a branch target
// to its block.
func (g *Cfg) BlockAtOffset(offset uint32) (BasicBlock, bool) {
	lo, hi := 0, len(g.Blocks)
	for lo < hi { // the blocks are sorted by StartOffset
		mid := (lo + hi) / 2
		if g.Blocks[mid].StartOffset < offset {
			lo = mid + 1
		} else {
			hi = mid
		}
	}
	if lo < len(g.Blocks) && g.Blocks[lo].StartOffset == offset {
		return g.Blocks[lo], true
	}
	return BasicBlock{}, false
}

// Empty reports whether the graph has no blocks: the method is abstract or
// native.
func (g *Cfg) Empty() bool { return len(g.Blocks) == 0 }

// Len returns the number of blocks.
func (g *Cfg) Len() int { return len(g.Blocks) }

// ---- Registers, try blocks, references ----------------------------------- //

// RegisterCount returns the number of registers in the method's frame. It is
// 0 for a method without code.
func (m *Method) RegisterCount() int {
	defer runtime.KeepAlive(m)
	return int(C.dexpp_method_register_count(m.p()))
}

// ParameterRegister is an incoming argument of a method and the register it
// arrives in. Dalvik puts the arguments in the last registers of the frame.
type ParameterRegister struct {
	Reg    uint16 // the register; a wide argument also uses Reg+1
	Type   string // the argument's type descriptor; the declaring class for "this"
	IsThis bool   // true for the implicit "this" of an instance method
	IsWide bool   // true for long and double, which take two registers
}

// ParameterRegisters returns the incoming arguments and their registers, with
// "this" first for an instance method, then the declared parameters in order.
// It is nil for a method without code.
func (m *Method) ParameterRegisters() []ParameterRegister {
	defer runtime.KeepAlive(m)
	var n C.size_t
	arr := C.dexpp_method_parameter_registers(m.p(), &n)
	defer C.dexpp_parameter_register_array_free(arr, n)
	if arr == nil || n == 0 {
		return nil
	}
	raw := unsafe.Slice(arr, int(n))
	out := make([]ParameterRegister, len(raw))
	for i, r := range raw {
		out[i] = ParameterRegister{uint16(r.reg), C.GoString(r._type), r.is_this != 0, r.is_wide != 0}
	}
	return out
}

// CatchHandler is one typed catch clause of a try block.
type CatchHandler struct {
	TypeDescriptor string // the exception class the clause catches
	HandlerOffset  uint32 // offset of the handler's first instruction
}

// TryBlock is a range of a method's code that is covered by exception
// handlers.
type TryBlock struct {
	StartOffset uint32         // offset of the first covered instruction
	EndOffset   uint32         // offset just past the covered range
	Handlers    []CatchHandler // the typed catch clauses, in the order they are tried
	// CatchAllOffset is the offset of the handler that catches every
	// exception, when HasCatchAll is true. A finally block is compiled to
	// such a handler.
	CatchAllOffset uint32
	HasCatchAll    bool
}

// TryBlocks returns the try ranges of the method and their handlers. It is
// nil when the method has none.
func (m *Method) TryBlocks() []TryBlock {
	defer runtime.KeepAlive(m)
	var n C.size_t
	arr := C.dexpp_method_try_blocks(m.p(), &n)
	defer C.dexpp_try_block_array_free(arr, n)
	if arr == nil || n == 0 {
		return nil
	}
	raw := unsafe.Slice(arr, int(n))
	out := make([]TryBlock, len(raw))
	for i := range raw {
		t := &raw[i]
		out[i] = TryBlock{
			StartOffset:    uint32(t.start_offset),
			EndOffset:      uint32(t.end_offset),
			CatchAllOffset: uint32(t.catch_all_offset),
			HasCatchAll:    t.has_catch_all != 0,
		}
		if t.handlers != nil && t.handler_count > 0 {
			handlers := unsafe.Slice(t.handlers, int(t.handler_count))
			out[i].Handlers = make([]CatchHandler, len(handlers))
			for k, h := range handlers {
				out[i].Handlers[k] = CatchHandler{C.GoString(h.type_descriptor), uint32(h.handler_offset)}
			}
		}
	}
	return out
}

// MethodCall is one call in a method's code.
type MethodCall struct {
	CodeOffset uint32     // offset of the invoke instruction, in code units
	Target     MethodID   // the called method, exactly as the bytecode names it
	Kind       InvokeKind // the kind of invoke instruction
}

// Calls returns every call in the method's code, in order. The targets are as
// the bytecode names them: inheritance and virtual dispatch are not applied.
// The call graph applies them.
func (m *Method) Calls() []MethodCall {
	defer runtime.KeepAlive(m)
	var n C.size_t
	arr := C.dexpp_method_calls(m.p(), &n)
	defer C.dexpp_method_call_array_free(arr, n)
	if arr == nil || n == 0 {
		return nil
	}
	raw := unsafe.Slice(arr, int(n))
	out := make([]MethodCall, len(raw))
	for i, c := range raw {
		out[i] = MethodCall{uint32(c.code_offset), goMethodID(c.target), InvokeKind(c.kind)}
	}
	return out
}

// FieldAccess is one read or write of a field in a method's code.
type FieldAccess struct {
	CodeOffset uint32  // offset of the instruction, in code units
	Field      FieldID // the field that is read or written
	IsWrite    bool    // true for a write, false for a read
	IsStatic   bool    // true for a static field access
}

// FieldAccesses returns every field read and write in the method's code, in
// order.
func (m *Method) FieldAccesses() []FieldAccess {
	defer runtime.KeepAlive(m)
	var n C.size_t
	arr := C.dexpp_method_field_accesses(m.p(), &n)
	defer C.dexpp_field_access_array_free(arr, n)
	if arr == nil || n == 0 {
		return nil
	}
	raw := unsafe.Slice(arr, int(n))
	out := make([]FieldAccess, len(raw))
	for i, a := range raw {
		out[i] = FieldAccess{uint32(a.code_offset), goFieldID(a.field), a.is_write != 0, a.is_static != 0}
	}
	return out
}

// StringLoad is one const-string instruction in a method's code.
type StringLoad struct {
	CodeOffset uint32 // offset of the instruction, in code units
	Value      string // the string that is loaded; it may contain NUL bytes
}

// StringLoads returns every const-string in the method's code, in order.
func (m *Method) StringLoads() []StringLoad {
	defer runtime.KeepAlive(m)
	var n C.size_t
	arr := C.dexpp_method_string_loads(m.p(), &n)
	defer C.dexpp_string_load_array_free(arr, n)
	if arr == nil || n == 0 {
		return nil
	}
	raw := unsafe.Slice(arr, int(n))
	out := make([]StringLoad, len(raw))
	for i, s := range raw {
		out[i] = StringLoad{uint32(s.code_offset), goText(s.value)}
	}
	return out
}

// TypeUse is one instruction in a method's code that names a type:
// const-class, check-cast, instance-of, new-instance, new-array or
// filled-new-array.
type TypeUse struct {
	CodeOffset uint32      // offset of the instruction, in code units
	Descriptor string      // the type the instruction names
	Kind       TypeRefKind // what the instruction does with the type
}

// TypeUses returns every instruction in the method's code that names a type,
// in order.
func (m *Method) TypeUses() []TypeUse {
	defer runtime.KeepAlive(m)
	var n C.size_t
	arr := C.dexpp_method_type_uses(m.p(), &n)
	defer C.dexpp_type_use_array_free(arr, n)
	if arr == nil || n == 0 {
		return nil
	}
	raw := unsafe.Slice(arr, int(n))
	out := make([]TypeUse, len(raw))
	for i, u := range raw {
		out[i] = TypeUse{uint32(u.code_offset), C.GoString(u.descriptor), TypeRefKind(u.kind)}
	}
	return out
}
