interface Animal {
    String sound();
}

abstract class Pet implements Animal {
    protected final String name;

    Pet(String name) {
        this.name = name;
    }

    String describe() {
        return name + " says " + sound();
    }
}

class Dog extends Pet {
    Dog(String name) {
        super(name);
    }

    @Override
    public String sound() {
        return "woof";
    }

    @Override
    String describe() {
        return super.describe() + "!";
    }
}

class Cat extends Pet {
    Cat(String name) {
        super(name);
    }

    @Override
    public String sound() {
        return "meow";
    }
}

class Kennel {
    static String noise(Animal a) {
        return a.sound();
    }

    static String dogNoise(Dog d) {
        return d.sound();
    }
}
