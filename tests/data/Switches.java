// Source of switches.dex: a packed-switch, for the switch-case edges of the
// control-flow graph.
//
//   javac --release 8 Switches.java && d8 --output . Switches.class
//   mv classes.dex switches.dex
public class Switches {
    public static int packed(int x) {
        switch (x) {
            case 10: return 100;
            case 11: return 110;
            case 12: return 120;
            case 13: return 130;
            default: return 0;
        }
    }
}
