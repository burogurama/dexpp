#include <span>
#include <tuple>
#include <unordered_map>

#include "model/io.hpp"
#include "raw/descriptors.hpp"

namespace dex::model {

namespace {

constexpr std::string_view kSignature = "Ldalvik/annotation/Signature;";

/** Finds the class names in a generic signature (JVMS 4.7.9.1: class,
 *  method or field signature).  For `Lpkg/Outer<...>.Inner<...>;` only the
 *  outer name `pkg/Outer` is a class name; the suffix is relative to it. */
class SignatureParser
{
  public:
    explicit SignatureParser(std::string_view sig) : s_(sig) {}

    /** The [start, end) spans of the class names, or nullopt if @p sig is not
     *  a well-formed signature. */
    std::optional<std::vector<std::pair<std::size_t, std::size_t>>> class_names()
    {
        if (peek() == '<' && !type_params())
            return std::nullopt;
        if (peek() == '(') {
            if (!method_rest())
                return std::nullopt;
        }
        else {
            // A class signature (superclass, interfaces) or a field signature.
            if (pos_ >= s_.size())
                return std::nullopt;
            while (pos_ < s_.size())
                if (!reference_type())
                    return std::nullopt;
        }
        if (pos_ != s_.size())
            return std::nullopt;
        return names_;
    }

  private:
    char peek() const { return pos_ < s_.size() ? s_[pos_] : '\0'; }
    bool eat(char c)
    {
        if (peek() != c)
            return false;
        ++pos_;
        return true;
    }
    // An identifier: up to one of the characters that end it.
    bool identifier(std::string_view stops)
    {
        std::size_t start = pos_;
        while (pos_ < s_.size() && stops.find(s_[pos_]) == std::string_view::npos)
            ++pos_;
        return pos_ > start;
    }
    bool type_params()
    {
        if (!eat('<'))
            return false;
        while (peek() != '>') {
            if (!identifier(":<>;/.[") || !eat(':'))
                return false;
            if (peek() == 'L' || peek() == 'T' || peek() == '[') // class bound (optional)
                if (!reference_type())
                    return false;
            while (eat(':')) // interface bounds
                if (!reference_type())
                    return false;
            if (pos_ >= s_.size())
                return false;
        }
        return eat('>');
    }
    bool method_rest()
    {
        if (!eat('('))
            return false;
        while (peek() != ')')
            if (pos_ >= s_.size() || !java_type())
                return false;
        eat(')');
        if (!eat('V') && !java_type())
            return false;
        while (eat('^'))
            if (!(peek() == 'T' ? type_var() : class_type()))
                return false;
        return true;
    }
    bool java_type()
    {
        switch (peek()) {
        case 'B':
        case 'C':
        case 'D':
        case 'F':
        case 'I':
        case 'J':
        case 'S':
        case 'Z':
            ++pos_;
            return true;
        default:
            return reference_type();
        }
    }
    bool reference_type()
    {
        // Nesting is bounded so an adversarial signature cannot exhaust the stack.
        if (++depth_ > kMaxDepth)
            return false;
        struct Leave
        {
            int &depth;
            ~Leave() { --depth; }
        } leave{depth_};
        switch (peek()) {
        case 'L':
            return class_type();
        case 'T':
            return type_var();
        case '[':
            ++pos_;
            return java_type();
        default:
            return false;
        }
    }
    bool class_type()
    {
        if (!eat('L'))
            return false;
        std::size_t start = pos_;
        if (!identifier("<.;"))
            return false;
        names_.emplace_back(start, pos_);
        if (peek() == '<' && !type_args())
            return false;
        while (eat('.')) {
            if (!identifier("<.;/"))
                return false;
            if (peek() == '<' && !type_args())
                return false;
        }
        return eat(';');
    }
    bool type_args()
    {
        if (!eat('<'))
            return false;
        while (peek() != '>') {
            if (pos_ >= s_.size())
                return false;
            if (eat('*'))
                continue;
            if (peek() == '+' || peek() == '-')
                ++pos_;
            if (!reference_type())
                return false;
        }
        return eat('>');
    }
    bool type_var() { return eat('T') && identifier(";<>.:/[") && eat(';'); }

    static constexpr int kMaxDepth = 256;

    std::string_view s_;
    std::size_t pos_ = 0;
    int depth_ = 0;
    std::vector<std::pair<std::size_t, std::size_t>> names_;
};

class Renamer
{
  public:
    using Fn = std::function<std::optional<std::string>(std::string_view)>;
    explicit Renamer(const Fn &fn) : fn_(fn) {}

    void dex(Dex &d)
    {
        for (ClassDef &c : d.classes)
            klass(c);
    }

  private:
    // ----- descriptors -----

    /** The new descriptor of class @p descriptor, or nullopt to keep it. */
    const std::optional<std::string> &renamed_class(std::string_view descriptor)
    {
        auto it = class_memo_.find(descriptor);
        if (it == class_memo_.end()) {
            std::optional<std::string> to = fn_(descriptor);
            if (to && *to == descriptor)
                to.reset();
            it = class_memo_.emplace(std::string(descriptor), std::move(to)).first;
        }
        return it->second;
    }

    /** A type descriptor with its class renamed (arrays of it too), or nullopt. */
    std::optional<std::string> renamed_type(std::string_view d)
    {
        std::size_t dims = 0;
        while (dims < d.size() && d[dims] == '[')
            ++dims;
        std::string_view element = d.substr(dims);
        if (element.size() < 2 || element.front() != 'L' || element.back() != ';')
            return std::nullopt;
        const auto &to = renamed_class(element);
        if (!to)
            return std::nullopt;
        return std::string(dims, '[') + *to;
    }

    std::optional<std::string> renamed_proto(std::string_view p)
    {
        auto parts = raw::parse_proto(p);
        if (!parts)
            return std::nullopt;
        bool changed = false;
        std::string out = "(";
        for (std::string_view param : parts->params) {
            auto to = renamed_type(param);
            changed = changed || to.has_value();
            out += to ? std::string_view(*to) : param;
        }
        out += ')';
        auto ret = renamed_type(parts->return_type);
        changed = changed || ret.has_value();
        out += ret ? std::string_view(*ret) : parts->return_type;
        if (!changed)
            return std::nullopt;
        return out;
    }

    void type(std::string &d)
    {
        if (auto to = renamed_type(d))
            d = std::move(*to);
    }
    void proto(std::string &p)
    {
        if (auto to = renamed_proto(p))
            p = std::move(*to);
    }
    void field(FieldId &f)
    {
        type(f.class_descriptor);
        type(f.type);
    }
    void method(MethodId &m)
    {
        type(m.class_descriptor);
        proto(m.proto);
    }
    void handle(MethodHandle &h)
    {
        std::visit(
            [&](auto &m) {
                if constexpr (std::is_same_v<std::decay_t<decltype(m)>, FieldId>)
                    field(m);
                else
                    method(m);
            },
            h.member);
    }

    /** Generic signature pieces with their class names renamed.  A class name
     *  is renamed inside the piece that holds it; if one spans pieces, the
     *  result is a single piece. */
    void signature_pieces(std::span<std::string *const> pieces)
    {
        std::string whole;
        std::vector<std::size_t> starts;
        for (std::string *p : pieces) {
            starts.push_back(whole.size());
            whole += *p;
        }
        auto names = SignatureParser(whole).class_names();
        if (!names)
            return;
        struct Edit
        {
            std::size_t start, end;
            std::string to;
        };
        std::vector<Edit> edits;
        for (auto [start, end] : *names) {
            std::string descriptor = "L" + whole.substr(start, end - start) + ";";
            const auto &to = renamed_class(descriptor);
            if (to)
                edits.push_back({start, end, to->substr(1, to->size() - 2)});
        }
        if (edits.empty())
            return;
        // Apply each edit inside its piece, last first so offsets stay valid.
        auto piece_of = [&](std::size_t at) {
            std::size_t k = 0;
            while (k + 1 < starts.size() && starts[k + 1] <= at)
                ++k;
            return k;
        };
        bool split = false;
        for (const Edit &e : edits) {
            std::size_t k = piece_of(e.start);
            if (e.end > starts[k] + pieces[k]->size())
                split = true;
        }
        if (split) {
            for (auto it = edits.rbegin(); it != edits.rend(); ++it)
                whole.replace(it->start, it->end - it->start, it->to);
            *pieces[0] = whole;
            for (std::size_t k = 1; k < pieces.size(); ++k)
                pieces[k]->clear();
            return;
        }
        for (auto it = edits.rbegin(); it != edits.rend(); ++it) {
            std::size_t k = piece_of(it->start);
            pieces[k]->replace(it->start - starts[k], it->end - it->start, it->to);
        }
    }

    // ----- values and annotations -----

    void value(Value &v)
    {
        std::visit(
            [&](auto &x) {
                using T = std::decay_t<decltype(x)>;
                if constexpr (std::is_same_v<T, Value::Type>)
                    type(x.descriptor);
                else if constexpr (std::is_same_v<T, Value::Field> ||
                                   std::is_same_v<T, Value::Enum>)
                    field(x.field);
                else if constexpr (std::is_same_v<T, Value::Method>)
                    method(x.method);
                else if constexpr (std::is_same_v<T, Value::MethodType>)
                    proto(x.proto);
                else if constexpr (std::is_same_v<T, MethodHandle>)
                    handle(x);
                else if constexpr (std::is_same_v<T, Value::Array>)
                    for (Value &e : x.values)
                        value(e);
                else if constexpr (std::is_same_v<T, Shared<EncodedAnnotation>>) {
                    EncodedAnnotation copy = x.get();
                    annotation(copy);
                    if (!(copy == x.get()))
                        x = std::move(copy);
                }
            },
            v.data);
    }

    void annotation(EncodedAnnotation &a)
    {
        if (a.type == kSignature) {
            for (NamedValue &e : a.elements) {
                auto *array = std::get_if<Value::Array>(&e.value.data);
                if (e.name != "value" || !array)
                    continue;
                std::vector<std::string *> pieces;
                for (Value &piece : array->values)
                    if (auto *s = std::get_if<Value::String>(&piece.data))
                        pieces.push_back(&s->value);
                if (pieces.size() == array->values.size())
                    signature_pieces(pieces);
            }
        }
        type(a.type);
        for (NamedValue &e : a.elements)
            value(e.value);
    }

    void annotations(std::vector<AnnotationItem> &set)
    {
        for (AnnotationItem &a : set)
            annotation(a.annotation);
    }

    // ----- shared instruction operands: each shared value is renamed once -----

    template <class T> using Memo = std::unordered_map<const void *, std::optional<Shared<T>>>;

    template <class T, class Edit> void shared(Shared<T> &value, Edit edit)
    {
        auto &memo = std::get<Memo<T>>(memos_);
        auto [it, inserted] = memo.try_emplace(&value.get());
        if (inserted) {
            T copy = value.get();
            edit(copy);
            if (!(copy == value.get()))
                it->second = Shared<T>(std::move(copy));
        }
        if (it->second)
            value = *it->second;
    }

    void insn(Insn &i)
    {
        std::visit(
            [&](auto &x) {
                using T = std::decay_t<decltype(x)>;
                auto as_field = [&](FieldId &f) { field(f); };
                auto as_method = [&](MethodId &m) { method(m); };
                if constexpr (std::is_same_v<T, ConstClass> || std::is_same_v<T, CheckCast> ||
                              std::is_same_v<T, InstanceOf> || std::is_same_v<T, NewInstance> ||
                              std::is_same_v<T, NewArray> || std::is_same_v<T, FilledNewArray>)
                    shared_type(x.type);
                else if constexpr (std::is_same_v<T, ConstMethodType>)
                    shared_proto(x.proto);
                else if constexpr (std::is_same_v<T, ConstMethodHandle>)
                    shared(x.handle, [&](MethodHandle &h) { handle(h); });
                else if constexpr (std::is_same_v<T, InstanceGet> ||
                                   std::is_same_v<T, InstancePut> || std::is_same_v<T, StaticGet> ||
                                   std::is_same_v<T, StaticPut>)
                    shared(x.field, as_field);
                else if constexpr (std::is_same_v<T, Invoke>)
                    shared(x.method, as_method);
                else if constexpr (std::is_same_v<T, InvokePolymorphic>) {
                    shared(x.method, as_method);
                    shared_proto(x.proto);
                }
                else if constexpr (std::is_same_v<T, InvokeCustom>)
                    shared(x.call_site, [&](CallSite &cs) {
                        handle(cs.bootstrap);
                        proto(cs.method_type);
                        for (Value &v : cs.extra_args)
                            value(v);
                    });
            },
            i);
    }

    // A shared string is renamed as a type or as a prototype, never both: one
    // shared string can be both a type operand and a string constant.
    void shared_type(Shared<std::string> &s)
    {
        auto [it, inserted] = type_memo_.try_emplace(&s.get());
        if (inserted)
            if (auto to = renamed_type(s.get()))
                it->second = Shared<std::string>(std::move(*to));
        if (it->second)
            s = *it->second;
    }
    void shared_proto(Shared<std::string> &s)
    {
        auto [it, inserted] = proto_memo_.try_emplace(&s.get());
        if (inserted)
            if (auto to = renamed_proto(s.get()))
                it->second = Shared<std::string>(std::move(*to));
        if (it->second)
            s = *it->second;
    }

    void code(Code &c)
    {
        for (Item &item : c.items) {
            if (Insn *i = std::get_if<Insn>(&item))
                insn(*i);
            else if (LocalStart *s = std::get_if<LocalStart>(&item))
                shared(s->local, [&](LocalVariable &v) {
                    if (v.type)
                        type(*v.type);
                    if (v.signature) {
                        std::string *one = &*v.signature;
                        signature_pieces({&one, 1});
                    }
                });
        }
        for (TryRange &t : c.tries)
            for (Catch &k : t.catches)
                type(k.type);
    }

    void klass(ClassDef &c)
    {
        type(c.type);
        if (c.superclass)
            type(*c.superclass);
        for (std::string &i : c.interfaces)
            type(i);
        annotations(c.annotations);
        for (FieldDef &f : c.fields) {
            type(f.type);
            if (f.initial_value)
                value(*f.initial_value);
            annotations(f.annotations);
        }
        for (MethodDef &m : c.methods) {
            proto(m.proto);
            annotations(m.annotations);
            for (auto &set : m.parameter_annotations)
                annotations(set);
            if (m.code)
                code(*m.code);
        }
    }

    struct StringHash
    {
        using is_transparent = void;
        std::size_t operator()(std::string_view s) const noexcept
        {
            return std::hash<std::string_view>{}(s);
        }
    };

    const Fn &fn_;
    std::unordered_map<std::string, std::optional<std::string>, StringHash, std::equal_to<>>
        class_memo_;
    std::tuple<Memo<FieldId>, Memo<MethodId>, Memo<MethodHandle>, Memo<CallSite>,
               Memo<LocalVariable>>
        memos_;
    std::unordered_map<const void *, std::optional<Shared<std::string>>> type_memo_, proto_memo_;
};

} // namespace

void rename_types(Dex &dex,
                  const std::function<std::optional<std::string>(std::string_view)> &rename)
{
    Renamer(rename).dex(dex);
}

} // namespace dex::model
