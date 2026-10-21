public class MultiStringTest {
    public static String greeting = "Hello, World!";
    public static String farewell = "Goodbye, World!";
    public static String question = "How are you?";
    public static String answer = "I am fine, thanks!";
    public static String emoji = "🎉 Party time! 🎊";
    public static String empty = "";
    public static String withNull = "test\0embedded";
    
    public static void main(String[] args) {
        System.out.println(greeting);
        System.out.println(farewell);
        System.out.println(question);
        System.out.println(answer);
        System.out.println(emoji);
        System.out.println(empty);
        System.out.println(withNull);
    }
}
