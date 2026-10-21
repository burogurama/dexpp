interface Shape {
    double area();
    double perimeter();
}

abstract class AbstractShape implements Shape {
    protected String color;

    AbstractShape(String color) {
        this.color = color;
    }

    public String getColor() {
        return color;
    }
}

class Circle extends AbstractShape {
    private double radius;

    Circle(String color, double radius) {
        super(color);
        this.radius = radius;
    }

    @Override
    public double area() {
        return Math.PI * radius * radius;
    }

    @Override
    public double perimeter() {
        return 2 * Math.PI * radius;
    }

    public double getRadius() {
        return radius;
    }
}

class Rectangle extends AbstractShape {
    private double width;
    private double height;

    Rectangle(String color, double width, double height) {
        super(color);
        this.width = width;
        this.height = height;
    }

    @Override
    public double area() {
        return width * height;
    }

    @Override
    public double perimeter() {
        return 2 * (width + height);
    }
}
