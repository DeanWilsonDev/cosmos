#pragma once

#include "cosmos/Ast.h"
#include "cosmos/Resolver.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace CosmosCli {

// One element of an element path, such as `Text[class="title" size=12]:hover`.
struct ElementDescription {
    std::string                        Tag;
    std::string                        Classes;
    std::map<std::string, std::string> Props;
    std::optional<Cosmos::PseudoKind>  Pseudo;
};

struct ElementPathError {
    std::string Message;
    std::size_t Column{0};
};

// Parses `Frame[class="card"] > Text[class="title" size=12]:hover`: elements outermost
// first, each the previous one's child. Tags are the primitives' names as Chaos writes them
// (any case); a pseudo-class may follow only the last element.
std::variant<std::vector<ElementDescription>, ElementPathError> ParseElementPath(std::string_view Text);

std::string DescribeElement(const ElementDescription& Element);

// An IStyleTarget for each element of a path, linked child to parent.
class ElementChain {
public:
    // RootIndex is the component root's position in Elements, from 0.
    ElementChain(const std::vector<ElementDescription>& Elements, std::size_t RootIndex);

    const Cosmos::IStyleTarget& Target() const;

private:
    class Element : public Cosmos::IStyleTarget {
    public:
        Element(const ElementDescription& Description, bool IsRoot, const Element* Parent)
            : Description_(Description), IsRoot_(IsRoot), Parent_(Parent) {}

        std::string                 ClassName() const override { return Description_.Classes; }
        std::string                 PrimitiveTag() const override { return Description_.Tag; }
        bool                        IsComponentRoot() const override { return IsRoot_; }
        const Cosmos::IStyleTarget* Parent() const override { return Parent_; }

    private:
        ElementDescription Description_;
        bool               IsRoot_;
        const Element*     Parent_;
    };

    std::vector<std::unique_ptr<Element>> Elements_;
};

} // namespace CosmosCli
