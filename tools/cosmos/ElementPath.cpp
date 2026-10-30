#include "ElementPath.h"

#include <cctype>

namespace CosmosCli {

namespace {

constexpr std::string_view kKnownTags =
    "Frame, Inline, Grid, Scroll, Image, Text, Input, TextArea, BarChart, LineChart, DonutChart, Series or Slice";

bool IsNameStart(char C) { return std::isalpha(static_cast<unsigned char>(C)) || C == '_'; }
bool IsNameChar(char C) { return std::isalnum(static_cast<unsigned char>(C)) || C == '_' || C == '-'; }

class PathReader {
public:
    explicit PathReader(std::string_view Text) : Text_(Text) {}

    std::variant<std::vector<ElementDescription>, ElementPathError> Read() {
        std::vector<ElementDescription> Elements;
        while (true) {
            SkipSpaces();
            auto Element = ReadElement();
            if (!Element) {
                return Error_;
            }
            Elements.push_back(std::move(*Element));
            SkipSpaces();
            if (AtEnd()) {
                break;
            }
            if (Text_[Pos_] != '>') {
                return Fail("expected `>` between elements");
            }
            if (Elements.back().Pseudo) {
                return Fail("only the last element can take a pseudo-class");
            }
            ++Pos_;
        }
        return Elements;
    }

private:
    std::optional<ElementDescription> ReadElement() {
        const std::size_t TagStart = Pos_;
        const std::string Tag      = ReadName();
        if (Tag.empty()) {
            Fail(AtEnd() ? "expected an element" : "expected an element's tag, like `Frame`");
            return std::nullopt;
        }
        std::string Lowered;
        for (const char C : Tag) {
            Lowered += static_cast<char>(std::tolower(static_cast<unsigned char>(C)));
        }
        const std::string_view Primitive = Cosmos::PrimitiveTagForSelector(Lowered);
        if (Primitive.empty()) {
            Pos_ = TagStart;
            Fail("`" + Tag + "` isn't an element Cosmos styles: it styles " + std::string(kKnownTags));
            return std::nullopt;
        }
        ElementDescription Element{std::string(Primitive), "", {}, std::nullopt};

        if (!AtEnd() && Text_[Pos_] == '[') {
            ++Pos_;
            while (true) {
                SkipSpaces();
                if (AtEnd()) {
                    Fail("expected `]`");
                    return std::nullopt;
                }
                if (Text_[Pos_] == ']') {
                    ++Pos_;
                    break;
                }
                const std::string Name = ReadName();
                if (Name.empty()) {
                    Fail("expected a prop's name");
                    return std::nullopt;
                }
                SkipSpaces();
                if (AtEnd() || Text_[Pos_] != '=') {
                    Fail("expected `=` after `" + Name + "`");
                    return std::nullopt;
                }
                ++Pos_;
                SkipSpaces();
                const std::optional<std::string> Value = ReadValue();
                if (!Value) {
                    return std::nullopt;
                }
                if (Name == "class") {
                    Element.Classes = *Value;
                } else {
                    Element.Props[Name] = *Value;
                }
            }
        }

        if (!AtEnd() && Text_[Pos_] == ':') {
            ++Pos_;
            const std::size_t PseudoStart = Pos_;
            const std::string Pseudo      = ReadName();
            if (Pseudo == "hover") {
                Element.Pseudo = Cosmos::PseudoKind::Hover;
            } else if (Pseudo == "active") {
                Element.Pseudo = Cosmos::PseudoKind::Active;
            } else if (Pseudo == "disabled") {
                Element.Pseudo = Cosmos::PseudoKind::Disabled;
            } else {
                Pos_ = PseudoStart;
                Fail("expected `hover`, `active` or `disabled` after `:`");
                return std::nullopt;
            }
        }
        return Element;
    }

    std::optional<std::string> ReadValue() {
        if (!AtEnd() && (Text_[Pos_] == '"' || Text_[Pos_] == '\'')) {
            const char        Quote = Text_[Pos_++];
            const std::size_t Start = Pos_;
            while (!AtEnd() && Text_[Pos_] != Quote) {
                ++Pos_;
            }
            if (AtEnd()) {
                Pos_ = Start - 1;
                Fail("this quote isn't closed");
                return std::nullopt;
            }
            return std::string(Text_.substr(Start, Pos_++ - Start));
        }
        const std::size_t Start = Pos_;
        while (!AtEnd() && !std::isspace(static_cast<unsigned char>(Text_[Pos_])) && Text_[Pos_] != ']') {
            ++Pos_;
        }
        if (Pos_ == Start) {
            Fail("expected a value");
            return std::nullopt;
        }
        return std::string(Text_.substr(Start, Pos_ - Start));
    }

    std::string ReadName() {
        if (AtEnd() || !IsNameStart(Text_[Pos_])) {
            return "";
        }
        const std::size_t Start = Pos_;
        while (!AtEnd() && IsNameChar(Text_[Pos_])) {
            ++Pos_;
        }
        return std::string(Text_.substr(Start, Pos_ - Start));
    }

    void SkipSpaces() {
        while (!AtEnd() && std::isspace(static_cast<unsigned char>(Text_[Pos_]))) {
            ++Pos_;
        }
    }

    bool AtEnd() const { return Pos_ >= Text_.size(); }

    ElementPathError Fail(std::string Message) {
        Error_ = ElementPathError{std::move(Message), Pos_ + 1};
        return Error_;
    }

    std::string_view Text_;
    std::size_t      Pos_{0};
    ElementPathError Error_;
};

std::string Quoted(const std::string& Value) { return "\"" + Value + "\""; }

} // namespace

std::variant<std::vector<ElementDescription>, ElementPathError> ParseElementPath(std::string_view Text) {
    return PathReader(Text).Read();
}

std::string DescribeElement(const ElementDescription& Element) {
    std::string Text = Element.Tag;
    std::string Attributes;
    if (!Element.Classes.empty()) {
        Attributes += "class=" + Quoted(Element.Classes);
    }
    for (const auto& [Name, Value] : Element.Props) {
        Attributes += (Attributes.empty() ? "" : " ") + Name + "=" + Quoted(Value);
    }
    if (!Attributes.empty()) {
        Text += "[" + Attributes + "]";
    }
    if (Element.Pseudo) {
        Text += *Element.Pseudo == Cosmos::PseudoKind::Hover    ? ":hover"
                : *Element.Pseudo == Cosmos::PseudoKind::Active ? ":active"
                                                                : ":disabled";
    }
    return Text;
}

ElementChain::ElementChain(const std::vector<ElementDescription>& Elements, std::size_t RootIndex) {
    const Element* Parent = nullptr;
    for (std::size_t I = 0; I < Elements.size(); ++I) {
        Elements_.push_back(std::make_unique<Element>(Elements[I], I == RootIndex, Parent));
        Parent = Elements_.back().get();
    }
}

const Cosmos::IStyleTarget& ElementChain::Target() const { return *Elements_.back(); }

} // namespace CosmosCli
