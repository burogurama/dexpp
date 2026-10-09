import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

@Retention(RetentionPolicy.RUNTIME)
@Target({ElementType.TYPE, ElementType.METHOD, ElementType.FIELD})
@interface Marker {
    String name();

    int level() default 1;

    String[] tags() default {};

    Class<?> target() default Object.class;
}

@Marker(name = "cls", level = 3, tags = {"a", "b"})
class Annotated {
    @Marker(name = "fld")
    static final int MAGIC = 42;

    static final String GREETING = "hello";
    static final long BIG = 1234567890123L;
    static final double PI = 3.25;
    static final boolean FLAG = true;

    static int counter; // no recorded initial value -> type default

    @Marker(name = "mth", target = String.class)
    String greet() {
        return GREETING;
    }

    @Deprecated
    void old() {
    }
}
