// Fixture for call-graph upward resolution (InheritedResolution edges) and the
// inherited-target CHA fan-out.  Build (see CLAUDE.md):
//   javac --release 8 Inherit.java && d8 *.class   -> inherit.dex
//   d8 Base2.class Mid2.class                      -> inherit_a.dex
//   d8 Leaf2.class UseB.class                      -> inherit_b.dex
// javac emits method_ids against the static receiver type, which is what makes
// these call sites resolve upward.
//
// Roles:
//   Base/Mid/Leaf/Other        shadowing (nearest-first), sibling non-fan-out,
//                              inherited static through a subclass qualifier
//   Base2/Mid2/Leaf2/UseB      stray-unresolved-node discriminator: viaMid()
//                              creates an unresolved Mid2.p node the walk from
//                              Leaf2 must resolve past; also the cross-DEX pair
//   Base3/Mid3/Leaf3           invoke-super through a non-defining direct super
//   Greeter/Host/SubHost       default method found via a chain class's
//                              interface (not the receiver's own)
//   Named/NamedDefault/Person  default method preferred over the abstract
//                              declaration it overrides
//   Tagged/UseD                abstract interface declaration as fallback
//   Runner/RunnerBase/FastRunner  sideways CHA: the implementer inherits the
//                              method from outside the interface's subtree
//   UseE.callUnknown           framework boundary: chain leaves the loaded set

class Base {
    void m() { }
    static void s() { }
}

class Mid extends Base {
    @Override
    void m() { }
}

class Leaf extends Mid { }

class Other extends Base {
    @Override
    void m() { }
}

class UseA {
    void callLeafM(Leaf l) { l.m(); }
    void callLeafS() { Leaf.s(); }
}

class Base2 {
    void p() { }
}

class Mid2 extends Base2 { }

class Leaf2 extends Mid2 { }

class UseB {
    void viaMid(Mid2 x) { x.p(); }
    void viaLeaf(Leaf2 x) { x.p(); }
}

class Base3 {
    void q() { }
}

class Mid3 extends Base3 { }

class Leaf3 extends Mid3 {
    @Override
    void q() { super.q(); }
}

interface Greeter {
    default void greet() { }
}

class Host implements Greeter { }

class SubHost extends Host { }

class UseC {
    void callGreet(SubHost d) { d.greet(); }
}

interface Named {
    String name();
}

interface NamedDefault extends Named {
    @Override
    default String name() { return "d"; }
}

class Person implements NamedDefault { }

interface Tagged extends Named { }

class UseD {
    String callPerson(Person p) { return p.name(); }
    String callName(Tagged t) { return t.name(); }
}

interface Runner {
    void run();
}

abstract class RunnerBase {
    public void run() { }
}

class FastRunner extends RunnerBase implements Runner { }

class UseE {
    void callRun(Runner r) { r.run(); }
    int callUnknown(java.util.ArrayList<String> l) { return l.size(); }
}
