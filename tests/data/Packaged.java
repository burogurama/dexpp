// Fixture for Class.package: classes in a named package, one of them nested.
// Build:
//   javac --release 8 -d out Packaged.java && d8 --output . out/com/example/app/*.class
//   mv classes.dex packaged.dex
package com.example.app;

public class Packaged {
    static class Inner {
        int value() {
            return 1;
        }
    }

    public static void main(String[] args) {
        System.out.println(new Inner().value());
    }
}
